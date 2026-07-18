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
#   %LOCALAPPDATA%\DAZ\Studio4\DazPythonBridge\plugins   (SDK4)
#   %LOCALAPPDATA%\DAZ\Studio6\DazPythonBridge\plugins   (SDK6)
mkdir -p "<plugins_dir>/hello_plugin"
cp examples/hello_plugin/manifest.json examples/hello_plugin/main.py "<plugins_dir>/hello_plugin/"
uv venv "<plugins_dir>/hello_plugin/venv" --python 3.11
echo '{"state": "ok"}' > "<plugins_dir>/hello_plugin/install_status.json"
```

The daemon re-scans `<plugins_dir>` on every `GET /plugins` poll (no restart
needed) -- it'll show up in the pane's Installed Plugins table within one
poll interval (5s).
