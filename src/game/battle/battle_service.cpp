#include "game/battle/battle_service.h"

#include "net/protocol.h"

#include <fmt/format.h>

namespace game::battle {

BattleService::BattleService(gateway::SessionManager& session_manager,
                             gateway::PushService& push_service,
                             room::RoomManager& room_manager,
                             BattleManager& battle_manager,
                             gateway::GatewayMetrics& metrics)
    : session_manager_(session_manager),
      push_service_(push_service),
      room_manager_(room_manager),
      battle_manager_(battle_manager),
      metrics_(metrics) {}

void BattleService::register_handlers(net::MessageDispatcher& dispatcher) const {
    dispatcher.register_handler(
        net::protocol::kBattleStartRequest,
        [this](const net::DispatchContext& context) {
            if (!session_manager_.is_authenticated(context.session)) {
                push_service_.send_error(
                    context.session, context.request_id, net::protocol::ErrorCode::kAuthRequired);
                return;
            }

            const auto room_snapshot = room_manager_.room_snapshot_of(context.session);
            if (!room_snapshot) {
                push_service_.send_error(
                    context.session, context.request_id, net::protocol::ErrorCode::kNotInRoom);
                return;
            }

            if (!room_snapshot->owner || room_snapshot->owner.get() != context.session.get()) {
                push_service_.send_error(
                    context.session, context.request_id, net::protocol::ErrorCode::kNotRoomOwner);
                return;
            }

            std::vector<std::string> player_ids;
            player_ids.reserve(room_snapshot->members.size());
            for (const auto& member : room_snapshot->members) {
                const auto login_context = session_manager_.login_context_of(member.session);
                if (!login_context) {
                    continue;
                }

                if (!member.ready) {
                    push_service_.send_error(
                        context.session, context.request_id, net::protocol::ErrorCode::kNotAllReady);
                    return;
                }

                player_ids.push_back(login_context->user_id);
            }

            const auto outcome = battle_manager_.start_battle(room_snapshot->room_id, std::move(player_ids));
            switch (outcome.result) {
                case BattleManager::StartBattleResult::kOk: {
                    metrics_.on_battle_start_success();
                    push_service_.send_ok(context.session,
                                          net::protocol::kBattleStartResponse,
                                          context.request_id,
                                          fmt::format("battle_started:{}:{}", outcome.room_id, outcome.player_count));
                    broadcast_to_room(outcome.room_id,
                                      net::protocol::kBattleStatePush,
                                      fmt::format("battle_state:started:{}:{}", outcome.room_id, outcome.player_count),
                                      context.session);
                    return;
                }

                case BattleManager::StartBattleResult::kNotEnoughPlayers:
                    push_service_.send_error(
                        context.session, context.request_id, net::protocol::ErrorCode::kNotEnoughPlayers);
                    return;

                case BattleManager::StartBattleResult::kAlreadyStarted:
                    push_service_.send_error(
                        context.session, context.request_id, net::protocol::ErrorCode::kBattleAlreadyStarted);
                    return;

                case BattleManager::StartBattleResult::kNotInRoom:
                    push_service_.send_error(
                        context.session, context.request_id, net::protocol::ErrorCode::kNotInRoom);
                    return;
            }
        });

    dispatcher.register_handler(
        net::protocol::kBattleInputRequest,
        [this](const net::DispatchContext& context) {
            const auto room_id = room_manager_.room_id_of(context.session);
            const auto login_context = session_manager_.login_context_of(context.session);
            if (!room_id || !login_context) {
                push_service_.send_error(
                    context.session, context.request_id, net::protocol::ErrorCode::kNotInRoom);
                return;
            }

            const auto outcome =
                battle_manager_.submit_input(*room_id, login_context->user_id, context.body);
            switch (outcome.result) {
                case BattleManager::SubmitInputResult::kOk:
                    push_service_.send_ok(context.session,
                                          net::protocol::kBattleInputResponse,
                                          context.request_id,
                                          fmt::format("battle_input_accepted:{}:{}", outcome.room_id, outcome.input.sequence));
                    broadcast_to_room(outcome.room_id,
                                      net::protocol::kBattleInputPush,
                                      fmt::format("battle_input:{}:{}:{}:{}",
                                                  outcome.room_id,
                                                  outcome.input.user_id,
                                                  outcome.input.sequence,
                                                  outcome.input.payload),
                                      context.session);
                    return;

                case BattleManager::SubmitInputResult::kBattleNotStarted:
                    push_service_.send_error(
                        context.session, context.request_id, net::protocol::ErrorCode::kBattleNotStarted);
                    return;

                case BattleManager::SubmitInputResult::kPlayerNotInBattle:
                    push_service_.send_error(
                        context.session, context.request_id, net::protocol::ErrorCode::kAuthRequired, "player_not_in_battle");
                    return;
            }
        });
}

void BattleService::broadcast_to_room(const std::string& room_id,
                                      std::uint16_t message_id,
                                      std::string body,
                                      const std::shared_ptr<net::Session>& exclude_session) const {
    for (const auto& member : room_manager_.room_members(room_id)) {
        if (exclude_session && member.get() == exclude_session.get()) {
            continue;
        }

        push_service_.send_push(member, message_id, body);
    }
}

}  // namespace game::battle
