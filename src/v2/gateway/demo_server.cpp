#include "v2/gateway/demo_server.h"

#include "app/logging.h"

#include <utility>

namespace v2::gateway {

DemoServer::DemoServer(std::uint16_t port,
                       net::SessionOptions session_options,
                       std::unique_ptr<v2::io::IoEngine> io_engine)
    : port_(port),
      session_options_(std::move(session_options)),
      io_engine_(std::move(io_engine)),
      adapter_(actor_system_, this),
      runtime_(actor_system_, adapter_) {
    gateway_actor_ = runtime_.create_gateway_actor();
    adapter_.bind_gateway(gateway_actor_);
    archive_store_ = std::make_unique<JsonFileBattleArchiveStore>("v2_archive");
    runtime_.set_archive_sink(archive_store_.get());
}

void DemoServer::start() {
    acceptor_ = io_engine_->listen("127.0.0.1", port_, session_options_);
    LOG_INFO("v2 demo server listening on 127.0.0.1:{}", acceptor_->local_port());
    do_accept();
    io_engine_->run();
}

void DemoServer::stop() {
    {
        std::scoped_lock lock(sessions_mutex_);
        acceptor_.reset();
        for (auto& [session_id, session] : sessions_) {
            (void)session_id;
            session->close();
        }
        sessions_.clear();
    }
    io_engine_->stop();
}

void DemoServer::deliver(SessionWrite write) {
    std::scoped_lock lock(sessions_mutex_);
    auto it = sessions_.find(write.envelope.session_id);
    if (it != sessions_.end()) {
        it->second->send(write.envelope.protocol_message_id,
                         write.envelope.request_id,
                         write.envelope.error_code,
                         std::move(write.envelope.body),
                         write.envelope.flags);
    }
}

std::uint16_t DemoServer::local_port() const {
    return acceptor_ == nullptr ? 0 : acceptor_->local_port();
}

std::uint32_t DemoServer::io_core_count() const {
    return io_engine_ == nullptr ? 0U : io_engine_->num_io_cores();
}

void DemoServer::do_accept() {
    if (!acceptor_) {
        return;
    }
    acceptor_->async_accept([this](std::unique_ptr<v2::io::IoSession> session) {
        if (!session) {
            return;
        }

        const auto session_id = next_session_id_++;
        session->set_packet_handler(
            [this, session_id](v2::io::IoSession::PacketMessage message) {
                (void)adapter_.handle_incoming(ClientEnvelope{
                    .session_id = session_id,
                    .protocol_message_id = message.message_id,
                    .request_id = message.request_id,
                    .error_code = message.error_code,
                    .flags = message.flags,
                    .body = std::move(message.body),
                });
            });
        session->set_close_handler([this, session_id]() {
            runtime_.on_session_closed(session_id);
            std::scoped_lock lock(sessions_mutex_);
            sessions_.erase(session_id);
        });

        {
            std::scoped_lock lock(sessions_mutex_);
            sessions_.emplace(session_id, std::move(session));
            sessions_.at(session_id)->start();
        }

        do_accept();
    });
}

}  // namespace v2::gateway
