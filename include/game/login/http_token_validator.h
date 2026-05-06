#pragma once

#include "game/login/token_validator.h"

#include <boost/asio/ip/tcp.hpp>

#include <chrono>
#include <string>

namespace game::login {

class HttpTokenValidator : public TokenValidator {
public:
    HttpTokenValidator(boost::asio::any_io_executor ex,
                       std::string endpoint_url,
                       std::chrono::milliseconds timeout = std::chrono::seconds(3));
    ~HttpTokenValidator() override;

    HttpTokenValidator(const HttpTokenValidator&) = delete;
    HttpTokenValidator& operator=(const HttpTokenValidator&) = delete;

    [[nodiscard]] TokenValidationResult validate(const std::string& user_id,
                                                  const std::string& token,
                                                  const std::optional<std::string>& display_name) const override;

private:
    boost::asio::any_io_executor executor_;
    std::string host_;
    std::string port_;
    std::string path_;
    std::chrono::milliseconds timeout_;
};

}  // namespace game::login
