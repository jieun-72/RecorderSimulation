#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "PatrolBiasSetupCommandlet.generated.h"

/** Install/verify the two patrol tasks and initial broad regions in Day_4. */
UCLASS()
class UPatrolBiasSetupCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UPatrolBiasSetupCommandlet();
    virtual int32 Main(const FString& Params) override;
};
