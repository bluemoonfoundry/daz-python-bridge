"""Minimal example plugin for manually validating the DPB plugin lifecycle
(install, resolving -> ready, start/stop/restart/enable/disable) from the
DSS status pane.

Not part of the automated test suite -- see tests/fixtures/echo_plugin.py and
tests/cpp_fixtures/valid_plugin.zip for those. This one is meant to be zipped
and dropped in, or copied straight into <plugins_dir>/hello_plugin/, for
manual testing against the real pane. No requirements.txt on purpose, so
install never depends on network access.
"""


def hello(name: str = "world") -> str:
    return f"Hello, {name}!"


def add(a: float, b: float) -> float:
    return a + b


def boom() -> None:
    """Deliberately raises, to exercise the worker's per-call error reporting
    -- a raised exception is returned as an error response, not a worker
    crash (see daemon/worker_runtime.py)."""
    raise RuntimeError("boom: this is a deliberate test failure")


EXPOSED_FUNCTIONS = {
    "hello": hello,
    "add": add,
    "boom": boom,
}
