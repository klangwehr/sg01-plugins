#!/usr/bin/env python3
"""Select only the plugin modules that need an ESP-IDF compatibility build."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SDK_PATH = "sdk"
SDK_COMPILED_INTERFACE = "include"


def git(*args: str, cwd: Path = ROOT, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", *args],
        cwd=cwd,
        check=check,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def module_names() -> list[str]:
    names: list[str] = []
    for metadata_path in sorted(ROOT.glob("*/module.json")):
        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
        if metadata.get("publish", True):
            names.append(metadata["id"])
    return names


def gitlink_at(ref: str) -> str | None:
    result = git("rev-parse", "--verify", f"{ref}:{SDK_PATH}", check=False)
    return result.stdout.strip() if result.returncode == 0 else None


def sdk_headers_changed(base_ref: str) -> bool:
    """Compare the pinned SDK include tree, not its documentation or tools.

    The SDK is a submodule, so the parent repository's diff only exposes a
    gitlink change.  Resolve both gitlinks inside the checked-out submodule.
    If the historical commit is unavailable, choose the safe full rebuild.
    """

    old_commit = gitlink_at(base_ref)
    new_commit = gitlink_at("HEAD")
    if not old_commit or not new_commit:
        return True
    if old_commit == new_commit:
        return False
    result = git(
        "diff", "--quiet", old_commit, new_commit, "--", SDK_COMPILED_INTERFACE,
        cwd=ROOT / SDK_PATH,
        check=False,
    )
    # A missing historical submodule commit is deliberately treated as a
    # change: fail closed and rebuild the complete compatibility matrix.
    return result.returncode != 0


def changed_modules(base_ref: str) -> tuple[list[str], bool]:
    available = module_names()
    if sdk_headers_changed(base_ref):
        return available, True

    changed = set(
        git("diff", "--name-only", base_ref, "--").stdout.splitlines()
    )
    selected = [name for name in available if any(
        path == name or path.startswith(f"{name}/") for path in changed
    )]
    return selected, False


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-ref", required=True)
    parser.add_argument("--format", choices=("json", "text"), default="json")
    args = parser.parse_args()

    if git("rev-parse", "--verify", args.base_ref, check=False).returncode != 0:
        print(f"Unknown base ref: {args.base_ref}", file=sys.stderr)
        return 2

    modules, full_rebuild = changed_modules(args.base_ref)
    if args.format == "json":
        print(json.dumps({"modules": modules, "fullRebuild": full_rebuild}, separators=(",", ":")))
    else:
        print("\n".join(modules))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
