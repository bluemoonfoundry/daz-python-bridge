import sys
from pathlib import Path

import pytest
from fastapi import HTTPException

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from daemon import auth


def test_token_file_path_is_under_daz3d_home(monkeypatch, tmp_path):
    monkeypatch.setattr(Path, "home", lambda: tmp_path)
    assert auth.token_file_path() == tmp_path / ".daz3d" / "dazpythonbridge_token.txt"


def test_load_token_missing_file_returns_none(monkeypatch, tmp_path):
    monkeypatch.setattr(Path, "home", lambda: tmp_path)
    assert auth.load_token() is None


def test_load_token_too_short_is_rejected(monkeypatch, tmp_path):
    monkeypatch.setattr(Path, "home", lambda: tmp_path)
    daz3d = tmp_path / ".daz3d"
    daz3d.mkdir()
    (daz3d / "dazpythonbridge_token.txt").write_text("short")
    assert auth.load_token() is None


def test_load_token_valid_is_trimmed(monkeypatch, tmp_path):
    monkeypatch.setattr(Path, "home", lambda: tmp_path)
    daz3d = tmp_path / ".daz3d"
    daz3d.mkdir()
    token = "b" * 32
    (daz3d / "dazpythonbridge_token.txt").write_text(f"  {token}  \n")
    assert auth.load_token() == token


def test_require_token_raises_when_no_token_loaded(monkeypatch):
    monkeypatch.setattr(auth, "_TOKEN", None)
    with pytest.raises(HTTPException) as excinfo:
        auth.require_token(x_dpb_token="anything")
    assert excinfo.value.status_code == 401


def test_require_token_raises_on_mismatch(monkeypatch):
    monkeypatch.setattr(auth, "_TOKEN", "c" * 32)
    with pytest.raises(HTTPException) as excinfo:
        auth.require_token(x_dpb_token="wrong")
    assert excinfo.value.status_code == 401


def test_require_token_accepts_matching_token(monkeypatch):
    monkeypatch.setattr(auth, "_TOKEN", "c" * 32)
    auth.require_token(x_dpb_token="c" * 32)  # does not raise
