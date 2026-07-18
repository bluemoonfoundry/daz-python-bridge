"""DPB daemon FastAPI app.

Launched by DaemonProcess (src/DaemonProcess.cpp) as:

    <run_venv>/bin/python -m uvicorn daemon.app:app --host 127.0.0.1 --port 18812

Always bound to loopback by the launcher — this app does not decide its own
bind address. Endpoints beyond /health (token auth, /run, /plugins/*) are
added by the other DPB child issues; this is deliberately just the bootstrap
skeleton so DaemonHealthMonitor has something to poll against.
"""

from fastapi import FastAPI

app = FastAPI(title="Daz Python Bridge Daemon")


@app.get("/health")
def health() -> dict:
    return {"status": "ok"}
