#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PiBlueprintBridgeEditorLibrary.generated.h"

class AActor;
class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UMaterial;
class USceneComponent;
class UK2Node_CallFunction;
class UK2Node_Event;
class UK2Node_Self;

/** One visual part in an Actor Blueprint assembled from StaticMeshComponents. */
USTRUCT(BlueprintType)
struct PIBLUEPRINTBRIDGEEDITOR_API FPiBlueprintStaticMeshPart
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pi|Blueprint Bridge")
    FName ComponentName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pi|Blueprint Bridge")
    FString StaticMeshPath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pi|Blueprint Bridge")
    FName ParentComponentName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pi|Blueprint Bridge")
    FVector RelativeLocation;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pi|Blueprint Bridge")
    FRotator RelativeRotation;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pi|Blueprint Bridge")
    FVector RelativeScale;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pi|Blueprint Bridge")
    FString MaterialPath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pi|Blueprint Bridge")
    bool bUseColor;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pi|Blueprint Bridge")
    FLinearColor Color;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pi|Blueprint Bridge")
    bool bCollisionEnabled;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pi|Blueprint Bridge")
    bool bCastShadow;

    FPiBlueprintStaticMeshPart();
};

/** Editor-only commands that let Python/Pi create Blueprint assets and K2 graph nodes safely. */
UCLASS()
class PIBLUEPRINTBRIDGEEDITOR_API UPiBlueprintBridgeEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Pi|Blueprint Bridge")
    static UBlueprint* CreateActorBlueprint(const FString& AssetPath, TSubclassOf<AActor> ParentClass, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Pi|Blueprint Bridge")
    static bool ClearBlueprintComponents(UBlueprint* Blueprint, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Pi|Blueprint Bridge")
    static bool AddSceneComponent(UBlueprint* Blueprint, FName ComponentName, FName ParentComponentName, bool bMakeRoot, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Pi|Blueprint Bridge")
    static bool AddStaticMeshComponent(UBlueprint* Blueprint, FName ComponentName, const FString& StaticMeshPath, bool bMakeRoot, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Pi|Blueprint Bridge")
    static bool AddStaticMeshComponentFromPart(UBlueprint* Blueprint, const FPiBlueprintStaticMeshPart& Part, bool bMakeRoot, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Pi|Blueprint Bridge")
    static bool SetComponentRelativeTransform(UBlueprint* Blueprint, FName ComponentName, FVector RelativeLocation, FRotator RelativeRotation, FVector RelativeScale, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Pi|Blueprint Bridge")
    static UMaterial* CreateSolidColorMaterial(const FString& AssetPath, FLinearColor BaseColor, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Pi|Blueprint Bridge")
    static UBlueprint* CreateStaticMeshAssemblyBlueprint(const FString& AssetPath, const TArray<FPiBlueprintStaticMeshPart>& Parts, bool bReplaceExistingComponents, FString& OutError);

    /** JSON convenience wrapper for Pi: creates an Actor Blueprint from a generated parts list without requiring Python struct construction. */
    UFUNCTION(BlueprintCallable, Category = "Pi|Blueprint Bridge")
    static UBlueprint* CreateStaticMeshAssemblyBlueprintFromJson(const FString& AssetPath, const FString& PartsJson, bool bReplaceExistingComponents, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Pi|Blueprint Bridge")
    static UEdGraph* GetEventGraph(UBlueprint* Blueprint, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Pi|Blueprint Bridge")
    static UK2Node_Event* AddEventNode(UBlueprint* Blueprint, UEdGraph* Graph, FName EventName, UClass* EventClass, FVector2D Location, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Pi|Blueprint Bridge")
    static UK2Node_CallFunction* AddCallFunctionNode(UEdGraph* Graph, UClass* FunctionOwnerClass, FName FunctionName, FVector2D Location, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Pi|Blueprint Bridge")
    static UK2Node_Self* AddSelfNode(UEdGraph* Graph, FVector2D Location, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Pi|Blueprint Bridge")
    static bool ConnectPins(UEdGraphNode* FromNode, FName FromPinName, UEdGraphNode* ToNode, FName ToPinName, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Pi|Blueprint Bridge")
    static bool SetPinDefault(UEdGraphNode* Node, FName PinName, const FString& DefaultValue, FString& OutError);

    /** Generic behavior hook: Event Tick -> arbitrary BlueprintCallable function, with Self wired to ActorPinName and optional default pin values. */
    UFUNCTION(BlueprintCallable, Category = "Pi|Blueprint Bridge")
    static bool AddTickFunctionCall(UBlueprint* Blueprint, UClass* FunctionOwnerClass, FName FunctionName, FName ActorPinName, const TMap<FName, FString>& PinDefaults, FVector2D Location, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Pi|Blueprint Bridge")
    static bool CompileBlueprint(UBlueprint* Blueprint, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Pi|Blueprint Bridge")
    static bool SaveBlueprint(UBlueprint* Blueprint, FString& OutError);

    /** End-to-end proof command: creates / updates an Actor Blueprint with a cube component and Event Tick graph that follows player pawn 0. */
    UFUNCTION(BlueprintCallable, Category = "Pi|Blueprint Bridge")
    static UBlueprint* CreateFollowPlayerCubeBlueprint(const FString& AssetPath, FString& OutError);
};
