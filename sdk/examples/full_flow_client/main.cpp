// SDK Full Business Flow Client
// Runs complete lifecycle against a running gateway server.
//
// Usage: full_flow_client [host] [port]
//   Default: 127.0.0.1:9201
//
// Flow: connect → login → match → room → battle → leaderboard → reconnect
//       + auto match → room → battle flow

#include "boost_gateway/sdk/client.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sdk = boost_gateway::sdk;
using namespace std::chrono_literals;

#define CHECK(expr, msg) do { \
    if (!(expr)) { std::cerr << "FAIL: " << msg << std::endl; return 1; } \
} while(0)

/// Wait for a match to be found by polling match_status.
/// Returns the match_id on success, empty string on timeout.
static std::string wait_for_match_found(
    sdk::SdkClient& client,
    const std::string& user_id,
    const std::string& mode,
    std::chrono::milliseconds timeout = 15s) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        auto status = client.match_status(user_id, mode, 5s);
        if (status.ok) {
            // Parse response to check if matched
            if (status.response_body.find("\"matched\":true") != std::string::npos) {
                // Extract match_id
                auto match_pos = status.response_body.find("\"match_id\":\"");
                if (match_pos != std::string::npos) {
                    auto start = match_pos + 12;  // length of "match_id":"\"
                    auto end = status.response_body.find("\"", start);
                    if (end != std::string::npos) {
                        return status.response_body.substr(start, end - start);
                    }
                }
                return "matched_unknown";
            }
        }
        std::this_thread::sleep_for(200ms);
    }
    return {};  // timeout
}

int main(int argc, char* argv[]) {
    std::string host = argc > 1 ? argv[1] : "127.0.0.1";
    std::uint16_t port = argc > 2 ? static_cast<std::uint16_t>(std::atoi(argv[2])) : 9201;

    std::cout << "=== BoostGateway SDK Full Flow Test ===" << std::endl;
    std::cout << "Target: " << host << ":" << port << std::endl;

    const auto run_id = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto run_suffix = std::to_string(run_id);
    const auto alice_id = "alice_" + run_suffix;
    const auto bob_id = "bob_" + run_suffix;
    const auto room_id = "sdk_test_room_" + std::to_string(run_id);
    const auto reconnect_room_id = "reconnect_room_" + std::to_string(run_id);
    const auto base_score = 9'000'000'000'000LL + static_cast<long long>(run_id % 1'000'000);
    const auto alice_score = base_score;
    const auto bob_score = base_score + 100;

    sdk::SdkClient alice, bob;
    std::mutex push_mutex;
    std::vector<sdk::PushMessage> pushes;
    auto on_push = [&](const sdk::PushMessage& push) {
        std::lock_guard<std::mutex> lock(push_mutex);
        pushes.push_back(push);
        std::cout << "  [PUSH #" << pushes.size() << "] msg_id="
                  << push.message_id << " body=" << push.body << std::endl;
    };
    alice.on_push(on_push);
    bob.on_push(on_push);

    // ═══════════════════════════════════════════════════════════════
    // 1. CONNECT
    // ═══════════════════════════════════════════════════════════════
    std::cout << "\n[1] Connecting..." << std::endl;
    CHECK(alice.connect(host, port, 5s), "Alice connect failed");
    CHECK(bob.connect(host, port, 5s), "Bob connect failed");
    std::cout << "  Both connected." << std::endl;

    // ═══════════════════════════════════════════════════════════════
    // 2. LOGIN
    // ═══════════════════════════════════════════════════════════════
    std::cout << "\n[2] Login..." << std::endl;
    auto alice_login = alice.login(alice_id, "token:" + alice_id, 5s);
    CHECK(alice_login.ok, "Alice login: " + alice_login.error_message);
    std::cout << "  Alice logged in as: " << alice_login.user_id << std::endl;

    auto bob_login = bob.login(bob_id, "token:" + bob_id, 5s);
    CHECK(bob_login.ok, "Bob login: " + bob_login.error_message);
    std::cout << "  Bob logged in as: " << bob_login.user_id << std::endl;

    // ═══════════════════════════════════════════════════════════════
    // 3. ECHO TEST
    // ═══════════════════════════════════════════════════════════════
    std::cout << "\n[3] Echo test..." << std::endl;
    auto echo = alice.echo("Hello from SDK!", 5s);
    CHECK(echo.ok, "Echo failed");
    std::cout << "  Echo: " << echo.echo_body << std::endl;

    // ═══════════════════════════════════════════════════════════════
    // 4. MATCHMAKING (manual join/status/leave)
    // ═══════════════════════════════════════════════════════════════
    std::cout << "\n[4] Matchmaking (manual)..." << std::endl;
    auto alice_match_join = alice.match_join(alice_id, 1200, "1v1", 5s);
    CHECK(alice_match_join.ok, "Alice match join: " + alice_match_join.error_message);
    CHECK(alice_match_join.response_body.find("\"queued\":true") != std::string::npos,
          "Alice match join missing queued=true: " + alice_match_join.response_body);
    auto alice_match_status = alice.match_status(alice_id, "1v1", 5s);
    CHECK(alice_match_status.ok, "Alice match status: " + alice_match_status.error_message);
    CHECK(alice_match_status.response_body.find("\"matched\":false") != std::string::npos ||
              alice_match_status.response_body.find("\"matched\":true") != std::string::npos,
          "Alice match status missing matched field: " + alice_match_status.response_body);

    auto bob_match_join = bob.match_join(bob_id, 1210, "1v1", 5s);
    CHECK(bob_match_join.ok, "Bob match join: " + bob_match_join.error_message);
    std::this_thread::sleep_for(1200ms);
    auto bob_match_status = bob.match_status(bob_id, "1v1", 5s);
    CHECK(bob_match_status.ok, "Bob match status: " + bob_match_status.error_message);
    auto bob_match_leave = bob.match_leave(bob_id, "1v1", 5s);
    CHECK(bob_match_leave.ok, "Bob match leave: " + bob_match_leave.error_message);
    std::cout << "  Match join/status/leave OK." << std::endl;

    // ═══════════════════════════════════════════════════════════════
    // 5. MATCHMAKING → AUTO BATTLE (fully automatic flow)
    // ═══════════════════════════════════════════════════════════════
    std::cout << "\n[5] Auto match → battle flow..." << std::endl;

    // Both players join the match queue
    auto a_join = alice.match_join(alice_id, 1500, "1v1", 5s);
    CHECK(a_join.ok, "Alice auto-match join: " + a_join.error_message);
    std::cout << "  Alice joined match queue." << std::endl;

    auto b_join = bob.match_join(bob_id, 1400, "1v1", 5s);
    CHECK(b_join.ok, "Bob auto-match join: " + b_join.error_message);
    std::cout << "  Bob joined match queue." << std::endl;

    // Wait for match found by polling (server-side auto room creation
    // is not yet deployed, so the client drives the flow)
    std::cout << "  Waiting for match..." << std::endl;
    auto match_id = wait_for_match_found(alice, alice_id, "1v1", 20s);
    CHECK(!match_id.empty(), "Match was not found within timeout");
    std::cout << "  Match found: " << match_id << std::endl;

    // Auto-create room from match
    const auto auto_room_id = "auto_room_" + match_id;
    auto create = alice.create_room(auto_room_id, 5s);
    CHECK(create.ok, "Auto room create: " + create.error_message);
    std::cout << "  Room auto-created: " << auto_room_id << std::endl;

    // Bob auto-joins the room
    auto join = bob.join_room(auto_room_id, 5s);
    CHECK(join.ok, "Bob auto-join room: " + join.error_message);
    std::cout << "  Bob auto-joined room." << std::endl;

    // Both ready up
    CHECK(alice.set_ready(true, 5s).ok, "Alice auto-ready failed");
    CHECK(bob.set_ready(true, 5s).ok, "Bob auto-ready failed");
    std::cout << "  Both auto-ready." << std::endl;

    // Alice starts battle
    auto battle = alice.start_battle(auto_room_id, 5s);
    CHECK(battle.ok, "Auto start battle: " + battle.error_message);
    std::cout << "  Battle auto-started." << std::endl;

    // Send some battle inputs
    std::this_thread::sleep_for(200ms);
    auto move1 = alice.send_battle_input("move:10,20", 5s);
    CHECK(move1.ok, "Alice auto-move rejected: " + move1.error_message);
    auto move2 = bob.send_battle_input("move:30,40", 5s);
    CHECK(move2.ok, "Bob auto-move rejected: " + move2.error_message);
    std::cout << "  Battle inputs sent." << std::endl;

    // Finish battle
    auto finish = alice.send_battle_input("finish:surrender", 5s);
    CHECK(finish.ok, "Auto surrender rejected: " + finish.error_message);
    std::cout << "  Battle finished (surrender)." << std::endl;

    // Leave the auto room
    CHECK(alice.leave_room(auto_room_id, 5s).ok, "Alice auto leave room failed");
    CHECK(bob.leave_room(auto_room_id, 5s).ok, "Bob auto leave room failed");
    std::cout << "  Auto match → battle flow complete." << std::endl;

    // ═══════════════════════════════════════════════════════════════
    // 6. CREATE ROOM (manual)
    // ═══════════════════════════════════════════════════════════════
    std::cout << "\n[6] Create room..." << std::endl;
    auto create_manual = alice.create_room(room_id, 5s);
    CHECK(create_manual.ok, "Create room: " + create_manual.error_message);
    std::cout << "  Room created: " << create_manual.room_id << std::endl;

    // ═══════════════════════════════════════════════════════════════
    // 7. JOIN ROOM
    // ═══════════════════════════════════════════════════════════════
    std::cout << "\n[7] Join room..." << std::endl;
    auto join_manual = bob.join_room(room_id, 5s);
    CHECK(join_manual.ok, "Join room: " + join_manual.error_message);
    std::cout << "  Bob joined: " << join_manual.room_id << std::endl;

    // ═══════════════════════════════════════════════════════════════
    // 8. READY
    // ═══════════════════════════════════════════════════════════════
    std::cout << "\n[8] Ready up..." << std::endl;
    CHECK(alice.set_ready(true, 5s).ok, "Alice ready failed");
    CHECK(bob.set_ready(true, 5s).ok, "Bob ready failed");
    std::cout << "  Both ready." << std::endl;

    // ═══════════════════════════════════════════════════════════════
    // 9. START BATTLE
    // ═══════════════════════════════════════════════════════════════
    std::cout << "\n[9] Start battle..." << std::endl;
    auto battle_manual = alice.start_battle(room_id, 5s);
    CHECK(battle_manual.ok, "Start battle: " + battle_manual.error_message);
    CHECK(battle_manual.error_message.find("battle_started") != std::string::npos,
          "Start battle response missing battle_started: " + battle_manual.error_message);
    std::cout << "  Battle started: " << battle_manual.error_message << std::endl;

    // Drain battle state pushes
    std::this_thread::sleep_for(200ms);

    // ═══════════════════════════════════════════════════════════════
    // 10. BATTLE INPUT
    // ═══════════════════════════════════════════════════════════════
    std::cout << "\n[10] Battle inputs..." << std::endl;
    auto move1_manual = alice.send_battle_input("move:50,50", 5s);
    CHECK(move1_manual.ok, "Alice move rejected: " + move1_manual.error_message);
    std::cout << "  Alice move: accepted" << std::endl;

    auto move2_manual = bob.send_battle_input("move:60,60", 5s);
    CHECK(move2_manual.ok, "Bob move rejected: " + move2_manual.error_message);
    std::cout << "  Bob move: accepted" << std::endl;

    // ═══════════════════════════════════════════════════════════════
    // 11. FINISH BATTLE
    // ═══════════════════════════════════════════════════════════════
    std::cout << "\n[11] Finish battle..." << std::endl;
    auto finish_manual = alice.send_battle_input("finish:surrender", 5s);
    CHECK(finish_manual.ok, "Surrender rejected: " + finish_manual.error_message);
    CHECK(finish_manual.error_message.find("battle_end_accepted:surrender") != std::string::npos,
          "Surrender response missing battle_end_accepted:surrender: " + finish_manual.error_message);
    std::cout << "  Surrender: accepted" << std::endl;

    auto saw_push = [&](const std::string& fragment) {
        std::lock_guard<std::mutex> lock(push_mutex);
        for (const auto& push : pushes) {
            if (push.body.find(fragment) != std::string::npos) return true;
        }
        return false;
    };

    auto after_finish = bob.send_battle_input("move:70,70", 5s);
    CHECK(!after_finish.ok, "Battle input after finish should be rejected");
    CHECK(after_finish.error_message.find("battle_not_started") != std::string::npos ||
              after_finish.error_message.find("BattleNotStarted") != std::string::npos ||
              after_finish.error_message.find("finished") != std::string::npos,
          "Unexpected after-finish rejection body: " + after_finish.error_message);
    std::cout << "  After-finish input rejected as expected: "
              << after_finish.error_message << std::endl;

    CHECK(saw_push("battle_state:kind=started") || saw_push("\"kind\":\"started\""),
          "Missing battle started push");
    CHECK(saw_push("battle_state:kind=settlement") ||
              saw_push("\"kind\":\"battle_finished\""),
          "Missing battle settlement/finished push");
    CHECK(saw_push("battle_state:kind=finished") ||
              saw_push("\"kind\":\"battle_finished\""),
          "Missing battle finished push");
    CHECK(saw_push("reason=surrender") ||
              saw_push("\"reason\":\"surrender\"") ||
              saw_push("\"reason\":\"user_requested\""),
          "Missing finish reason in battle push");

    // ═══════════════════════════════════════════════════════════════
    // 12. LEADERBOARD
    // ═══════════════════════════════════════════════════════════════
    std::cout << "\n[12] Leaderboard..." << std::endl;
    auto top = alice.leaderboard_top(20, 5s);
    CHECK(top.ok, "Leaderboard top: " + top.error_message);
    CHECK(top.response_body.find("\"entries\"") != std::string::npos,
          "Leaderboard top missing entries: " + top.response_body);

    auto wait_for_rank = [&](const std::string& user_id) {
        sdk::LeaderboardQueryResult latest;
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (std::chrono::steady_clock::now() < deadline) {
            latest = alice.leaderboard_rank(user_id, 5s);
            if (latest.ok &&
                latest.response_body.find("\"user_id\":\"" + user_id + "\"") != std::string::npos) {
                return latest;
            }
            std::this_thread::sleep_for(100ms);
        }
        return latest;
    };

    auto rank = wait_for_rank(alice_id);
    CHECK(rank.ok, "Leaderboard rank Alice: " + rank.error_message);
    CHECK(rank.response_body.find("\"user_id\":\"" + alice_id + "\"") != std::string::npos,
          "Leaderboard rank missing Alice: " + rank.response_body);
    auto bob_rank = wait_for_rank(bob_id);
    CHECK(bob_rank.ok, "Leaderboard rank Bob: " + bob_rank.error_message);
    CHECK(bob_rank.response_body.find("\"user_id\":\"" + bob_id + "\"") != std::string::npos,
          "Leaderboard rank missing Bob: " + bob_rank.response_body);
    std::cout << "  Auto settlement leaderboard rank OK." << std::endl;

    auto alice_submit = alice.leaderboard_submit(alice_id, "Alice", alice_score, 5s);
    CHECK(alice_submit.ok, "Manual Alice leaderboard submit: " + alice_submit.error_message);
    auto bob_submit = bob.leaderboard_submit(bob_id, "Bob", bob_score, 5s);
    CHECK(bob_submit.ok, "Manual Bob leaderboard submit: " + bob_submit.error_message);
    std::cout << "  Manual leaderboard submit path OK." << std::endl;

    // ═══════════════════════════════════════════════════════════════
    // 13. LEAVE ROOM
    // ═══════════════════════════════════════════════════════════════
    std::cout << "\n[13] Leave room..." << std::endl;
    CHECK(alice.leave_room(room_id, 5s).ok, "Alice leave failed");
    CHECK(bob.leave_room(room_id, 5s).ok, "Bob leave failed");
    std::cout << "  Both left room." << std::endl;

    // ═══════════════════════════════════════════════════════════════
    // 14. RECONNECT TEST
    // ═══════════════════════════════════════════════════════════════
    std::cout << "\n[14] Reconnect test..." << std::endl;
    CHECK(alice.create_room(reconnect_room_id, 5s).ok, "Create reconnect room failed");
    alice.disconnect();
    std::cout << "  Disconnected. Reconnecting..." << std::endl;
    CHECK(alice.connect(host, port, 5s), "Reconnect failed");
    auto relogin = alice.login(alice_id, "token:" + alice_id, 5s);
    std::cout << "  Relogin: " << (relogin.ok ? "OK" : "FAILED") << std::endl;

    // ═══════════════════════════════════════════════════════════════
    // CLEANUP
    // ═══════════════════════════════════════════════════════════════
    alice.leave_room(reconnect_room_id, 5s);
    alice.disconnect();
    bob.disconnect();

    std::cout << "\n=== ALL TESTS PASSED ===" << std::endl;
    std::cout << "Push messages received: " << pushes.size() << std::endl;
    return 0;
}
