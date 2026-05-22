#pragma once

#include "v2/battle/runtime_components.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace v2::security {

struct Position {
    std::int32_t x = 0;
    std::int32_t y = 0;
};

struct CheatReport {
    std::string player_id;
    std::string cheat_type;
    std::string details;
    std::int64_t frame;
};

class AntiCheatManager {
public:
    [[nodiscard]] bool detect_teleport(Position old_pos, Position new_pos, int max_delta);
    [[nodiscard]] bool detect_statistical_anomaly(const std::vector<int>& recent_speeds);
    [[nodiscard]] bool validate_damage(int damage, int min, int max);
    [[nodiscard]] bool validate_attack_distance(Position attacker, Position target, int range);
    [[nodiscard]] std::vector<CheatReport> pending_reports();
    void clear_reports();

private:
    std::vector<CheatReport> reports_;
    std::mutex mutex_;
};

}  // namespace v2::security
