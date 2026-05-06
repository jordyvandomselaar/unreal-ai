#include "PiBlueprintRuntimeLibrary.h"

#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

namespace
{
    struct FPiWanderState
    {
        FVector Origin = FVector::ZeroVector;
        FVector Target = FVector::ZeroVector;
        bool bHasTarget = false;
    };

    static TMap<TWeakObjectPtr<AActor>, FPiWanderState> GWanderStates;

    static FVector PickWanderTarget(const FVector& Origin, float Radius)
    {
        const float AngleRadians = FMath::FRandRange(0.0f, UE_TWO_PI);
        const float Distance = FMath::Sqrt(FMath::FRand()) * Radius;
        return Origin + FVector(FMath::Cos(AngleRadians) * Distance, FMath::Sin(AngleRadians) * Distance, 0.0f);
    }
}

void UPiBlueprintRuntimeLibrary::FollowPlayerPawn(AActor* Follower, float DeltaSeconds, FVector Offset, float Speed)
{
    if (!IsValid(Follower) || !Follower->GetWorld())
    {
        return;
    }

    APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(Follower, 0);
    if (!IsValid(PlayerPawn))
    {
        return;
    }

    const FVector TargetLocation = PlayerPawn->GetActorLocation() + Offset;
    const FVector NewLocation = FMath::VInterpTo(
        Follower->GetActorLocation(),
        TargetLocation,
        FMath::Max(DeltaSeconds, 0.0f),
        FMath::Max(Speed, 0.0f));

    Follower->SetActorLocation(NewLocation, false, nullptr, ETeleportType::None);
}

void UPiBlueprintRuntimeLibrary::WanderActor(AActor* Actor, float DeltaSeconds, float Radius, float Speed, float AcceptanceRadius, float TurnSpeed)
{
    if (!IsValid(Actor) || !Actor->GetWorld())
    {
        return;
    }

    const float ClampedDeltaSeconds = FMath::Max(DeltaSeconds, 0.0f);
    const float ClampedRadius = FMath::Max(Radius, 0.0f);
    const float ClampedSpeed = FMath::Max(Speed, 0.0f);
    const float ClampedAcceptanceRadius = FMath::Max(AcceptanceRadius, 1.0f);
    if (ClampedDeltaSeconds <= 0.0f || ClampedRadius <= 0.0f || ClampedSpeed <= 0.0f)
    {
        return;
    }

    const TWeakObjectPtr<AActor> ActorKey(Actor);
    FPiWanderState& State = GWanderStates.FindOrAdd(ActorKey);
    if (State.Origin.IsNearlyZero() && !State.bHasTarget)
    {
        State.Origin = Actor->GetActorLocation();
    }

    const FVector CurrentLocation = Actor->GetActorLocation();
    FVector FlatTargetDelta = State.Target - CurrentLocation;
    FlatTargetDelta.Z = 0.0f;

    if (!State.bHasTarget || FlatTargetDelta.SizeSquared() <= FMath::Square(ClampedAcceptanceRadius))
    {
        State.Target = PickWanderTarget(State.Origin, ClampedRadius);
        State.Target.Z = CurrentLocation.Z;
        State.bHasTarget = true;
        FlatTargetDelta = State.Target - CurrentLocation;
        FlatTargetDelta.Z = 0.0f;
    }

    const float DistanceToTarget = FlatTargetDelta.Size();
    if (DistanceToTarget <= KINDA_SMALL_NUMBER)
    {
        return;
    }

    const FVector Step = FlatTargetDelta.GetSafeNormal() * FMath::Min(DistanceToTarget, ClampedSpeed * ClampedDeltaSeconds);
    Actor->SetActorLocation(CurrentLocation + Step, false, nullptr, ETeleportType::None);

    const FRotator CurrentRotation = Actor->GetActorRotation();
    const FRotator DesiredRotation(0.0f, FlatTargetDelta.Rotation().Yaw, 0.0f);
    const FRotator NewRotation = FMath::RInterpTo(CurrentRotation, DesiredRotation, ClampedDeltaSeconds, FMath::Max(TurnSpeed, 0.0f));
    Actor->SetActorRotation(NewRotation, ETeleportType::None);
}
