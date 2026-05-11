#pragma once

#include "v2/gateway/backend_metrics.h"
#include "v2/service/service_registry.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace v2::diagnostics {

// 单个检查项结果
struct HealthCheckItem {
    std::string name;          // "backend:login", "backend:room", "service_registry"
    std::string status;        // "pass", "fail", "warn"
    std::string message;       // 人类可读描述
    std::chrono::steady_clock::time_point checked_at;
};

// 整体健康状态
struct HealthStatus {
    std::string status;        // "pass", "fail", "warn"
    std::vector<HealthCheckItem> checks;
    std::chrono::steady_clock::time_point timestamp;

    [[nodiscard]] bool is_healthy() const noexcept { return status != "fail"; }
};

// 健康检查条件配置
struct HealthCheckConfig {
    // 后端至少有此数量健康实例才算 pass
    std::size_t min_healthy_backends_per_service = 0;  // 0 = at least one

    // 服务注册中心至少需要多少实例
    std::size_t min_total_instances = 0;  // 0 = no minimum

    // 是否检查后端指标（成功率过低则 warn）
    bool check_backend_error_rate = true;
    double max_error_rate = 0.50;  // 50% 错误率阈值（包括 timeout+unavailable+errors）
};

class HealthCheck {
public:
    HealthCheck();
    explicit HealthCheck(HealthCheckConfig config);
    ~HealthCheck();

    HealthCheck(const HealthCheck&) = delete;
    HealthCheck& operator=(const HealthCheck&) = delete;

    // 设置数据源
    void set_backend_metrics(std::shared_ptr<v2::gateway::BackendMetrics> metrics);
    void set_service_registry(std::shared_ptr<v2::service::ServiceRegistry> registry);

    // 执行全部检查，返回结构化结果
    [[nodiscard]] HealthStatus check() const;

    // 序列化 HealthStatus 为 JSON（用于 HTTP /health 响应）
    [[nodiscard]] static std::string to_json(const HealthStatus& status);

private:
    HealthCheckItem check_backend(v2::service::ServiceId id,
                                  const std::string& name) const;
    HealthCheckItem check_registry() const;

    HealthCheckConfig config_;
    std::shared_ptr<v2::gateway::BackendMetrics> backend_metrics_;
    std::shared_ptr<v2::service::ServiceRegistry> service_registry_;
};

}  // namespace v2::diagnostics
