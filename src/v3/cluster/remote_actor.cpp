// v3.0.0 D2: RemoteActorRef::tell() implementation.
// Routes messages to local ActorRef or serializes through the default transport.

#include "v3/cluster/remote_actor.h"

#include <spdlog/spdlog.h>

namespace v3::cluster {
namespace {

/// Default transport used by RemoteActorRef for remote message delivery.
/// Set via RemoteActorRef::set_default_transport().
RemoteActorTransport* g_default_transport = nullptr;

}  // anonymous namespace

void RemoteActorRef::set_default_transport(RemoteActorTransport* transport) {
    g_default_transport = transport;
}

void RemoteActorRef::tell(v2::actor::Message msg) const {
    if (is_local()) {
        // Local delivery: forward to the underlying ActorRef.
        if (local_ref_.is_valid()) {
            local_ref_.tell(std::move(msg));
        }
        return;
    }

    // Remote delivery: serialize and send through the default transport.
    if (!g_default_transport) {
        SPDLOG_WARN(
            "RemoteActorRef::tell() called for remote actor {} on node '{}' "
            "but no default transport is set. Message dropped.",
            remote_actor_id_, node_.node_name);
        return;
    }

    auto serialized = RemoteActorTransport::serialize(msg);
    g_default_transport->send_to_node(node_, serialized);
}

}  // namespace v3::cluster
