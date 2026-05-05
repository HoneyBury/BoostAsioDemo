#include "game/login/http_token_validator.h"

#include "app/logging.h"

#include <boost/asio/connect.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/write.hpp>
#include <nlohmann/json.hpp>

#include <memory>

namespace game::login {

namespace beast = boost::beast;
namespace http = beast::http;
using tcp = boost::asio::ip::tcp;
using json = nlohmann::json;

namespace {

std::string build_auth_request_body(const std::string& user_id,
                                     const std::string& token) {
    return json{{"user_id", user_id}, {"token", token}}.dump();
}

TokenValidationResult parse_auth_response(const std::string& body) {
    try {
        const auto doc = json::parse(body);
        if (!doc.value("valid", false)) {
            return {};
        }
        return TokenValidationResult{
            .ok = true,
            .user_id = doc.value("user_id", ""),
            .display_name = doc.value("display_name", ""),
        };
    } catch (const std::exception& ex) {
        LOG_WARN("Failed to parse auth HTTP response: {}", ex.what());
        return {};
    }
}

}  // namespace

HttpTokenValidator::HttpTokenValidator(boost::asio::any_io_executor ex,
                                         std::string endpoint_url,
                                         std::chrono::milliseconds timeout)
    : executor_(std::move(ex)), timeout_(timeout) {
    // Parse endpoint_url: "http://host:port/path"
    std::string_view url = endpoint_url;

    if (url.starts_with("http://")) {
        url.remove_prefix(7);
    } else if (url.starts_with("https://")) {
        url.remove_prefix(8);
    }

    const auto path_pos = url.find('/');
    if (path_pos != std::string_view::npos) {
        path_ = std::string(url.substr(path_pos));
        url = url.substr(0, path_pos);
    } else {
        path_ = "/";
    }

    const auto colon_pos = url.find(':');
    if (colon_pos != std::string_view::npos) {
        host_ = std::string(url.substr(0, colon_pos));
        port_ = std::string(url.substr(colon_pos + 1));
    } else {
        host_ = std::string(url);
        port_ = "80";
    }
}

HttpTokenValidator::~HttpTokenValidator() = default;

TokenValidationResult HttpTokenValidator::validate(const std::string& user_id,
                                                     const std::string& token,
                                                     const std::optional<std::string>& /*display_name*/) const {
    try {
        tcp::resolver resolver(executor_);
        const auto endpoints = resolver.resolve(host_, port_);

        tcp::socket socket(executor_);
        boost::asio::connect(socket, endpoints);

        const auto body = build_auth_request_body(user_id, token);
        http::request<http::string_body> req{http::verb::post, path_, 11};
        req.set(http::field::host, host_);
        req.set(http::field::content_type, "application/json");
        req.body() = body;
        req.prepare_payload();

        http::write(socket, req);

        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(socket, buffer, res);

        socket.close();
        return parse_auth_response(res.body());
    } catch (const std::exception& ex) {
        LOG_WARN("Auth HTTP request failed for {}: {}", user_id, ex.what());
        return {};
    }
}

}  // namespace game::login
