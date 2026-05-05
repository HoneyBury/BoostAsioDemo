#pragma once

#include <filesystem>
#include <spdlog/spdlog.h>
#include <memory>
#include <string>
#include <string_view>

namespace app::logging {

void init(std::string_view app_name,
          const std::filesystem::path& log_directory = "logs");

std::shared_ptr<spdlog::logger> get_logger();

}  // namespace app::logging

#define LOG_TRACE(...) SPDLOG_LOGGER_TRACE(::app::logging::get_logger(), __VA_ARGS__)
#define LOG_DEBUG(...) SPDLOG_LOGGER_DEBUG(::app::logging::get_logger(), __VA_ARGS__)
#define LOG_INFO(...) SPDLOG_LOGGER_INFO(::app::logging::get_logger(), __VA_ARGS__)
#define LOG_WARN(...) SPDLOG_LOGGER_WARN(::app::logging::get_logger(), __VA_ARGS__)
#define LOG_ERROR(...) SPDLOG_LOGGER_ERROR(::app::logging::get_logger(), __VA_ARGS__)
#define LOG_CRITICAL(...) SPDLOG_LOGGER_CRITICAL(::app::logging::get_logger(), __VA_ARGS__)
