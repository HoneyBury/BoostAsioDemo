#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "v2/persistence/writebehind.h"

namespace v2::persistence {

/// Asynchronous replay-data storage that delegates writes to a
/// WriteBehindEngine and performs synchronous file reads.
///
/// File layout (default):  runtime/data/replays/<instance_id>.json
class ReplayStorage {
public:
    /// @param storage_dir  Root directory for replay files.
    ///                     Created on first write if it does not exist.
    explicit ReplayStorage(
        std::filesystem::path storage_dir = "runtime/data/replays");

    ~ReplayStorage();

    // -- Write ---------------------------------------------------------------

    /// Enqueue a replay for async persistence.  Returns false if the
    /// write-behind engine is under backpressure.
    bool store_replay(const std::string& instance_id,
                      const nlohmann::json& frames_json);

    // -- Read ----------------------------------------------------------------

    /// Synchronously read a previously persisted replay from disk.
    /// Returns std::nullopt if the file does not exist or is corrupt.
    std::optional<nlohmann::json> get_replay(
        const std::string& instance_id) const;

    // -- Lifecycle -----------------------------------------------------------

    void flush();
    std::uint64_t drain(std::chrono::milliseconds timeout);

    // -- Introspection -------------------------------------------------------

    WriteBehindMetrics metrics() const;

private:
    std::filesystem::path storage_dir_;
    std::unique_ptr<WriteBehindEngine> engine_;

    std::filesystem::path replay_path(const std::string& instance_id) const;
};

}  // namespace v2::persistence
