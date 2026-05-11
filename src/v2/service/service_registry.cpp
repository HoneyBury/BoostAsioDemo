#include "v2/service/service_registry.h"

#include <algorithm>
#include <mutex>

namespace v2::service {

ServiceRegistry::ServiceRegistry(std::chrono::milliseconds default_ttl)
    : default_ttl_(default_ttl) {}

void ServiceRegistry::register_instance(ServiceId id, std::string host,
                                         std::uint16_t port) {
    std::unique_lock lock(mutex_);

    auto it = std::find_if(instances_.begin(), instances_.end(),
                           [&](const ServiceInstance& inst) {
                               return inst.service_id == id &&
                                      inst.host == host && inst.port == port;
                           });

    auto now = std::chrono::steady_clock::now();
    if (it != instances_.end()) {
        it->last_heartbeat = now;
        it->healthy = true;
        return;
    }

    instances_.push_back(ServiceInstance{
        .service_id = id,
        .host = std::move(host),
        .port = port,
        .healthy = true,
        .last_heartbeat = now,
        .registered_at = now,
    });
}

void ServiceRegistry::heartbeat(ServiceId id, const std::string& host,
                                 std::uint16_t port) {
    std::unique_lock lock(mutex_);

    auto it = std::find_if(instances_.begin(), instances_.end(),
                           [&](const ServiceInstance& inst) {
                               return inst.service_id == id &&
                                      inst.host == host && inst.port == port;
                           });

    if (it == instances_.end()) {
        auto now = std::chrono::steady_clock::now();
        instances_.push_back(ServiceInstance{
            .service_id = id,
            .host = host,
            .port = port,
            .healthy = true,
            .last_heartbeat = now,
            .registered_at = now,
        });
        return;
    }

    it->last_heartbeat = std::chrono::steady_clock::now();
    it->healthy = true;
}

void ServiceRegistry::mark_unhealthy(ServiceId id, const std::string& host,
                                      std::uint16_t port) {
    std::unique_lock lock(mutex_);

    auto it = std::find_if(instances_.begin(), instances_.end(),
                           [&](const ServiceInstance& inst) {
                               return inst.service_id == id &&
                                      inst.host == host && inst.port == port;
                           });

    if (it != instances_.end()) {
        it->healthy = false;
    }
}

std::vector<ServiceInstance> ServiceRegistry::healthy_instances(
    ServiceId id) const {
    std::shared_lock lock(mutex_);
    std::vector<ServiceInstance> result;
    for (const auto& inst : instances_) {
        if (inst.service_id == id && inst.healthy) {
            result.push_back(inst);
        }
    }
    return result;
}

std::vector<ServiceInstance> ServiceRegistry::unhealthy_instances(
    ServiceId id) const {
    std::shared_lock lock(mutex_);
    std::vector<ServiceInstance> result;
    for (const auto& inst : instances_) {
        if (inst.service_id == id && !inst.healthy) {
            result.push_back(inst);
        }
    }
    return result;
}

std::vector<ServiceInstance> ServiceRegistry::all_instances() const {
    std::shared_lock lock(mutex_);
    return instances_;
}

std::size_t ServiceRegistry::purge_expired() {
    std::unique_lock lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    auto it = std::remove_if(instances_.begin(), instances_.end(),
                             [&](const ServiceInstance& inst) {
                                 return (now - inst.last_heartbeat) >
                                        default_ttl_;
                             });
    auto removed = static_cast<std::size_t>(std::distance(it, instances_.end()));
    instances_.erase(it, instances_.end());
    return removed;
}

std::size_t ServiceRegistry::instance_count() const {
    std::shared_lock lock(mutex_);
    return instances_.size();
}

}  // namespace v2::service
