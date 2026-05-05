#include "app/config.h"

#include "app/logging.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <string_view>

namespace app::config {
namespace {

std::string trim(std::string value) {
    const auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(),
                std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

template <typename T>
std::optional<T> parse_integer(std::string_view value) {
    T parsed{};
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return parsed;
}

}  // namespace

bool ConfigStore::load(const std::filesystem::path& path) {
    values_.clear();

    std::ifstream input(path);
    if (!input.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line.starts_with('#')) {
            continue;
        }

        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }

        auto key = trim(line.substr(0, separator));
        auto value = trim(line.substr(separator + 1));
        if (!key.empty()) {
            values_[std::move(key)] = std::move(value);
        }
    }

    return true;
}

std::optional<std::string> ConfigStore::get_string(const std::string& key) const {
    const auto it = values_.find(key);
    if (it == values_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<std::uint16_t> ConfigStore::get_uint16(const std::string& key) const {
    const auto value = get_string(key);
    return value ? parse_integer<std::uint16_t>(*value) : std::nullopt;
}

std::optional<std::uint32_t> ConfigStore::get_uint32(const std::string& key) const {
    const auto value = get_string(key);
    return value ? parse_integer<std::uint32_t>(*value) : std::nullopt;
}

std::optional<std::size_t> ConfigStore::get_size(const std::string& key) const {
    const auto value = get_string(key);
    return value ? parse_integer<std::size_t>(*value) : std::nullopt;
}

std::optional<std::chrono::milliseconds> ConfigStore::get_milliseconds(const std::string& key) const {
    const auto value = get_string(key);
    const auto parsed = value ? parse_integer<std::uint64_t>(*value) : std::nullopt;
    if (!parsed) {
        return std::nullopt;
    }
    return std::chrono::milliseconds(*parsed);
}

GatewayAppConfig load_gateway_config(const std::filesystem::path& path) {
    GatewayAppConfig config;
    ConfigStore store;
    if (!store.load(path)) {
        LOG_WARN("Gateway config file {} not found, using defaults", path.string());
        return config;
    }

    if (const auto value = store.get_uint16("gateway.port")) {
        config.port = *value;
    }
    if (const auto value = store.get_size("gateway.io_threads")) {
        config.io_threads = std::max<std::size_t>(1, *value);
    }
    if (const auto value = store.get_size("gateway.business_threads")) {
        config.business_threads = std::max<std::size_t>(1, *value);
    }
    if (const auto value = store.get_milliseconds("gateway.metrics_log_interval_ms")) {
        config.metrics_log_interval = *value;
    }
    if (const auto value = store.get_uint32("session.max_packet_size")) {
        config.session_max_packet_size = *value;
    }
    if (const auto value = store.get_size("session.max_pending_write_bytes")) {
        config.session_max_pending_write_bytes = *value;
    }
    if (const auto value = store.get_milliseconds("session.heartbeat_check_interval_ms")) {
        config.session_heartbeat_check_interval = *value;
    }
    if (const auto value = store.get_milliseconds("session.heartbeat_timeout_ms")) {
        config.session_heartbeat_timeout = *value;
    }

    LOG_INFO("Loaded gateway config from {}", path.string());
    return config;
}

}  // namespace app::config
