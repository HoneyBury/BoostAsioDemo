#!/usr/bin/env python3
"""Validate production configuration governance conventions.

This gate intentionally avoids third-party JSON schema dependencies so it can
run in a fresh CI or server shell. It checks the parts that have caused real
operability drift in this project: expected files, service names, ports, and
documented schema ownership.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ENVIRONMENTS = ("local", "docker", "production")
SERVICES = {
    "login": 9202,
    "room": 9302,
    "battle": 9303,
    "matchmaking": 9304,
    "leaderboard": 9305,
}


def fail(message: str) -> None:
    print(f"FAIL: {message}")
    sys.exit(1)


def load_json(path: Path) -> dict:
    try:
        with path.open("r", encoding="utf-8") as handle:
            return json.load(handle)
    except Exception as exc:  # pragma: no cover - diagnostics path
        fail(f"{path.relative_to(ROOT)} is not valid JSON: {exc}")


def require_file(path: Path) -> None:
    if not path.is_file():
        fail(f"missing {path.relative_to(ROOT)}")


def check_gateway(env: str) -> None:
    path = ROOT / "config" / "environments" / env / "gateway.json"
    require_file(path)
    doc = load_json(path)
    gateway = doc.get("gateway", {})
    backends = doc.get("backends", {})
    port = gateway.get("port")
    if not isinstance(port, int) or not 1 <= port <= 65535:
        fail(f"{path.relative_to(ROOT)} gateway.port must be a valid TCP port")
    for service, expected_port in SERVICES.items():
        backend_key = "match" if service == "matchmaking" else service
        backend = backends.get(backend_key, {})
        if backend.get("port") != expected_port:
            fail(
                f"{path.relative_to(ROOT)} backends.{backend_key}.port "
                f"must be {expected_port}"
            )


def check_backend(env: str, service: str, expected_port: int) -> None:
    path = ROOT / "config" / "environments" / env / f"{service}.json"
    require_file(path)
    doc = load_json(path)
    service_doc = doc.get("service", {})
    if service_doc.get("name") != service:
        fail(f"{path.relative_to(ROOT)} service.name must be {service}")
    if service_doc.get("port") != expected_port:
        fail(f"{path.relative_to(ROOT)} service.port must be {expected_port}")
    version = service_doc.get("config_version")
    if not isinstance(version, str) or not version:
        fail(f"{path.relative_to(ROOT)} service.config_version is required")

    if service == "room":
        max_frames = doc.get("battle", {}).get("max_frames")
        if not isinstance(max_frames, int) or max_frames <= 0:
            fail(f"{path.relative_to(ROOT)} battle.max_frames must be positive")
    if service == "leaderboard":
        redis = doc.get("redis", {})
        redis_port = redis.get("port")
        if not isinstance(redis_port, int) or not 1 <= redis_port <= 65535:
            fail(f"{path.relative_to(ROOT)} redis.port must be a valid TCP port")


def check_schema_files() -> None:
    require_file(ROOT / "config" / "schemas" / "gateway.schema.json")
    require_file(ROOT / "config" / "schemas" / "backend-service.schema.json")
    for service in SERVICES:
        require_file(ROOT / "config" / "schemas" / f"{service}.schema.json")


def main() -> int:
    check_schema_files()
    for env in ENVIRONMENTS:
        check_gateway(env)
        for service, port in SERVICES.items():
            check_backend(env, service, port)
    require_file(ROOT / "config" / "secrets" / ".env.example")
    print("PASS: configuration governance checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
