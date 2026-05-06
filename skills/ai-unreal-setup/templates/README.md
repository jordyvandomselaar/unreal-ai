# Unreal editor automation bridge

This project is configured so Pi can control a running Unreal Editor instance through Epic's built-in Python remote execution protocol.

## One-time editor step

Restart Unreal Editor after pulling these project files. The project enables:

- `PythonScriptPlugin`
- `EditorScriptingUtilities`
- `RemoteControl`

Remote execution and Remote Control are both configured for local-only access.

## Commands

From Windows PowerShell/CMD:

```bat
tools\unreal\ue_remote.cmd list
tools\unreal\ue_remote.cmd smoke
tools\unreal\ue_remote.cmd exec "print('hello from unreal')"
```

From Pi/WSL, use Windows Python so the UDP discovery stays on the same Windows network stack as the editor:

```bash
cmd.exe /c tools\\unreal\\ue_remote.cmd list
cmd.exe /c tools\\unreal\\ue_remote.cmd smoke
```

For larger edits, put Python in a file and run:

```bat
tools\unreal\ue_remote.cmd exec --file path\to\script.py
```

## Notes

- Python remote execution is the main path for editor automation: moving actors, spawning actors, saving assets/levels, and running editor utilities.
- Remote Control HTTP/WebSocket is enabled on `127.0.0.1` for reflected property/function access on ports `30010` and `30020`.
- Complex Blueprint graph authoring may still need a small C++ editor plugin later; binary `.uasset` graph surgery is not something to fake with text edits.
