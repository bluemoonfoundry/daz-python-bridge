"""One-shot inline Python execution for POST /run (daz-python-bridge-sop.2).

Launched fresh per request (see inline_runner.py) as:

    <run_venv_python> -m daemon.inline_runtime

Reads the script to run from stdin, executes it in a scratch namespace, and
writes a single JSON line to stdout, the same envelope shape as DazScript's
own /execute endpoint on port 18811:

    {"success": true,  "result": <json-or-null>, "output": [...], "error": ""}
    {"success": false, "result": null,            "output": [...], "error": "<traceback>"}

Return-value semantics mirror JavaScript's `eval()` of a whole script (what
DazScript's /execute does under the hood): if the script's last statement is
a bare expression, its value becomes `result`; everything before it runs as
plain statements via exec().

A fresh subprocess per call (rather than a warm worker like WorkerManager's
plugin workers) keeps run_venv a true clean-slate scratch interpreter -- no
state leaks between /run calls, and a script that hangs or crashes only
takes down this one-shot process, never the daemon itself.
"""

from __future__ import annotations

import ast
import io
import json
import sys
import traceback
from contextlib import redirect_stdout


def _run(code: str) -> dict:
    try:
        tree = ast.parse(code, mode="exec")
    except SyntaxError as exc:
        return {
            "success": False,
            "result": None,
            "output": [],
            "error": "".join(traceback.format_exception_only(type(exc), exc)).strip(),
        }

    last_expr = None
    if tree.body and isinstance(tree.body[-1], ast.Expr):
        last_expr = ast.Expression(tree.body.pop().value)
        ast.fix_missing_locations(last_expr)

    output = io.StringIO()
    namespace: dict = {"__name__": "__dpb_run__"}
    try:
        with redirect_stdout(output):
            exec(compile(tree, "<run>", "exec"), namespace)
            result = eval(compile(last_expr, "<run>", "eval"), namespace) if last_expr is not None else None
        json.dumps(result)  # fail fast if the result can't cross the wire as JSON
    except Exception as exc:
        return {
            "success": False,
            "result": None,
            "output": output.getvalue().splitlines(),
            "error": "".join(traceback.format_exception(type(exc), exc, exc.__traceback__)).strip(),
        }

    return {"success": True, "result": result, "output": output.getvalue().splitlines(), "error": ""}


def main() -> None:
    code = sys.stdin.read()
    sys.stdout.write(json.dumps(_run(code)) + "\n")
    sys.stdout.flush()


if __name__ == "__main__":
    main()
