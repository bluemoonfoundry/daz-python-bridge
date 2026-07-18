"""Tests for daemon/inline_runtime.py's in-process script execution (daz-python-bridge-sop.2).

Calls _run() directly rather than through the subprocess wrapper (inline_runner.py)
so these stay fast; tests/test_app_run_api.py covers the real subprocess + HTTP path.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from daemon.inline_runtime import _run


def test_last_expression_becomes_result():
    result = _run("1 + 1")
    assert result == {"success": True, "result": 2, "output": [], "error": ""}


def test_script_without_trailing_expression_has_null_result():
    result = _run("x = 1 + 1")
    assert result["success"] is True
    assert result["result"] is None


def test_print_output_is_captured_as_lines():
    result = _run("print('hello')\nprint('world')\n42")
    assert result["success"] is True
    assert result["output"] == ["hello", "world"]
    assert result["result"] == 42


def test_syntax_error_reports_failure():
    result = _run("def f(\n")
    assert result["success"] is False
    assert result["result"] is None
    assert "SyntaxError" in result["error"]


def test_runtime_error_reports_failure_and_preserves_output_so_far():
    result = _run("print('before')\n1 / 0")
    assert result["success"] is False
    assert result["output"] == ["before"]
    assert "ZeroDivisionError" in result["error"]


def test_non_json_serializable_result_reports_failure():
    result = _run("object()")
    assert result["success"] is False
    assert "not JSON serializable" in result["error"]


def test_namespace_is_fresh_per_call():
    _run("x = 'leaked'")
    result = _run("x")
    assert result["success"] is False
    assert "NameError" in result["error"]
