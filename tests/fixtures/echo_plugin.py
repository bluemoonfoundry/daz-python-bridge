"""Trivial worker_runtime entry module used by test_worker_manager.py."""

import os


def echo(value):
    return value


def pid():
    return os.getpid()


def noisy(value):
    """Prints before returning, to verify worker_runtime.py's stdout
    redirection keeps this from corrupting the JSON-RPC protocol (which is
    itself carried over stdout)."""
    print(f"noisy: about to return {value!r}")
    print("noisy: second line")
    return value


EXPOSED_FUNCTIONS = {"echo": echo, "pid": pid, "noisy": noisy}
