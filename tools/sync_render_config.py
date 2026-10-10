#!/usr/bin/env python3
"""
tools/sync_render_config.py

Re-copies config.json and the signing keys into backend_lake/ and
backend_flag/ so both Render services carry byte-identical chain material.

THIS MATTERS: the vault selector is derived from config.json by BOTH the
solver and the flagger. If the two services ever drift apart, stage 22 will
fail with "the chain did not hold together" and it will look like a solver
bug rather than a deployment bug.

Run it after ANY change to config.json, private_key.pem or public_key.pem.
"""

import os
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TARGETS = ("backend_lake", "backend_flag")
FILES = ("config.json", "private_key.pem", "public_key.pem")


def main() -> int:
    changed = False
    for name in FILES:
        src = os.path.join(ROOT, name)
        if not os.path.exists(src) and name != "config.json":
            src = os.path.join(ROOT, "backend", name)
        if not os.path.exists(src):
            print(f"[!] missing {name}; skipping")
            continue
        for target in TARGETS:
            dst_dir = os.path.join(ROOT, target)
            dst = os.path.join(dst_dir, name)
            os.makedirs(dst_dir, exist_ok=True)
            if os.path.exists(dst) and open(src, "rb").read() == open(dst, "rb").read():
                continue
            shutil.copy2(src, dst)
            changed = True
            print(f"[+] synced {name} -> {target}/")
    if not changed:
        print("[=] all Render deploy folders already in sync")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
