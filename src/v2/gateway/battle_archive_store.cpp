#include "v2/gateway/battle_archive_store.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <utility>

namespace v2::gateway {

JsonFileBattleArchiveStore::JsonFileBattleArchiveStore(std::filesystem::path dir)
    : dir_(std::move(dir)),
      replay_store_(dir_ / "replays") {
    std::filesystem::create_directories(dir_);
}

bool JsonFileBattleArchiveStore::persist(const Runtime::BattleArchive& archive) {
    const auto report_path = dir_ / (archive.battle_id + ".report.json");

    nlohmann::json scores = nlohmann::json::array();
    for (const auto& score : archive.result.scores) {
        scores.push_back({
            {"user_id", score.user_id},
            {"score", score.score},
        });
    }

    nlohmann::json report{
        {"battle_id", archive.battle_id},
        {"room_id", archive.room_id},
        {"reason", archive.reason},
        {"triggering_user_id", archive.triggering_user_id},
        {"total_frames", archive.total_frames},
        {"participants", archive.participant_user_ids},
        {"winner_user_id", archive.result.winner_user_id.has_value()
                               ? nlohmann::json(*archive.result.winner_user_id)
                               : nlohmann::json(nullptr)},
        {"scores", std::move(scores)},
    };

    std::ofstream output(report_path);
    if (!output.is_open()) {
        return false;
    }
    output << report.dump(2);
    output.close();
    return replay_store_.save_replay(archive.battle_id, archive.replay_payload);
}

std::optional<std::string> JsonFileBattleArchiveStore::load_report(const std::string& battle_id) const {
    const auto report_path = dir_ / (battle_id + ".report.json");
    std::ifstream input(report_path);
    if (!input.is_open()) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(input), {});
}

}  // namespace v2::gateway
