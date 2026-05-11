#include "v2/diagnostics/health_check.h"

#include <string>
#include <utility>

namespace v2::diagnostics {

namespace {

// Minimal JSON string escaping for safe field values.
std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (auto ch : s) {
        switch (ch) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x",
                             static_cast<unsigned>(ch));
                    out += buf;
                } else {
                    out += ch;
                }
                break;
        }
    }
    return out;
}

}  // anonymous namespace

// ── Construction / destruction ────────────────────────────

HealthCheck::HealthCheck() : HealthCheck(HealthCheckConfig{}) {}

HealthCheck::HealthCheck(HealthCheckConfig config) : config_(std::move(config)) {}

HealthCheck::~HealthCheck() = default;

// ── Data source setters ───────────────────────────────────

void HealthCheck::set_backend_metrics(
    std::shared_ptr<v2::gateway::BackendMetrics> metrics) {
    backend_metrics_ = std::move(metrics);
}

void HealthCheck::set_service_registry(
    std::shared_ptr<v2::service::ServiceRegistry> registry) {
    service_registry_ = std::move(registry);
}

// ── Per-backend check ─────────────────────────────────────

HealthCheckItem HealthCheck::check_backend(
    v2::service::ServiceId id, const std::string& name) const {
    HealthCheckItem item;
    item.name = "backend:" + name;
    item.checked_at = std::chrono::steady_clock::now();

    std::size_t healthy = 0;
    std::size_t unhealthy = 0;

    if (service_registry_) {
        healthy = service_registry_->healthy_instances(id).size();
        unhealthy = service_registry_->unhealthy_instances(id).size();
    }

    // Determine base status from instance health
    const std::size_t required =
        (config_.min_healthy_backends_per_service == 0)
            ? 1
            : config_.min_healthy_backends_per_service;

    if (healthy >= required) {
        item.status = "pass";
        item.message = std::to_string(healthy) + " healthy instance(s)";
    } else if (unhealthy > 0) {
        item.status = "fail";
        item.message = "0 healthy instance(s), " +
                       std::to_string(unhealthy) + " unhealthy";
    } else {
        item.status = "warn";
        item.message = "No backends registered";
    }

    // Check error rate when the backend is nominally healthy
    if (config_.check_backend_error_rate && backend_metrics_ &&
        item.status == "pass") {
        auto snap = backend_metrics_->snapshot(id);
        if (snap.total_requests > 0) {
            const auto failures = snap.total_timeouts +
                                  snap.total_unavailable + snap.total_errors;
            const double error_rate =
                static_cast<double>(failures) /
                static_cast<double>(snap.total_requests);
            if (error_rate > config_.max_error_rate) {
                item.status = "warn";
                item.message += " (error rate " +
                                std::to_string(
                                    static_cast<int>(error_rate * 100.0)) +
                                "% exceeds threshold)";
            }
        }
    }

    return item;
}

// ── Registry check ────────────────────────────────────────

HealthCheckItem HealthCheck::check_registry() const {
    HealthCheckItem item;
    item.name = "service_registry";
    item.checked_at = std::chrono::steady_clock::now();

    std::size_t total = 0;
    if (service_registry_) {
        total = service_registry_->instance_count();
    }

    if (config_.min_total_instances > 0 &&
        total < config_.min_total_instances) {
        item.status = "fail";
        item.message = "Only " + std::to_string(total) +
                       " instance(s) (minimum " +
                       std::to_string(config_.min_total_instances) + ")";
    } else {
        item.status = "pass";
        item.message = std::to_string(total) + " total instance(s)";
    }

    return item;
}

// ── Aggregate check ───────────────────────────────────────

HealthStatus HealthCheck::check() const {
    HealthStatus result;
    result.timestamp = std::chrono::steady_clock::now();

    result.checks.push_back(
        check_backend(v2::service::ServiceId::kLogin, "login"));
    result.checks.push_back(
        check_backend(v2::service::ServiceId::kRoom, "room"));
    result.checks.push_back(
        check_backend(v2::service::ServiceId::kBattle, "battle"));
    result.checks.push_back(check_registry());

    // Aggregate status: fail > warn > pass
    bool has_warn = false;
    for (const auto& chk : result.checks) {
        if (chk.status == "fail") {
            result.status = "fail";
            return result;
        }
        if (chk.status == "warn") {
            has_warn = true;
        }
    }

    result.status = has_warn ? "warn" : "pass";
    return result;
}

// ── JSON serialization (RFC health check response) ────────

std::string HealthCheck::to_json(const HealthStatus& status) {
    std::string json;
    json.reserve(512);

    json += "{\n";
    json += "  \"status\":\"" + json_escape(status.status) + "\",\n";
    json += "  \"checks\":[\n";

    for (std::size_t i = 0; i < status.checks.size(); ++i) {
        if (i > 0) {
            json += ",\n";
        }
        const auto& c = status.checks[i];
        json += "    {";
        json += "\"name\":\"" + json_escape(c.name) + "\",";
        json += "\"status\":\"" + json_escape(c.status) + "\",";
        json += "\"message\":\"" + json_escape(c.message) + "\"";
        json += "}";
    }

    json += "\n  ]\n";
    json += "}\n";

    return json;
}

}  // namespace v2::diagnostics
