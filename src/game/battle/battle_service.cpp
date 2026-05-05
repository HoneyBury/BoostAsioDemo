#include "game/battle/battle_service.h"

#include "net/protocol.h"

namespace game::battle {

BattleService::BattleService(gateway::SessionManager& session_manager, gateway::GatewayMetrics& metrics)
    : session_manager_(session_manager), metrics_(metrics) {}

void BattleService::register_handlers(net::MessageDispatcher& dispatcher) const {
    dispatcher.register_handler(
        net::protocol::kBattleStartRequest,
        [this](const net::DispatchContext& context) {
            const auto outcome = session_manager_.start_battle(context.session);
            switch (outcome.result) {
                case gateway::SessionManager::StartBattleResult::kOk:
                    metrics_.on_battle_start_success();
                    context.session->send(net::protocol::kBattleStartResponse,
                                          "battle_started:" + outcome.room_id + ":" +
                                              std::to_string(outcome.player_count));
                    return;

                case gateway::SessionManager::StartBattleResult::kNotEnoughPlayers:
                    context.session->send(net::protocol::kErrorResponse, "not_enough_players");
                    return;

                case gateway::SessionManager::StartBattleResult::kAlreadyStarted:
                    context.session->send(net::protocol::kErrorResponse, "battle_already_started");
                    return;

                case gateway::SessionManager::StartBattleResult::kNotInRoom:
                    context.session->send(net::protocol::kErrorResponse, "not_in_room");
                    return;

                case gateway::SessionManager::StartBattleResult::kNotAuthenticated:
                    context.session->send(net::protocol::kErrorResponse, "auth_required");
                    return;

                case gateway::SessionManager::StartBattleResult::kSessionNotFound:
                    context.session->send(net::protocol::kErrorResponse, "session_not_found");
                    return;
            }
        });
}

}  // namespace game::battle
