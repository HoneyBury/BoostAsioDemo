#!/usr/bin/env python3
"""Validate default production mainline boundaries and P2 evidence readiness."""

from __future__ import annotations

import argparse
import json
from datetime import UTC, datetime
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def exists(relative: str) -> bool:
    return (ROOT / relative).exists()


def add(checks: list[dict[str, Any]], name: str, passed: bool, detail: str) -> None:
    checks.append({"name": name, "passed": passed, "detail": detail})


def validate_p0_docs(checks: list[dict[str, Any]]) -> None:
    readme = read("README.md")
    docs = read("docs/README.md")
    current = read("docs/current-state.md")
    root_cmake = read("CMakeLists.txt")
    add(checks, "p0:docs-index-current-state", "current-state.md" in docs, "docs index points to current-state")
    add(checks, "p0:docs-index-archive-policy", "docs/archive/" in docs and "current-state.md" in docs, "docs index documents archive policy")
    add(checks, "p0:current-state-default-chain", "默认生产主链仍是 SDK + TCP gateway" in current, "current-state states the default production chain")
    add(checks, "p0:readme-boostgateway-title", "# BoostGateway" in readme, "README uses BoostGateway title")
    add(checks, "p0:cmake-framework-description", 'DESCRIPTION "Enterprise-grade C++20 realtime service framework"' in root_cmake, "CMake description matches framework positioning")
    add(checks, "p0:legacy-helper-doc-listed", "legacy-helper-inventory.md" in docs, "docs index lists legacy/helper inventory")


def validate_p1_mainline(checks: list[dict[str, Any]]) -> None:
    demo = read("src/v2/gateway/demo_server.cpp")
    runtime = read("src/v2/gateway/runtime.cpp")
    arch = read("docs/architecture-overview.md")
    current = read("docs/current-state.md")

    add(
        checks,
        "p1:demo-server-queues-business-packets",
        "enqueue_packet(session_id, std::move(message));" in demo,
        "DemoServer sends non-fast-path packets into SessionAdapter/GatewayActor",
    )
    for token in ("parse_login_body", "parse_match_body", "parse_leaderboard_submit_body", "send_backend_request("):
        add(checks, f"p1:demo-server-no-{token}", token not in demo, f"DemoServer no longer owns {token}")
    add(
        checks,
        "p1:runtime-owns-bridge-routing",
        "bridge_->route(v2::service::ServiceId::kLogin" in runtime
        and "bridge_->route(v2::service::ServiceId::kRoom" in runtime
        and "bridge_->route(v2::service::ServiceId::kBattle" in runtime
        and "bridge_->route(v2::service::ServiceId::kLeaderboard" in runtime,
        "Runtime owns backend bridge routing for core services",
    )
    add(
        checks,
        "p1:architecture-data-flow",
        "Client -> Gateway Session -> GatewayActor -> GatewayServiceBridge" in arch,
        "architecture overview documents the main request path",
    )
    add(
        checks,
        "p1:grpc-stays-experimental",
        "gRPC" in current and "不进入默认生产链路" in current,
        "current-state keeps gRPC out of the default production chain",
    )
    add(
        checks,
        "p1:tank-plugin-stays-demo",
        "TankBattlePlugin" in current and "不属于默认生产 battle 主链" in current,
        "current-state keeps TankBattlePlugin outside the default battle mainline",
    )


def validate_p2_evidence(checks: list[dict[str, Any]]) -> None:
    manifest = json.loads(read("docs/production-candidate-evidence-manifest.json"))
    ids = {entry.get("id") for entry in manifest.get("evidence", []) if isinstance(entry, dict)}
    for evidence_id in (
        "long_soak_capacity",
        "fixed_runner_release_capacity",
        "preprod_recovery_drill",
        "tls_preprod_multi_run",
    ):
        add(checks, f"p2:manifest:{evidence_id}", evidence_id in ids, f"manifest declares {evidence_id}")

    for script in (
        "scripts/check_script_inventory.py",
        "scripts/check_validation_summary_contract.py",
        "scripts/check_config_source_layout.py",
        "scripts/verify_fixed_runner_release_capacity.py",
        "scripts/verify_preprod_recovery_drill.py",
        "scripts/verify_tls_preprod_multi_run.py",
        "scripts/check_production_evidence_manifest.py",
        "scripts/render_production_readiness_report.py",
    ):
        add(checks, f"p2:script:{script}", exists(script), f"{script} exists")

    workflow = read(".github/workflows/production-evidence.yml")
    for token in (
        "include_redis_live",
        "include_operator_kind",
        "include_capacity_baseline",
        "include_observability_runtime",
        "actions/upload-artifact@v4",
        "scripts/render_validation_summary.py",
    ):
        add(checks, f"p2:workflow:{token}", token in workflow, f"production evidence workflow includes {token}")

    fixed_runner = read("docs/fixed-runner-playbook.md")
    evidence_runner = read("docs/production-evidence-runner.md")
    add(checks, "p2:fixed-runner-r4", "verify_fixed_runner_release_capacity.py" in fixed_runner, "fixed runner playbook documents R4")
    add(checks, "p2:fixed-runner-r5", "verify_preprod_recovery_drill.py" in fixed_runner, "fixed runner playbook documents R5")
    add(checks, "p2:fixed-runner-r6", "verify_tls_preprod_multi_run.py" in fixed_runner, "fixed runner playbook documents R6")
    add(checks, "p2:evidence-manifest-require-fixed-runner", "--require-fixed-runner" in evidence_runner, "evidence runner documents fixed-runner blocking mode")


def validate_p3_governance(checks: list[dict[str, Any]]) -> None:
    inventory = json.loads(read("docs/script-inventory.json"))
    public = set(inventory.get("public_entrypoints", []))
    scripts = inventory.get("scripts", {})
    add(checks, "p3:script-inventory-present", bool(scripts), "script inventory declares scripts")
    for entrypoint in (
        "scripts/verify_release_candidate.py",
        "scripts/check_mainline_readiness.py",
        "scripts/verify_production_candidate_evidence.py",
        "scripts/check_production_evidence_manifest.py",
        "scripts/run_long_soak_capacity.py",
        "scripts/verify_sdk_enterprise_delivery.py",
    ):
        add(checks, f"p3:public-entrypoint:{entrypoint}", entrypoint in public, f"{entrypoint} is public")

    env_readme = read("env/README.md")
    add(checks, "p3:env-source-of-truth", "`env/` is the maintained production configuration source of truth" in env_readme, "env README declares config source of truth")
    add(checks, "p3:legacy-config-boundary", "legacy/reference surfaces" in env_readme, "env README documents legacy config boundary")
    add(checks, "p3:legacy-helper-gate-exists", exists("scripts/check_legacy_helper_inventory.py"), "legacy/helper governance gate exists")
    add(checks, "p3:conan-governance-readme", exists("conan/README.md"), "repository documents Conan governance entrypoints")
    add(checks, "p3:conan-managed-profile", exists("conan/profiles/windows-msvc-x64"), "repository ships a managed Conan profile")
    add(checks, "p3:conan-linux-profile", exists("conan/profiles/linux-gcc-x64"), "repository ships a Linux fixed-runner Conan profile")
    add(checks, "p3:conan-lock-generator", exists("scripts/tools/generate_conan_lock.py"), "repository ships a Conan lockfile generator")
    linux_profile = read("conan/profiles/linux-gcc-x64")
    add(checks, "p3:conan-linux-profile-pins-compiler-version", "compiler.version=" in linux_profile, "Linux fixed-runner Conan profile pins compiler.version")
    release_baseline_workflow = read(".github/workflows/release-baseline.yml")
    add(checks, "p3:conan-linux-lockfile-default", "conan/locks/linux-gcc-x64-release-nogrpc-nosqlite.lock" in release_baseline_workflow, "release baseline workflow defaults to the Linux nosqlite lockfile path")

    examples_cmake = read("examples/CMakeLists.txt")
    root_cmake = read("CMakeLists.txt")
    add(checks, "p3:legacy-examples-option", "BOOST_BUILD_V1_LEGACY_EXAMPLES" in root_cmake, "root CMake declares the legacy examples option")
    add(checks, "p3:legacy-examples-default-off", "if(BOOST_BUILD_V1_LEGACY_EXAMPLES)" in examples_cmake, "legacy examples are gated in examples/CMakeLists.txt")
    add(checks, "p3:legacy-core-default-off", 'option(BOOST_BUILD_V1_LEGACY_CORE "Build legacy v1 core libraries under src/game" OFF)' in root_cmake, "legacy v1 core is opt-in")
    add(checks, "p3:legacy-tests-default-off", 'option(BOOST_BUILD_V1_LEGACY_TESTS "Build legacy v1-root unit/integration tests" OFF)' in root_cmake, "legacy v1-root tests are opt-in")
    add(checks, "p3:sqlite-default-off", 'option(BOOST_BUILD_SQLITE "Build SQLite-backed storage (requires sqlite3)" OFF)' in root_cmake, "default mainline keeps SQLite opt-in")

    src_cmake = read("src/CMakeLists.txt")
    tests_cmake = read("tests/CMakeLists.txt")
    add(
        checks,
        "p3:legacy-core-not-default-subdir",
        "if(BOOST_BUILD_V1_LEGACY_CORE OR BOOST_BUILD_V1_LEGACY_EXAMPLES OR BOOST_BUILD_V1_LEGACY_TESTS)" in src_cmake
        and "add_subdirectory(game)" in src_cmake,
        "src/game is only built behind legacy surface switches",
    )
    add(
        checks,
        "p3:legacy-tests-not-default-subdir",
        "if(BOOST_BUILD_V1_LEGACY_TESTS)" in tests_cmake,
        "root v1 unit/integration tests are only built behind BOOST_BUILD_V1_LEGACY_TESTS",
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--summary-path",
        type=Path,
        default=ROOT / "runtime/validation/mainline-readiness-summary.json",
    )
    args = parser.parse_args()
    summary_path = args.summary_path if args.summary_path.is_absolute() else ROOT / args.summary_path

    checks: list[dict[str, Any]] = []
    validate_p0_docs(checks)
    validate_p1_mainline(checks)
    validate_p2_evidence(checks)
    validate_p3_governance(checks)

    failed = [check for check in checks if not check["passed"]]
    summary = {
        "summary_version": 2,
        "generated_at": datetime.now(UTC).isoformat(timespec="seconds").replace("+00:00", "Z"),
        "overall_pass": not failed,
        "passed": not failed,
        "failed_category": "mainline_readiness" if failed else "",
        "failed_step": failed[0]["name"] if failed else "",
        "total_checks": len(checks),
        "failed_checks": len(failed),
        "checks": checks,
        "artifacts": {"summary_path": str(summary_path)},
    }
    summary_path.parent.mkdir(parents=True, exist_ok=True)
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True), encoding="utf-8")

    print(f"mainline readiness gate: {'PASS' if summary['passed'] else 'FAIL'} ({len(checks) - len(failed)}/{len(checks)} checks)")
    if failed:
        for check in failed:
            print(f"  - {check['name']}: {check['detail']}")
        return 1
    print(f"summary: {summary_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
