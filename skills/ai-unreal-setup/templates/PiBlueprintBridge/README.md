# Pi Blueprint Bridge

Project-local Unreal plugin that exposes editor-safe Blueprint generation commands to Python/Pi.

Modules:

- `PiBlueprintBridgeRuntime`: runtime Blueprint helper functions, currently `FollowPlayerPawn`.
- `PiBlueprintBridgeEditor`: editor-only asset, component, and K2 graph generation commands.

After the plugin compiles and Unreal restarts, Pi can generate a proof Blueprint with:

```bash
cmd.exe /c tools\\unreal\\ue_remote.cmd exec "import unreal; print(unreal.PiBlueprintBridgeEditorLibrary.create_follow_player_cube_blueprint('/Game/Blueprints/BP_FollowPlayerCube'))"
```

That command creates an Actor Blueprint with:

- a `Cube` StaticMeshComponent using `/Engine/BasicShapes/Cube.Cube`
- an Event Tick node
- a call to `PiBlueprintRuntimeLibrary.FollowPlayerPawn`
- pin wiring from Tick exec/DeltaSeconds and Self/Follower

If the project has never built C++ before, UnrealBuildTool requires the normal Windows C++/.NET build prerequisites.
