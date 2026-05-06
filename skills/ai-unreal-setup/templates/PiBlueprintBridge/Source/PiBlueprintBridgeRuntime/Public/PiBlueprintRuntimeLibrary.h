#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PiBlueprintRuntimeLibrary.generated.h"

class AActor;

/** Runtime Blueprint helper functions used by generated Blueprints. */
UCLASS()
class PIBLUEPRINTBRIDGERUNTIME_API UPiBlueprintRuntimeLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Move Follower toward player pawn 0 plus Offset. Safe no-op when no pawn exists. */
    UFUNCTION(BlueprintCallable, Category = "Pi|Blueprint Bridge")
    static void FollowPlayerPawn(AActor* Follower, float DeltaSeconds, FVector Offset, float Speed = 6.0f);

    /** Move Actor around its initial location by choosing new reachable wander targets. Safe no-op for invalid actors. */
    UFUNCTION(BlueprintCallable, Category = "Pi|Blueprint Bridge")
    static void WanderActor(AActor* Actor, float DeltaSeconds, float Radius = 500.0f, float Speed = 150.0f, float AcceptanceRadius = 75.0f, float TurnSpeed = 8.0f);
};
