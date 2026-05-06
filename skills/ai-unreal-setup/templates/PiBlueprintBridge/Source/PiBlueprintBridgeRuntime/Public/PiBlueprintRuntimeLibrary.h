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
};
