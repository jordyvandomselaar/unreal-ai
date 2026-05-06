#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PiBlueprintBridgeEditorLibrary.generated.h"

class AActor;
class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UK2Node_CallFunction;
class UK2Node_Event;
class UK2Node_Self;

/** Editor-only commands that let Python/Pi create Blueprint assets and K2 graph nodes safely. */
UCLASS()
class PIBLUEPRINTBRIDGEEDITOR_API UPiBlueprintBridgeEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Pi|Blueprint Bridge")
    static UBlueprint* CreateActorBlueprint(const FString& AssetPath, TSubclassOf<AActor> ParentClass, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Pi|Blueprint Bridge")
    static bool AddStaticMeshComponent(UBlueprint* Blueprint, FName ComponentName, const FString& StaticMeshPath, bool bMakeRoot, FString& OutError);

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

    UFUNCTION(BlueprintCallable, Category = "Pi|Blueprint Bridge")
    static bool CompileBlueprint(UBlueprint* Blueprint, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Pi|Blueprint Bridge")
    static bool SaveBlueprint(UBlueprint* Blueprint, FString& OutError);

    /** End-to-end proof command: creates / updates an Actor Blueprint with a cube component and Event Tick graph that follows player pawn 0. */
    UFUNCTION(BlueprintCallable, Category = "Pi|Blueprint Bridge")
    static UBlueprint* CreateFollowPlayerCubeBlueprint(const FString& AssetPath, FString& OutError);
};
