#include "PiBlueprintRuntimeLibrary.h"

#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

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
