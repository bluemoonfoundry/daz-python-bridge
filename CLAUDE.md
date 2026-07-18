# Project Instructions for AI Agents

This file provides instructions and context for AI coding agents working on this project.

<!-- BEGIN BEADS INTEGRATION v:1 profile:minimal hash:7510c1e2 -->
## Beads Issue Tracker

This project uses **bd (beads)** for issue tracking. Run `bd prime` to see full workflow context and commands.

### Quick Reference

```bash
bd ready              # Find available work
bd show <id>          # View issue details
bd update <id> --claim  # Claim work
bd close <id>         # Complete work
```

### Rules

- Use `bd` for ALL task tracking — do NOT use TodoWrite, TaskCreate, or markdown TODO lists
- Run `bd prime` for detailed command reference and session close protocol
- Use `bd remember` for persistent knowledge — do NOT use MEMORY.md files

**Architecture in one line:** issues live in a local Dolt DB; sync uses `refs/dolt/data` on your git remote; `.beads/issues.jsonl` is a passive export. See https://github.com/gastownhall/beads/blob/main/docs/SYNC_CONCEPTS.md for details and anti-patterns.

## Session Completion

**When ending a work session**, you MUST complete ALL steps below. Work is NOT complete until `git push` succeeds.

**MANDATORY WORKFLOW:**

1. **File issues for remaining work** - Create issues for anything that needs follow-up
2. **Run quality gates** (if code changed) - Tests, linters, builds
3. **Update issue status** - Close finished work, update in-progress items
4. **PUSH TO REMOTE** - This is MANDATORY:
   ```bash
   git pull --rebase
   git push
   git status  # MUST show "up to date with origin"
   ```
5. **Clean up** - Clear stashes, prune remote branches
6. **Verify** - All changes committed AND pushed
7. **Hand off** - Provide context for next session

**CRITICAL RULES:**
- Work is NOT complete until `git push` succeeds
- NEVER stop before pushing - that leaves work stranded locally
- NEVER say "ready to push when you are" - YOU must push
- If push fails, resolve and retry until it succeeds
<!-- END BEADS INTEGRATION -->


## Build & Test

**Daemon (Python, daemon/):**
```bash
pip install -e ".[test]"
pytest
```

**DSS plugin (C++, requires the DAZ Studio SDK + Qt6 devkit):**
```bash
./build.sh                        # configure + build only, SDK4 (default)
./build.sh install                # + copy DLL into DAZ_STUDIO_EXE_DIR/plugins
./build.sh install --sdk-version 6
./build.sh --help                 # full option list
```
Reads `DAZ_SDK_DIR`, `DAZ_SDK_DIR_V6`, `QT6_DIR`, `DAZ_STUDIO_EXE_DIR`,
`DAZ_STUDIO_EXE_DIR_V6` from a local `.env` (gitignored, machine-specific
paths). `QT6_DIR` is required for every build, not just `--sdk-version 6` --
`DazPythonBridgeCore` (the SDK-independent bootstrap/zip-install/daemon-client
backend) is always built against plain Qt6, even when the DSS plugin itself
links Qt4 for SDK4. `install` refuses to run while DAZ Studio is open (it
locks the DLL).

**Core-only C++ (no DAZ SDK, standalone via `build_test/`):**
```bash
cmake -B build_test -S . -DQt6_DIR=<qt6-cmake-dir>
cmake --build build_test --config Release
```
Builds `DazPythonBridgeCore` plus its manual test harnesses
(`test_zip_installer`, `test_plugin_dependency_installer`,
`test_plugin_installer` — not wired into CTest; run the `.exe`s directly and
check exit code/output). `test_plugin_dependency_installer` and
`test_plugin_installer` need `uv` on PATH and network access.

## Architecture Overview

Two DAZ Studio-facing pieces:
- **`daemon/`**: a FastAPI service DSS launches on 127.0.0.1:18812, running
  inside an isolated `run_venv`. `/plugins/*` manages community Python
  plugins (each gets its own venv + warm worker subprocess); `/run` executes
  inline Python for the Script-IDE-style pane. Complements the existing
  DazScript-facing daemon on port 18811 (DazScript-as-server) with the
  reverse direction (DAZ calling into Python).
- **`src/`/`include/`**: the DSS plugin pane (`DzPythonBridge`/SDK6,
  `dsp_DazPythonBridge` name-prefixed for SDK6's plugin scanner). Splits into
  `DazPythonBridgeCore` (Qt Core+Network only, no DAZ SDK dependency —
  daemon bootstrap, hardened zip install, plugin/worker status polling,
  inline-run HTTP client) and the SDK-dependent pane/registration code on
  top. `DazPythonBridgeCore`'s sources are compiled twice: once into the
  `DazPythonBridgeCore` static lib (Qt6, linked into the SDK6 plugin), and
  once compiled directly into the SDK4 plugin target against Qt 4.8 — the two
  Qt major versions aren't ABI-compatible, so the Qt6-built lib can't be
  linked into the SDK4 DLL. `JsonStd.h`/`PortableFs.h` are what let the exact
  same source files compile under either Qt version.

## Conventions & Patterns

- Qt4/Qt6 dual compatibility in `src`/`include` (SDK4 predates Qt5): no
  `QJsonDocument`/`QJsonObject`/`QJsonArray` (use `JsonStd.h`'s QVariant-based
  `parseObject`/`variantToJson` instead), no `QDir::removeRecursively()` (use
  `PortableFs.h`), no PMF-based `connect()` or lambda slots (old-style
  `SIGNAL()`/`SLOT()` macros only — a lambda that needs the firing
  `QNetworkReply*` back should become a real slot using `sender()`; extra
  context beyond the reply itself travels via `QObject::setProperty()`), no
  `QStringLiteral`/`QRegularExpression`/`QStandardPaths`/`QUuid::WithoutBraces`/
  `QStringList` brace-init/`QFileInfo::exists(path)` static overload/
  `QProcess::setProgram`+`setArguments`+`start()`. Guard anything else Qt5+
  behind `#if DAZ_SDK_MAJOR_VERSION >= 6`. Verify against the real SDK
  (`./build.sh install --sdk-version 4`) before assuming something compiles —
  Qt4/Qt5+ incompatibilities here have repeatedly turned out bigger or
  different than they look from documentation alone.
- `daemon/` code has no DAZ SDK dependency and is tested purely via pytest
  against a `TestClient`; C++ SDK-independent code (`DazPythonBridgeCore`) is
  tested via manual harnesses in `tests/cpp/`, not CTest.
