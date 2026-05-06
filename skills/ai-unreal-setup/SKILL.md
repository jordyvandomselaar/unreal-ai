---
name: ai-unreal-setup
description: Set up and debug a local-only Pi-to-Unreal-Editor automation bridge using Unreal Python remote execution plus Remote Control. Use when a user wants Pi to control Unreal Engine projects, move actors, run editor Python, create/save assets, or prepare for Blueprint/editor automation.
---

# AI Unreal Setup

Use this skill to make a local Unreal Engine project controllable from Pi or another local automation agent. The supported setup is:

- Unreal's built-in **Python remote execution** as the primary editor-control path.
- Unreal's **Remote Control** HTTP/WebSocket API as a secondary reflected property/function path.
- A small project-local bridge under `tools/unreal/` plus helper functions under `Content/Python/`.
- Everything bound to `127.0.0.1`; do not expose this to LAN/VPN.

Remote Python execution is remote code execution. Treat it like a loaded nail gun.

## Reference facts to verify against the installed Unreal version

Before editing a project, inspect the target project's `.uproject`, config files, and installed Unreal plugins. For stock UE 5.x installations, the relevant locations are typically:

- Python Script plugin descriptor: `Engine/Plugins/Experimental/PythonScriptPlugin/PythonScriptPlugin.uplugin`
- Editor Scripting Utilities descriptor: `Engine/Plugins/Editor/EditorScriptingUtilities/EditorScriptingUtilities.uplugin`
- Remote Control descriptor: `Engine/Plugins/VirtualProduction/RemoteControl/RemoteControl.uplugin`
- Remote Control Web Interface descriptor: `Engine/Plugins/VirtualProduction/RemoteControlWebInterface/RemoteControlWebInterface.uplugin`
- Unreal Python remote execution client: `Engine/Plugins/Experimental/PythonScriptPlugin/Content/Python/remote_execution.py`
- Python plugin settings source: `Engine/Plugins/Experimental/PythonScriptPlugin/Source/PythonScriptPlugin/Private/PythonScriptPluginSettings.*`
- Remote Control settings source/header: `Engine/Plugins/VirtualProduction/RemoteControl/Source/RemoteControlCommon/Public/RemoteControlSettings.h`

Key assumptions this skill relies on:

- `PythonScriptPlugin` exposes `UPythonScriptPluginSettings` in config section `[/Script/PythonScriptPlugin.PythonScriptPluginSettings]`.
- Python remote execution defaults are multicast endpoint `239.0.0.1:6766`, bind address `127.0.0.1`, command endpoint `127.0.0.1:6776`, TTL `0`.
- Unreal's `remote_execution.py` should be imported from the installed engine, not vendored or reimplemented.
- `RemoteControl` exposes `URemoteControlSettings` in config section `[/Script/RemoteControlCommon.RemoteControlSettings]`.
- `RemoteControlWebInterface` exists but should **not** be enabled by default because it starts a web app and may require Node. It is not needed for Pi control.

If the target Unreal version differs from these assumptions, inspect the plugin source before changing project files.

## Files this skill carries

Copy these templates into the target project instead of rewriting from memory:

- `templates/ue_remote.py` → `tools/unreal/ue_remote.py`
- `templates/ue_remote.cmd` → `tools/unreal/ue_remote.cmd`
- `templates/README.md` → `tools/unreal/README.md`
- `templates/pi_bridge.py` → `Content/Python/pi_bridge.py`
- `scripts/setup_unreal_bridge.py` can apply the whole setup to a project.

The command bridge auto-detects the project root, project name, and default engine root from the target `.uproject`.

## Safety and approval rules

- Do **not** run install/setup/bootstrap/make/migration commands blindly.
- This skill's `scripts/setup_unreal_bridge.py` does not install dependencies; it only edits project files and copies templates. Still, before running it, tell the user exactly which files will change.
- If Unreal Editor is running, do not kill or restart it. Ask the human to save work and restart after config changes.
- Never bind Remote Control or Python remote execution to `0.0.0.0` unless the user explicitly requests it and accepts the RCE risk. The default here is local-only.
- Do not enable `RemoteControlWebInterface` by default.
- Do not save levels/assets unless the user explicitly asks. A running editor may be on an unsaved `/Temp/Untitled_*` world.

## Setup workflow for a new project

### 1. Inspect the project and engine

From the project directory:

```bash
pwd
rg --files -g '*.uproject' -g 'Config/*.ini' | sed -n '1,80p'
cat *.uproject
```

Find the engine install. Common examples:

```bash
ls -1 "/mnt/c/Program Files/Epic Games"
ls -1 "$HOME/UnrealEngine"
```

If the project uses a different engine version or custom engine location, re-check plugin locations/settings names with `rg` before editing. Example for a Windows install mounted into WSL:

```bash
rg -n "UPythonScriptPluginSettings|bRemoteExecution|RemoteExecutionMulticast" "/mnt/c/Program Files/Epic Games/UE_X.Y/Engine/Plugins/Experimental/PythonScriptPlugin" -S
rg -n "URemoteControlSettings|RemoteControlHttpServerPort|RemoteControlWebSocketServerPort|bRestrictServerAccess" "/mnt/c/Program Files/Epic Games/UE_X.Y/Engine/Plugins/VirtualProduction/RemoteControl" -S
```

### 2. Apply the bridge files

Preferred: use the bundled script after explaining the intended writes.

```bash
python3 skills/ai-unreal-setup/scripts/setup_unreal_bridge.py --dry-run /path/to/UnrealProject
python3 skills/ai-unreal-setup/scripts/setup_unreal_bridge.py /path/to/UnrealProject
```

If the skill is installed somewhere else, run the script from that installed skill path and pass the target project path.

Manual equivalent:

1. In the `.uproject`, ensure:

```json
{
  "Name": "PythonScriptPlugin",
  "Enabled": true,
  "TargetAllowList": ["Editor"]
}
```

```json
{
  "Name": "EditorScriptingUtilities",
  "Enabled": true,
  "TargetAllowList": ["Editor"]
}
```

```json
{
  "Name": "RemoteControl",
  "Enabled": true
}
```

2. Add/update `Config/DefaultEngine.ini`:

```ini
[/Script/PythonScriptPlugin.PythonScriptPluginSettings]
bRemoteExecution=True
RemoteExecutionMulticastGroupEndpoint=239.0.0.1:6766
RemoteExecutionMulticastBindAddress=127.0.0.1
RemoteExecutionSendBufferSizeBytes=2097152
RemoteExecutionReceiveBufferSizeBytes=2097152
RemoteExecutionMulticastTtl=0
bRunPipInstallOnStartup=False
```

3. Add/update `Config/DefaultRemoteControl.ini`:

```ini
[/Script/RemoteControlCommon.RemoteControlSettings]
bAutoStartWebServer=True
bAutoStartWebSocketServer=True
RemoteControlHttpServerPort=30010
RemoteControlWebSocketServerPort=30020
RemoteControlWebsocketServerBindAddress=127.0.0.1
RemoteControlWebInterfaceBindAddress=127.0.0.1
RemoteControlWebInterfacePort=30000
bRestrictServerAccess=True
bEnableRemotePythonExecution=False
bAllowConsoleCommandRemoteExecution=False
AllowedOrigin=*
!AllowlistedClients=ClearArray
+AllowlistedClients=(LowerBound=(ClassA=127,ClassB=0,ClassC=0,ClassD=1),UpperBound=(ClassA=127,ClassB=0,ClassC=0,ClassD=1))
bEnforcePassphraseForRemoteClients=True
```

4. Copy templates:

```bash
mkdir -p tools/unreal Content/Python
cp /path/to/ai-unreal-setup/templates/ue_remote.py tools/unreal/ue_remote.py
cp /path/to/ai-unreal-setup/templates/ue_remote.cmd tools/unreal/ue_remote.cmd
cp /path/to/ai-unreal-setup/templates/README.md tools/unreal/README.md
cp /path/to/ai-unreal-setup/templates/pi_bridge.py Content/Python/pi_bridge.py
```

### 3. Update copied scripts when project settings are dynamic

After copying templates, inspect `tools/unreal/ue_remote.py` and keep it in sync with the actual project config.

- The script auto-detects the project name from the `.uproject`; do not hard-code personal or example project names into the template.
- If you change Python remote execution values in `Config/DefaultEngine.ini`, update `_remote_config()` in `tools/unreal/ue_remote.py` to match exactly. This includes multicast group, multicast port, bind address, TTL, and command endpoint.
- If the engine is not under a standard Epic install path, prefer setting `UE_ENGINE_ROOT`. If that is not acceptable for the project, update `_default_engine_root()` in the copied script.
- If the target project needs custom default timeouts, node selection, or command behavior, update the copied script in the target project rather than changing the public template unless the behavior should apply to every project.
- If multiple Unreal editors may be open, prefer passing `--project <ProjectName>` instead of baking a machine-specific default into the script.

### 4. Human action required

The human must:

1. Save any open work in Unreal Editor.
2. Restart the project after the `.uproject` and config edits.
3. Tell Pi to verify after the editor is back up.

This restart is required because Unreal loads plugins and project Python paths at editor startup.

### 5. Verify

When Unreal Editor is running on Windows and Pi/WSL is driving it, use Windows Python so UDP discovery runs on the same Windows networking stack as the editor:

```bash
cmd.exe /c tools\\unreal\\ue_remote.cmd --timeout 8 list
cmd.exe /c tools\\unreal\\ue_remote.cmd --timeout 8 smoke
cmd.exe /c tools\\unreal\\ue_remote.cmd --timeout 8 exec --json "import pi_bridge; pi_bridge.print_snapshot()"
```

Expected `list` shape:

```text
<node-id> project=<ProjectName> engine=<EngineVersion> root=<ProjectRoot>
```

Expected `smoke` shape:

```text
PI_SMOKE {"actor_count": ..., "engine_version": "...", "project_dir": "...", "selected_actors": [], "world": "..."}
```

### 6. Use after verification

Examples:

```bash
cmd.exe /c tools\\unreal\\ue_remote.cmd exec "import pi_bridge; pi_bridge.print_snapshot()"
cmd.exe /c tools\\unreal\\ue_remote.cmd exec "import pi_bridge; print(pi_bridge.snapshot())"
```

For larger editor actions, write a temporary Python file and run:

```bash
cmd.exe /c tools\\unreal\\ue_remote.cmd exec --file path\\to\\script.py
```

Prefer transactions for actor/asset edits:

```python
with unreal.ScopedEditorTransaction("Pi edit"):
    actor.modify()
    actor.set_actor_location(unreal.Vector(0, 0, 100), False, True)
```

## Debugging matrix

### `No Unreal Editor Python remote execution nodes found`

Check:

1. Unreal Editor is open and the target project has been restarted after plugin/config changes.
2. `.uproject` includes `PythonScriptPlugin` with `Enabled: true`.
3. `Config/DefaultEngine.ini` has `bRemoteExecution=True` under `[/Script/PythonScriptPlugin.PythonScriptPluginSettings]`.
4. The command is using Windows Python via `cmd.exe /c tools\\unreal\\ue_remote.cmd ...`, not WSL Python directly, when controlling a Windows editor from WSL.
5. No other process is occupying UDP port `6766` or TCP port `6776`.
6. Windows firewall/security software is not blocking local UDP multicast loopback.

### Multiple Unreal nodes found

Close extra editor instances or pass the desired project:

```bash
cmd.exe /c tools\\unreal\\ue_remote.cmd --project YourProjectName list
```

### Script cannot find `remote_execution.py`

The bridge derives the engine root from `.uproject` `EngineAssociation`, e.g. `5.4` → `C:\Program Files\Epic Games\UE_5.4` on Windows or `/mnt/c/Program Files/Epic Games/UE_5.4` from WSL. If Unreal is elsewhere, set `UE_ENGINE_ROOT` before invoking:

```bat
set UE_ENGINE_ROOT=C:\Path\To\UE_5.4
tools\unreal\ue_remote.cmd smoke
```

### `import pi_bridge` fails

Check that `Content/Python/pi_bridge.py` exists and restart the editor. Unreal adds project Python paths at startup.

### Remote Control HTTP does not respond

Python remote execution can still work without HTTP. For HTTP specifically, check:

```bash
curl http://127.0.0.1:30010/remote/info
```

Then inspect `Saved/Logs/<Project>.log` for `WebRemoteControl` or `RemoteControl` startup errors.

### The editor opens an unsaved `/Temp/Untitled_*` world

That is normal for blank/template projects. Do not save it unless the user asks. Ask which map/level should be edited or saved.

### Blueprint graph editing request

Python can create assets and do some Blueprint setup, but complex K2 graph/node surgery is brittle through Python alone. If the user wants serious Blueprint graph authoring, propose a small C++ Editor plugin that exposes stable commands such as:

- `create_blueprint`
- `add_component`
- `add_event_graph_node`
- `connect_pins`
- `compile_blueprint`
- `save_asset`

Do not fake binary `.uasset` edits with text manipulation.
