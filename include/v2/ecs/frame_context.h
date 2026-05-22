#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace v2::ecs {

using FrameNumber = std::uint32_t;

struct FrameContext {
    std::string battle_id;
    std::string room_id;
    FrameNumber frame_number = 0;
    std::string trigger;
    std::chrono::milliseconds tick_interval{33};
};

}  // namespace v2::ecs
