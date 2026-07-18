"""Spawns the one-shot run_venv subprocess behind POST /run (daz-python-bridge-sop.2).

Deliberately does NOT keep a warm process around between calls the way
WorkerManager does for plugin workers -- run_venv must stay a clean-slate
scratch interpreter with no state (or installed-package) leakage between
/run calls, mirroring DAZ Studio's own Script IDE. See inline_runtime.py for
what actually runs inside the subprocess.
"""

from __future__ import annotations

import json
import subprocess
import sys

DEFAULT_RUN_TIMEOUT_SECONDS = 30


def run_inline(code: str, timeout: float = DEFAULT_RUN_TIMEOUT_SECONDS) -> dict:
    try:
        completed = subprocess.run(
            [sys.executable, "-m", "daemon.inline_runtime"],
            input=code,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        return {
            "success": False,
            "result": None,
            "output": [],
            "error": f"Execution timed out after {timeout}s",
        }

    stdout = completed.stdout.strip()
    if completed.returncode != 0 or not stdout:
        error = completed.stderr.strip() or f"run_venv process exited with code {completed.returncode}"
        return {"success": False, "result": None, "output": [], "error": error}

    try:
        return json.loads(stdout.splitlines()[-1])
    except (json.JSONDecodeError, IndexError):
        return {
            "success": False,
            "result": None,
            "output": [],
            "error": "Malformed response from run_venv subprocess",
        }
