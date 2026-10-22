#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DeveloperSettings.h"
#include "Perception/AIPerceptionTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "AITelemetry.generated.h"

class AAIController;
class UAIPerceptionComponent;
class UBTNode;
class UBehaviorTree;
struct FPatrolLogData;

UCLASS(Config=AITelemetry, DefaultConfig, meta=(DisplayName="Day 4 AI Telemetry"))
class AITELEMETRY_API UAITelemetrySettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UAITelemetrySettings();
    UPROPERTY(Config, EditAnywhere, Category="Recording") bool bEnabled = true;
    UPROPERTY(Config, EditAnywhere, Category="Recording") FString TargetMap = TEXT("/Game/Day_4");
    UPROPERTY(Config, EditAnywhere, Category="Recording") FString ControllerClassPath = TEXT("/Game/BP_Object/AIGhost/AIC_Ghost.AIC_Ghost_C");
    UPROPERTY(Config, EditAnywhere, Category="Recording") FString Condition = TEXT("baseline");
    UPROPERTY(Config, EditAnywhere, Category="Recording") FString TesterId = TEXT("unassigned");
    UPROPERTY(Config, EditAnywhere, Category="Recording") FString BuildLabel = TEXT("unspecified");
    UPROPERTY(Config, EditAnywhere, Category="Recording", meta=(ClampMin="0.05")) float SampleIntervalSeconds = 0.5f;
    /** 0 keeps native Perception state. Positive values expire hearing only in the logger. */
    UPROPERTY(Config, EditAnywhere, Category="Detection", meta=(ClampMin="0.0")) float HearingQuietSeconds = 0.f;
    UPROPERTY(Config, EditAnywhere, Category="Patrol Bias") bool bEnablePatrolBias = true;
    UPROPERTY(Config, EditAnywhere, Category="Patrol Bias", meta=(ClampMin="0.0")) float PatrolContactThresholdSeconds = 40.f;
    UPROPERTY(Config, EditAnywhere, Category="Patrol Bias", meta=(ClampMin="0.0")) float PatrolChaseCooldownSeconds = 10.f;
};

/** Pure measurement state: never writes to the AI's own Perception/Blackboard. */
struct AITELEMETRY_API FAIEncounterState
{
    bool bSight = false;
    bool bHearing = false;
    double LastHearingSuccess = -1.;
    int32 EncounterCount = 0;
    bool IsDetected() const { return bSight || bHearing; }
    /** Returns +1 for an encounter start, -1 for end, otherwise 0. */
    int32 Update(bool bIsSight, bool bSuccess, double Now);
    int32 ExpireHearing(double Now, double QuietSeconds);
};

/** Returns the actual top-level BT branch, never a guess from Blackboard contents. */
AITELEMETRY_API FString ClassifyTelemetryBranch(const UBTNode* ActiveNode, const UBehaviorTree* Tree);
AITELEMETRY_API bool IsTelemetryTargetActive(const UWorld* World);

UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class AITELEMETRY_API UAITelemetryComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UAITelemetryComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float Delta, ELevelTick Tick, FActorComponentTickFunction* Function) override;
    void Observe();
private:
    UFUNCTION() void PerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);
    void TryBind();
    void EmitTransition(int32 Transition, const FString& Sense, const FString& Reason);
    void WriteEvent(const FString& Kind, const FString& Sense, const FString& Reason, const FAIStimulus* Stimulus = nullptr);
    AAIController* Controller() const;
    TWeakObjectPtr<UAIPerceptionComponent> Perception;
    TWeakObjectPtr<AActor> Player;
    FAIEncounterState Encounter;
    double NextSample = -1.;
    float HearingQuietSeconds = 0.f;
    bool bAnnounced = false;
};

/** One recording session per activation of Day_4, including streamed levels. */
UCLASS()
class AITELEMETRY_API UAITelemetrySubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    virtual void Deinitialize() override;
    virtual void Tick(float Delta) override;
    virtual TStatId GetStatId() const override;
    bool StartRecording(const FString& Kind = TEXT("playtest"));
    void StopRecording(const FString& Reason);
    bool IsRecording() const { return bRecording; }
    double Elapsed() const;
    float SampleInterval() const { return SampleSeconds; }
    float HearingQuiet() const { return HearingSeconds; }
    void Sample(AActor* Ghost, const AActor* Player, const FString& State, const FString& ActiveNode, bool bSight, bool bHearing);
    void Event(AActor* Ghost, const AActor* Player, const FString& Kind, const FString& Sense,
        const FString& Reason, bool bSight, bool bHearing, int32 Count, const FAIStimulus* Stimulus = nullptr, const FPatrolLogData* Patrol = nullptr);
    FString GetRunDirectory() const { return RunDirectory; }
private:
    void Attach(AActor* Actor);
    bool IsTargetActive() const;
    void UpdateRecording();
    void Flush();
    FString Prefix(AActor* Ghost, const AActor* Player) const;
    FString RunId, RunDirectory, Condition, Tester, Build, RunKind, Map, SamplesPending, EventsPending;
    FString SamplesFile, EventsFile;
    float SampleSeconds = 0.5f, HearingSeconds = 0.f;
    double StartTime = 0., NextScan = 0., NextFlush = 0.;
    bool bRecording = false;
    FDelegateHandle SpawnHandle;
};
