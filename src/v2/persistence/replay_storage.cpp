#include "v2/persistence/replay_storage.h"

#include <fstream>
#include <utility>

#include <spdlog/spdlog.h>

namespace v2::persistence {

// -----------------------------------------------------------------------
// Construction / Destruction
// -----------------------------------------------------------------------

ReplayStorage::ReplayStorage(std::filesystem::path storage_dir)
    : storage_dir_(std::move(storage_dir)),
      engine_(std::make_unique<WriteBehindEngine>()) {
    std::filesystem::create_directories(storage_dir_);
    spdlog::info("[ReplayStorage] storage dir: {}",
                 storage_dir_.string());
}

ReplayStorage::~ReplayStorage() = default;

// -----------------------------------------------------------------------
// Write
// -----------------------------------------------------------------------

bool ReplayStorage::store_replay(const std::string& instance_id,
                                  const nlohmann::json& frames_json) {
    const auto path = replay_path(instance_id);
    const std::string payload = frames_json.dump(2);

    // Capture a copy of the path and payload for the async task.
    return engine_->try_push(
        [path = std::move(path), payload = std::move(payload)]() -> bool {
            try {
                std::filesystem::create_directories(path.parent_path());
                std::ofstream ofs(path, std::ios::binary);
                if (!ofs.is_open()) {
                    spdlog::error("[ReplayStorage] cannot open file: {}",
                                  path.string());
                    return false;
                }
                ofs.write(payload.data(),
                          static_cast<std::streamsize>(payload.size()));
                ofs.close();
                return ofs.good();
            } catch (const std::exception& e) {
                spdlog::error("[ReplayStorage] write failed for {}: {}",
                              path.string(), e.what());
                return false;
            }
        });
}

// -----------------------------------------------------------------------
// Read
// -----------------------------------------------------------------------

std::optional<nlohmann::json> ReplayStorage::get_replay(
    const std::string& instance_id) const {
    const auto path = replay_path(instance_id);
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) {
        return std::nullopt;
    }

    try {
        nlohmann::json doc;
        ifs >> doc;
        return doc;
    } catch (const std::exception& e) {
        spdlog::warn("[ReplayStorage] parse error for {}: {}",
                     path.string(), e.what());
        return std::nullopt;
    }
}

// -----------------------------------------------------------------------
// Lifecycle
// -----------------------------------------------------------------------

void ReplayStorage::flush() {
    engine_->flush();
}

std::uint64_t ReplayStorage::drain(std::chrono::milliseconds timeout) {
    return engine_->drain(timeout);
}

// -----------------------------------------------------------------------
// Introspection
// -----------------------------------------------------------------------

WriteBehindMetrics ReplayStorage::metrics() const {
    return engine_->get_metrics();
}

// -----------------------------------------------------------------------
// Internal helpers
// -----------------------------------------------------------------------

std::filesystem::path ReplayStorage::replay_path(
    const std::string& instance_id) const {
    return storage_dir_ / (instance_id + ".json");
}

}  // namespace v2::persistence
