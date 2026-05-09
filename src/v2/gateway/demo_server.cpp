#include "v2/gateway/demo_server.h"

#include "app/logging.h"

#include <utility>

namespace v2::gateway {

DemoServer::DemoServer(asio::io_context& io_context,
                       std::uint16_t port,
                       net::SessionOptions session_options)
    : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)),
      session_options_(std::move(session_options)),
      adapter_(actor_system_, this),
      runtime_(actor_system_, adapter_) {
    gateway_actor_ = runtime_.create_gateway_actor();
    adapter_.bind_gateway(gateway_actor_);
    archive_store_ = std::make_unique<JsonFileBattleArchiveStore>("v2_archive");
    runtime_.set_archive_sink(archive_store_.get());
}

void DemoServer::start() {
    LOG_INFO("v2 demo server listening on 0.0.0.0:{}", acceptor_.local_endpoint().port());
    do_accept();
}

void DemoServer::stop() {
    boost::system::error_code ignored_ec;
    acceptor_.close(ignored_ec);
    for (auto& [session_id, session] : sessions_) {
        (void)session_id;
        session->stop();
    }
}

void DemoServer::deliver(SessionWrite write) {
    auto it = sessions_.find(write.envelope.session_id);
    if (it == sessions_.end()) {
        return;
    }
    it->second->send(write.envelope.protocol_message_id,
                     write.envelope.request_id,
                     write.envelope.error_code,
                     std::move(write.envelope.body),
                     write.envelope.flags);
}

std::uint16_t DemoServer::local_port() const {
    return acceptor_.local_endpoint().port();
}

void DemoServer::do_accept() {
    acceptor_.async_accept([this](const boost::system::error_code& ec, tcp::socket socket) {
        if (ec) {
            return;
        }

        const auto session_id = next_session_id_++;
        auto session = std::make_shared<net::Session>(std::move(socket), session_options_);
        sessions_.emplace(session_id, session);

        session->set_packet_handler(
            [this, session_id](const std::shared_ptr<net::Session>&, net::Session::PacketMessage message) {
                (void)adapter_.handle_incoming(ClientEnvelope{
                    .session_id = session_id,
                    .protocol_message_id = message.message_id,
                    .request_id = message.request_id,
                    .error_code = message.error_code,
                    .flags = message.flags,
                    .body = std::move(message.body),
                });
            });

        session->set_close_handler(
            [this, session_id](const std::shared_ptr<net::Session>&, const boost::system::error_code&) {
                runtime_.on_session_closed(session_id);
                sessions_.erase(session_id);
            });

        session->start();
        do_accept();
    });
}

}  // namespace v2::gateway
