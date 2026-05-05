#include "app/logging.h"
#include "app/config.h"

#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

TEST(ConfigTest, LoadsGatewayConfigFromFile) {
    app::logging::init("project_tests");

    const auto path = std::filesystem::temp_directory_path() / "gateway_test.conf";
    {
        std::ofstream output(path);
        output << "gateway.port=9200\n";
        output << "gateway.io_threads=4\n";
        output << "gateway.business_threads=6\n";
        output << "gateway.metrics_log_interval_ms=7000\n";
        output << "session.max_packet_size=2048\n";
        output << "session.max_pending_write_bytes=4096\n";
        output << "session.heartbeat_check_interval_ms=150\n";
        output << "session.heartbeat_timeout_ms=600\n";
    }

    const auto config = app::config::load_gateway_config(path);
    EXPECT_EQ(config.port, 9200);
    EXPECT_EQ(config.io_threads, 4U);
    EXPECT_EQ(config.business_threads, 6U);
    EXPECT_EQ(config.metrics_log_interval, std::chrono::milliseconds(7000));
    EXPECT_EQ(config.session_max_packet_size, 2048U);
    EXPECT_EQ(config.session_max_pending_write_bytes, 4096U);
    EXPECT_EQ(config.session_heartbeat_check_interval, std::chrono::milliseconds(150));
    EXPECT_EQ(config.session_heartbeat_timeout, std::chrono::milliseconds(600));

    std::filesystem::remove(path);
}
