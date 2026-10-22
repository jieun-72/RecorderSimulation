#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "BehaviorTree/Tasks/BTTask_BlackboardBase.h"
#include "BehaviorTree/Tasks/BTTask_MoveTo.h"
#include "Perception/AIPerceptionTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "PatrolBias.generated.h"

class UBoxComponent;
class UAIPerceptionComponent;
class AAIController;

/** Gameplay timing, independent of the telemetry encounter definition. */
struct AITELEMETRY_API FPatrolContactClock
{
    double LastContact = 0.;
    double CooldownUntil = 0.;
    bool bSightActive = false;
    bool bWasChasing = false;
    void Observe(double Now, bool bChasing, double CooldownSeconds);
    void ObserveState(double Now, const FString& State, double CooldownSeconds);
    double Gap(double Now) const { return FMath::Max(0., Now - LastContact); }
    bool Eligible(double Now, double Threshold) const;
};

struct AITELEMETRY_API FPatrolLogData
{
    bool bEnabled = false;
    FString Mode = TEXT("normal");
    FString Zone;
    FVector Target = FVector::ZeroVector;
    bool bHasTarget = false;
    float Threshold = 40.f;
    float CooldownSeconds = 10.f;
    double ContactGap = 0.;
    double CooldownRemaining = 0.;
};

/** Non-colliding region with local-space, authored patrol candidates. */
UCLASS()
class AITELEMETRY_API APatrolBiasZone : public AActor
{
    GENERATED_BODY()
public:
    APatrolBiasZone();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Patrol") TObjectPtr<UBoxComponent> Bounds;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Patrol") FName ZoneId;
    UPROPERTY(EditAnywhere, Category="Patrol", meta=(MakeEditWidget=true)) TArray<FVector> CandidateOffsets;
    bool Contains(const FVector& Position) const;
    FVector Candidate(int32 Index) const;
};

UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class AITELEMETRY_API UPatrolBiasComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UPatrolBiasComponent();
    static UPatrolBiasComponent* Ensure(AAIController* AI);
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float Delta, ELevelTick Tick, FActorComponentTickFunction* Function) override;
    bool SelectTarget(FVector& OutTarget);
    void MoveFinished(const FString& Result);
    void ResetSession();
    FPatrolLogData Snapshot() const;
    FPatrolContactClock Contact;
private:
    void Observe();
    void BindPerception();
    void Log(const FString& Kind, const FString& Reason) const;
    void SetMode(const FString& Mode, const FString& Reason);
    AAIController* Controller() const;
    UFUNCTION() void PerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);
    TWeakObjectPtr<UAIPerceptionComponent> Perception;
    TWeakObjectPtr<AActor> TrackedPlayer;
    FPatrolLogData Selection;
    FPatrolLogData PendingSelection;
    bool bInitialized = false;
    bool bPendingMove = false;
};

UCLASS()
class AITELEMETRY_API UBTTask_SelectPatrolWithBias : public UBTTask_BlackboardBase
{
    GENERATED_BODY()
public:
    UBTTask_SelectPatrolWithBias();
    virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};

/** Retains engine MoveTo behavior and logs this patrol branch's completion. */
UCLASS()
class AITELEMETRY_API UBTTask_PatrolMoveWithLog : public UBTTask_MoveTo
{
    GENERATED_BODY()
public:
    UBTTask_PatrolMoveWithLog(const FObjectInitializer& ObjectInitializer);
    virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult) override;
};

/** Attaches gameplay even when CSV recording is disabled; resets on Day_4 reentry. */
UCLASS()
class AITELEMETRY_API UPatrolBiasWorldSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
    virtual void Tick(float Delta) override;
    virtual TStatId GetStatId() const override;
private:
    bool bTargetActive = false;
    double NextScan = 0.;
};
