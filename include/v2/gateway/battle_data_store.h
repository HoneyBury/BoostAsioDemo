#pragma once

#include "v2/gateway/runtime.h"

#include <filesystem>
#include <optional>
#include <string>

namespace v2::gateway {

class BattleArchiveSink {
public:
    virtual ~BattleArchiveSink() = default;

    // High-level: persist full archive (legacy, delegates to individual methods).
    virtual bool persist(const Runtime::BattleArchive& archive) = 0;

    // Low-level: individual artifact persistence with versioned formatting.
    virtual bool save_replay(const std::string& battle_id, std::string_view replay_json) = 0;
    virtual std::optional<std::string> load_replay(const std::string& battle_id) = 0;
    virtual bool save_result(const std::string& battle_id, std::string_view result_json) = 0;
    virtual std::optional<std::string> load_result(const std::string& battle_id) = 0;
    virtual bool save_snapshot(const std::string& battle_id, std::string_view snapshot_json) = 0;
    virtual std::optional<std::string> load_snapshot(const std::string& battle_id) = 0;
};

class JsonFileBattleDataStore final : public BattleArchiveSink {
public:
    explicit JsonFileBattleDataStore(std::filesystem::path dir);

    bool persist(const Runtime::BattleArchive& archive) override;

    bool save_replay(const std::string& battle_id, std::string_view replay_json) override;
    std::optional<std::string> load_replay(const std::string& battle_id) override;

    bool save_result(const std::string& battle_id, std::string_view result_json) override;
    std::optional<std::string> load_result(const std::string& battle_id) override;

    bool save_snapshot(const std::string& battle_id, std::string_view snapshot_json) override;
    std::optional<std::string> load_snapshot(const std::string& battle_id) override;

private:
    std::filesystem::path dir_;
    std::filesystem::path replay_dir_;
    std::filesystem::path result_dir_;
    std::filesystem::path snapshot_dir_;
};

}  // namespace v2::gateway
