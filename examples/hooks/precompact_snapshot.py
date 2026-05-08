#!/usr/bin/env python3
"""
icmg PreCompact hook (Phase 40 T2).

Fires on Claude Code /compact or auto-compaction. Snapshots active session
state via `icmg session save auto-precompact-<ts>` so decisions and recall
context survive the context compression pass.

Install via: icmg init --install-hooks
"""
import json
import os
import shutil
import subprocess
import sys
import time

def main():
    # Read hook input (we don't need fields; presence is enough).
    try:
        sys.stdin.read()
    except Exception:
        pass

    icmg = shutil.which("icmg") or shutil.which("icmg.exe")
    if not icmg:
        # Silent no-op when icmg unavailable — don't block compact.
        print(json.dumps({"continue": True}))
        return 0

    tag = "auto-precompact-" + time.strftime("%Y%m%d-%H%M%S")
    try:
        subprocess.run(
            [icmg, "session", "save", tag],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            timeout=15
        )
        print(json.dumps({
            "continue": True,
            "systemMessage": f"icmg snapshot saved: {tag} (restore with `icmg session restore {tag}`)"
        }))
    except Exception:
        # Never block compaction even if snapshot fails.
        print(json.dumps({"continue": True}))
    return 0

if __name__ == "__main__":
    sys.exit(main())
