#include "app/logging.h"
#include "net/message_dispatcher.h"

#include <boost/asio/thread_pool.hpp>

#include <atomic>
#include <future>
#include <memory>

#include <gtest/gtest.h>

TEST(MessageDispatcherTest, DispatchesRegisteredHandlerOnBusinessPool) {
    app::logging::init("project_tests");

    boost::asio::thread_pool pool(1);
    net::MessageDispatcher dispatcher(pool);

    auto promise = std::make_shared<std::promise<std::string>>();
    auto future = promise->get_future();

    EXPECT_TRUE(dispatcher.register_handler(42, [promise](const net::DispatchContext& context) {
        promise->set_value(context.body);
    }));

    EXPECT_TRUE(dispatcher.dispatch(std::shared_ptr<net::Session>{}, 42, 1, 0, "business_payload"));

    pool.join();
    EXPECT_EQ(future.get(), "business_payload");
}

TEST(MessageDispatcherTest, RejectsDuplicateHandlerRegistration) {
    app::logging::init("project_tests");

    boost::asio::thread_pool pool(1);
    net::MessageDispatcher dispatcher(pool);

    EXPECT_TRUE(dispatcher.register_handler(7, [](const net::DispatchContext&) {}));
    EXPECT_FALSE(dispatcher.register_handler(7, [](const net::DispatchContext&) {}));
}

TEST(MessageDispatcherTest, MiddlewareCanBlockMessageBeforeHandlerRuns) {
    app::logging::init("project_tests");

    boost::asio::thread_pool pool(1);
    net::MessageDispatcher dispatcher(pool);

    auto promise = std::make_shared<std::promise<bool>>();
    auto future = promise->get_future();

    dispatcher.register_middleware("block_all", [promise](const net::DispatchContext&) {
        promise->set_value(true);
        return false;
    });

    EXPECT_TRUE(dispatcher.register_handler(99, [](const net::DispatchContext&) {
        FAIL() << "Handler should not run when middleware blocks the message.";
    }));

    EXPECT_TRUE(dispatcher.dispatch(std::shared_ptr<net::Session>{}, 99, 2, 0, "blocked_payload"));

    pool.join();
    EXPECT_TRUE(future.get());
    EXPECT_EQ(dispatcher.middleware_count(), 1U);
}

// v1.1.3 / T05：ingress 在 asio::post 到业务池之前同步执行。
TEST(MessageDispatcherTest, IngressMiddlewareRunsSynchronouslyBeforeBusinessPool) {
    app::logging::init("project_tests");

    boost::asio::io_context io_ctx;
    boost::asio::thread_pool pool(1);
    net::MessageDispatcher dispatcher(pool);

    auto session = std::make_shared<net::Session>(net::tcp::socket(io_ctx));

    std::atomic<int> order{0};
    std::atomic<int> ingress_phase{0};

    dispatcher.register_ingress_middleware("ingress", [&](const net::DispatchContext&) {
        ingress_phase.store(1, std::memory_order_release);
        order.fetch_add(1, std::memory_order_acq_rel);
        return false;  // blocked — handler must not run
    });

    EXPECT_TRUE(dispatcher.register_handler(
        55, [&](const net::DispatchContext&) {
            order.fetch_add(10, std::memory_order_acq_rel);
            FAIL() << "handler ran after ingress blocked";
        }));

    EXPECT_TRUE(dispatcher.dispatch(session, 55, 1, 0, "x"));

    // Ingress ran on this thread synchronously before any pool work could start.
    EXPECT_EQ(ingress_phase.load(std::memory_order_acquire), 1);
    EXPECT_EQ(order.load(), 1);

    pool.join();
    EXPECT_EQ(order.load(), 1);
}

TEST(MessageDispatcherTest, IngressSkippedWhenSessionIsNull_InternalBusStyle) {
    app::logging::init("project_tests");

    boost::asio::thread_pool pool(1);
    net::MessageDispatcher dispatcher(pool);

    dispatcher.register_ingress_middleware(
        "must_not_run",
        [](const net::DispatchContext&) -> bool {
            ADD_FAILURE() << "ingress must be skipped when session is nullptr";
            return false;
        });

    auto promise = std::make_shared<std::promise<bool>>();
    auto future = promise->get_future();

    EXPECT_TRUE(dispatcher.register_handler(77, [promise](const net::DispatchContext&) {
        promise->set_value(true);
    }));

    EXPECT_TRUE(dispatcher.dispatch({}, 77, 1, 0, "internal_bus_style"));

    pool.join();
    EXPECT_TRUE(future.get());
    EXPECT_EQ(dispatcher.ingress_middleware_count(), 1U);
}
