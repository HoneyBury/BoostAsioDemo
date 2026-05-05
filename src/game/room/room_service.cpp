#include "game/room/room_service.h"

#include "net/protocol.h"

namespace game::room {

RoomService::RoomService(gateway::SessionManager& session_manager, gateway::GatewayMetrics& metrics)
    : session_manager_(session_manager), metrics_(metrics) {}

void RoomService::register_handlers(net::MessageDispatcher& dispatcher) const {
    dispatcher.register_handler(
        net::protocol::kRoomJoinRequest,
        [this](const net::DispatchContext& context) {
            const auto outcome = session_manager_.join_room(context.session, context.body);
            switch (outcome.result) {
                case gateway::SessionManager::JoinRoomResult::kOk:
                    metrics_.on_room_join_success();
                    context.session->send(net::protocol::kRoomJoinResponse,
                                          "room_joined:" + outcome.room_id + ":" +
                                              std::to_string(outcome.player_count));
                    return;

                case gateway::SessionManager::JoinRoomResult::kInvalidRoomId:
                    context.session->send(net::protocol::kErrorResponse, "invalid_room_id");
                    return;

                case gateway::SessionManager::JoinRoomResult::kBattleAlreadyStarted:
                    context.session->send(net::protocol::kErrorResponse, "room_in_battle");
                    return;

                case gateway::SessionManager::JoinRoomResult::kNotAuthenticated:
                    context.session->send(net::protocol::kErrorResponse, "auth_required");
                    return;

                case gateway::SessionManager::JoinRoomResult::kSessionNotFound:
                    context.session->send(net::protocol::kErrorResponse, "session_not_found");
                    return;
            }
        });
}

}  // namespace game::room
