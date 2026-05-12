// SDK v4.1.0: C API wrapper around SdkClient for cross-language binding.
#define BOOST_GATEWAY_SDK_EXPORTS
#include "boost_gateway/sdk/c_api.h"
#include "boost_gateway/sdk/client.h"
#include <cstring>

using namespace boost_gateway::sdk;

struct gsdk_client_t { SdkClient client; void* push_ud; void* dc_ud; gsdk_push_callback_t push_cb; gsdk_disconnect_callback_t dc_cb; };

static void copy_str(char* dst, size_t n, const std::string& src) {
    auto len = src.size() < n-1 ? src.size() : n-1;
    std::memcpy(dst, src.c_str(), len); dst[len] = 0;
}

extern "C" {

gsdk_client_t* gsdk_create() { return new gsdk_client_t; }
void gsdk_destroy(gsdk_client_t* c) { delete c; }

int gsdk_connect(gsdk_client_t* c, const char* host, uint16_t port, int32_t ms) {
    return c->client.connect(host, port, std::chrono::milliseconds(ms)) ? 1 : 0;
}
void gsdk_disconnect(gsdk_client_t* c) { c->client.disconnect(); }
int gsdk_is_connected(const gsdk_client_t* c) { return c->client.is_connected() ? 1 : 0; }

void gsdk_on_push(gsdk_client_t* c, gsdk_push_callback_t cb, void* ud) {
    c->push_cb = cb; c->push_ud = ud;
    c->client.on_push([c](const PushMessage& m) { if(c->push_cb) c->push_cb(m.message_id, m.body.c_str(), c->push_ud); });
}
void gsdk_on_disconnect(gsdk_client_t* c, gsdk_disconnect_callback_t cb, void* ud) {
    c->dc_cb = cb; c->dc_ud = ud;
    c->client.on_disconnect([c]() { if(c->dc_cb) c->dc_cb(c->dc_ud); });
}

gsdk_login_result_t gsdk_login(gsdk_client_t* c, const char* uid, const char* tok, int32_t ms) {
    auto r = c->client.login(uid, tok, std::chrono::milliseconds(ms));
    gsdk_login_result_t out{}; out.ok = r.ok; out.error_code = r.error_code;
    copy_str(out.user_id, 64, r.user_id); copy_str(out.display_name, 64, r.display_name); copy_str(out.error_message, 256, r.error_message);
    return out;
}

gsdk_room_result_t gsdk_create_room(gsdk_client_t* c, const char* rid, int32_t ms) {
    auto r = c->client.create_room(rid, std::chrono::milliseconds(ms));
    gsdk_room_result_t out{}; out.ok = r.ok; out.error_code = r.error_code; out.member_count = r.member_count;
    copy_str(out.room_id, 64, r.room_id); copy_str(out.error_message, 256, r.error_message);
    return out;
}
gsdk_room_result_t gsdk_join_room(gsdk_client_t* c, const char* rid, int32_t ms) {
    auto r = c->client.join_room(rid, std::chrono::milliseconds(ms));
    gsdk_room_result_t out{}; out.ok = r.ok; out.error_code = r.error_code;
    copy_str(out.room_id, 64, r.room_id); copy_str(out.error_message, 256, r.error_message);
    return out;
}
gsdk_room_result_t gsdk_leave_room(gsdk_client_t* c, const char* rid, int32_t ms) {
    auto r = c->client.leave_room(rid, std::chrono::milliseconds(ms));
    gsdk_room_result_t out{}; out.ok = r.ok; out.error_code = r.error_code;
    copy_str(out.room_id, 64, r.room_id); copy_str(out.error_message, 256, r.error_message);
    return out;
}
gsdk_room_result_t gsdk_set_ready(gsdk_client_t* c, int ready, int32_t ms) {
    auto r = c->client.set_ready(ready != 0, std::chrono::milliseconds(ms));
    gsdk_room_result_t out{}; out.ok = r.ok; out.error_code = r.error_code;
    copy_str(out.error_message, 256, r.error_message);
    return out;
}

gsdk_battle_start_result_t gsdk_start_battle(gsdk_client_t* c, const char* rid, int32_t ms) {
    auto r = c->client.start_battle(rid, std::chrono::milliseconds(ms));
    gsdk_battle_start_result_t out{}; out.ok = r.ok; out.error_code = r.error_code;
    copy_str(out.battle_id, 64, r.battle_id); copy_str(out.error_message, 256, r.error_message);
    return out;
}
gsdk_battle_input_result_t gsdk_send_battle_input(gsdk_client_t* c, const char* data, int32_t ms) {
    auto r = c->client.send_battle_input(data, std::chrono::milliseconds(ms));
    gsdk_battle_input_result_t out{}; out.ok = r.ok; out.error_code = r.error_code; out.input_seq = r.input_seq;
    copy_str(out.error_message, 256, r.error_message);
    return out;
}

gsdk_echo_result_t gsdk_echo(gsdk_client_t* c, const char* body, int32_t ms) {
    auto r = c->client.echo(body, std::chrono::milliseconds(ms));
    gsdk_echo_result_t out{}; out.ok = r.ok;
    copy_str(out.body, 4096, r.echo_body);
    return out;
}

} // extern "C"
