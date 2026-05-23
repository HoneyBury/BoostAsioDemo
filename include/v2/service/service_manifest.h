#pragma once

#include "v2/service/service_id.h"

#include <string>
#include <vector>

namespace v2::service {

struct ServiceManifest {
    ServiceId service_id;
    std::string description;
    std::vector<std::string> owned_state;
    std::vector<std::string> handled_messages;
    std::vector<std::string> read_only_state;
};

[[nodiscard]] ServiceManifest gateway_manifest();
[[nodiscard]] ServiceManifest login_manifest();
[[nodiscard]] ServiceManifest room_manifest();
[[nodiscard]] ServiceManifest battle_manifest();

/// Return all known service manifests.
[[nodiscard]] const std::vector<ServiceManifest>& all_manifests();

[[nodiscard]] ServiceId owner_of(const std::string& state_name);
[[nodiscard]] ServiceId handler_of(const std::string& message_type);

}  // namespace v2::service
