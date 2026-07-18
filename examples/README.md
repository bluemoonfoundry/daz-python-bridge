# Examples

## hello_plugin

Minimal example plugin for manually validating the DPB plugin lifecycle
(resolving → ready, start/stop/restart/enable/disable, worker calls) against
a real DAZ Studio pane. Exposes three functions: `hello(name="world")`,
`add(a, b)`, and `boom()` (deliberately raises, to exercise per-call error
reporting without crashing the worker).

`hello_plugin.zip` is the packaged form -- what a real drag-and-drop install
would consume once that's wired into the pane (not yet; see
daz-python-bridge-7wq's notes). Until then, install it manually:

```bash
# <plugins_dir> is <DaemonPaths::baseDir()>/plugins, e.g. on Windows:
#   %LOCALAPPDATA%\DAZ\Studio4\DazPythonBridge\plugins   (SDK4 -- QDesktopServices)
#   %APPDATA%\DAZ\Studio6\DazPythonBridge\plugins        (SDK6 -- QStandardPaths;
#                                                          note Roaming, not Local)
mkdir -p "<plugins_dir>/hello_plugin"
cp examples/hello_plugin/manifest.json examples/hello_plugin/main.py "<plugins_dir>/hello_plugin/"
uv venv "<plugins_dir>/hello_plugin/venv" --python 3.11
echo '{"state": "ok"}' > "<plugins_dir>/hello_plugin/install_status.json"
```

The daemon re-scans `<plugins_dir>` on every `GET /plugins` poll (no restart
needed) -- it'll show up in the pane's Installed Plugins table within one
poll interval (5s).

## call_hello_plugin.dsa

DazScript test script that calls `hello_plugin`'s `hello()`, `add()`, and
`boom()` over `POST /plugins/hello_plugin/call`, via `DzHttpHelper` -- the
actual intended way DazScript uses an installed DPB plugin (see
`daemon/app.py`'s `call_plugin()` docstring for the canonical client sketch
this mirrors). The pane's Start/Stop/etc. controls are admin-only and never
invoke a plugin's own functions; this is the real usage path.

Run it from DAZ Studio's Script IDE (or via daz-script-server's own
`/execute`). Requires the Daz Python Bridge pane to have been opened at
least once this session (that's what launches the daemon) and
`hello_plugin` to be installed -- no need to Start it first, `/call` spawns
its worker lazily on first use.
