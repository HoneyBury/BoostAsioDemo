#include "game/gateway/admin_service.h"

#include "app/audit_log.h"
#include "net/message_dispatcher.h"
#include "net/protocol.h"

#include <fmt/format.h>

#include <algorithm>
#include <string>
#include <string_view>

namespace game::gateway {
namespace {

// Avoid breaking audit JSON further: strip quotes/control from body excerpt (matrix §4.4 仍为近似 JSON）。
std::string clipped_payload_excerpt(std::string_view raw, std::size_t max_chars = 64) {
    std::string out;
    out.reserve(std::min(raw.size(), max_chars) + 1);
    for (char c : raw.substr(0, max_chars)) {
        if (c == '"' || c == '\\' || c == '\r' || c == '\n' || c == '\t') {
            out.push_back('_');
        } else {
            out.push_back(c);
        }
    }
    return out;
}

void audit_admin_invoke(const net::DispatchContext& ctx, std::string_view action_name,
                       const std::string& excerpt_field) {
    const std::string peer = ctx.session ? ctx.session->remote_endpoint() : "none";
    const std::string details =
        excerpt_field.empty()
            ? fmt::format("layer=L3_admin action={} outcome=accepted actor_endpoint={} request_id={} trace_id={}",
                          action_name, peer, ctx.request_id, ctx.trace_id)
            : fmt::format("layer=L3_admin action={} outcome=accepted actor_endpoint={} request_id={} trace_id={} {}",
                          action_name, peer, ctx.request_id, ctx.trace_id, excerpt_field);
    AUDIT_LOG("admin_invoke", details);
}

}  // namespace

void AdminService::register_handlers(net::MessageDispatcher& dispatcher) {
    dispatcher.register_handler(net::protocol::kAdminServerStatus,
        [this](const net::DispatchContext& ctx) {
            audit_admin_invoke(ctx, "server_status", {});
            auto status = on_status_ ? on_status_() : "{}";
            if (ctx.session) {
                if (push_service_ != nullptr) {
                    push_service_->send_ok(ctx.session,
                                           net::protocol::kAdminResponse,
                                           ctx.request_id,
                                           std::move(status));
                } else {
                    ctx.session->send(net::protocol::kAdminResponse, ctx.request_id, 0, std::move(status));
                }
            }
        });

    dispatcher.register_handler(net::protocol::kAdminReloadConfig,
        [this](const net::DispatchContext& ctx) {
            audit_admin_invoke(ctx, "reload_config",
                               fmt::format("payload_excerpt={}", clipped_payload_excerpt(ctx.body)));
            if (on_reload_) {
                on_reload_();
            }
            if (ctx.session) {
                if (push_service_ != nullptr) {
                    push_service_->send_ok(ctx.session,
                                           net::protocol::kAdminResponse,
                                           ctx.request_id,
                                           "reload_ok");
                } else {
                    ctx.session->send(net::protocol::kAdminResponse, ctx.request_id, 0, "reload_ok");
                }
            }
        });

    dispatcher.register_handler(net::protocol::kAdminKickPlayer,
        [this](const net::DispatchContext& ctx) {
            audit_admin_invoke(ctx, "kick_player",
                               fmt::format("payload_excerpt={}", clipped_payload_excerpt(ctx.body)));
            if (on_kick_) {
                on_kick_(ctx.body);
            }
            if (ctx.session) {
                if (push_service_ != nullptr) {
                    push_service_->send_ok(ctx.session,
                                           net::protocol::kAdminResponse,
                                           ctx.request_id,
                                           "kick_ok");
                } else {
                    ctx.session->send(net::protocol::kAdminResponse, ctx.request_id, 0, "kick_ok");
                }
            }
        });

    dispatcher.register_handler(net::protocol::kAdminBanIp,
        [this](const net::DispatchContext& ctx) {
            audit_admin_invoke(ctx, "ban_ip", fmt::format("payload_excerpt={}", clipped_payload_excerpt(ctx.body)));
            if (on_ban_) {
                on_ban_(ctx.body, 3600);
            }
            if (ctx.session) {
                if (push_service_ != nullptr) {
                    push_service_->send_ok(ctx.session,
                                           net::protocol::kAdminResponse,
                                           ctx.request_id,
                                           "ban_ok");
                } else {
                    ctx.session->send(net::protocol::kAdminResponse, ctx.request_id, 0, "ban_ok");
                }
            }
        });
}

}  // namespace game::gateway
