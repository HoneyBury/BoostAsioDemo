#!/usr/bin/env python3
"""Prepare a repository-local Conan home with offline/cache/internal-remote defaults."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CONAN_HOME = ROOT / ".conan2-local"
DEFAULT_REMOTES_FILE = ROOT / "conan" / "remotes.example.json"


def run(cmd: list[str], env: dict[str, str]) -> None:
    completed = subprocess.run(cmd, cwd=ROOT, env=env, check=False)
    if completed.returncode != 0:
        raise SystemExit(completed.returncode)


def conan_env(conan_home: Path) -> dict[str, str]:
    env = os.environ.copy()
    env["CONAN_HOME"] = str(conan_home)
    return env


def configure_remotes(conan_home: Path, remotes_file: Path, allow_public: bool) -> None:
    env = conan_env(conan_home)
    run(["conan", "remote", "disable", "*"], env)

    payload = json.loads(remotes_file.read_text(encoding="utf-8"))
    for remote in payload.get("remotes", []):
        name = str(remote["name"])
        url = str(remote["url"])
        verify_ssl = bool(remote.get("verify_ssl", True))
        enabled = bool(remote.get("enabled", True))
        if name == "conancenter" and not allow_public:
            enabled = False
        run(["conan", "remote", "add", name, url, "--force"], env)
        if not verify_ssl:
            run(["conan", "remote", "update", name, "--insecure"], env)
        if not enabled:
            run(["conan", "remote", "disable", name], env)
        else:
            run(["conan", "remote", "enable", name], env)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--conan-home", type=Path, default=DEFAULT_CONAN_HOME)
    parser.add_argument("--remotes-file", type=Path, default=DEFAULT_REMOTES_FILE)
    parser.add_argument("--allow-public", action="store_true", help="Enable public conancenter if present in remotes file.")
    parser.add_argument("--reset-home", action="store_true", help="Delete the target Conan home before bootstrapping.")
    parser.add_argument("--skip-profile-detect", action="store_true")
    args = parser.parse_args()

    conan_home = args.conan_home if args.conan_home.is_absolute() else ROOT / args.conan_home
    remotes_file = args.remotes_file if args.remotes_file.is_absolute() else ROOT / args.remotes_file
    conan_home.mkdir(parents=True, exist_ok=True)

    if args.reset_home and conan_home.exists():
        shutil.rmtree(conan_home)
        conan_home.mkdir(parents=True, exist_ok=True)

    env = conan_env(conan_home)

    if not args.skip_profile_detect:
        run(["conan", "profile", "detect", "--force"], env)

    if remotes_file.exists():
        configure_remotes(conan_home, remotes_file, allow_public=args.allow_public)

    print(f"conan bootstrap complete: CONAN_HOME={conan_home}")
    print(f"remotes file: {remotes_file}")
    print(f"public remotes enabled: {args.allow_public}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
