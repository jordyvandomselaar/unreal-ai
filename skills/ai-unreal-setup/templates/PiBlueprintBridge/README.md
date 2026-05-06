# Pi Blueprint Bridge

Project-local Unreal plugin that exposes editor-safe Blueprint generation commands to Python/Pi.

Modules:

- `PiBlueprintBridgeRuntime`: reusable runtime Blueprint helper functions such as `FollowPlayerPawn` and `WanderActor`.
- `PiBlueprintBridgeEditor`: editor-only asset, component, and K2 graph generation commands.

Generic Blueprint assembly support:

- `CreateStaticMeshAssemblyBlueprint` creates or updates an Actor Blueprint from an array of `PiBlueprintStaticMeshPart` values.
- `CreateStaticMeshAssemblyBlueprintFromJson` accepts the same part model as JSON, which is the easiest Pi entrypoint for ad-hoc objects like trees, houses, props, creatures, etc.
- Each part can set mesh path, parent, relative location/rotation/scale, collision, shadowing, a material path, or an inline solid color.
- Mesh path accepts full asset paths or aliases for built-in primitives: `cube`, `sphere`, `cylinder`, `cone`, and `plane`.
- Inline colors are materialized as reusable assets under `/Game/Pi/GeneratedMaterials`.
- `AddTickFunctionCall` wires Event Tick to any BlueprintCallable function and can set default pins, so generated actors can receive behavior without hard-coded object-specific Blueprint creators.

After the plugin compiles and Unreal restarts, Pi can generate a proof Blueprint with:

```bash
cmd.exe /c tools\\unreal\\ue_remote.cmd exec "import unreal; print(unreal.PiBlueprintBridgeEditorLibrary.create_follow_player_cube_blueprint('/Game/Blueprints/BP_FollowPlayerCube'))"
```

That command creates an Actor Blueprint with:

- a `Cube` StaticMeshComponent using `/Engine/BasicShapes/Cube.Cube`
- an Event Tick node
- a call to `PiBlueprintRuntimeLibrary.FollowPlayerPawn`
- pin wiring from Tick exec/DeltaSeconds and Self/Follower

And a multi-part static mesh object with JSON:

```bash
cmd.exe /c tools\\unreal\\ue_remote.cmd exec "import unreal, json; parts=[{'name':'Trunk','mesh':'cylinder','location':[0,0,140],'scale':[0.7,0.7,2.8],'color':[0.35,0.18,0.08]}, {'name':'Canopy','mesh':'sphere','location':[0,0,360],'scale':[2.4,2.4,1.7],'color':[0.05,0.45,0.12]}]; print(unreal.PiBlueprintBridgeEditorLibrary.create_static_mesh_assembly_blueprint_from_json('/Game/Blueprints/BP_PiTree', json.dumps(parts), True))"
```

If the project has never built C++ before, UnrealBuildTool requires the normal Windows C++/.NET build prerequisites.
