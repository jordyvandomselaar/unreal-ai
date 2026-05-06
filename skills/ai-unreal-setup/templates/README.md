# Unreal editor automation bridge

This project is configured so Pi can control a running Unreal Editor instance through Epic's built-in Python remote execution protocol. It can also generate real Blueprint assets through the project-local `PiBlueprintBridge` C++ plugin after the plugin is built.

## One-time editor step

Restart Unreal Editor after these project files are added. The project enables:

- `PythonScriptPlugin`
- `EditorScriptingUtilities`
- `RemoteControl`
- `PiBlueprintBridge`

Remote execution and Remote Control are configured for local-only access.

## Python remote execution commands

From Windows PowerShell/CMD:

```bat
tools\unreal\ue_remote.cmd list
tools\unreal\ue_remote.cmd smoke
tools\unreal\ue_remote.cmd exec "print('hello from unreal')"
```

From Pi/WSL, use Windows Python so UDP discovery stays on the same Windows network stack as the editor:

```bash
cmd.exe /c tools\\unreal\\ue_remote.cmd list
cmd.exe /c tools\\unreal\\ue_remote.cmd smoke
```

For larger edits, put Python in a file and run:

```bat
tools\unreal\ue_remote.cmd exec --file path\to\script.py
```

## Blueprint bridge

The C++ plugin source lives at:

```text
Plugins/PiBlueprintBridge
```

Build it with UnrealBuildTool while Unreal Editor is closed or Live Coding is disabled. Replace the engine and project paths with the local project values:

```powershell
& 'C:\Path\To\UnrealEngine\Engine\Build\BatchFiles\Build.bat' UnrealEditor Win64 Development -Project='C:\path\to\Project.uproject' -WaitMutex -FromMsBuild
```

After build/restart, Python can call:

```python
unreal.PiBlueprintBridgeEditorLibrary.create_follow_player_cube_blueprint('/Game/Blueprints/BP_FollowPlayerCube')
```

That creates an Actor Blueprint with a Cube StaticMeshComponent and Event Tick graph wired to `PiBlueprintRuntimeLibrary.FollowPlayerPawn`.

## Notes

- Python remote execution is the main path for editor automation: moving actors, spawning actors, saving assets/levels, and running editor utilities.
- `PiBlueprintBridge` is the path for durable Blueprint asset/component/K2 graph generation.
- Remote Control HTTP/WebSocket is enabled on `127.0.0.1` for reflected property/function access on ports `30010` and `30020`.
- Do not expose these ports or Python remote execution outside localhost unless you intentionally want a remote code execution foot-gun.
