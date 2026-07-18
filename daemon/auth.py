"""X-DPB-Token enforcement (daz-python-bridge-sop.7).

DSS is the sole token generator, reusing its existing SecureRandom-backed
AuthenticationService (src/AuthenticationService.cpp) exactly as it already
does for dazscriptserver_token.txt on port 18811 -- writing to
~/.daz3d/dazpythonbridge_token.txt with the same chmod-600 enforcement. This
module only ever reads that file, once, at daemon startup; it never
generates or rotates a token itself.

If the file doesn't exist yet (DSS hasn't started the daemon before), no
token is loaded and every gated request is rejected -- there is no
"unauthenticated fallback" mode.
"""

from __future__ import annotations

import hmac
from pathlib import Path
from typing import Optional

from fastapi import Header, HTTPException


def token_file_path() -> Path:
    return Path.home() / ".daz3d" / "dazpythonbridge_token.txt"


def load_token() -> Optional[str]:
    path = token_file_path()
    if not path.is_file():
        return None
    try:
        token = path.read_text().strip()
    except OSError:
        return None
    return token if len(token) >= 32 else None


_TOKEN = load_token()


def require_token(x_dpb_token: str = Header(default="")) -> None:
    if not _TOKEN or not hmac.compare_digest(x_dpb_token, _TOKEN):
        raise HTTPException(status_code=401, detail="Missing or invalid X-DPB-Token")
