#!/usr/bin/env python3
"""Require immutable version bumps for changed native plugin modules."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MODULES = ROOT
DESCRIPTOR_VERSION = re.compile(r'\.version\s*=\s*"([^"]+)"')
SDK_PATH = "sdk"
SDK_ABI_CONTRACT = "abi-contract.json"


def git(*args: str, cwd: Path = ROOT, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", *args],
        cwd=cwd,
        check=check,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def metadata_at(ref: str, relative_path: str) -> dict[str, object] | None:
    result = git("show", f"{ref}:{relative_path}", check=False)
    if result.returncode != 0:
        return None
    return json.loads(result.stdout)


def changed(base_ref: str, path: str) -> bool:
    return git("diff", "--quiet", base_ref, "--", path, check=False).returncode != 0


def gitlink_at(ref: str) -> str | None:
    result = git("rev-parse", "--verify", f"{ref}:{SDK_PATH}", check=False)
    return result.stdout.strip() if result.returncode == 0 else None


def sdk_contract_changed(base_ref: str) -> bool:
    """An ABI contract change makes every package version-sensitive."""
    old_commit = gitlink_at(base_ref)
    new_commit = gitlink_at("HEAD")
    if not old_commit or not new_commit:
        return True
    if old_commit == new_commit:
        return False
    result = git(
        "diff", "--quiet", old_commit, new_commit, "--", SDK_ABI_CONTRACT,
        cwd=ROOT / SDK_PATH,
        check=False,
    )
    return result.returncode != 0


def descriptor_versions(module_dir: Path) -> set[str]:
    versions: set[str] = set()
    for source in (module_dir / "main").glob("*.c"):
        versions.update(DESCRIPTOR_VERSION.findall(source.read_text(encoding="utf-8")))
    return versions


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-ref", required=True)
    args = parser.parse_args()

    if git("rev-parse", "--verify", args.base_ref, check=False).returncode != 0:
        print(f"Unknown base ref: {args.base_ref}", file=sys.stderr)
        return 2

    shared_changed = sdk_contract_changed(args.base_ref)
    errors: list[str] = []
    checked = 0

    for metadata_path in sorted(MODULES.glob("*/module.json")):
        module_dir = metadata_path.parent
        relative_metadata = metadata_path.relative_to(ROOT).as_posix()
        relative_module = module_dir.relative_to(ROOT).as_posix()
        current = json.loads(metadata_path.read_text(encoding="utf-8"))
        if current.get("publish", True) is False:
            continue

        checked += 1
        module_id = current["id"]
        version = current["version"]
        versions = descriptor_versions(module_dir)
        if versions != {version}:
            errors.append(
                f"{module_id}: module.json version {version} does not match "
                f"the exported descriptor version(s) {sorted(versions)}"
            )

        previous = metadata_at(args.base_ref, relative_metadata)
        module_changed = shared_changed or changed(args.base_ref, relative_module)
        if previous is not None and module_changed and previous.get("version") == version:
            reason = "SDK ABI contract changed" if shared_changed else "plugin files changed"
            errors.append(
                f"{module_id}: {reason}, but version is still {version}; "
                "bump module.json and the module descriptor version"
            )

    if errors:
        print("Plugin version validation failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    print(f"Validated immutable versions for {checked} publishable plugins.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
