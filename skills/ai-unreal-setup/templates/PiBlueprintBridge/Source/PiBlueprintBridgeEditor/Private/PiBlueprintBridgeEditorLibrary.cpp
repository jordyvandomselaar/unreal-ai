#include "PiBlueprintBridgeEditorLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/StaticMeshComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EditorAssetLibrary.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_Self.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphSchema_K2_Actions.h"
#include "Misc/PackageName.h"
#include "PiBlueprintRuntimeLibrary.h"

#define LOCTEXT_NAMESPACE "PiBlueprintBridgeEditor"

namespace PiBlueprintBridge
{
    static bool Fail(FString& OutError, const FString& Message)
    {
        OutError = Message;
        UE_LOG(LogTemp, Warning, TEXT("PiBlueprintBridge: %s"), *Message);
        return false;
    }

    static FName NormalizePinName(FName PinName)
    {
        const FString Name = PinName.ToString();
        if (Name.Equals(TEXT("exec"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("execute"), ESearchCase::IgnoreCase))
        {
            return UEdGraphSchema_K2::PN_Execute;
        }
        if (Name.Equals(TEXT("then"), ESearchCase::IgnoreCase))
        {
            return UEdGraphSchema_K2::PN_Then;
        }
        if (Name.Equals(TEXT("self"), ESearchCase::IgnoreCase))
        {
            return UEdGraphSchema_K2::PN_Self;
        }
        return PinName;
    }

    static UEdGraphPin* FindPin(UEdGraphNode* Node, FName PinName, TOptional<EEdGraphPinDirection> Direction = TOptional<EEdGraphPinDirection>())
    {
        if (!Node)
        {
            return nullptr;
        }

        const FName NormalizedPinName = NormalizePinName(PinName);
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!Pin)
            {
                continue;
            }

            if (Direction.IsSet() && Pin->Direction != Direction.GetValue())
            {
                continue;
            }

            if (Pin->PinName == NormalizedPinName || Pin->PinName.ToString().Equals(NormalizedPinName.ToString(), ESearchCase::IgnoreCase))
            {
                return Pin;
            }
        }

        return nullptr;
    }

    static FString PinList(UEdGraphNode* Node)
    {
        if (!Node)
        {
            return TEXT("<null node>");
        }

        TArray<FString> Names;
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin)
            {
                Names.Add(FString::Printf(TEXT("%s:%s"), Pin->Direction == EGPD_Output ? TEXT("out") : TEXT("in"), *Pin->PinName.ToString()));
            }
        }
        return FString::Join(Names, TEXT(", "));
    }
}

UBlueprint* UPiBlueprintBridgeEditorLibrary::CreateActorBlueprint(const FString& AssetPath, TSubclassOf<AActor> ParentClass, FString& OutError)
{
    OutError.Reset();

    if (AssetPath.IsEmpty() || !FPackageName::IsValidLongPackageName(AssetPath))
    {
        PiBlueprintBridge::Fail(OutError, FString::Printf(TEXT("Invalid Blueprint asset path '%s'. Use a long package path like /Game/Blueprints/BP_MyActor."), *AssetPath));
        return nullptr;
    }

    UClass* Parent = ParentClass ? *ParentClass : AActor::StaticClass();
    if (!Parent || !Parent->IsChildOf(AActor::StaticClass()))
    {
        PiBlueprintBridge::Fail(OutError, TEXT("ParentClass must be AActor or a subclass."));
        return nullptr;
    }

    if (UObject* ExistingAsset = UEditorAssetLibrary::LoadAsset(AssetPath))
    {
        if (UBlueprint* ExistingBlueprint = Cast<UBlueprint>(ExistingAsset))
        {
            return ExistingBlueprint;
        }

        PiBlueprintBridge::Fail(OutError, FString::Printf(TEXT("Asset already exists at '%s' and is not a Blueprint."), *AssetPath));
        return nullptr;
    }

    const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
    UPackage* Package = CreatePackage(*AssetPath);
    if (!Package)
    {
        PiBlueprintBridge::Fail(OutError, FString::Printf(TEXT("Failed to create package '%s'."), *AssetPath));
        return nullptr;
    }

    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        Parent,
        Package,
        FName(*AssetName),
        BPTYPE_Normal,
        UBlueprint::StaticClass(),
        UBlueprintGeneratedClass::StaticClass(),
        FName(TEXT("PiBlueprintBridge")));

    if (!Blueprint)
    {
        PiBlueprintBridge::Fail(OutError, FString::Printf(TEXT("Failed to create Blueprint '%s'."), *AssetPath));
        return nullptr;
    }

    FAssetRegistryModule::AssetCreated(Blueprint);
    Package->MarkPackageDirty();
    return Blueprint;
}

bool UPiBlueprintBridgeEditorLibrary::AddStaticMeshComponent(UBlueprint* Blueprint, FName ComponentName, const FString& StaticMeshPath, bool bMakeRoot, FString& OutError)
{
    OutError.Reset();

    if (!Blueprint || !Blueprint->SimpleConstructionScript)
    {
        return PiBlueprintBridge::Fail(OutError, TEXT("Blueprint or SimpleConstructionScript is null."));
    }

    if (ComponentName.IsNone())
    {
        return PiBlueprintBridge::Fail(OutError, TEXT("ComponentName cannot be None."));
    }

    UStaticMesh* StaticMesh = LoadObject<UStaticMesh>(nullptr, *StaticMeshPath);
    if (!StaticMesh)
    {
        return PiBlueprintBridge::Fail(OutError, FString::Printf(TEXT("Could not load static mesh '%s'."), *StaticMeshPath));
    }

    Blueprint->Modify();
    Blueprint->SimpleConstructionScript->Modify();

    USCS_Node* Node = Blueprint->SimpleConstructionScript->FindSCSNode(ComponentName);
    if (!Node)
    {
        Node = Blueprint->SimpleConstructionScript->CreateNode(UStaticMeshComponent::StaticClass(), ComponentName);
        if (!Node)
        {
            return PiBlueprintBridge::Fail(OutError, FString::Printf(TEXT("Failed to create StaticMeshComponent node '%s'."), *ComponentName.ToString()));
        }

        if (bMakeRoot || Blueprint->SimpleConstructionScript->GetRootNodes().Num() == 0)
        {
            Blueprint->SimpleConstructionScript->AddNode(Node);
        }
        else
        {
            Blueprint->SimpleConstructionScript->GetRootNodes()[0]->AddChildNode(Node);
        }
    }

    UStaticMeshComponent* ComponentTemplate = Cast<UStaticMeshComponent>(Node->ComponentTemplate);
    if (!ComponentTemplate)
    {
        return PiBlueprintBridge::Fail(OutError, FString::Printf(TEXT("SCS node '%s' is not a StaticMeshComponent."), *ComponentName.ToString()));
    }

    ComponentTemplate->Modify();
    ComponentTemplate->SetStaticMesh(StaticMesh);
    ComponentTemplate->SetMobility(EComponentMobility::Movable);
    ComponentTemplate->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    return true;
}

UEdGraph* UPiBlueprintBridgeEditorLibrary::GetEventGraph(UBlueprint* Blueprint, FString& OutError)
{
    OutError.Reset();

    if (!Blueprint)
    {
        PiBlueprintBridge::Fail(OutError, TEXT("Blueprint is null."));
        return nullptr;
    }

    if (UEdGraph* ExistingGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint))
    {
        return ExistingGraph;
    }

    UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
        Blueprint,
        FName(TEXT("EventGraph")),
        UEdGraph::StaticClass(),
        UEdGraphSchema_K2::StaticClass());

    if (!NewGraph)
    {
        PiBlueprintBridge::Fail(OutError, TEXT("Failed to create EventGraph."));
        return nullptr;
    }

    FBlueprintEditorUtils::AddUbergraphPage(Blueprint, NewGraph);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    return NewGraph;
}

UK2Node_Event* UPiBlueprintBridgeEditorLibrary::AddEventNode(UBlueprint* Blueprint, UEdGraph* Graph, FName EventName, UClass* EventClass, FVector2D Location, FString& OutError)
{
    OutError.Reset();

    if (!Blueprint || !Graph)
    {
        PiBlueprintBridge::Fail(OutError, TEXT("Blueprint and Graph are required."));
        return nullptr;
    }

    if (EventName.IsNone())
    {
        PiBlueprintBridge::Fail(OutError, TEXT("EventName cannot be None."));
        return nullptr;
    }

    UClass* ResolvedEventClass = EventClass;
    if (!ResolvedEventClass)
    {
        ResolvedEventClass = Blueprint->ParentClass;
    }
    if (!ResolvedEventClass)
    {
        ResolvedEventClass = AActor::StaticClass();
    }

    for (UEdGraphNode* ExistingNode : Graph->Nodes)
    {
        if (UK2Node_Event* ExistingEvent = Cast<UK2Node_Event>(ExistingNode))
        {
            if (ExistingEvent->EventReference.GetMemberName() == EventName)
            {
                return ExistingEvent;
            }
        }
    }

    int32 NodePosY = static_cast<int32>(Location.Y);
    UK2Node_Event* EventNode = FKismetEditorUtilities::AddDefaultEventNode(Blueprint, Graph, EventName, ResolvedEventClass, NodePosY);
    if (!EventNode)
    {
        PiBlueprintBridge::Fail(OutError, FString::Printf(TEXT("Failed to add event '%s'. It may already exist or may not be valid for this Blueprint."), *EventName.ToString()));
        return nullptr;
    }

    EventNode->NodePosX = static_cast<int32>(Location.X);
    EventNode->NodePosY = static_cast<int32>(Location.Y);
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    return EventNode;
}

UK2Node_CallFunction* UPiBlueprintBridgeEditorLibrary::AddCallFunctionNode(UEdGraph* Graph, UClass* FunctionOwnerClass, FName FunctionName, FVector2D Location, FString& OutError)
{
    OutError.Reset();

    if (!Graph || !FunctionOwnerClass)
    {
        PiBlueprintBridge::Fail(OutError, TEXT("Graph and FunctionOwnerClass are required."));
        return nullptr;
    }

    UFunction* Function = FunctionOwnerClass->FindFunctionByName(FunctionName);
    if (!Function)
    {
        PiBlueprintBridge::Fail(OutError, FString::Printf(TEXT("Function '%s' was not found on class '%s'."), *FunctionName.ToString(), *FunctionOwnerClass->GetName()));
        return nullptr;
    }

    UK2Node_CallFunction* Node = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_CallFunction>(
        Graph,
        Location,
        EK2NewNodeFlags::None,
        [Function](UK2Node_CallFunction* NewNode)
        {
            NewNode->SetFromFunction(Function);
        });

    return Node;
}

UK2Node_Self* UPiBlueprintBridgeEditorLibrary::AddSelfNode(UEdGraph* Graph, FVector2D Location, FString& OutError)
{
    OutError.Reset();

    if (!Graph)
    {
        PiBlueprintBridge::Fail(OutError, TEXT("Graph is required."));
        return nullptr;
    }

    return FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_Self>(Graph, Location, EK2NewNodeFlags::None);
}

bool UPiBlueprintBridgeEditorLibrary::ConnectPins(UEdGraphNode* FromNode, FName FromPinName, UEdGraphNode* ToNode, FName ToPinName, FString& OutError)
{
    OutError.Reset();

    UEdGraphPin* FromPin = PiBlueprintBridge::FindPin(FromNode, FromPinName, EGPD_Output);
    UEdGraphPin* ToPin = PiBlueprintBridge::FindPin(ToNode, ToPinName, EGPD_Input);
    if (!FromPin)
    {
        return PiBlueprintBridge::Fail(OutError, FString::Printf(TEXT("Output pin '%s' not found. Available pins: %s"), *FromPinName.ToString(), *PiBlueprintBridge::PinList(FromNode)));
    }
    if (!ToPin)
    {
        return PiBlueprintBridge::Fail(OutError, FString::Printf(TEXT("Input pin '%s' not found. Available pins: %s"), *ToPinName.ToString(), *PiBlueprintBridge::PinList(ToNode)));
    }

    const UEdGraphSchema* Schema = FromNode && FromNode->GetGraph() ? FromNode->GetGraph()->GetSchema() : nullptr;
    if (!Schema)
    {
        return PiBlueprintBridge::Fail(OutError, TEXT("Could not resolve graph schema for connection."));
    }

    if (!Schema->TryCreateConnection(FromPin, ToPin))
    {
        return PiBlueprintBridge::Fail(OutError, FString::Printf(TEXT("Failed to connect '%s' to '%s'."), *FromPinName.ToString(), *ToPinName.ToString()));
    }

    if (UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(FromNode))
    {
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    }

    return true;
}

bool UPiBlueprintBridgeEditorLibrary::SetPinDefault(UEdGraphNode* Node, FName PinName, const FString& DefaultValue, FString& OutError)
{
    OutError.Reset();

    UEdGraphPin* Pin = PiBlueprintBridge::FindPin(Node, PinName, EGPD_Input);
    if (!Pin)
    {
        return PiBlueprintBridge::Fail(OutError, FString::Printf(TEXT("Input pin '%s' not found. Available pins: %s"), *PinName.ToString(), *PiBlueprintBridge::PinList(Node)));
    }

    const UEdGraphSchema* Schema = Node && Node->GetGraph() ? Node->GetGraph()->GetSchema() : nullptr;
    if (!Schema)
    {
        return PiBlueprintBridge::Fail(OutError, TEXT("Could not resolve graph schema for pin default."));
    }

    Schema->TrySetDefaultValue(*Pin, DefaultValue);

    if (UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(Node))
    {
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    }

    return true;
}

bool UPiBlueprintBridgeEditorLibrary::CompileBlueprint(UBlueprint* Blueprint, FString& OutError)
{
    OutError.Reset();

    if (!Blueprint)
    {
        return PiBlueprintBridge::Fail(OutError, TEXT("Blueprint is null."));
    }

    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    return true;
}

bool UPiBlueprintBridgeEditorLibrary::SaveBlueprint(UBlueprint* Blueprint, FString& OutError)
{
    OutError.Reset();

    if (!Blueprint)
    {
        return PiBlueprintBridge::Fail(OutError, TEXT("Blueprint is null."));
    }

    if (!UEditorAssetLibrary::SaveLoadedAsset(Blueprint, false))
    {
        return PiBlueprintBridge::Fail(OutError, FString::Printf(TEXT("Failed to save Blueprint '%s'."), *Blueprint->GetPathName()));
    }
    return true;
}

UBlueprint* UPiBlueprintBridgeEditorLibrary::CreateFollowPlayerCubeBlueprint(const FString& AssetPath, FString& OutError)
{
    OutError.Reset();

    UBlueprint* Blueprint = CreateActorBlueprint(AssetPath, AActor::StaticClass(), OutError);
    if (!Blueprint)
    {
        return nullptr;
    }

    if (!AddStaticMeshComponent(Blueprint, FName(TEXT("Cube")), TEXT("/Engine/BasicShapes/Cube.Cube"), true, OutError))
    {
        return nullptr;
    }

    UEdGraph* Graph = GetEventGraph(Blueprint, OutError);
    if (!Graph)
    {
        return nullptr;
    }

    UK2Node_Event* TickNode = AddEventNode(Blueprint, Graph, FName(TEXT("ReceiveTick")), AActor::StaticClass(), FVector2D(0.0, 0.0), OutError);
    if (!TickNode)
    {
        return nullptr;
    }

    UK2Node_CallFunction* FollowNode = AddCallFunctionNode(
        Graph,
        UPiBlueprintRuntimeLibrary::StaticClass(),
        GET_FUNCTION_NAME_CHECKED(UPiBlueprintRuntimeLibrary, FollowPlayerPawn),
        FVector2D(360.0, 0.0),
        OutError);
    if (!FollowNode)
    {
        return nullptr;
    }

    UK2Node_Self* SelfNode = AddSelfNode(Graph, FVector2D(120.0, 180.0), OutError);
    if (!SelfNode)
    {
        return nullptr;
    }

    if (!ConnectPins(TickNode, FName(TEXT("then")), FollowNode, FName(TEXT("execute")), OutError))
    {
        return nullptr;
    }
    if (!ConnectPins(TickNode, FName(TEXT("DeltaSeconds")), FollowNode, FName(TEXT("DeltaSeconds")), OutError))
    {
        return nullptr;
    }
    if (!ConnectPins(SelfNode, FName(TEXT("self")), FollowNode, FName(TEXT("Follower")), OutError))
    {
        return nullptr;
    }
    if (!SetPinDefault(FollowNode, FName(TEXT("Offset")), TEXT("(X=-250.000000,Y=120.000000,Z=120.000000)"), OutError))
    {
        return nullptr;
    }
    if (!SetPinDefault(FollowNode, FName(TEXT("Speed")), TEXT("6.000000"), OutError))
    {
        return nullptr;
    }

    if (!CompileBlueprint(Blueprint, OutError))
    {
        return nullptr;
    }
    if (!SaveBlueprint(Blueprint, OutError))
    {
        return nullptr;
    }

    return Blueprint;
}

#undef LOCTEXT_NAMESPACE
