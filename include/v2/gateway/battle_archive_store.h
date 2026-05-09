#pragma once

#include "game/persistence/player_store.h"
#include "v2/gateway/runtime.h"

#include <filesystem>
#include <optional>

namespace v2::gateway {

class BattleArchiveSink {
public:
    virtual ~BattleArchiveSink() = default;
    virtual bool persist(const Runtime::BattleArchive& archive) = 0;
};

class JsonFileBattleArchiveStore final : public BattleArchiveSink {
public:
    explicit JsonFileBattleArchiveStore(std::filesystem::path dir);

    bool persist(const Runtime::BattleArchive& archive) override;
    [[nodiscard]] std::optional<std::string> load_report(const std::string& battle_id) const;

private:
    std::filesystem::path dir_;
    game::persistence::JsonFileBattleReplayStore replay_store_;
};

}  // namespace v2::gateway
