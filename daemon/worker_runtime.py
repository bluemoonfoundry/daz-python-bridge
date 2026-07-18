"""Generic worker runtime for per-plugin warm worker subprocesses (daz-python-bridge-sop.5).

Launched inside a plugin's own venv (see DaemonPaths::pluginVenvDir, which per-plugin
venv creation from daz-python-bridge-sop.4 populates) as:

    <plugin_venv_python> -m daemon.worker_runtime --entry <plugin_dir>/main.py

Loads the plugin's entry module, which must define EXPOSED_FUNCTIONS: a dict of
{name: callable}. Speaks newline-delimited JSON over stdin/stdout with the parent
WorkerManager (see worker_manager.py):

    parent -> child : {"id": int, "op": "call", "function": str, "args": [...], "kwargs": {...}}
    child -> parent : {"id": int, "ok": true, "result": ...}
                    | {"id": int, "ok": false, "error": {"type": str, "message": str}}

On startup, emits a single {"op": "ready"} line once the entry module has loaded, so
the parent can bound the visible cold-start latency (see WorkerManager._ensure_worker).

A plugin function raising an exception is reported back as an error response and does
NOT end the process — only the worker process actually dying counts as a crash for
WorkerManager's restart-once-then-fail policy.

A plugin function's own print() output is redirected away from the real stdout while
it runs and discarded: this protocol's transport IS stdout, so a plugin author's
ordinary print()-debugging would otherwise interleave raw text into the
newline-delimited JSON stream and corrupt it for every call after.
"""

from __future__ import annotations

import argparse
import contextlib
import importlib.util
import io
import json
import sys


def _load_entry(entry_path: str):
    spec = importlib.util.spec_from_file_location("plugin_entry", entry_path)
    if spec is None or spec.loader is None:
        raise ImportError(f"Cannot load plugin entry module: {entry_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _send(message: dict) -> None:
    sys.stdout.write(json.dumps(message) + "\n")
    sys.stdout.flush()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--entry", required=True, help="Path to the plugin's entry module")
    args = parser.parse_args()

    module = _load_entry(args.entry)
    exposed = getattr(module, "EXPOSED_FUNCTIONS", {})

    _send({"op": "ready"})

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        request = json.loads(line)
        request_id = request.get("id")
        function_name = request.get("function")
        call_args = request.get("args", [])
        call_kwargs = request.get("kwargs", {})

        func = exposed.get(function_name)
        if func is None:
            _send({
                "id": request_id,
                "ok": False,
                "error": {"type": "NotFound", "message": f"No exposed function '{function_name}'"},
            })
            continue

        try:
            with contextlib.redirect_stdout(io.StringIO()):
                result = func(*call_args, **call_kwargs)
            _send({"id": request_id, "ok": True, "result": result})
        except Exception as exc:  # plugin code is untrusted; report, don't crash the loop
            _send({
                "id": request_id,
                "ok": False,
                "error": {"type": type(exc).__name__, "message": str(exc)},
            })


if __name__ == "__main__":
    main()
