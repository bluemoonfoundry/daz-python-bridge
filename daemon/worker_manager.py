"""Warm worker process lifecycle and crash policy (daz-python-bridge-sop.5).

Each plugin gets at most one live worker subprocess, spawned lazily on its first
/execute call rather than eagerly at daemon startup or per-call. Workers speak the
newline-delimited JSON protocol implemented by worker_runtime.py over stdin/stdout.

This module has no opinion on venv layout, zip installs, or plugin discovery
(daz-python-bridge-sop.3/.4/.6) -- callers supply a build_command(plugin_id)
callback that returns the argv to spawn, e.g.:

    [str(DaemonPaths.pluginVenvDir(plugin_id) / "Scripts" / "python.exe"),
     "-m", "daemon.worker_runtime", "--entry", str(plugin_dir / "main.py")]

Crash policy: one automatic restart on worker crash; if the immediate retry also
crashes, the plugin flips to FAILED and further calls are rejected until
restart_failed() is invoked (wired up to the status UI's manual restart control
in daz-python-bridge-sop.6).
"""

from __future__ import annotations

import json
import subprocess
import threading
import time
from dataclasses import dataclass, field
from enum import Enum
from typing import Callable, Optional

DEFAULT_IDLE_TIMEOUT_SECONDS = 600  # 10 minutes, per design
DEFAULT_STARTUP_TIMEOUT_SECONDS = 15
DEFAULT_CALL_TIMEOUT_SECONDS = 30
DEFAULT_EVICTION_POLL_INTERVAL_SECONDS = 30


class WorkerState(str, Enum):
    NOT_STARTED = "not_started"
    STARTING = "starting"
    RUNNING = "running"
    FAILED = "failed"


class WorkerFailedError(RuntimeError):
    """Raised when a call is routed to a plugin whose worker has crashed twice in a row."""


class _WorkerCrashError(RuntimeError):
    """Internal: the worker process died unexpectedly during spawn or a call."""


@dataclass
class _Worker:
    process: subprocess.Popen
    lock: threading.Lock = field(default_factory=threading.Lock)
    last_used: float = field(default_factory=time.monotonic)
    request_seq: int = 0


class WorkerManager:
    def __init__(
        self,
        build_command: Callable[[str], list[str]],
        idle_timeout_seconds: float = DEFAULT_IDLE_TIMEOUT_SECONDS,
        startup_timeout_seconds: float = DEFAULT_STARTUP_TIMEOUT_SECONDS,
        eviction_poll_interval_seconds: float = DEFAULT_EVICTION_POLL_INTERVAL_SECONDS,
    ) -> None:
        self._build_command = build_command
        self._idle_timeout = idle_timeout_seconds
        self._startup_timeout = startup_timeout_seconds

        self._structural_lock = threading.RLock()
        self._workers: dict[str, _Worker] = {}
        self._states: dict[str, WorkerState] = {}

        self._stop_event = threading.Event()
        self._eviction_thread = threading.Thread(
            target=self._eviction_loop,
            args=(eviction_poll_interval_seconds,),
            daemon=True,
        )
        self._eviction_thread.start()

    # ─── Public API ─────────────────────────────────────────────────────────

    def call(
        self,
        plugin_id: str,
        function: str,
        args: Optional[list] = None,
        kwargs: Optional[dict] = None,
        timeout: float = DEFAULT_CALL_TIMEOUT_SECONDS,
    ):
        return self._with_crash_retry(
            plugin_id, lambda: self._call_once(plugin_id, function, args, kwargs, timeout)
        )

    def start(self, plugin_id: str) -> None:
        """Explicitly spawn (or confirm) plugin_id's worker, for the status UI's
        manual start control -- unlike call(), this makes no plugin function call."""
        self._with_crash_retry(plugin_id, lambda: self._ensure_worker(plugin_id))

    def _with_crash_retry(self, plugin_id: str, action: Callable):
        if self._states.get(plugin_id) == WorkerState.FAILED:
            raise WorkerFailedError(
                f"Plugin '{plugin_id}' worker is in failed state; manual restart required."
            )

        last_exc: Optional[Exception] = None
        for attempt in range(2):  # spawn/current + one automatic restart
            try:
                return action()
            except _WorkerCrashError as exc:
                last_exc = exc
                with self._structural_lock:
                    self._workers.pop(plugin_id, None)
                    self._states[plugin_id] = WorkerState.NOT_STARTED
                if attempt == 1:  # crashed again on the automatic restart
                    with self._structural_lock:
                        self._states[plugin_id] = WorkerState.FAILED
                    raise WorkerFailedError(
                        f"Plugin '{plugin_id}' worker crashed twice in a row; failed state set."
                    ) from exc
        raise WorkerFailedError(f"Plugin '{plugin_id}' worker unavailable") from last_exc

    def status(self, plugin_id: str) -> dict:
        worker = self._workers.get(plugin_id)
        return {
            "plugin_id": plugin_id,
            "state": self._states.get(plugin_id, WorkerState.NOT_STARTED).value,
            "pid": worker.process.pid if worker else None,
            "last_used": worker.last_used if worker else None,
        }

    def restart_failed(self, plugin_id: str) -> None:
        """Manual recovery hook for the status UI (sop.6): clears FAILED back to NOT_STARTED."""
        with self._structural_lock:
            self._states[plugin_id] = WorkerState.NOT_STARTED
            self._workers.pop(plugin_id, None)

    def stop(self, plugin_id: str) -> None:
        with self._structural_lock:
            worker = self._workers.pop(plugin_id, None)
            self._states[plugin_id] = WorkerState.NOT_STARTED
        if worker:
            self._terminate(worker)

    def shutdown(self) -> None:
        self._stop_event.set()
        self._eviction_thread.join(timeout=5)
        with self._structural_lock:
            plugin_ids = list(self._workers.keys())
        for plugin_id in plugin_ids:
            self.stop(plugin_id)

    # ─── Internals ──────────────────────────────────────────────────────────

    def _call_once(self, plugin_id, function, args, kwargs, timeout):
        worker = self._ensure_worker(plugin_id)
        with worker.lock:
            worker.request_seq += 1
            request = {
                "id": worker.request_seq,
                "op": "call",
                "function": function,
                "args": args or [],
                "kwargs": kwargs or {},
            }
            try:
                self._send(worker.process, request)
                response = self._recv(worker.process, timeout)
            except (BrokenPipeError, OSError, TimeoutError) as exc:
                raise _WorkerCrashError(str(exc)) from exc

            if response is None or worker.process.poll() is not None:
                raise _WorkerCrashError(f"Worker for plugin '{plugin_id}' exited unexpectedly")

            worker.last_used = time.monotonic()

        if response.get("ok"):
            return response.get("result")
        error = response.get("error", {})
        raise RuntimeError(error.get("message", "Unknown plugin worker error"))

    def _ensure_worker(self, plugin_id: str) -> _Worker:
        with self._structural_lock:
            worker = self._workers.get(plugin_id)
            if worker is not None and worker.process.poll() is None:
                return worker
            self._states[plugin_id] = WorkerState.STARTING
            command = self._build_command(plugin_id)
            process = subprocess.Popen(
                command,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
                text=True,
                bufsize=1,
            )
            worker = _Worker(process=process)
            self._workers[plugin_id] = worker

        try:
            ready = self._recv(worker.process, self._startup_timeout)
        except (OSError, TimeoutError) as exc:
            with self._structural_lock:
                self._workers.pop(plugin_id, None)
            raise _WorkerCrashError(f"Worker for plugin '{plugin_id}' failed to start") from exc

        if ready is None or ready.get("op") != "ready":
            with self._structural_lock:
                self._workers.pop(plugin_id, None)
            raise _WorkerCrashError(f"Worker for plugin '{plugin_id}' failed to start")

        with self._structural_lock:
            self._states[plugin_id] = WorkerState.RUNNING
        return worker

    @staticmethod
    def _send(process: subprocess.Popen, message: dict) -> None:
        assert process.stdin is not None
        process.stdin.write(json.dumps(message) + "\n")
        process.stdin.flush()

    @staticmethod
    def _recv(process: subprocess.Popen, timeout: float) -> Optional[dict]:
        assert process.stdout is not None
        result: dict[str, Optional[str]] = {"line": None}

        def _read():
            result["line"] = process.stdout.readline()

        reader = threading.Thread(target=_read, daemon=True)
        reader.start()
        reader.join(timeout)
        if reader.is_alive():
            raise TimeoutError("Timed out waiting for worker response")
        line = result["line"]
        if not line:
            return None
        return json.loads(line)

    @staticmethod
    def _terminate(worker: "_Worker") -> None:
        if worker.process.poll() is not None:
            return
        try:
            if worker.process.stdin:
                worker.process.stdin.close()
        except OSError:
            pass
        try:
            worker.process.terminate()
            worker.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            worker.process.kill()
            worker.process.wait(timeout=5)

    def _eviction_loop(self, poll_interval: float) -> None:
        while not self._stop_event.wait(poll_interval):
            with self._structural_lock:
                idle_ids = [
                    plugin_id
                    for plugin_id, worker in self._workers.items()
                    if time.monotonic() - worker.last_used > self._idle_timeout
                ]
            for plugin_id in idle_ids:
                self.stop(plugin_id)
