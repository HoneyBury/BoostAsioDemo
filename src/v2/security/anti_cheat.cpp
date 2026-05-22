#include "v2/security/anti_cheat.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numeric>

namespace v2::security {

bool AntiCheatManager::detect_teleport(Position old_pos, Position new_pos, int max_delta) {
    auto dx = std::abs(static_cast<std::int64_t>(new_pos.x) - static_cast<std::int64_t>(old_pos.x));
    auto dy = std::abs(static_cast<std::int64_t>(new_pos.y) - static_cast<std::int64_t>(old_pos.y));
    auto distance = dx + dy;
    if (distance > max_delta) {
        std::scoped_lock lock(mutex_);
        reports_.push_back(CheatReport{
            .player_id = "",
            .cheat_type = "teleport_hack",
            .details = "distance=" + std::to_string(distance) +
                       " max_delta=" + std::to_string(max_delta),
            .frame = 0,
        });
        return false;
    }
    return true;
}

bool AntiCheatManager::detect_statistical_anomaly(const std::vector<int>& recent_speeds) {
    if (recent_speeds.size() < 3) return false;

    double sum = std::accumulate(recent_speeds.begin(), recent_speeds.end(), 0.0);
    double mean = sum / static_cast<double>(recent_speeds.size());

    double sq_sum = 0.0;
    for (auto v : recent_speeds) {
        auto diff = static_cast<double>(v) - mean;
        sq_sum += diff * diff;
    }
    double stddev = std::sqrt(sq_sum / static_cast<double>(recent_speeds.size()));

    double threshold = mean + 2.0 * stddev;

    for (auto v : recent_speeds) {
        if (static_cast<double>(v) > threshold) {
            std::scoped_lock lock(mutex_);
            reports_.push_back(CheatReport{
                .player_id = "",
                .cheat_type = "speed_anomaly",
                .details = "speed=" + std::to_string(v) +
                           " mean=" + std::to_string(mean) +
                           " threshold=" + std::to_string(threshold),
                .frame = 0,
            });
            return true;
        }
    }
    return false;
}

bool AntiCheatManager::validate_damage(int damage, int min, int max) {
    if (damage < min || damage > max) {
        std::scoped_lock lock(mutex_);
        reports_.push_back(CheatReport{
            .player_id = "",
            .cheat_type = "damage_cheat",
            .details = "damage=" + std::to_string(damage) +
                       " range=[" + std::to_string(min) + "," +
                       std::to_string(max) + "]",
            .frame = 0,
        });
        return false;
    }
    return true;
}

bool AntiCheatManager::validate_attack_distance(Position attacker, Position target, int range) {
    auto dx = std::abs(static_cast<std::int64_t>(target.x) - static_cast<std::int64_t>(attacker.x));
    auto dy = std::abs(static_cast<std::int64_t>(target.y) - static_cast<std::int64_t>(attacker.y));
    if (dx > range || dy > range) {
        std::scoped_lock lock(mutex_);
        reports_.push_back(CheatReport{
            .player_id = "",
            .cheat_type = "attack_range_cheat",
            .details = "dx=" + std::to_string(dx) +
                       " dy=" + std::to_string(dy) +
                       " range=" + std::to_string(range),
            .frame = 0,
        });
        return false;
    }
    return true;
}

std::vector<CheatReport> AntiCheatManager::pending_reports() {
    std::scoped_lock lock(mutex_);
    std::vector<CheatReport> result;
    result.swap(reports_);
    return result;
}

void AntiCheatManager::clear_reports() {
    std::scoped_lock lock(mutex_);
    reports_.clear();
}

}  // namespace v2::security
