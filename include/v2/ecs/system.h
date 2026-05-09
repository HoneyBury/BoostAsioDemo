#pragma once

namespace v2::ecs {

struct FrameContext;
class World;

class System {
public:
    virtual ~System() = default;
    virtual void run(World& world, const FrameContext& ctx) = 0;
};

}  // namespace v2::ecs
