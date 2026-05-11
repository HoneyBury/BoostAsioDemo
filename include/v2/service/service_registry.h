#pragma once

#include "v2/service/service_id.h"

#include <chrono>
#include <cstdint>
#include <shared_mutex>
#include <string>
#include <vector>

namespace v2::service {

struct ServiceInstance {
    ServiceId service_id = ServiceId::kGateway;
    std::string host;
    std::uint16_t port = 0;
    bool healthy = true;
    std::chrono::steady_clock::time_point last_heartbeat;
    std::chrono::steady_clock::time_point registered_at;
};

class ServiceRegistry {
public:
    explicit ServiceRegistry(
        std::chrono::milliseconds default_ttl = std::chrono::seconds(30));

    void register_instance(ServiceId id, std::string host, std::uint16_t port);
    void heartbeat(ServiceId id, const std::string& host, std::uint16_t port);
    void mark_unhealthy(ServiceId id, const std::string& host,
                        std::uint16_t port);

    [[nodiscard]] std::vector<ServiceInstance> healthy_instances(
        ServiceId id) const;
    [[nodiscard]] std::vector<ServiceInstance> unhealthy_instances(
        ServiceId id) const;
    [[nodiscard]] std::vector<ServiceInstance> all_instances() const;

    std::size_t purge_expired();

    [[nodiscard]] std::size_t instance_count() const;

private:
    mutable std::shared_mutex mutex_;
    std::vector<ServiceInstance> instances_;
    std::chrono::milliseconds default_ttl_;
};

}  // namespace v2::service
