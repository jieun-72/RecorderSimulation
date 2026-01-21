#include "TrapComponent.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Components/StaticMeshComponent.h"

UTrapComponent::UTrapComponent()
{
    // 매 프레임 Tick이 실행되도록 설정
    PrimaryComponentTick.bCanEverTick = true;
}

void UTrapComponent::BeginPlay()
{
    Super::BeginPlay();

    // 초기 위치와 회전값 저장
    InitialLocation = GetOwner()->GetActorLocation();
    InitialRotation = GetOwner()->GetActorRotation();
}

void UTrapComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!bIsTriggered)
    {
        CheckPlayerDistance();
    }
    else
    {
        AActor* Owner = GetOwner();
        if (!Owner) return;

        // 선택된 모드에 따라 동작
        switch (ScareMode)
        {
        case EScareMode::Rotate:
            Owner->AddActorLocalRotation(FRotator(0, 180.f * DeltaTime * ActionSpeed, 0));
            break;

        case EScareMode::Slide:
            {
                float SineValue = FMath::Sin(GetWorld()->GetTimeSeconds() * ActionSpeed);
                Owner->SetActorLocation(InitialLocation + (MoveOffset * SineValue));
            }
            break;

        case EScareMode::Fall:
            // Fall은 OnOverlap 시점에 한 번만 처리하므로 Tick에서는 할 일이 없습니다.
            break;
        }
    }
}

void UTrapComponent::CheckPlayerDistance()
{
    APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
    if (!PlayerPawn) return;

    float Distance = FVector::Dist(GetOwner()->GetActorLocation(), PlayerPawn->GetActorLocation());

    // 설정한 반지름보다 가까워지면 발동
    if (Distance <= TriggerRadius)
    {
        bIsTriggered = true;

        if (ScareMode == EScareMode::Fall)
        {
            UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(GetOwner()->GetRootComponent());
            if (Mesh)
            {
                Mesh->SetSimulatePhysics(true);
            }
        }
        
        UE_LOG(LogTemp, Warning, TEXT("%s Trap Triggered!"), *GetOwner()->GetName());
    }
}