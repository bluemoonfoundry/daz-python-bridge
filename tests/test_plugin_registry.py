import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from daemon.plugin_registry import DazPluginNotReadyError, PluginNotFoundError, PluginRegistry
from daemon.worker_manager import WorkerManager, WorkerState

FIXTURES_DIR = Path(__file__).resolve().parent / "fixtures"
WORKER_RUNTIME = Path(__file__).resolve().parents[1] / "daemon" / "worker_runtime.py"


def _make_plugin_dir(plugins_dir: Path, plugin_id: str, install_state: str = "ok") -> Path:
    plugin_dir = plugins_dir / plugin_id
    plugin_dir.mkdir(parents=True)
    if install_state is not None:
        (plugin_dir / "install_status.json").write_text(json.dumps({"state": install_state}))
    return plugin_dir


def _build_command(plugin_id: str):
    return [sys.executable, str(WORKER_RUNTIME), "--entry", str(FIXTURES_DIR / "echo_plugin.py")]


@pytest.fixture
def registry(tmp_path):
    mgr = WorkerManager(build_command=_build_command)
    reg = PluginRegistry(plugins_dir=tmp_path, worker_manager=mgr)
    yield reg
    mgr.shutdown()


def test_plugin_still_installing_is_resolving(registry, tmp_path):
    _make_plugin_dir(tmp_path, "p1", install_state=None)
    assert registry.status("p1")["state"] == "resolving"


def test_failed_install_is_failed(registry, tmp_path):
    _make_plugin_dir(tmp_path, "p1", install_state="failed")
    assert registry.status("p1")["state"] == "failed"


def test_ok_install_not_yet_called_is_ready(registry, tmp_path):
    _make_plugin_dir(tmp_path, "p1")
    assert registry.status("p1")["state"] == "ready"


def test_unknown_plugin_raises(registry):
    with pytest.raises(PluginNotFoundError):
        registry.status("nope")


def test_start_spawns_worker_and_reports_running(registry, tmp_path):
    _make_plugin_dir(tmp_path, "p1")
    status = registry.start("p1")
    assert status["state"] == "running"
    assert status["pid"] is not None
    assert status["memory_bytes"] is not None


def test_stop_returns_to_ready(registry, tmp_path):
    _make_plugin_dir(tmp_path, "p1")
    registry.start("p1")
    status = registry.stop("p1")
    assert status["state"] == "ready"
    assert status["pid"] is None


def test_restart_gets_a_new_pid(registry, tmp_path):
    _make_plugin_dir(tmp_path, "p1")
    pid1 = registry.start("p1")["pid"]
    pid2 = registry.restart("p1")["pid"]
    assert pid1 != pid2


def test_disable_stops_worker_and_blocks_start(registry, tmp_path):
    _make_plugin_dir(tmp_path, "p1")
    registry.start("p1")
    status = registry.disable("p1")
    assert status["state"] == "disabled"

    with pytest.raises(DazPluginNotReadyError):
        registry.start("p1")


def test_enable_after_disable_allows_start_again(registry, tmp_path):
    _make_plugin_dir(tmp_path, "p1")
    registry.disable("p1")
    status = registry.enable("p1")
    assert status["state"] == "ready"

    status = registry.start("p1")
    assert status["state"] == "running"


def test_call_on_resolving_plugin_raises_not_ready(registry, tmp_path):
    _make_plugin_dir(tmp_path, "p1", install_state=None)
    with pytest.raises(DazPluginNotReadyError):
        registry.call("p1", "echo", args=["hi"])


def test_call_on_disabled_plugin_raises_not_ready(registry, tmp_path):
    _make_plugin_dir(tmp_path, "p1")
    registry.disable("p1")
    with pytest.raises(DazPluginNotReadyError):
        registry.call("p1", "echo", args=["hi"])


def test_call_on_ready_plugin_delegates_to_worker_manager(registry, tmp_path):
    _make_plugin_dir(tmp_path, "p1")
    assert registry.call("p1", "echo", args=["hi"]) == "hi"
    assert registry.status("p1")["state"] == WorkerState.RUNNING.value


def test_list_plugins_covers_all_directories(registry, tmp_path):
    _make_plugin_dir(tmp_path, "p1")
    _make_plugin_dir(tmp_path, "p2", install_state=None)
    ids = {p["plugin_id"] for p in registry.list_plugins()}
    assert ids == {"p1", "p2"}
