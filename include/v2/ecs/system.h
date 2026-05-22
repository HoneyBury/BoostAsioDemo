#pragma once

#include "v2/perf/hot_path.h"

namespace v2::ecs {

struct FrameContext;
class World;

class System {
public:
    virtual ~System() = default;
    BOOST_HOT_PATH virtual void run(World& world, const FrameContext& ctx) = 0;
};

}  // namespace v2::ecs
