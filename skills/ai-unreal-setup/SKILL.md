---
name: ai-unreal-setup
description: Set up and debug a local-only Pi-to-Unreal-Editor automation bridge using Unreal Python remote execution, Remote Control, and a C++ Blueprint-generation editor plugin. Use when a user wants Pi to control Unreal Engine projects, move actors, run editor Python, create/save assets, or generate Blueprint components/K2 graphs.
---

# AI Unreal Setup

Use this skill to make a local Unreal Engine project controllable from Pi or another local automation agent, including durable Blueprint asset generation.

The supported setup has two layers:

1. **Python/editor automation bridge**
   - Enables Unreal's built-in Python remote execution.
   - Adds `tools/unreal/ue_remote.py` and `tools/unreal/ue_remote.cmd`.
   - Adds project helper module `Content/Python/pi_bridge.py`.
   - Lets Pi inspect/move actors, spawn actors, run editor Python, create/save assets, etc.

2. **C++ Blueprint bridge plugin**
   - Adds project plugin `Plugins/PiBlueprintBridge`.
   - Exposes `unreal.PiBlueprintBridgeEditorLibrary` to Python.
   - Lets Pi create Actor Blueprints, add components, add Event Graph nodes, wire pins, compile, and save.
   - Adds runtime function `PiBlueprintRuntimeLibrary.FollowPlayerPawn` used by the proof Blueprint.

Everything network-facing should stay local-only. Python remote execution is remote code execution. Treat it like a loaded nail gun.

## Public-skill hygiene

This repository is public. Do **not** add specific user names, project names, local machine names, private paths, or one-off verification output to the skill or templates.

When a target project needs local values:

- Derive them from the target `.uproject`, config files, installed Unreal location, `UE_ENGINE_ROOT`, or the user's explicit input.
- Put them only in the target project's copied files or commands being run for that project.
- Do not commit target-specific paths, project names, engine build strings, actor counts, or logs back into this skill repo.
- Keep `templates/ue_remote.py` generic. It should auto-detect the project name from the `.uproject`; do not reintroduce hard-coded example project names.

## Bundled code templates

Do not rewrite from memory. Copy these files from this skill:

```text
templates/ue_remote.py              -> tools/unreal/ue_remote.py
templates/ue_remote.cmd             -> tools/unreal/ue_remote.cmd
templates/README.md                 -> tools/unreal/README.md
templates/pi_bridge.py              -> Content/Python/pi_bridge.py
templates/PiBlueprintBridge/        -> Plugins/PiBlueprintBridge/
scripts/setup_unreal_bridge.py      -> applies file/config/plugin setup
```

`templates/PiBlueprintBridge/` contains the C++ plugin source:

```text
PiBlueprintBridge.uplugin
README.md
Source/PiBlueprintBridgeRuntime/PiBlueprintBridgeRuntime.Build.cs
Source/PiBlueprintBridgeRuntime/Public/PiBlueprintRuntimeLibrary.h
Source/PiBlueprintBridgeRuntime/Private/PiBlueprintRuntimeLibrary.cpp
Source/PiBlueprintBridgeRuntime/Private/PiBlueprintBridgeRuntimeModule.cpp
Source/PiBlueprintBridgeEditor/PiBlueprintBridgeEditor.Build.cs
Source/PiBlueprintBridgeEditor/Public/PiBlueprintBridgeEditorLibrary.h
Source/PiBlueprintBridgeEditor/Private/PiBlueprintBridgeEditorLibrary.cpp
Source/PiBlueprintBridgeEditor/Private/PiBlueprintBridgeEditorModule.cpp
```

## Reference facts to verify against the installed Unreal version

Before editing a project, inspect the target project's `.uproject`, config files, and installed Unreal plugins. For stock UE 5.x installations, the relevant locations are typically:

- Python Script plugin descriptor: `Engine/Plugins/Experimental/PythonScriptPlugin/PythonScriptPlugin.uplugin`
- Editor Scripting Utilities descriptor: `Engine/Plugins/Editor/EditorScriptingUtilities/EditorScriptingUtilities.uplugin`
- Remote Control descriptor: `Engine/Plugins/VirtualProduction/RemoteControl/RemoteControl.uplugin`
- Remote Control Web Interface descriptor: `Engine/Plugins/VirtualProduction/RemoteControlWebInterface/RemoteControlWebInterface.uplugin`
- Unreal Python remote execution client: `Engine/Plugins/Experimental/PythonScriptPlugin/Content/Python/remote_execution.py`
- Python plugin settings source: `Engine/Plugins/Experimental/PythonScriptPlugin/Source/PythonScriptPlugin/Private/PythonScriptPluginSettings.*`
- Remote Control settings source/header: `Engine/Plugins/VirtualProduction/RemoteControl/Source/RemoteControlCommon/Public/RemoteControlSettings.h`
- Blueprint/C++ editor APIs:
  - `Engine/Source/Editor/UnrealEd/Public/Kismet2/KismetEditorUtilities.h`
  - `Engine/Source/Editor/UnrealEd/Public/Kismet2/BlueprintEditorUtils.h`
  - `Engine/Source/Editor/BlueprintGraph/Classes/EdGraphSchema_K2_Actions.h`
  - `Engine/Source/Runtime/Engine/Classes/Engine/SimpleConstructionScript.h`

Key assumptions this skill relies on:

- `PythonScriptPlugin` exposes `UPythonScriptPluginSettings` in config section `[/Script/PythonScriptPlugin.PythonScriptPluginSettings]`.
- Python remote execution defaults are multicast endpoint `239.0.0.1:6766`, bind address `127.0.0.1`, command endpoint `127.0.0.1:6776`, TTL `0`.
- Use Epic's installed `remote_execution.py`; do not reimplement the protocol.
- `RemoteControl` uses config section `[/Script/RemoteControlCommon.RemoteControlSettings]`.
- `RemoteControlWebInterface` exists but should not be enabled by default; it can start a web app and may require Node. It is not needed for Pi control.
- Python can create basic Blueprint assets/components, but reliable Event Graph node/pin wiring needs a C++ editor plugin.

If the target Unreal version differs from these assumptions, inspect the plugin source before changing project files.

## Safety and approval rules

- Never install system dependencies without explicit approval in the current conversation.
- Never run setup/bootstrap/install/migration commands blindly. Inspect first.
- `scripts/setup_unreal_bridge.py` is safe to run after explanation: it edits project files and copies templates only. It does **not** install Visual Studio, .NET, packages, or compile code.
- Building the plugin runs UnrealBuildTool and the local C++ toolchain; run it only after the user agrees to build and Unreal Editor is closed or Live Coding is disabled.
- Installing `.NET Framework Developer Pack`, Visual Studio components, SDKs, or other toolchain pieces is system-install territory. Ask first.
- If Unreal Editor is running, do not kill it without explicit approval. Ask the human to save/close first, especially if dirty maps exist.
- Do not bind Python remote execution or Remote Control to `0.0.0.0` unless the user explicitly accepts the RCE risk.
- Do not save, delete, overwrite, or migrate levels/assets unless the user asks or approves the exact asset path.

## Full setup workflow for a new project

### 1. Inspect project and engine

From the target project directory:

```bash
pwd
rg --files -g '*.uproject' -g 'Config/*.ini' | sed -n '1,80p'
cat *.uproject
ls -1 "/mnt/c/Program Files/Epic Games" 2>/dev/null || true
```

Find engine version from `.uproject` `EngineAssociation`, e.g. `5.4` maps to a default Epic install path like `C:\Program Files\Epic Games\UE_5.4` on Windows or `/mnt/c/Program Files/Epic Games/UE_5.4` from WSL.

If the engine version or install layout differs, re-check settings/API names:

```bash
rg -n "UPythonScriptPluginSettings|bRemoteExecution|RemoteExecutionMulticast" "/path/to/UE_X.Y/Engine/Plugins/Experimental/PythonScriptPlugin" -S
rg -n "URemoteControlSettings|RemoteControlHttpServerPort|RemoteControlWebSocketServerPort|bRestrictServerAccess" "/path/to/UE_X.Y/Engine/Plugins/VirtualProduction/RemoteControl" -S
```

### 2. Apply bridge files/config

Preferred: use the bundled script after explaining the intended writes.

```bash
python3 /path/to/ai-unreal-setup/scripts/setup_unreal_bridge.py --dry-run /path/to/UnrealProject
python3 /path/to/ai-unreal-setup/scripts/setup_unreal_bridge.py /path/to/UnrealProject
```

The script updates/copies:

- `<Project>.uproject`
- `Config/DefaultEngine.ini`
- `Config/DefaultRemoteControl.ini`
- `tools/unreal/ue_remote.py`
- `tools/unreal/ue_remote.cmd`
- `tools/unreal/README.md`
- `Content/Python/pi_bridge.py`
- `Plugins/PiBlueprintBridge/**`

Manual equivalent:

1. Enable these plugins in `.uproject`:

```json
{ "Name": "PythonScriptPlugin", "Enabled": true, "TargetAllowList": ["Editor"] }
{ "Name": "EditorScriptingUtilities", "Enabled": true, "TargetAllowList": ["Editor"] }
{ "Name": "RemoteControl", "Enabled": true }
{ "Name": "PiBlueprintBridge", "Enabled": true }
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
bRestrictServerAccess=False
bEnableRemotePythonExecution=False
bAllowConsoleCommandRemoteExecution=False
AllowedOrigin=*
bEnforcePassphraseForRemoteClients=True
```

Important: avoid hand-written `AllowlistedClients=(LowerBound=...,UpperBound=...)` struct lines. Some Unreal versions reject that config shape. The setup script removes existing `AllowlistedClients` lines and relies on loopback-only bind addresses.

4. Copy templates:

```bash
mkdir -p tools/unreal Content/Python Plugins
cp /path/to/ai-unreal-setup/templates/ue_remote.py tools/unreal/ue_remote.py
cp /path/to/ai-unreal-setup/templates/ue_remote.cmd tools/unreal/ue_remote.cmd
cp /path/to/ai-unreal-setup/templates/README.md tools/unreal/README.md
cp /path/to/ai-unreal-setup/templates/pi_bridge.py Content/Python/pi_bridge.py
cp -R /path/to/ai-unreal-setup/templates/PiBlueprintBridge Plugins/PiBlueprintBridge
```

### 3. Update copied scripts when project settings are dynamic

After copying templates, inspect `tools/unreal/ue_remote.py` and keep it in sync with the actual target project.

- The script auto-detects the project name from the `.uproject`; do not hard-code personal or example project names into the template.
- If Python remote execution values in `Config/DefaultEngine.ini` differ from the defaults, update `_remote_config()` in `tools/unreal/ue_remote.py` to match exactly. This includes multicast group, multicast port, bind address, TTL, and command endpoint.
- If the engine is not under a standard Epic install path, prefer setting `UE_ENGINE_ROOT`. If that is not acceptable for the project, update `_default_engine_root()` in the copied script.
- If the target project needs custom default timeouts, node selection, or command behavior, update the copied script in the target project rather than changing the public template unless the behavior should apply to every project.
- Build commands must be constructed dynamically from the target `.uproject` and resolved engine root. Do not paste machine-specific examples into committed docs or templates.
- If multiple Unreal editors may be open, prefer passing `--project <ProjectName>` instead of baking a machine-specific default into the script.

### 4. Human action after file setup

The human must:

1. Save work in Unreal Editor.
2. Restart the project so plugin/config changes load.
3. For the C++ plugin build, close Unreal Editor or disable Live Coding.

### 5. Build the C++ Blueprint bridge

Only run after the user approves build and Unreal is closed or Live Coding is disabled.

Resolve these values from the target project:

- `ENGINE_ROOT`: Unreal engine root, e.g. from `UE_ENGINE_ROOT` or `.uproject` `EngineAssociation`.
- `PROJECT_FILE`: full path to the target `.uproject`.

Then build with UnrealBuildTool. Template command:

```bash
powershell.exe -NoProfile -Command "& '<ENGINE_ROOT>\Engine\Build\BatchFiles\Build.bat' UnrealEditor Win64 Development -Project='<PROJECT_FILE>' -WaitMutex -FromMsBuild"
```

Expected success includes plugin runtime/editor library links and `Result: Succeeded`.

If UnrealBuildTool says the .NET Framework SDK is missing, stop and ask for system-install approval before installing any developer pack or toolchain component.

### 6. Verify Python/editor bridge

After restarting Unreal with the project open, use Windows Python when controlling a Windows editor from WSL/Pi so UDP discovery runs on the same Windows networking stack as the editor:

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

### 7. Verify Blueprint generation

Use an unused proof asset path, or ask before deleting/overwriting an existing asset.

With Unreal closed, a commandlet can verify the C++ bridge without needing the remote Python server. Generate the proof script in the target project's `Saved/` directory:

```python
import unreal

asset_path = '/Game/PiProof/BP_FollowPlayerCube'
if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
    raise RuntimeError(f'{asset_path} already exists; choose a new proof path or get approval to replace it')

result = unreal.PiBlueprintBridgeEditorLibrary.create_follow_player_cube_blueprint(asset_path)
if isinstance(result, tuple):
    bp = result[0]
    error = result[1] if len(result) > 1 else ''
else:
    bp = result
    error = ''
if not bp:
    raise RuntimeError(f'Failed to create {asset_path}: {error}')
print(f'PI_BLUEPRINT_CREATED {bp.get_path_name()}')
```

Then run the commandlet with dynamically resolved paths:

```bash
powershell.exe -NoProfile -Command "& '<ENGINE_ROOT>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' '<PROJECT_FILE>' -run=pythonscript -script='<PROJECT_SAVED_DIR>\PiCreateBlueprintProof.py' -unattended -nop4 -nosplash"
```

Clean up temporary proof scripts after verification. Do not delete generated assets unless the user approves.

## What the Blueprint bridge exposes

After build/restart, Python sees:

```python
unreal.PiBlueprintBridgeEditorLibrary
unreal.PiBlueprintRuntimeLibrary
```

Useful proof call:

```python
result = unreal.PiBlueprintBridgeEditorLibrary.create_follow_player_cube_blueprint('/Game/PiProof/BP_FollowPlayerCube')
bp = result[0] if isinstance(result, tuple) else result
```

Why tuple handling? Unreal Python returns `UFUNCTION` out parameters as tuples. Functions with `FString& OutError` can return `(ReturnValue, OutError)`.

The generated proof Blueprint contains:

- Actor Blueprint at the requested asset path.
- `Cube` StaticMeshComponent using `/Engine/BasicShapes/Cube.Cube`.
- Event Tick node.
- Self node.
- Call to `PiBlueprintRuntimeLibrary.FollowPlayerPawn`.
- Wired exec, DeltaSeconds, and Follower pins.
- Offset default `(-250, 120, 120)` and Speed `6`.

## Debugging matrix

### `No Unreal Editor Python remote execution nodes found`

Check:

1. Unreal Editor is open and restarted after config/plugin changes.
2. `.uproject` has `PythonScriptPlugin` enabled.
3. `DefaultEngine.ini` has `bRemoteExecution=True`.
4. Invoke with Windows Python from WSL/Pi when controlling a Windows editor:
   ```bash
   cmd.exe /c tools\\unreal\\ue_remote.cmd list
   ```
5. UDP port `6766` / TCP port `6776` are not blocked.

### Multiple Unreal nodes found

Close extra editors or pass target project:

```bash
cmd.exe /c tools\\unreal\\ue_remote.cmd --project YourProjectName list
```

### `remote_execution.py` not found

The bridge maps `.uproject` `EngineAssociation` to the common Epic install path. If Unreal is elsewhere:

```bat
set UE_ENGINE_ROOT=C:\Path\To\UE_X.Y
tools\unreal\ue_remote.cmd smoke
```

### `import pi_bridge` fails

Ensure `Content/Python/pi_bridge.py` exists and restart Unreal. Project Python paths are loaded at editor startup.

### `NetFxSDK install dir` missing

Install a .NET Framework Developer Pack only after explicit user approval. This is a system dependency install.

### `Unable to build while Live Coding is active`

Close Unreal Editor, or stop Live Coding in the editor. Killing only the Live Coding console may not clear the lock.

### C++ ambiguous ternary around `TSubclassOf` / `UClass*`

Use explicit assignments:

```cpp
UClass* ResolvedEventClass = EventClass;
if (!ResolvedEventClass) { ResolvedEventClass = Blueprint->ParentClass; }
if (!ResolvedEventClass) { ResolvedEventClass = AActor::StaticClass(); }
```

### Duplicate `ReceiveTick` / multiple Event Tick nodes

Make Event node creation idempotent. The bundled bridge checks existing `UK2Node_Event` nodes before calling `FKismetEditorUtilities::AddDefaultEventNode`.

### RemoteControl `AllowlistedClients` config import failed

Remove hand-written `AllowlistedClients` struct lines and use loopback bind addresses. The bundled setup uses:

```ini
bRestrictServerAccess=False
RemoteControlWebsocketServerBindAddress=127.0.0.1
RemoteControlWebInterfaceBindAddress=127.0.0.1
```

### Commandlet returns failure even Python says script executed successfully

Unreal treats prior log errors as commandlet failure. Fix the underlying logged errors, not just Python exceptions.

### Unsaved `/Temp/Untitled_*` map

Normal for blank/template projects. Do not save/discard without asking.
