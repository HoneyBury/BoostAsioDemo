// v3.0.0 D1: ClusterRouter implementation.

#include "v3/cluster/cluster_router.h"

#include "v3/cluster/remote_actor.h"

#include "app/audit_log.h"
#include "app/logging.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

namespace v3::cluster {

struct ClusterRouter::RemoteRoutingData {
    // RemoteActorRefs keyed by NodeId::node_name.
    // Protected by ClusterRouter::mutex_.
    std::unordered_map<std::string, RemoteActorRef> refs;
};

ClusterRouter::ClusterRouter(HealthCheckConfig config)
    : config_(std::move(config)),
      remote_routing_(std::make_unique<RemoteRoutingData>()) {}

ClusterRouter::~ClusterRouter() = default;

void ClusterRouter::register_service(ServiceInstance instance) {
    std::lock_guard lock(mutex_);
    instance.registered_at = std::chrono::steady_clock::now();
    instance.last_heartbeat = instance.registered_at;
    if (instance.state == ServiceState::kUnknown) {
        instance.state = ServiceState::kHealthy;
    }

    // Capture node info before instance is moved into the route table
    const bool is_remote_node = local_node_id_.has_value() &&
                                instance.node.node_name != local_node_id_->node_name;
    const auto node_name = instance.node.node_name;
    const auto node_host = instance.node.host;
    const auto node_port = instance.node.port;
    const auto svc_name = instance.service_name;

    auto& route = routes_[instance.service_name];
    route.service_name = instance.service_name;

    auto* existing = find_instance(instance.service_name, instance.node);
    if (existing) {
        *existing = std::move(instance);
    } else {
        route.instances.push_back(std::move(instance));
    }

    // Create a RemoteActorRef when a remote node is first discovered
    if (is_remote_node && remote_routing_->refs.find(node_name) == remote_routing_->refs.end()) {
        NodeId remote_node;
        remote_node.node_name = node_name;
        remote_node.host = node_host;
        remote_node.port = node_port;

        remote_routing_->refs.emplace(
            node_name,
            RemoteActorRef::remote(0, remote_node));

        AUDIT_LOG("cluster_remote_node_discovered",
                  "node=" + node_name +
                  " host=" + node_host +
                  ":" + std::to_string(node_port) +
                  " service=" + svc_name);
    }
}

void ClusterRouter::deregister_service(
    const std::string& service_name, const NodeId& node) {
    std::lock_guard lock(mutex_);
    auto it = routes_.find(service_name);
    if (it == routes_.end()) return;
    auto& instances = it->second.instances;
    instances.erase(
        std::remove_if(instances.begin(), instances.end(),
                       [&](const ServiceInstance& si) { return si.node == node; }),
        instances.end());
}

std::optional<ServiceInstance> ClusterRouter::discover(
    const std::string& service_name) {
    std::lock_guard lock(mutex_);
    auto it = routes_.find(service_name);
    if (it == routes_.end()) return std::nullopt;

    auto& instances = it->second.instances;
    std::vector<ServiceInstance*> healthy;
    for (auto& inst : instances) {
        if (inst.state == ServiceState::kHealthy) healthy.push_back(&inst);
    }
    if (healthy.empty()) return std::nullopt;

    auto idx = it->second.round_robin_index++ % healthy.size();
    return *healthy[idx];
}

std::vector<ServiceInstance> ClusterRouter::discover_all(
    const std::string& service_name) {
    std::lock_guard lock(mutex_);
    auto it = routes_.find(service_name);
    if (it == routes_.end()) return {};
    std::vector<ServiceInstance> healthy;
    for (const auto& inst : it->second.instances) {
        if (inst.state == ServiceState::kHealthy) {
            healthy.push_back(inst);
        }
    }
    return healthy;
}

std::unordered_map<std::string, RouteEntry> ClusterRouter::route_table() const {
    std::lock_guard lock(mutex_);
    return routes_;
}

void ClusterRouter::run_health_checks() {
    if (!health_check_fn_) return;

    std::lock_guard lock(mutex_);
    for (auto& [name, route] : routes_) {
        for (auto& inst : route.instances) {
            if (inst.state == ServiceState::kDraining) {
                auto elapsed = std::chrono::steady_clock::now() - inst.last_heartbeat;
                if (elapsed > config_.drain_timeout) {
                    inst.state = ServiceState::kUnhealthy;
                }
                continue;
            }

            bool alive = health_check_fn_(inst.node);
            auto key = name + ":" + inst.node.node_name;

            if (alive) {
                auto& sc = success_counts_[key];
                sc++;
                if (inst.state == ServiceState::kUnhealthy &&
                    sc >= config_.recovery_threshold) {
                    inst.state = ServiceState::kHealthy;
                    sc = 0;
                }
                failure_counts_[key] = 0;
                inst.last_heartbeat = std::chrono::steady_clock::now();
            } else {
                auto& fc = failure_counts_[key];
                fc++;
                if (fc >= config_.failure_threshold) {
                    inst.state = ServiceState::kUnhealthy;
                }
                success_counts_[key] = 0;
            }
        }
    }
}

void ClusterRouter::mark_healthy(
    const std::string& service_name, const NodeId& node) {
    std::lock_guard lock(mutex_);
    auto* inst = find_instance(service_name, node);
    if (inst) inst->state = ServiceState::kHealthy;
}

void ClusterRouter::mark_unhealthy(
    const std::string& service_name, const NodeId& node) {
    std::lock_guard lock(mutex_);
    auto* inst = find_instance(service_name, node);
    if (inst) inst->state = ServiceState::kUnhealthy;
}

void ClusterRouter::start_drain(
    const std::string& service_name, const NodeId& node) {
    std::lock_guard lock(mutex_);
    auto* inst = find_instance(service_name, node);
    if (inst) {
        inst->state = ServiceState::kDraining;
        inst->last_heartbeat = std::chrono::steady_clock::now();
    }
}

void ClusterRouter::set_local_node_id(NodeId node) {
    std::lock_guard lock(mutex_);
    local_node_id_ = std::move(node);
}

bool ClusterRouter::send_to(const std::string& node_id,
                            const std::string& actor_id,
                            const std::string& payload) {
    // Parse the target actor ID from its decimal string representation
    v2::actor::ActorId target_actor = 0;
    try {
        target_actor = static_cast<v2::actor::ActorId>(std::stoull(actor_id));
    } catch (const std::exception&) {
        LOG_WARN("ClusterRouter::send_to: invalid actor_id '{}'", actor_id);
        AUDIT_LOG("cluster_send_to_invalid_actor",
                  "node=" + node_id + " actor_id=" + actor_id);
        return false;
    }

    std::lock_guard lock(mutex_);

    // Prevent sending via the remote path to a node that is actually local
    if (local_node_id_.has_value() && local_node_id_->node_name == node_id) {
        LOG_WARN("ClusterRouter::send_to: node '{}' is local, use local delivery",
                 node_id);
        return false;
    }

    // Look up or lazily create a RemoteActorRef for the target node
    auto ref_it = remote_routing_->refs.find(node_id);
    if (ref_it == remote_routing_->refs.end()) {
        // Search the route table for the node's host/port details
        NodeId target_node;
        bool found = false;
        for (const auto& [name, route] : routes_) {
            (void)name;
            for (const auto& inst : route.instances) {
                if (inst.node.node_name == node_id) {
                    target_node = inst.node;
                    found = true;
                    break;
                }
            }
            if (found) break;
        }

        if (!found) {
            LOG_WARN("ClusterRouter::send_to: unknown node '{}'", node_id);
            AUDIT_LOG("cluster_send_to_unknown_node", "node=" + node_id);
            return false;
        }

        ref_it = remote_routing_->refs
                     .emplace(node_id,
                              RemoteActorRef::remote(target_actor, target_node))
                     .first;

        AUDIT_LOG("cluster_remote_ref_created",
                  "node=" + target_node.node_name +
                      " host=" + target_node.host +
                      ":" + std::to_string(target_node.port));
    }

    // Build the actor message
    v2::actor::Message msg;
    msg.header.kind = v2::actor::MessageKind::kUser;
    msg.header.target_actor = target_actor;
    msg.header.created_at =
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
    msg.payload = payload;

    ref_it->second.tell(std::move(msg));

    AUDIT_LOG("cluster_send_to",
              "node=" + node_id +
                  " actor_id=" + actor_id +
                  " payload_size=" + std::to_string(payload.size()));
    return true;
}

std::size_t ClusterRouter::total_services() const {
    std::lock_guard lock(mutex_);
    return routes_.size();
}

std::size_t ClusterRouter::healthy_count(
    const std::string& service_name) const {
    std::lock_guard lock(mutex_);
    auto it = routes_.find(service_name);
    if (it == routes_.end()) return 0;
    std::size_t count = 0;
    for (auto& inst : it->second.instances) {
        if (inst.state == ServiceState::kHealthy) ++count;
    }
    return count;
}

std::size_t ClusterRouter::unhealthy_count(
    const std::string& service_name) const {
    std::lock_guard lock(mutex_);
    auto it = routes_.find(service_name);
    if (it == routes_.end()) return 0;
    std::size_t count = 0;
    for (auto& inst : it->second.instances) {
        if (inst.state == ServiceState::kUnhealthy) ++count;
    }
    return count;
}

ServiceInstance* ClusterRouter::find_instance(
    const std::string& service_name, const NodeId& node) {
    auto it = routes_.find(service_name);
    if (it == routes_.end()) return nullptr;
    for (auto& inst : it->second.instances) {
        if (inst.node == node) return &inst;
    }
    return nullptr;
}

}  // namespace v3::cluster
