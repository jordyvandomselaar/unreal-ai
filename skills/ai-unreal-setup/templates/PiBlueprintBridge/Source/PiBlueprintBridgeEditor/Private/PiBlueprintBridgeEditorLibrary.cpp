#include "PiBlueprintBridgeEditorLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SceneComponent.h"
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
#include "Dom/JsonObject.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_Self.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialInterface.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphSchema_K2_Actions.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "PiBlueprintRuntimeLibrary.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/SoftObjectPath.h"

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

    static FString SanitizeAssetToken(FString Value)
    {
        for (TCHAR& Character : Value)
        {
            if (!FChar::IsAlnum(Character) && Character != TEXT('_'))
            {
                Character = TEXT('_');
            }
        }

        return Value.IsEmpty() ? TEXT("Generated") : Value;
    }

    static FString GeneratedMaterialPath(UBlueprint* Blueprint, FName ComponentName)
    {
        const FString BlueprintPackageName = Blueprint ? Blueprint->GetOutermost()->GetName() : TEXT("/Game/Generated");
        const FString BlueprintAssetName = SanitizeAssetToken(FPackageName::GetLongPackageAssetName(BlueprintPackageName));
        const FString ComponentAssetName = SanitizeAssetToken(ComponentName.ToString());
        return FString::Printf(TEXT("/Game/Pi/GeneratedMaterials/M_%s_%s"), *BlueprintAssetName, *ComponentAssetName);
    }

    static FSoftObjectPath ObjectPathForAssetPath(const FString& AssetPath)
    {
        const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
        return FSoftObjectPath(FString::Printf(TEXT("%s.%s"), *AssetPath, *AssetName));
    }

    static UObject* LoadExistingAsset(const FString& AssetPath)
    {
        FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
        const FSoftObjectPath ObjectPath = ObjectPathForAssetPath(AssetPath);
        FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(ObjectPath);
        return AssetData.IsValid() ? AssetData.GetAsset() : nullptr;
    }

    static FString ResolveStaticMeshPath(const FString& StaticMeshPath)
    {
        const FString Trimmed = StaticMeshPath.TrimStartAndEnd();
        if (Trimmed.IsEmpty())
        {
            return TEXT("/Engine/BasicShapes/Cube.Cube");
        }

        if (Trimmed.StartsWith(TEXT("/")))
        {
            return Trimmed;
        }

        const FString Lower = Trimmed.ToLower();
        if (Lower == TEXT("cube") || Lower == TEXT("box"))
        {
            return TEXT("/Engine/BasicShapes/Cube.Cube");
        }
        if (Lower == TEXT("sphere") || Lower == TEXT("ball"))
        {
            return TEXT("/Engine/BasicShapes/Sphere.Sphere");
        }
        if (Lower == TEXT("cylinder"))
        {
            return TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
        }
        if (Lower == TEXT("cone"))
        {
            return TEXT("/Engine/BasicShapes/Cone.Cone");
        }
        if (Lower == TEXT("plane"))
        {
            return TEXT("/Engine/BasicShapes/Plane.Plane");
        }

        return Trimmed;
    }

    static FString ReadStringAlias(const TSharedPtr<FJsonObject>& Object, const TCHAR* PrimaryName, const TCHAR* AlternateName, const FString& DefaultValue = TEXT(""))
    {
        FString Value;
        if (Object->TryGetStringField(PrimaryName, Value))
        {
            return Value;
        }
        if (AlternateName && Object->TryGetStringField(AlternateName, Value))
        {
            return Value;
        }
        return DefaultValue;
    }

    static bool ReadBoolAlias(const TSharedPtr<FJsonObject>& Object, const TCHAR* PrimaryName, const TCHAR* AlternateName, bool DefaultValue)
    {
        bool Value = DefaultValue;
        if (Object->TryGetBoolField(PrimaryName, Value))
        {
            return Value;
        }
        if (AlternateName && Object->TryGetBoolField(AlternateName, Value))
        {
            return Value;
        }
        return DefaultValue;
    }

    static FVector ReadVectorField(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, FVector DefaultValue)
    {
        const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
        if (Object->TryGetArrayField(FieldName, Array) && Array && Array->Num() >= 3)
        {
            return FVector(
                static_cast<float>((*Array)[0]->AsNumber()),
                static_cast<float>((*Array)[1]->AsNumber()),
                static_cast<float>((*Array)[2]->AsNumber()));
        }

        const TSharedPtr<FJsonObject>* VectorObject = nullptr;
        if (Object->TryGetObjectField(FieldName, VectorObject) && VectorObject && VectorObject->IsValid())
        {
            const TSharedPtr<FJsonObject>& Value = *VectorObject;
            double X = DefaultValue.X;
            double Y = DefaultValue.Y;
            double Z = DefaultValue.Z;
            Value->TryGetNumberField(TEXT("x"), X);
            Value->TryGetNumberField(TEXT("y"), Y);
            Value->TryGetNumberField(TEXT("z"), Z);
            return FVector(static_cast<float>(X), static_cast<float>(Y), static_cast<float>(Z));
        }

        return DefaultValue;
    }

    static FRotator ReadRotatorField(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, FRotator DefaultValue)
    {
        const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
        if (Object->TryGetArrayField(FieldName, Array) && Array && Array->Num() >= 3)
        {
            return FRotator(
                static_cast<float>((*Array)[0]->AsNumber()),
                static_cast<float>((*Array)[1]->AsNumber()),
                static_cast<float>((*Array)[2]->AsNumber()));
        }

        const TSharedPtr<FJsonObject>* RotatorObject = nullptr;
        if (Object->TryGetObjectField(FieldName, RotatorObject) && RotatorObject && RotatorObject->IsValid())
        {
            const TSharedPtr<FJsonObject>& Value = *RotatorObject;
            double Pitch = DefaultValue.Pitch;
            double Yaw = DefaultValue.Yaw;
            double Roll = DefaultValue.Roll;
            Value->TryGetNumberField(TEXT("pitch"), Pitch);
            Value->TryGetNumberField(TEXT("yaw"), Yaw);
            Value->TryGetNumberField(TEXT("roll"), Roll);
            return FRotator(static_cast<float>(Pitch), static_cast<float>(Yaw), static_cast<float>(Roll));
        }

        return DefaultValue;
    }

    static bool ReadColorField(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, FLinearColor& OutColor)
    {
        const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
        if (Object->TryGetArrayField(FieldName, Array) && Array && Array->Num() >= 3)
        {
            float R = static_cast<float>((*Array)[0]->AsNumber());
            float G = static_cast<float>((*Array)[1]->AsNumber());
            float B = static_cast<float>((*Array)[2]->AsNumber());
            float A = Array->Num() >= 4 ? static_cast<float>((*Array)[3]->AsNumber()) : 1.0f;
            if (R > 1.0f || G > 1.0f || B > 1.0f || A > 1.0f)
            {
                R /= 255.0f;
                G /= 255.0f;
                B /= 255.0f;
                A = FMath::Min(A / 255.0f, 1.0f);
            }
            OutColor = FLinearColor(R, G, B, A);
            return true;
        }

        const TSharedPtr<FJsonObject>* ColorObject = nullptr;
        if (Object->TryGetObjectField(FieldName, ColorObject) && ColorObject && ColorObject->IsValid())
        {
            const TSharedPtr<FJsonObject>& Value = *ColorObject;
            double RValue = 1.0;
            double GValue = 1.0;
            double BValue = 1.0;
            double AValue = 1.0;
            Value->TryGetNumberField(TEXT("r"), RValue);
            Value->TryGetNumberField(TEXT("g"), GValue);
            Value->TryGetNumberField(TEXT("b"), BValue);
            Value->TryGetNumberField(TEXT("a"), AValue);
            float R = static_cast<float>(RValue);
            float G = static_cast<float>(GValue);
            float B = static_cast<float>(BValue);
            float A = static_cast<float>(AValue);
            if (R > 1.0f || G > 1.0f || B > 1.0f || A > 1.0f)
            {
                R /= 255.0f;
                G /= 255.0f;
                B /= 255.0f;
                A = FMath::Min(A / 255.0f, 1.0f);
            }
            OutColor = FLinearColor(R, G, B, A);
            return true;
        }

        return false;
    }

    static USCS_Node* FindSCSNode(UBlueprint* Blueprint, FName ComponentName)
    {
        if (!Blueprint || !Blueprint->SimpleConstructionScript || ComponentName.IsNone())
        {
            return nullptr;
        }

        return Blueprint->SimpleConstructionScript->FindSCSNode(ComponentName);
    }

    static bool AttachNewNode(UBlueprint* Blueprint, USCS_Node* Node, FName ParentComponentName, bool bMakeRoot, FString& OutError)
    {
        if (!Blueprint || !Blueprint->SimpleConstructionScript || !Node)
        {
            return Fail(OutError, TEXT("Blueprint, SimpleConstructionScript, and node are required."));
        }

        USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
        if (!ParentComponentName.IsNone())
        {
            USCS_Node* ParentNode = SCS->FindSCSNode(ParentComponentName);
            if (!ParentNode)
            {
                return Fail(OutError, FString::Printf(TEXT("Parent component '%s' was not found."), *ParentComponentName.ToString()));
            }

            ParentNode->AddChildNode(Node);
            return true;
        }

        if (bMakeRoot || SCS->GetRootNodes().Num() == 0)
        {
            SCS->AddNode(Node);
            return true;
        }

        USCS_Node* RootNode = SCS->GetRootNodes()[0];
        if (!RootNode)
        {
            SCS->AddNode(Node);
            return true;
        }

        RootNode->AddChildNode(Node);
        return true;
    }

    static UMaterialInterface* ResolvePartMaterial(UBlueprint* Blueprint, const FPiBlueprintStaticMeshPart& Part, FString& OutError)
    {
        if (!Part.MaterialPath.IsEmpty())
        {
            UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *Part.MaterialPath);
            if (!Material)
            {
                Fail(OutError, FString::Printf(TEXT("Could not load material '%s'."), *Part.MaterialPath));
                return nullptr;
            }

            return Material;
        }

        if (!Part.bUseColor)
        {
            return nullptr;
        }

        return UPiBlueprintBridgeEditorLibrary::CreateSolidColorMaterial(GeneratedMaterialPath(Blueprint, Part.ComponentName), Part.Color, OutError);
    }
}

FPiBlueprintStaticMeshPart::FPiBlueprintStaticMeshPart()
    : ComponentName(NAME_None)
    , StaticMeshPath(TEXT("/Engine/BasicShapes/Cube.Cube"))
    , ParentComponentName(NAME_None)
    , RelativeLocation(FVector::ZeroVector)
    , RelativeRotation(FRotator::ZeroRotator)
    , RelativeScale(FVector::OneVector)
    , MaterialPath(TEXT(""))
    , bUseColor(false)
    , Color(FLinearColor::White)
    , bCollisionEnabled(true)
    , bCastShadow(true)
{
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

    if (UObject* ExistingAsset = PiBlueprintBridge::LoadExistingAsset(AssetPath))
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

bool UPiBlueprintBridgeEditorLibrary::ClearBlueprintComponents(UBlueprint* Blueprint, FString& OutError)
{
    OutError.Reset();

    if (!Blueprint || !Blueprint->SimpleConstructionScript)
    {
        return PiBlueprintBridge::Fail(OutError, TEXT("Blueprint or SimpleConstructionScript is null."));
    }

    Blueprint->Modify();
    USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
    SCS->Modify();

    TArray<USCS_Node*> Nodes = SCS->GetAllNodes();
    auto DepthOf = [SCS](const USCS_Node* Node)
    {
        int32 Depth = 0;
        const USCS_Node* Current = Node;
        while (Current)
        {
            ++Depth;
            Current = SCS->FindParentNode(const_cast<USCS_Node*>(Current));
        }
        return Depth;
    };

    Nodes.Sort([&DepthOf](const USCS_Node& A, const USCS_Node& B)
    {
        return DepthOf(&A) > DepthOf(&B);
    });

    for (USCS_Node* Node : Nodes)
    {
        if (Node && SCS->GetAllNodes().Contains(Node))
        {
            SCS->RemoveNode(Node, false);
        }
    }

    SCS->ValidateSceneRootNodes();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    return true;
}

bool UPiBlueprintBridgeEditorLibrary::AddSceneComponent(UBlueprint* Blueprint, FName ComponentName, FName ParentComponentName, bool bMakeRoot, FString& OutError)
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

    Blueprint->Modify();
    USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
    SCS->Modify();

    USCS_Node* Node = SCS->FindSCSNode(ComponentName);
    if (!Node)
    {
        Node = SCS->CreateNode(USceneComponent::StaticClass(), ComponentName);
        if (!Node)
        {
            return PiBlueprintBridge::Fail(OutError, FString::Printf(TEXT("Failed to create SceneComponent node '%s'."), *ComponentName.ToString()));
        }

        if (!PiBlueprintBridge::AttachNewNode(Blueprint, Node, ParentComponentName, bMakeRoot, OutError))
        {
            return false;
        }
    }

    USceneComponent* ComponentTemplate = Cast<USceneComponent>(Node->ComponentTemplate);
    if (!ComponentTemplate)
    {
        return PiBlueprintBridge::Fail(OutError, FString::Printf(TEXT("SCS node '%s' is not a SceneComponent."), *ComponentName.ToString()));
    }

    ComponentTemplate->Modify();
    ComponentTemplate->SetMobility(EComponentMobility::Movable);
    ComponentTemplate->SetRelativeLocation(FVector::ZeroVector);
    ComponentTemplate->SetRelativeRotation(FRotator::ZeroRotator);
    ComponentTemplate->SetRelativeScale3D(FVector::OneVector);

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    return true;
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

    const FString ResolvedStaticMeshPath = PiBlueprintBridge::ResolveStaticMeshPath(StaticMeshPath);
    UStaticMesh* StaticMesh = LoadObject<UStaticMesh>(nullptr, *ResolvedStaticMeshPath);
    if (!StaticMesh)
    {
        return PiBlueprintBridge::Fail(OutError, FString::Printf(TEXT("Could not load static mesh '%s'."), *ResolvedStaticMeshPath));
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
            USCS_Node* RootNode = Blueprint->SimpleConstructionScript->GetRootNodes()[0];
            RootNode->AddChildNode(Node);
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

bool UPiBlueprintBridgeEditorLibrary::AddStaticMeshComponentFromPart(UBlueprint* Blueprint, const FPiBlueprintStaticMeshPart& Part, bool bMakeRoot, FString& OutError)
{
    OutError.Reset();

    if (!Blueprint || !Blueprint->SimpleConstructionScript)
    {
        return PiBlueprintBridge::Fail(OutError, TEXT("Blueprint or SimpleConstructionScript is null."));
    }

    if (Part.ComponentName.IsNone())
    {
        return PiBlueprintBridge::Fail(OutError, TEXT("Part.ComponentName cannot be None."));
    }

    if (Part.StaticMeshPath.IsEmpty())
    {
        return PiBlueprintBridge::Fail(OutError, FString::Printf(TEXT("Static mesh path is required for part '%s'."), *Part.ComponentName.ToString()));
    }

    const FString ResolvedStaticMeshPath = PiBlueprintBridge::ResolveStaticMeshPath(Part.StaticMeshPath);
    UStaticMesh* StaticMesh = LoadObject<UStaticMesh>(nullptr, *ResolvedStaticMeshPath);
    if (!StaticMesh)
    {
        return PiBlueprintBridge::Fail(OutError, FString::Printf(TEXT("Could not load static mesh '%s'."), *ResolvedStaticMeshPath));
    }

    UMaterialInterface* Material = PiBlueprintBridge::ResolvePartMaterial(Blueprint, Part, OutError);
    if (!OutError.IsEmpty())
    {
        return false;
    }

    Blueprint->Modify();
    USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
    SCS->Modify();

    USCS_Node* Node = SCS->FindSCSNode(Part.ComponentName);
    if (!Node)
    {
        Node = SCS->CreateNode(UStaticMeshComponent::StaticClass(), Part.ComponentName);
        if (!Node)
        {
            return PiBlueprintBridge::Fail(OutError, FString::Printf(TEXT("Failed to create StaticMeshComponent node '%s'."), *Part.ComponentName.ToString()));
        }

        if (!PiBlueprintBridge::AttachNewNode(Blueprint, Node, Part.ParentComponentName, bMakeRoot, OutError))
        {
            return false;
        }
    }

    UStaticMeshComponent* ComponentTemplate = Cast<UStaticMeshComponent>(Node->ComponentTemplate);
    if (!ComponentTemplate)
    {
        return PiBlueprintBridge::Fail(OutError, FString::Printf(TEXT("SCS node '%s' is not a StaticMeshComponent."), *Part.ComponentName.ToString()));
    }

    ComponentTemplate->Modify();
    ComponentTemplate->SetStaticMesh(StaticMesh);
    ComponentTemplate->SetMobility(EComponentMobility::Movable);
    ComponentTemplate->SetRelativeLocation(Part.RelativeLocation);
    ComponentTemplate->SetRelativeRotation(Part.RelativeRotation);
    ComponentTemplate->SetRelativeScale3D(Part.RelativeScale);
    ComponentTemplate->SetCollisionEnabled(Part.bCollisionEnabled ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
    ComponentTemplate->SetCastShadow(Part.bCastShadow);
    if (Material)
    {
        ComponentTemplate->SetMaterial(0, Material);
    }

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    return true;
}

bool UPiBlueprintBridgeEditorLibrary::SetComponentRelativeTransform(UBlueprint* Blueprint, FName ComponentName, FVector RelativeLocation, FRotator RelativeRotation, FVector RelativeScale, FString& OutError)
{
    OutError.Reset();

    USCS_Node* Node = PiBlueprintBridge::FindSCSNode(Blueprint, ComponentName);
    if (!Node)
    {
        return PiBlueprintBridge::Fail(OutError, FString::Printf(TEXT("Component '%s' was not found."), *ComponentName.ToString()));
    }

    USceneComponent* ComponentTemplate = Cast<USceneComponent>(Node->ComponentTemplate);
    if (!ComponentTemplate)
    {
        return PiBlueprintBridge::Fail(OutError, FString::Printf(TEXT("Component '%s' is not a SceneComponent."), *ComponentName.ToString()));
    }

    Blueprint->Modify();
    ComponentTemplate->Modify();
    ComponentTemplate->SetRelativeLocation(RelativeLocation);
    ComponentTemplate->SetRelativeRotation(RelativeRotation);
    ComponentTemplate->SetRelativeScale3D(RelativeScale);

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    return true;
}

UMaterial* UPiBlueprintBridgeEditorLibrary::CreateSolidColorMaterial(const FString& AssetPath, FLinearColor BaseColor, FString& OutError)
{
    OutError.Reset();

    if (AssetPath.IsEmpty() || !FPackageName::IsValidLongPackageName(AssetPath))
    {
        PiBlueprintBridge::Fail(OutError, FString::Printf(TEXT("Invalid material asset path '%s'. Use a long package path like /Game/Materials/M_Red."), *AssetPath));
        return nullptr;
    }

    bool bCreated = false;
    UMaterial* Material = Cast<UMaterial>(PiBlueprintBridge::LoadExistingAsset(AssetPath));
    if (!Material)
    {
        if (UObject* ExistingAsset = PiBlueprintBridge::LoadExistingAsset(AssetPath))
        {
            PiBlueprintBridge::Fail(OutError, FString::Printf(TEXT("Asset already exists at '%s' and is not a Material."), *AssetPath));
            return nullptr;
        }

        UPackage* Package = CreatePackage(*AssetPath);
        if (!Package)
        {
            PiBlueprintBridge::Fail(OutError, FString::Printf(TEXT("Failed to create material package '%s'."), *AssetPath));
            return nullptr;
        }

        const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
        Material = NewObject<UMaterial>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
        if (!Material)
        {
            PiBlueprintBridge::Fail(OutError, FString::Printf(TEXT("Failed to create material '%s'."), *AssetPath));
            return nullptr;
        }

        FAssetRegistryModule::AssetCreated(Material);
        bCreated = true;
    }

    UMaterialEditorOnlyData* MaterialEditorOnly = Material->GetEditorOnlyData();
    if (!MaterialEditorOnly)
    {
        PiBlueprintBridge::Fail(OutError, FString::Printf(TEXT("Material '%s' has no editor-only data."), *AssetPath));
        return nullptr;
    }

    Material->Modify();
    Material->GetExpressionCollection().Empty();

    UMaterialExpressionConstant4Vector* BaseColorExpression = NewObject<UMaterialExpressionConstant4Vector>(Material);
    BaseColorExpression->Constant = BaseColor;
    BaseColorExpression->MaterialExpressionEditorX = -300;
    BaseColorExpression->MaterialExpressionEditorY = 0;
    Material->GetExpressionCollection().AddExpression(BaseColorExpression);
    MaterialEditorOnly->BaseColor.Expression = BaseColorExpression;
    MaterialEditorOnly->Roughness.Constant = 0.55f;

    Material->PostEditChange();
    Material->MarkPackageDirty();

    if (bCreated)
    {
        Material->GetOutermost()->MarkPackageDirty();
    }

    if (!UEditorAssetLibrary::SaveLoadedAsset(Material, false))
    {
        PiBlueprintBridge::Fail(OutError, FString::Printf(TEXT("Failed to save material '%s'."), *AssetPath));
        return nullptr;
    }

    return Material;
}

UBlueprint* UPiBlueprintBridgeEditorLibrary::CreateStaticMeshAssemblyBlueprint(const FString& AssetPath, const TArray<FPiBlueprintStaticMeshPart>& Parts, bool bReplaceExistingComponents, FString& OutError)
{
    OutError.Reset();

    if (Parts.Num() == 0)
    {
        PiBlueprintBridge::Fail(OutError, TEXT("CreateStaticMeshAssemblyBlueprint requires at least one part."));
        return nullptr;
    }

    UBlueprint* Blueprint = CreateActorBlueprint(AssetPath, AActor::StaticClass(), OutError);
    if (!Blueprint)
    {
        return nullptr;
    }

    if (bReplaceExistingComponents && !ClearBlueprintComponents(Blueprint, OutError))
    {
        return nullptr;
    }

    if (!AddSceneComponent(Blueprint, FName(TEXT("Root")), NAME_None, true, OutError))
    {
        return nullptr;
    }

    TSet<FName> SeenNames;
    SeenNames.Add(FName(TEXT("Root")));
    for (const FPiBlueprintStaticMeshPart& Part : Parts)
    {
        if (Part.ComponentName.IsNone())
        {
            PiBlueprintBridge::Fail(OutError, TEXT("Every part must have a ComponentName."));
            return nullptr;
        }

        if (SeenNames.Contains(Part.ComponentName))
        {
            PiBlueprintBridge::Fail(OutError, FString::Printf(TEXT("Duplicate component name '%s'."), *Part.ComponentName.ToString()));
            return nullptr;
        }
        SeenNames.Add(Part.ComponentName);

        FPiBlueprintStaticMeshPart ResolvedPart = Part;
        if (ResolvedPart.ParentComponentName.IsNone())
        {
            ResolvedPart.ParentComponentName = FName(TEXT("Root"));
        }

        if (!AddStaticMeshComponentFromPart(Blueprint, ResolvedPart, false, OutError))
        {
            return nullptr;
        }
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

UBlueprint* UPiBlueprintBridgeEditorLibrary::CreateStaticMeshAssemblyBlueprintFromJson(const FString& AssetPath, const FString& PartsJson, bool bReplaceExistingComponents, FString& OutError)
{
    OutError.Reset();

    TSharedPtr<FJsonValue> RootValue;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PartsJson);
    if (!FJsonSerializer::Deserialize(Reader, RootValue) || !RootValue.IsValid())
    {
        PiBlueprintBridge::Fail(OutError, TEXT("PartsJson is not valid JSON. Expected an array of part objects or an object with a 'parts' array."));
        return nullptr;
    }

    const TArray<TSharedPtr<FJsonValue>>* PartValues = nullptr;
    if (RootValue->Type == EJson::Array)
    {
        PartValues = &RootValue->AsArray();
    }
    else if (RootValue->Type == EJson::Object)
    {
        const TSharedPtr<FJsonObject> RootObject = RootValue->AsObject();
        if (!RootObject.IsValid() || !RootObject->TryGetArrayField(TEXT("parts"), PartValues))
        {
            PiBlueprintBridge::Fail(OutError, TEXT("PartsJson object must contain a 'parts' array."));
            return nullptr;
        }
    }

    if (!PartValues || PartValues->Num() == 0)
    {
        PiBlueprintBridge::Fail(OutError, TEXT("PartsJson must contain at least one part."));
        return nullptr;
    }

    TArray<FPiBlueprintStaticMeshPart> Parts;
    Parts.Reserve(PartValues->Num());
    for (int32 Index = 0; Index < PartValues->Num(); ++Index)
    {
        const TSharedPtr<FJsonObject> PartObject = (*PartValues)[Index].IsValid() ? (*PartValues)[Index]->AsObject() : nullptr;
        if (!PartObject.IsValid())
        {
            PiBlueprintBridge::Fail(OutError, FString::Printf(TEXT("Part %d is not an object."), Index));
            return nullptr;
        }

        FPiBlueprintStaticMeshPart Part;
        const FString Name = PiBlueprintBridge::ReadStringAlias(PartObject, TEXT("componentName"), TEXT("name"), FString::Printf(TEXT("Part_%03d"), Index));
        Part.ComponentName = FName(*Name);
        Part.StaticMeshPath = PiBlueprintBridge::ReadStringAlias(PartObject, TEXT("staticMeshPath"), TEXT("mesh"), TEXT("cube"));
        const FString ParentName = PiBlueprintBridge::ReadStringAlias(PartObject, TEXT("parentComponentName"), TEXT("parent"));
        Part.ParentComponentName = ParentName.IsEmpty() ? NAME_None : FName(*ParentName);
        Part.RelativeLocation = PiBlueprintBridge::ReadVectorField(PartObject, TEXT("relativeLocation"), PiBlueprintBridge::ReadVectorField(PartObject, TEXT("location"), FVector::ZeroVector));
        Part.RelativeRotation = PiBlueprintBridge::ReadRotatorField(PartObject, TEXT("relativeRotation"), PiBlueprintBridge::ReadRotatorField(PartObject, TEXT("rotation"), FRotator::ZeroRotator));
        Part.RelativeScale = PiBlueprintBridge::ReadVectorField(PartObject, TEXT("relativeScale"), PiBlueprintBridge::ReadVectorField(PartObject, TEXT("scale"), FVector::OneVector));
        Part.MaterialPath = PiBlueprintBridge::ReadStringAlias(PartObject, TEXT("materialPath"), TEXT("material"));
        Part.bCollisionEnabled = PiBlueprintBridge::ReadBoolAlias(PartObject, TEXT("collisionEnabled"), TEXT("collision"), true);
        Part.bCastShadow = PiBlueprintBridge::ReadBoolAlias(PartObject, TEXT("castShadow"), TEXT("shadow"), true);

        FLinearColor Color;
        if (PiBlueprintBridge::ReadColorField(PartObject, TEXT("color"), Color) || PiBlueprintBridge::ReadColorField(PartObject, TEXT("baseColor"), Color))
        {
            Part.bUseColor = true;
            Part.Color = Color;
        }

        Parts.Add(Part);
    }

    return CreateStaticMeshAssemblyBlueprint(AssetPath, Parts, bReplaceExistingComponents, OutError);
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

bool UPiBlueprintBridgeEditorLibrary::AddTickFunctionCall(UBlueprint* Blueprint, UClass* FunctionOwnerClass, FName FunctionName, FName ActorPinName, const TMap<FName, FString>& PinDefaults, FVector2D Location, FString& OutError)
{
    OutError.Reset();

    UEdGraph* Graph = GetEventGraph(Blueprint, OutError);
    if (!Graph)
    {
        return false;
    }

    UK2Node_Event* TickNode = AddEventNode(Blueprint, Graph, FName(TEXT("ReceiveTick")), AActor::StaticClass(), Location, OutError);
    if (!TickNode)
    {
        return false;
    }

    UK2Node_CallFunction* FunctionNode = AddCallFunctionNode(Graph, FunctionOwnerClass, FunctionName, Location + FVector2D(360.0, 0.0), OutError);
    if (!FunctionNode)
    {
        return false;
    }

    if (!ConnectPins(TickNode, FName(TEXT("then")), FunctionNode, FName(TEXT("execute")), OutError))
    {
        return false;
    }

    if (PiBlueprintBridge::FindPin(TickNode, FName(TEXT("DeltaSeconds")), EGPD_Output) && PiBlueprintBridge::FindPin(FunctionNode, FName(TEXT("DeltaSeconds")), EGPD_Input))
    {
        if (!ConnectPins(TickNode, FName(TEXT("DeltaSeconds")), FunctionNode, FName(TEXT("DeltaSeconds")), OutError))
        {
            return false;
        }
    }

    if (!ActorPinName.IsNone())
    {
        UK2Node_Self* SelfNode = AddSelfNode(Graph, Location + FVector2D(120.0, 180.0), OutError);
        if (!SelfNode)
        {
            return false;
        }

        if (!ConnectPins(SelfNode, FName(TEXT("self")), FunctionNode, ActorPinName, OutError))
        {
            return false;
        }
    }

    for (const TPair<FName, FString>& PinDefault : PinDefaults)
    {
        if (!SetPinDefault(FunctionNode, PinDefault.Key, PinDefault.Value, OutError))
        {
            return false;
        }
    }

    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
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
