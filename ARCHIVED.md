# Archived

This repo is **archived** as of 2026-07-18. Active development has moved to
[`daz-script-server`](https://github.com/bluemoonfoundry/daz-script-server),
specifically its **Package Runner** (`.dzpkg` files, opened via File > Open /
drag-drop / content-library, same as a `.dsa` today — see that repo's
`package_runner/MANIFEST_SCHEMA.md` and `DzPackageImporter`).

## Why

This repo implemented a persistent-daemon "plugin" architecture: a FastAPI
server (port 18812) managing multiple installed plugins, each with its own
venv and a warm-worker subprocess pool, a JSON-RPC protocol, and a
`/plugins/*` REST API for lifecycle control (start/stop/restart/enable/
disable). It worked end to end, but didn't match how a non-technical DAZ
Studio artist actually wants to use a Python-backed capability — they want to
open something and have it run, not navigate to a plugin-management pane,
start a worker, and separately trigger a call.

`daz-script-server`'s Package Runner replaces it: a `.dzpkg` is a manifest +
a `dazpy`-based Python script, opened like a `.dsa`, run as a one-shot
subprocess against a lazily-created per-package venv, no persistent daemon.
The script calls back into DAZ Studio directly via `dazpy` against
`daz-script-server`'s existing port 18811.

Reusable pieces (`ZipInstaller`, the `uv`-based venv bootstrap, dependency
installation, the JSON envelope pattern) were ported over rather than
rebuilt from scratch — see `daz-script-server`'s `daz-script-server-5sw`
epic and its linked issues for the full history.

## Status

`main` is frozen. Not deleted — the warm-worker/JSON-RPC design here could
be worth resurrecting for a genuinely different use case (e.g. long-lived ML
model state across many fast calls); this archival is about UX fit for the
artist-facing "run a package" workflow, not a verdict that warm workers are
categorically wrong.
