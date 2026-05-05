#pragma once

#include "net/session.h"
#include "net/tls_config.h"

#include <boost/asio/ssl.hpp>

namespace net {

// TLS-enabled acceptor wrapper.
// When TLS is enabled, performs async SSL handshake after accept,
// then passes the SSL stream to Session.
class TlsAcceptor {
public:
    TlsAcceptor(asio::io_context& io, std::uint16_t port, const TlsConfig& cfg)
        : acceptor_(io, tcp::endpoint(tcp::v4(), port)), tls_enabled_(cfg.enabled) {
        if (tls_enabled_) {
            ssl_ctx_ = std::make_unique<asio::ssl::context>(asio::ssl::context::tlsv12_server);
            ssl_ctx_->set_options(asio::ssl::context::default_workarounds |
                                  asio::ssl::context::no_sslv2 |
                                  asio::ssl::context::single_dh_use);
            if (!cfg.cert_chain_path.empty()) {
                ssl_ctx_->use_certificate_chain_file(cfg.cert_chain_path);
            }
            if (!cfg.private_key_path.empty()) {
                ssl_ctx_->use_private_key_file(cfg.private_key_path, asio::ssl::context::pem);
            }
        }
    }

    tcp::acceptor& acceptor() { return acceptor_; }
    bool tls_enabled() const { return tls_enabled_; }
    asio::ssl::context* ssl_context() { return ssl_ctx_.get(); }

private:
    tcp::acceptor acceptor_;
    bool tls_enabled_;
    std::unique_ptr<asio::ssl::context> ssl_ctx_;
};

}  // namespace net
