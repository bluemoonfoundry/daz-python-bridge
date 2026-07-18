"""Tests for the GET/POST /plugins/* endpoints (daz-python-bridge-sop.6/.7).

Sets DPB_PLUGINS_DIR before importing daemon.app, since PluginRegistry's
plugins_dir is resolved once at import time (module-level construction,
matching how the real daemon builds it once at process startup). Likewise
plants a token file under a fake home directory before import, since
daemon.auth reads it once at import time too (mirroring how DSS's
AuthenticationService only ever reads it at the real daemon's startup).
"""

import json
import os
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

VALID_TOKEN = "a" * 32


@pytest.fixture
def client(tmp_path, monkeypatch):
    plugins_dir = tmp_path / "plugins"
    plugins_dir.mkdir()
    monkeypatch.setenv("DPB_PLUGINS_DIR", str(plugins_dir))

    fake_home = tmp_path / "home"
    daz3d_dir = fake_home / ".daz3d"
    daz3d_dir.mkdir(parents=True)
    (daz3d_dir / "dazpythonbridge_token.txt").write_text(VALID_TOKEN)
    monkeypatch.setattr(Path, "home", lambda: fake_home)

    for mod in ("daemon.app", "daemon.auth", "daemon.paths", "daemon.plugin_registry", "daemon.worker_manager"):
        sys.modules.pop(mod, None)

    from fastapi.testclient import TestClient

    import daemon.app as app_module

    # The real _build_command expects a per-plugin venv (daz-python-bridge-sop.4)
    # that these tests don't set up; swap in the same fixture entry the worker
    # manager tests use so the start/stop/restart endpoints spawn a real process.
    fixtures_dir = Path(__file__).resolve().parent / "fixtures"
    worker_runtime = Path(__file__).resolve().parents[1] / "daemon" / "worker_runtime.py"
    app_module.worker_manager._build_command = lambda plugin_id: [
        sys.executable, str(worker_runtime), "--entry", str(fixtures_dir / "echo_plugin.py"),
    ]

    with TestClient(app_module.app, headers={"X-DPB-Token": VALID_TOKEN}) as c:
        yield c, plugins_dir

    app_module.worker_manager.shutdown()


def _make_plugin_dir(plugins_dir: Path, plugin_id: str, install_state: str = "ok") -> Path:
    plugin_dir = plugins_dir / plugin_id
    plugin_dir.mkdir(parents=True)
    if install_state is not None:
        (plugin_dir / "install_status.json").write_text(json.dumps({"state": install_state}))
    return plugin_dir


def test_list_plugins_empty(client):
    c, _ = client
    resp = c.get("/plugins")
    assert resp.status_code == 200
    assert resp.json() == {"plugins": []}


def test_get_unknown_plugin_is_404(client):
    c, _ = client
    resp = c.get("/plugins/nope")
    assert resp.status_code == 404


def test_get_plugin_reflects_resolving_state(client):
    c, tmp_path = client
    _make_plugin_dir(tmp_path, "p1", install_state=None)
    resp = c.get("/plugins/p1")
    assert resp.status_code == 200
    assert resp.json()["state"] == "resolving"


def test_start_stop_action_endpoints(client):
    c, tmp_path = client
    _make_plugin_dir(tmp_path, "p1")

    resp = c.post("/plugins/p1/start")
    assert resp.status_code == 200
    assert resp.json()["state"] == "running"

    resp = c.post("/plugins/p1/stop")
    assert resp.status_code == 200
    assert resp.json()["state"] == "ready"


def test_start_on_disabled_plugin_returns_409(client):
    c, tmp_path = client
    _make_plugin_dir(tmp_path, "p1")

    c.post("/plugins/p1/disable")
    resp = c.post("/plugins/p1/start")
    assert resp.status_code == 409


def test_action_on_unknown_plugin_is_404(client):
    c, _ = client
    resp = c.post("/plugins/nope/start")
    assert resp.status_code == 404


def test_health_does_not_require_token(client):
    c, _ = client
    resp = c.get("/health", headers={"X-DPB-Token": ""})
    assert resp.status_code == 200


def test_plugins_list_rejects_missing_token(client):
    c, _ = client
    resp = c.get("/plugins", headers={"X-DPB-Token": ""})
    assert resp.status_code == 401


def test_plugins_list_rejects_wrong_token(client):
    c, _ = client
    resp = c.get("/plugins", headers={"X-DPB-Token": "wrong"})
    assert resp.status_code == 401


def test_action_endpoint_rejects_missing_token(client):
    c, tmp_path = client
    _make_plugin_dir(tmp_path, "p1")
    resp = c.post("/plugins/p1/start", headers={"X-DPB-Token": ""})
    assert resp.status_code == 401
