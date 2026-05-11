#pragma once

#include <cstdint>
#include <memory>

namespace v2::battle {

class BattleBackendService {
public:
    explicit BattleBackendService(std::uint16_t port);
    ~BattleBackendService();

    void start();
    void stop();
    [[nodiscard]] std::uint16_t local_port() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace v2::battle
