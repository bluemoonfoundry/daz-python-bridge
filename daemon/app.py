"""DPB daemon FastAPI app.

Launched by DaemonProcess (src/DaemonProcess.cpp) as:

    <run_venv>/bin/python -m uvicorn daemon.app:app --host 127.0.0.1 --port 18812

Always bound to loopback by the launcher — this app does not decide its own
bind address. /health is intentionally unauthenticated (liveness probe, no
sensitive data); every other route requires X-DPB-Token (daz-python-bridge-sop.7).
/plugins/* (daz-python-bridge-sop.6) is what DSS's plugin status pane polls
via QNetworkAccessManager; /run (daz-python-bridge-sop.2) is the inline
scripting endpoint behind DSS's Script-IDE-style pane, see inline_runner.py.
"""

import os
from pathlib import Path

from fastapi import APIRouter, Depends, FastAPI, HTTPException
from pydantic import BaseModel

from . import paths
from .auth import require_token
from .inline_runner import run_inline
from .plugin_registry import DazPluginNotReadyError, PluginNotFoundError, PluginRegistry
from .worker_manager import WorkerFailedError, WorkerManager

app = FastAPI(title="Daz Python Bridge Daemon")
plugins_router = APIRouter(dependencies=[Depends(require_token)])
run_router = APIRouter(dependencies=[Depends(require_token)])

# worker_runtime.py's own file path, resolved relative to this installed
# package rather than run as `-m daemon.worker_runtime`: a per-plugin venv
# only ever gets requirements.txt installed into it (PluginDependencyInstaller,
# daz-python-bridge-sop.4), never the daemon package itself, so `-m
# daemon.worker_runtime` would fail there with ModuleNotFoundError. Invoking
# by direct path works because worker_runtime.py has zero third-party
# dependencies -- only stdlib -- so it needs nothing installed to run.
_WORKER_RUNTIME_PATH = Path(__file__).resolve().parent / "worker_runtime.py"


def _build_command(plugin_id: str) -> list[str]:
    plugin_venv = paths.plugins_dir() / plugin_id / "venv"
    python = plugin_venv / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    entry = paths.plugins_dir() / plugin_id / "main.py"
    return [str(python), str(_WORKER_RUNTIME_PATH), "--entry", str(entry)]


worker_manager = WorkerManager(build_command=_build_command)
plugin_registry = PluginRegistry(plugins_dir=paths.plugins_dir(), worker_manager=worker_manager)


@app.get("/health")
def health() -> dict:
    return {"status": "ok"}


class RunRequest(BaseModel):
    code: str


@run_router.post("/run")
def run(request: RunRequest) -> dict:
    return run_inline(request.code)


@plugins_router.get("/plugins")
def list_plugins() -> dict:
    return {"plugins": plugin_registry.list_plugins()}


@plugins_router.get("/plugins/{plugin_id}")
def get_plugin(plugin_id: str) -> dict:
    try:
        return plugin_registry.status(plugin_id)
    except PluginNotFoundError:
        raise HTTPException(status_code=404, detail=f"Unknown plugin '{plugin_id}'")


def _do_action(plugin_id: str, action) -> dict:
    try:
        return action(plugin_id)
    except PluginNotFoundError:
        raise HTTPException(status_code=404, detail=f"Unknown plugin '{plugin_id}'")
    except DazPluginNotReadyError as exc:
        raise HTTPException(status_code=409, detail=str(exc))
    except WorkerFailedError as exc:
        raise HTTPException(status_code=502, detail=str(exc))


@plugins_router.post("/plugins/{plugin_id}/start")
def start_plugin(plugin_id: str) -> dict:
    return _do_action(plugin_id, plugin_registry.start)


@plugins_router.post("/plugins/{plugin_id}/stop")
def stop_plugin(plugin_id: str) -> dict:
    return _do_action(plugin_id, plugin_registry.stop)


@plugins_router.post("/plugins/{plugin_id}/restart")
def restart_plugin(plugin_id: str) -> dict:
    return _do_action(plugin_id, plugin_registry.restart)


@plugins_router.post("/plugins/{plugin_id}/enable")
def enable_plugin(plugin_id: str) -> dict:
    return _do_action(plugin_id, plugin_registry.enable)


@plugins_router.post("/plugins/{plugin_id}/disable")
def disable_plugin(plugin_id: str) -> dict:
    return _do_action(plugin_id, plugin_registry.disable)


app.include_router(plugins_router)
app.include_router(run_router)
