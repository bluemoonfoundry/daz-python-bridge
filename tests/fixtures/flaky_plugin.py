"""worker_runtime entry module that crashes its own process a configurable number
of times before behaving, used by test_worker_manager.py to exercise the
crash-once-auto-restart / crash-twice-in-a-row-fails policy.

Since each crash kills the whole interpreter, the crash count must survive
across process respawns -- it is persisted to a file (DPB_TEST_COUNTER_PATH)
rather than kept in a module-level variable.
"""

import os

_COUNTER_PATH = os.environ["DPB_TEST_COUNTER_PATH"]
_CRASH_THRESHOLD = int(os.environ.get("DPB_TEST_CRASH_THRESHOLD", "1"))


def _read_counter() -> int:
    try:
        with open(_COUNTER_PATH) as f:
            return int(f.read().strip() or "0")
    except FileNotFoundError:
        return 0


def _increment_counter() -> int:
    count = _read_counter() + 1
    with open(_COUNTER_PATH, "w") as f:
        f.write(str(count))
    return count


def flaky(value):
    count = _increment_counter()
    if count <= _CRASH_THRESHOLD:
        os._exit(1)  # simulate a hard crash mid-call, no response sent
    return value


def echo(value):
    return value


EXPOSED_FUNCTIONS = {"flaky": flaky, "echo": echo}
