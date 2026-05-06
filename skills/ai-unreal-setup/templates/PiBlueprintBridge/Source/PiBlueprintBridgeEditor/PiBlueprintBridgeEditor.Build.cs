using UnrealBuildTool;

public class PiBlueprintBridgeEditor : ModuleRules
{
    public PiBlueprintBridgeEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "PiBlueprintBridgeRuntime"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "AssetRegistry",
            "BlueprintGraph",
            "EditorScriptingUtilities",
            "Json",
            "Kismet",
            "KismetCompiler",
            "UnrealEd"
        });
    }
}
