"""Python-side mirror of DaemonPaths (src/DaemonPaths.cpp) for the pieces the
daemon process itself needs to resolve.

DaemonProcess.cpp launches the daemon with its working directory set to
DaemonPaths::baseDir(), so "plugins" relative to cwd is exactly
DaemonPaths::pluginsDir(). DPB_PLUGINS_DIR overrides this for tests, which
don't run with that cwd convention in place.
"""

from __future__ import annotations

import os
from pathlib import Path


def plugins_dir() -> Path:
    override = os.environ.get("DPB_PLUGINS_DIR")
    if override:
        return Path(override)
    return Path.cwd() / "plugins"
