"""Plugin status/management for the DSS status pane (daz-python-bridge-sop.6).

Bridges two things that each know only part of a plugin's story:

  - install state, on disk as <plugins_dir>/<id>/install_status.json, written
    once by PluginDependencyInstaller (daz-python-bridge-sop.4) after zip
    extraction (daz-python-bridge-sop.3) lands the plugin directory. Absence
    of this file means installation is still in flight.
  - worker state, tracked in memory by WorkerManager (daz-python-bridge-sop.5)
    once the plugin's warm worker subprocess has been spawned.

enable/disable is registry-level, persisted as <plugins_dir>/<id>/disabled,
a marker file whose mere presence means disabled (no content is read).
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from enum import Enum
from pathlib import Path
from typing import Optional

from .worker_manager import WorkerManager, WorkerState

try:
    import psutil
except ImportError:  # pragma: no cover - psutil is a declared dependency
    psutil = None


class PluginState(str, Enum):
    RESOLVING = "resolving"
    READY = "ready"
    STARTING = "starting"
    RUNNING = "running"
    FAILED = "failed"
    DISABLED = "disabled"


class PluginNotFoundError(KeyError):
    """Raised when an action targets a plugin id with no directory under plugins_dir."""


class DazPluginNotReadyError(RuntimeError):
    """Raised when a caller tries to invoke a plugin's exposed_functions while it is
    resolving (install still in flight) or disabled, instead of surfacing a raw
    connection failure to the caller."""


@dataclass
class PluginRegistry:
    plugins_dir: Path
    worker_manager: WorkerManager

    # ─── Discovery ──────────────────────────────────────────────────────────

    def _plugin_dir(self, plugin_id: str) -> Path:
        plugin_dir = self.plugins_dir / plugin_id
        if not plugin_dir.is_dir():
            raise PluginNotFoundError(plugin_id)
        return plugin_dir

    def _iter_plugin_ids(self):
        if not self.plugins_dir.is_dir():
            return
        for entry in sorted(self.plugins_dir.iterdir()):
            if entry.is_dir() and not entry.name.startswith("."):
                yield entry.name

    def _install_status(self, plugin_dir: Path) -> Optional[dict]:
        status_path = plugin_dir / "install_status.json"
        if not status_path.is_file():
            return None
        try:
            return json.loads(status_path.read_text())
        except (OSError, json.JSONDecodeError):
            return None

    def _is_disabled(self, plugin_dir: Path) -> bool:
        return (plugin_dir / "disabled").is_file()

    def _memory_bytes(self, pid: Optional[int]) -> Optional[int]:
        if pid is None or psutil is None:
            return None
        try:
            return psutil.Process(pid).memory_info().rss
        except psutil.Error:
            return None

    # ─── Status ─────────────────────────────────────────────────────────────

    def status(self, plugin_id: str) -> dict:
        plugin_dir = self._plugin_dir(plugin_id)
        install_status = self._install_status(plugin_dir)
        worker_status = self.worker_manager.status(plugin_id)

        if install_status is None:
            state = PluginState.RESOLVING
        elif install_status.get("state") == "failed":
            state = PluginState.FAILED
        elif self._is_disabled(plugin_dir):
            state = PluginState.DISABLED
        else:
            state = {
                WorkerState.NOT_STARTED.value: PluginState.READY,
                WorkerState.STARTING.value: PluginState.STARTING,
                WorkerState.RUNNING.value: PluginState.RUNNING,
                WorkerState.FAILED.value: PluginState.FAILED,
            }[worker_status["state"]]

        pid = worker_status["pid"]
        return {
            "plugin_id": plugin_id,
            "state": state.value,
            "pid": pid,
            "memory_bytes": self._memory_bytes(pid),
            "last_used": worker_status["last_used"],
        }

    def list_plugins(self) -> list[dict]:
        return [self.status(plugin_id) for plugin_id in self._iter_plugin_ids()]

    # ─── Actions ────────────────────────────────────────────────────────────

    def start(self, plugin_id: str) -> dict:
        plugin_dir = self._plugin_dir(plugin_id)
        if self._is_disabled(plugin_dir):
            raise DazPluginNotReadyError(f"Plugin '{plugin_id}' is disabled")
        self.worker_manager.start(plugin_id)
        return self.status(plugin_id)

    def stop(self, plugin_id: str) -> dict:
        self._plugin_dir(plugin_id)
        self.worker_manager.stop(plugin_id)
        return self.status(plugin_id)

    def restart(self, plugin_id: str) -> dict:
        plugin_dir = self._plugin_dir(plugin_id)
        if self._is_disabled(plugin_dir):
            raise DazPluginNotReadyError(f"Plugin '{plugin_id}' is disabled")
        self.worker_manager.stop(plugin_id)  # always resets state to NOT_STARTED, even from FAILED
        self.worker_manager.start(plugin_id)
        return self.status(plugin_id)

    def enable(self, plugin_id: str) -> dict:
        plugin_dir = self._plugin_dir(plugin_id)
        disabled_marker = plugin_dir / "disabled"
        disabled_marker.unlink(missing_ok=True)
        return self.status(plugin_id)

    def disable(self, plugin_id: str) -> dict:
        plugin_dir = self._plugin_dir(plugin_id)
        (plugin_dir / "disabled").touch()
        self.worker_manager.stop(plugin_id)
        return self.status(plugin_id)

    def call(self, plugin_id: str, function: str, args: Optional[list] = None,
              kwargs: Optional[dict] = None):
        """Routes a plugin function call through the resolving/disabled check before
        handing off to WorkerManager, so callers see DazPluginNotReadyError instead of
        a raw connection failure while the plugin isn't callable yet."""
        current = self.status(plugin_id)
        if current["state"] in (PluginState.RESOLVING.value, PluginState.DISABLED.value):
            raise DazPluginNotReadyError(
                f"Plugin '{plugin_id}' is {current['state']} and cannot be called yet"
            )
        return self.worker_manager.call(plugin_id, function, args, kwargs)
