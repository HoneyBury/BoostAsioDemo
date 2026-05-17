#!/usr/bin/env python3
"""Validate static BoostGateway Operator manifests used by the P5 gate."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

import yaml


REPO_ROOT = Path(__file__).resolve().parents[1]
REQUIRED_COMPONENTS = ["gateway", "login", "room", "battle", "match", "leaderboard"]
REQUIRED_CONDITIONS = ["Ready", "Progressing", "Degraded", "TLSReady"]
REQUIRED_STATUS_FIELDS = [
    "phase",
    "readyReplicas",
    "desiredReplicas",
    "components",
    "conditions",
]


def load_yaml_documents(path: Path) -> list[dict[str, Any]]:
    with path.open("r", encoding="utf-8") as fh:
        return [doc for doc in yaml.safe_load_all(fh) if isinstance(doc, dict)]


def nested(mapping: dict[str, Any], *keys: str | int) -> Any:
    cur: Any = mapping
    for key in keys:
        if isinstance(key, int):
            if not isinstance(cur, list) or key >= len(cur):
                return None
            cur = cur[key]
        else:
            if not isinstance(cur, dict) or key not in cur:
                return None
            cur = cur[key]
    return cur


def add_check(checks: list[dict[str, Any]], name: str, passed: bool, detail: str = "") -> None:
    checks.append({"name": name, "passed": passed, "detail": detail})


def validate_crd(operator_dir: Path, checks: list[dict[str, Any]]) -> None:
    path = operator_dir / "config/crd/bases/gateway.boost.io_boostgatewayclusters.yaml"
    docs = load_yaml_documents(path)
    crd = docs[0] if docs else {}
    add_check(checks, "crd-kind", crd.get("kind") == "CustomResourceDefinition")
    add_check(checks, "crd-name", nested(crd, "metadata", "name") == "boostgatewayclusters.gateway.boost.io")
    add_check(checks, "crd-status-subresource", nested(crd, "spec", "versions", 0, "subresources", "status") == {})

    spec_props = nested(crd, "spec", "versions", 0, "schema", "openAPIV3Schema", "properties", "spec", "properties")
    status_props = nested(crd, "spec", "versions", 0, "schema", "openAPIV3Schema", "properties", "status", "properties")
    add_check(checks, "crd-spec-components", isinstance(spec_props, dict) and all(name in spec_props for name in REQUIRED_COMPONENTS))
    add_check(checks, "crd-status-fields", isinstance(status_props, dict) and all(name in status_props for name in REQUIRED_STATUS_FIELDS))

    component_items = nested(status_props or {}, "components", "items", "properties")
    add_check(
        checks,
        "crd-component-status-shape",
        isinstance(component_items, dict)
        and all(field in component_items for field in ["name", "kind", "desiredReplicas", "readyReplicas", "availableReplicas"]),
    )


def validate_rbac(operator_dir: Path, checks: list[dict[str, Any]]) -> None:
    docs = load_yaml_documents(operator_dir / "config/rbac/role.yaml")
    service_account = next((doc for doc in docs if doc.get("kind") == "ServiceAccount"), {})
    role = next((doc for doc in docs if doc.get("kind") == "ClusterRole"), {})
    binding = next((doc for doc in docs if doc.get("kind") == "ClusterRoleBinding"), {})
    add_check(checks, "rbac-service-account", nested(service_account, "metadata", "name") == "boostgateway-operator-controller-manager")
    add_check(checks, "rbac-role-binding", nested(binding, "roleRef", "name") == nested(role, "metadata", "name"))

    rules = role.get("rules", [])
    resources = {resource for rule in rules for resource in rule.get("resources", [])}
    required_resources = {
        "boostgatewayclusters",
        "boostgatewayclusters/status",
        "deployments",
        "statefulsets",
        "services",
        "configmaps",
        "secrets",
        "certificates",
        "events",
    }
    add_check(checks, "rbac-required-resources", required_resources.issubset(resources), f"missing={sorted(required_resources - resources)}")


def validate_manager(operator_dir: Path, checks: list[dict[str, Any]]) -> None:
    docs = load_yaml_documents(operator_dir / "config/manager/manager.yaml")
    deployment = docs[0] if docs else {}
    containers = nested(deployment, "spec", "template", "spec", "containers")
    container = containers[0] if isinstance(containers, list) and containers else {}
    ports = {port.get("name"): port.get("containerPort") for port in container.get("ports", [])}
    add_check(checks, "manager-deployment", deployment.get("kind") == "Deployment")
    add_check(checks, "manager-service-account", nested(deployment, "spec", "template", "spec", "serviceAccountName") == "boostgateway-operator-controller-manager")
    add_check(checks, "manager-probe-port", ports.get("probes") == 8081)
    add_check(checks, "manager-metrics-port", ports.get("metrics") == 8080)
    add_check(checks, "manager-health-probes", bool(container.get("readinessProbe")) and bool(container.get("livenessProbe")))


def validate_sample(operator_dir: Path, checks: list[dict[str, Any]]) -> None:
    docs = load_yaml_documents(operator_dir / "config/samples/gateway_v1alpha1_boostgatewaycluster.yaml")
    sample = docs[0] if docs else {}
    spec = sample.get("spec", {})
    add_check(checks, "sample-kind", sample.get("kind") == "BoostGatewayCluster")
    add_check(checks, "sample-components", isinstance(spec, dict) and all(name in spec for name in REQUIRED_COMPONENTS))
    ports_ok = all(isinstance(nested(spec, name, "port"), int) and nested(spec, name, "port") > 0 for name in REQUIRED_COMPONENTS)
    add_check(checks, "sample-component-ports", ports_ok)
    add_check(checks, "sample-gateway-management-port", isinstance(nested(spec, "gateway", "managementPort"), int))


def write_summary(path: Path, checks: list[dict[str, Any]]) -> int:
    failed = [check for check in checks if not check["passed"]]
    summary = {
        "passed": not failed,
        "total_checks": len(checks),
        "failed_checks": len(failed),
        "checks": checks,
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(summary, indent=2, sort_keys=True), encoding="utf-8")
    print(f"operator manifests: {'PASS' if summary['passed'] else 'FAIL'} ({len(checks) - len(failed)}/{len(checks)} checks)")
    if failed:
        for check in failed:
            print(f"  - {check['name']}: {check.get('detail', '')}")
        return 1
    print(f"summary: {path}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--operator-dir", type=Path, default=REPO_ROOT / "operator/boostgateway-operator")
    parser.add_argument("--summary-path", type=Path, default=REPO_ROOT / "runtime/validation/operator-manifests-summary.json")
    args = parser.parse_args()

    checks: list[dict[str, Any]] = []
    operator_dir = args.operator_dir if args.operator_dir.is_absolute() else REPO_ROOT / args.operator_dir
    validate_crd(operator_dir, checks)
    validate_rbac(operator_dir, checks)
    validate_manager(operator_dir, checks)
    validate_sample(operator_dir, checks)
    return write_summary(args.summary_path, checks)


if __name__ == "__main__":
    raise SystemExit(main())
