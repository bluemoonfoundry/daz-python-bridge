"""Trivial worker_runtime entry module used by test_worker_manager.py."""

import os


def echo(value):
    return value


def pid():
    return os.getpid()


EXPOSED_FUNCTIONS = {"echo": echo, "pid": pid}
