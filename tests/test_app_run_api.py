"""Tests for the POST /run endpoint (daz-python-bridge-sop.2).

Mirrors test_app_plugins_api.py's fixture setup: DPB_PLUGINS_DIR and the fake
token file both need to be in place before daemon.app is imported, since
daemon.auth reads the token once at import time.
"""

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

    for mod in ("daemon.app", "daemon.auth", "daemon.paths", "daemon.inline_runner", "daemon.inline_runtime"):
        sys.modules.pop(mod, None)

    from fastapi.testclient import TestClient

    import daemon.app as app_module

    with TestClient(app_module.app, headers={"X-DPB-Token": VALID_TOKEN}) as c:
        yield c

    app_module.worker_manager.shutdown()


def test_run_returns_result_and_output(client):
    resp = client.post("/run", json={"code": "print('hi')\n1 + 1"})
    assert resp.status_code == 200
    body = resp.json()
    assert body == {"success": True, "result": 2, "output": ["hi"], "error": ""}


def test_run_reports_runtime_errors(client):
    resp = client.post("/run", json={"code": "1 / 0"})
    assert resp.status_code == 200
    body = resp.json()
    assert body["success"] is False
    assert "ZeroDivisionError" in body["error"]


def test_run_reports_import_errors_for_unavailable_modules(client):
    resp = client.post("/run", json={"code": "import this_module_does_not_exist_anywhere"})
    assert resp.status_code == 200
    body = resp.json()
    assert body["success"] is False
    assert "ModuleNotFoundError" in body["error"]


def test_run_rejects_missing_token(client):
    resp = client.post("/run", json={"code": "1"}, headers={"X-DPB-Token": ""})
    assert resp.status_code == 401


def test_run_rejects_wrong_token(client):
    resp = client.post("/run", json={"code": "1"}, headers={"X-DPB-Token": "wrong"})
    assert resp.status_code == 401
