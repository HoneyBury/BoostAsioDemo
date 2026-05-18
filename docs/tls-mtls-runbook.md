# TLS / mTLS Runbook

更新时间：2026-05-18

本文档对应生产业务闭环 P6。当前项目已经具备 gateway->backend TLS client 配置、security policy、feature flag、证书生成和测试门禁；默认生产链路仍是 plain TCP。不要把默认生产部署描述成已经启用全链路 TLS/mTLS。

## 当前边界

默认配置：

- `feature_flags.v3_tls_enabled.enabled=false`
- `feature_flags.v3_tls_enabled.rollout_percentage=0`
- `security_policy.require_tls=false`

这意味着默认 Docker Compose / K8s 生产链路不强制 TLS。各服务的 `tls_required` / `mtls_required` 是灰度策略声明，只有在全局 `require_tls=true` 且 feature flag 命中时才进入 TLS backend client 路径。

## 证书

开发/预发证书：

```bash
python3 scripts/gen_certs.py
```

产物：

- `certs/ca.crt`
- `certs/server.crt`
- `certs/server.key`

生产不得使用开发 CA。正式环境应由 Vault、云 Secret Manager、cert-manager 或企业 CA 管理，并在发布记录里写清证书指纹、过期时间和轮换窗口。

## 验证入口

TLS profile 边界检查：

```bash
python3 scripts/check_tls_profile.py --generate-dev-certs
```

N4 传输安全与配置治理聚合门禁：

```bash
python3 scripts/check_transport_config_governance.py --generate-dev-certs --summary-path runtime/validation/n4-transport-config-governance-summary.json
```

P5-P8 聚合入口会自动运行该检查：

```bash
python3 scripts/verify_p5_p8_business_closure.py --build-dir build/default --skip-build
```

该检查验证：

- 默认生产 config 没有误开启 TLS。
- leaderboard 保留 mTLS 敏感服务策略。
- gateway bridge 按 `security_policy.require_tls` 和 `v3_tls_enabled` 控制 TLS。
- backend connection 在传入 `tls_config` 时具备 TLS handshake 路径。
- 证书生成器和证书可读性正常。
- 配置治理门禁能发现 Docker/K8s/Helm 与生产配置事实源之间的漂移。

`scripts/verify_production_resilience_gate.py` 也会运行 N4 聚合门禁，默认写出 `runtime/validation/p5-transport-config-governance-summary.json`，用于发布前归档。

## 证书轮换与回滚

生产证书轮换建议使用灰度发布：

1. 在 Secret Manager、Kubernetes Secret 或 Vault 中发布新 CA/server/client 证书，记录指纹与过期时间。
2. 先运行 `scripts/check_tls_profile.py --generate-dev-certs` 验证本地 profile 没有误打开默认 TLS。
3. 在预发或固定 runner 上打开 `security_policy.require_tls=true` 与 `feature_flags.v3_tls_enabled` 灰度比例，运行 SDK full-flow。
4. 观察连接失败率、backend TLS handshake 错误、leaderboard mTLS 拒绝数和证书过期告警。
5. 回滚时先关闭 `v3_tls_enabled`，必要时回退 Secret 版本并滚动重启 gateway/backend。

当前仓库仍没有 backend 服务端 TLS listener 的生产实现，因此证书轮换流程是上线前置流程，不是默认生产链路的启用说明。

## 上线要求

真正启用 TLS/mTLS 前，必须额外完成：

- backend 服务端 TLS listener 与证书加载。
- Compose/K8s TLS profile 中 Secret/volume 挂载。
- SDK full-flow 在 TLS profile 下通过。
- 错误证书、CA 不匹配、服务名不匹配、client cert 缺失的诊断用例。

上述内容未完成前，P6 的交付状态是“安全配置与灰度边界收束完成”，不是“默认生产 TLS transport 已上线”。
