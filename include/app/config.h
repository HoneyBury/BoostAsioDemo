#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

namespace app::config {

class ConfigStore {
public:
    bool load(const std::filesystem::path& path);

    [[nodiscard]] std::optional<std::string> get_string(const std::string& key) const;
    [[nodiscard]] std::optional<std::uint16_t> get_uint16(const std::string& key) const;
    [[nodiscard]] std::optional<std::uint32_t> get_uint32(const std::string& key) const;
    [[nodiscard]] std::optional<std::size_t> get_size(const std::string& key) const;
    [[nodiscard]] std::optional<std::chrono::milliseconds> get_milliseconds(const std::string& key) const;

private:
    std::unordered_map<std::string, std::string> values_;
};

struct GatewayAppConfig {
    std::uint16_t port = 9000;
    std::size_t io_threads = 2;
    std::size_t business_threads = 2;
    std::chrono::milliseconds metrics_log_interval{5000};
    std::uint32_t session_max_packet_size = 1024 * 1024;
    std::size_t session_max_pending_write_bytes = 256 * 1024;
    std::chrono::milliseconds session_heartbeat_check_interval{5000};
    std::chrono::milliseconds session_heartbeat_timeout{30000};
};

[[nodiscard]] GatewayAppConfig load_gateway_config(const std::filesystem::path& path);

}  // namespace app::config
