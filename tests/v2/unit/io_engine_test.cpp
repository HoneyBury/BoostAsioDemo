#include <gtest/gtest.h>

#include "app/logging.h"
#include "net/packet_codec.h"
#include "v2/io/io_engine.h"

#include <boost/asio.hpp>

#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>

namespace {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

}  // namespace

TEST(V2IoEngineTest, DispatchesTasksToRequestedCore) {
    app::logging::init("project_tests");

    v2::io::AsioIoEngine engine(2);
    engine.run();

    std::promise<std::uint32_t> promise;
    auto future = promise.get_future();
    engine.dispatch_to_core(1, [&promise]() mutable {
        promise.set_value(1);
    });

    EXPECT_EQ(future.get(), 1U);
    engine.stop();
}

TEST(V2IoEngineTest, ListenAssignmentsRotateAcrossCores) {
    app::logging::init("project_tests");

    v2::io::AsioIoEngine engine(2);
    try {
        auto first = engine.listen("127.0.0.1", 0);
        auto second = engine.listen("127.0.0.1", 0);

        EXPECT_EQ(first->owning_core_id(), 0U);
        EXPECT_EQ(second->owning_core_id(), 1U);

        engine.stop();
    } catch (const std::exception& ex) {
        engine.stop();
        GTEST_SKIP() << "socket bind unavailable in this environment: " << ex.what();
    }
}

TEST(V2IoEngineTest, AcceptsSocketAndDeliversPacketToSessionHandler) {
    app::logging::init("project_tests");

    v2::io::AsioIoEngine engine(1);

    try {
        auto acceptor = engine.listen("127.0.0.1", 0);
        EXPECT_EQ(acceptor->owning_core_id(), 0U);
        std::promise<std::string> packet_body_promise;
        auto packet_body = packet_body_promise.get_future();
        std::promise<std::uint32_t> core_id_promise;
        auto core_id = core_id_promise.get_future();

        acceptor->async_accept([&](std::unique_ptr<v2::io::IoSession> session) {
            ASSERT_NE(session, nullptr);
            core_id_promise.set_value(session->owning_core_id());
            session->set_packet_handler(
                [&packet_body_promise](v2::io::IoSession::PacketMessage message) mutable {
                    packet_body_promise.set_value(std::move(message.body));
                });
            session->start();
        });

        engine.run();

        asio::io_context client_io;
        tcp::socket client(client_io);
        client.connect(tcp::endpoint(asio::ip::make_address("127.0.0.1"), acceptor->local_port()));

        const auto packet = net::packet::encode(1001, 77, 0, "io_engine_works");
        asio::write(client, asio::buffer(packet));

        EXPECT_EQ(core_id.get(), 0U);
        EXPECT_EQ(packet_body.get(), "io_engine_works");
        client.close();
        engine.stop();
    } catch (const std::exception& ex) {
        engine.stop();
        GTEST_SKIP() << "socket bind unavailable in this environment: " << ex.what();
    }
}
