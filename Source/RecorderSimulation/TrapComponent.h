// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TrapComponent.generated.h"

UENUM(BlueprintType)
enum class EScareMode : uint8
{
    Fall    UMETA(DisplayName = "Fall Down"),
    Rotate  UMETA(DisplayName = "Rotate"),
    Slide   UMETA(DisplayName = "Slide Side to Side")
};

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class RECORDERSIMULATION_API UTrapComponent : public UActorComponent
{
	GENERATED_BODY()

public:    
    UTrapComponent();

protected:
    virtual void BeginPlay() override;

public:    
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    // --- 에디터 설정 변수 ---
    UPROPERTY(EditAnywhere, Category = "Scare Settings")
    EScareMode ScareMode = EScareMode::Fall;

    UPROPERTY(EditAnywhere, Category = "Scare Settings")
    float TriggerRadius = 200.f;

    UPROPERTY(EditAnywhere, Category = "Scare Settings")
    FVector MoveOffset = FVector(100.f, 0, 0);

    UPROPERTY(EditAnywhere, Category = "Scare Settings")
    float ActionSpeed = 5.0f;

    // --- 내부 로직 변수 ---
    bool bIsTriggered = false;
    FVector InitialLocation;
    FRotator InitialRotation;

    // 플레이어와의 거리를 체크하는 함수
    void CheckPlayerDistance();
};
