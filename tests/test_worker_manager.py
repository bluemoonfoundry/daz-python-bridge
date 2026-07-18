import os
import sys
import time
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from daemon.worker_manager import WorkerFailedError, WorkerManager, WorkerState

REPO_ROOT = Path(__file__).resolve().parents[1]
WORKER_RUNTIME = REPO_ROOT / "daemon" / "worker_runtime.py"
FIXTURES_DIR = Path(__file__).resolve().parent / "fixtures"


def _command_for(entry_name: str):
    entry_path = FIXTURES_DIR / entry_name

    def build_command(plugin_id: str):
        return [sys.executable, str(WORKER_RUNTIME), "--entry", str(entry_path)]

    return build_command


@pytest.fixture
def manager():
    mgr = WorkerManager(
        build_command=_command_for("echo_plugin.py"),
        idle_timeout_seconds=600,
        eviction_poll_interval_seconds=30,
    )
    yield mgr
    mgr.shutdown()


def test_lazy_spawn_not_eager():
    spawn_count = {"n": 0}
    entry_path = FIXTURES_DIR / "echo_plugin.py"

    def build_command(plugin_id):
        spawn_count["n"] += 1
        return [sys.executable, str(WORKER_RUNTIME), "--entry", str(entry_path)]

    mgr = WorkerManager(build_command=build_command)
    try:
        assert spawn_count["n"] == 0  # nothing spawned just from construction
        assert mgr.status("p1")["state"] == WorkerState.NOT_STARTED.value

        result = mgr.call("p1", "echo", args=["hello"])

        assert result == "hello"
        assert spawn_count["n"] == 1
    finally:
        mgr.shutdown()


def test_repeated_calls_reuse_process_no_respawn(manager):
    pid1 = manager.call("p1", "pid")
    pid2 = manager.call("p1", "pid")
    pid3 = manager.call("p1", "pid")

    assert pid1 == pid2 == pid3
    assert manager.status("p1")["state"] == WorkerState.RUNNING.value


def test_idle_worker_is_reaped_and_respawns_transparently():
    entry_path = FIXTURES_DIR / "echo_plugin.py"

    def build_command(plugin_id):
        return [sys.executable, str(WORKER_RUNTIME), "--entry", str(entry_path)]

    mgr = WorkerManager(
        build_command=build_command,
        idle_timeout_seconds=0.2,
        eviction_poll_interval_seconds=0.1,
    )
    try:
        pid1 = mgr.call("p1", "pid")

        # Wait past idle timeout + at least one eviction poll tick.
        time.sleep(0.6)
        assert mgr.status("p1")["state"] == WorkerState.NOT_STARTED.value

        pid2 = mgr.call("p1", "pid")
        assert pid2 != pid1
        assert mgr.status("p1")["state"] == WorkerState.RUNNING.value
    finally:
        mgr.shutdown()


def test_worker_crash_triggers_one_automatic_restart(tmp_path):
    counter_path = tmp_path / "crash_counter.txt"
    entry_path = FIXTURES_DIR / "flaky_plugin.py"

    def build_command(plugin_id):
        return [sys.executable, str(WORKER_RUNTIME), "--entry", str(entry_path)]

    env_backup = os.environ.copy()
    os.environ["DPB_TEST_COUNTER_PATH"] = str(counter_path)
    os.environ["DPB_TEST_CRASH_THRESHOLD"] = "1"  # crash on first invocation only
    mgr = WorkerManager(build_command=build_command)
    try:
        # First call: worker crashes once handling "flaky", auto-restarts, and the
        # retry on the fresh process succeeds transparently -- caller sees success.
        result = mgr.call("p1", "flaky", args=[42])
        assert result == 42
        assert mgr.status("p1")["state"] == WorkerState.RUNNING.value
    finally:
        mgr.shutdown()
        os.environ.clear()
        os.environ.update(env_backup)


def test_worker_crashing_twice_in_a_row_ends_in_failed_state(tmp_path):
    counter_path = tmp_path / "crash_counter.txt"
    entry_path = FIXTURES_DIR / "flaky_plugin.py"
    spawn_count = {"n": 0}

    def build_command(plugin_id):
        spawn_count["n"] += 1
        return [sys.executable, str(WORKER_RUNTIME), "--entry", str(entry_path)]

    env_backup = os.environ.copy()
    os.environ["DPB_TEST_COUNTER_PATH"] = str(counter_path)
    os.environ["DPB_TEST_CRASH_THRESHOLD"] = "99"  # always crash
    mgr = WorkerManager(build_command=build_command)
    try:
        with pytest.raises(WorkerFailedError):
            mgr.call("p1", "flaky", args=[42])

        assert mgr.status("p1")["state"] == WorkerState.FAILED.value
        assert spawn_count["n"] == 2  # initial spawn + one automatic restart, no more

        # Further calls are rejected immediately without spawning a third worker.
        with pytest.raises(WorkerFailedError):
            mgr.call("p1", "flaky", args=[42])
        assert spawn_count["n"] == 2

        # Manual restart (status UI action) clears the failed state.
        mgr.restart_failed("p1")
        assert mgr.status("p1")["state"] == WorkerState.NOT_STARTED.value
    finally:
        mgr.shutdown()
        os.environ.clear()
        os.environ.update(env_backup)


def test_manual_restart_after_failure_recovers(tmp_path):
    counter_path = tmp_path / "crash_counter.txt"
    entry_path = FIXTURES_DIR / "flaky_plugin.py"

    def build_command(plugin_id):
        return [sys.executable, str(WORKER_RUNTIME), "--entry", str(entry_path)]

    env_backup = os.environ.copy()
    os.environ["DPB_TEST_COUNTER_PATH"] = str(counter_path)
    os.environ["DPB_TEST_CRASH_THRESHOLD"] = "99"
    mgr = WorkerManager(build_command=build_command)
    try:
        with pytest.raises(WorkerFailedError):
            mgr.call("p1", "flaky", args=[42])
        assert mgr.status("p1")["state"] == WorkerState.FAILED.value

        mgr.restart_failed("p1")

        # Now let the plugin behave (raise the crash threshold) and confirm a
        # fresh call succeeds normally post-recovery.
        os.environ["DPB_TEST_CRASH_THRESHOLD"] = "0"
        counter_path.write_text("0")
        result = mgr.call("p1", "echo", args=["recovered"])
        assert result == "recovered"
        assert mgr.status("p1")["state"] == WorkerState.RUNNING.value
    finally:
        mgr.shutdown()
        os.environ.clear()
        os.environ.update(env_backup)


def test_last_used_is_elapsed_seconds_not_a_raw_timestamp(manager):
    manager.call("p1", "echo", args=["hi"])
    last_used = manager.status("p1")["last_used"]

    # A raw time.monotonic() reading would be some large, arbitrary-epoch
    # number (easily thousands of seconds); elapsed-since-last-use must be
    # small and non-negative immediately after a call.
    assert last_used is not None
    assert 0 <= last_used < 5


def test_last_used_persists_after_stop_instead_of_reporting_never(manager):
    manager.call("p1", "echo", args=["hi"])
    manager.stop("p1")

    status = manager.status("p1")
    assert status["state"] == WorkerState.NOT_STARTED.value
    assert status["pid"] is None
    assert status["last_used"] is not None
    assert status["last_used"] >= 0


def test_last_used_is_null_for_a_plugin_never_started():
    mgr = WorkerManager(build_command=_command_for("echo_plugin.py"))
    try:
        assert mgr.status("never_started")["last_used"] is None
    finally:
        mgr.shutdown()


def test_call_to_unknown_function_returns_error_without_crashing(manager):
    with pytest.raises(RuntimeError, match="No exposed function"):
        manager.call("p1", "does_not_exist")

    # The worker process itself is still alive and usable afterwards.
    assert manager.status("p1")["state"] == WorkerState.RUNNING.value
    assert manager.call("p1", "echo", args=["still alive"]) == "still alive"
