#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "AITelemetrySmokeCommandlet.generated.h"

UCLASS()
class UAITelemetrySmokeCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UAITelemetrySmokeCommandlet();
    virtual int32 Main(const FString& Params) override;
};
