#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include "v3/cluster/tls_config.h"

namespace v2::room {

class RoomBackendService {
public:
    explicit RoomBackendService(std::uint16_t port);
    RoomBackendService(std::uint16_t port, std::uint32_t battle_max_frames);
    ~RoomBackendService();

    void start();
    void stop();
    [[nodiscard]] std::uint16_t local_port() const;
    void set_tls_config(std::optional<v3::cluster::TlsSessionConfig> tls_config);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace v2::room
