#include "PatrolBias.h"
#include "AITelemetry.h"
#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AIPerceptionSystem.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AISense_Sight.h"

void FPatrolContactClock::Observe(double Now, bool bChasing, double CooldownSeconds)
{
    if (bSightActive || bChasing) LastContact = Now;
    if (bWasChasing && !bChasing) CooldownUntil = Now + FMath::Max(0., CooldownSeconds);
    bWasChasing = bChasing;
}
bool FPatrolContactClock::Eligible(double Now, double Threshold) const
{
    return !bSightActive && !bWasChasing && Now >= CooldownUntil && Gap(Now) >= FMath::Max(0., Threshold);
}
void FPatrolContactClock::ObserveState(double Now, const FString& State, double CooldownSeconds)
{
    Observe(Now, State == TEXT("Unknown") ? bWasChasing : State == TEXT("Chase"), CooldownSeconds);
}

APatrolBiasZone::APatrolBiasZone()
{
    Bounds = CreateDefaultSubobject<UBoxComponent>(TEXT("ZoneBounds"));
    SetRootComponent(Bounds);
    Bounds->SetBoxExtent(FVector(1000., 1000., 800.));
    Bounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Bounds->SetGenerateOverlapEvents(false);
    Bounds->SetCanEverAffectNavigation(false);
    Bounds->SetHiddenInGame(true);
}
bool APatrolBiasZone::Contains(const FVector& Position) const
{
    const FVector Local = Bounds->GetComponentTransform().InverseTransformPosition(Position);
    const FVector Extent = Bounds->GetUnscaledBoxExtent();
    // Half-open bounds make adjacent region ownership unambiguous.
    return Local.X >= -Extent.X && Local.X < Extent.X && Local.Y >= -Extent.Y && Local.Y < Extent.Y &&
        Local.Z >= -Extent.Z && Local.Z < Extent.Z;
}
FVector APatrolBiasZone::Candidate(int32 Index) const { return GetActorTransform().TransformPosition(CandidateOffsets[Index]); }

UPatrolBiasComponent::UPatrolBiasComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickInterval = 0.1f;
}
AAIController* UPatrolBiasComponent::Controller() const { return Cast<AAIController>(GetOwner()); }
UPatrolBiasComponent* UPatrolBiasComponent::Ensure(AAIController* AI)
{
    if (!AI || !AI->GetWorld() || AI->GetNetMode() == NM_Client || !IsTelemetryTargetActive(AI->GetWorld())) return nullptr;
    if (UPatrolBiasComponent* Existing = AI->FindComponentByClass<UPatrolBiasComponent>()) return Existing;
    UPatrolBiasComponent* Component = NewObject<UPatrolBiasComponent>(AI);
    AI->AddInstanceComponent(Component);
    Component->RegisterComponent();
    Component->ResetSession();
    return Component;
}
void UPatrolBiasComponent::BeginPlay() { Super::BeginPlay(); ResetSession(); }
void UPatrolBiasComponent::ResetSession()
{
    Contact = FPatrolContactClock();
    Contact.LastContact = GetWorld()->GetTimeSeconds();
    Selection = FPatrolLogData();
    bPendingMove = false;
    bInitialized = true;
    TrackedPlayer = UGameplayStatics::GetPlayerPawn(this, 0);
    if (Perception.IsValid()) Perception->OnTargetPerceptionUpdated.RemoveDynamic(this, &UPatrolBiasComponent::PerceptionUpdated);
    Perception.Reset();
    BindPerception();
}
void UPatrolBiasComponent::BindPerception()
{
    AAIController* AI = Controller();
    UAIPerceptionComponent* Current = AI ? AI->FindComponentByClass<UAIPerceptionComponent>() : nullptr;
    if (Current == Perception.Get()) return;
    if (Perception.IsValid()) Perception->OnTargetPerceptionUpdated.RemoveDynamic(this, &UPatrolBiasComponent::PerceptionUpdated);
    Perception = Current;
    if (!Current) return;
    Current->OnTargetPerceptionUpdated.AddUniqueDynamic(this, &UPatrolBiasComponent::PerceptionUpdated);
    FActorPerceptionBlueprintInfo Info;
    if (TrackedPlayer.IsValid() && Current->GetActorsPerception(TrackedPlayer.Get(), Info))
        for (const FAIStimulus& Stimulus : Info.LastSensedStimuli)
        {
            if (!Stimulus.WasSuccessfullySensed() || Stimulus.IsExpired()) continue;
            const TSubclassOf<UAISense> Sense = UAIPerceptionSystem::GetSenseClassForStimulus(this, Stimulus);
            if (Sense == UAISense_Sight::StaticClass()) Contact.bSightActive = true;
            // Preserve stimulus age: a stale hearing success is not fresh contact.
            if (Sense == UAISense_Hearing::StaticClass())
                Contact.LastContact = FMath::Max(Contact.LastContact, double(GetWorld()->GetTimeSeconds()) - Stimulus.GetAge());
        }
}
void UPatrolBiasComponent::PerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
    if (!IsTelemetryTargetActive(GetWorld()) || Actor != UGameplayStatics::GetPlayerPawn(this, 0)) return;
    const auto Sense = UAIPerceptionSystem::GetSenseClassForStimulus(this, Stimulus);
    const bool bSuccess = Stimulus.WasSuccessfullySensed() && !Stimulus.IsExpired();
    if (Sense == UAISense_Sight::StaticClass())
    {
        Contact.bSightActive = bSuccess;
        // The loss event closes an ongoing visual contact period.
        Contact.LastContact = GetWorld()->GetTimeSeconds();
    }
    else if (Sense == UAISense_Hearing::StaticClass() && bSuccess) Contact.LastContact = GetWorld()->GetTimeSeconds();
    if (bSuccess && (Sense == UAISense_Sight::StaticClass() || Sense == UAISense_Hearing::StaticClass()))
        SetMode(TEXT("normal"), TEXT("actual_contact"));
}
void UPatrolBiasComponent::Observe()
{
    if (!bInitialized) ResetSession();
    if (!IsTelemetryTargetActive(GetWorld())) return;
    AActor* CurrentPlayer = UGameplayStatics::GetPlayerPawn(this, 0);
    if (TrackedPlayer.Get() != CurrentPlayer) ResetSession();
    BindPerception();
    AAIController* AI = Controller();
    const UBehaviorTreeComponent* BT = AI ? Cast<UBehaviorTreeComponent>(AI->GetBrainComponent()) : nullptr;
    const FString State = ClassifyTelemetryBranch(BT ? BT->GetActiveNode() : nullptr, BT ? BT->GetCurrentTree() : nullptr);
    const auto* Settings = GetDefault<UAITelemetrySettings>();
    // ExecuteTask can run while the engine's active-node pointer is transitioning.
    // Unknown is not evidence of a chase ending or of leaving patrol.
    Contact.ObserveState(GetWorld()->GetTimeSeconds(), State, Settings->PatrolChaseCooldownSeconds);
    if (Selection.Mode == TEXT("zone") && (!Settings->bEnablePatrolBias || (State != TEXT("Unknown") && State != TEXT("Patrol")) ||
        !Contact.Eligible(GetWorld()->GetTimeSeconds(), Settings->PatrolContactThresholdSeconds)))
        SetMode(TEXT("normal"), TEXT("contact_cooldown_or_state"));
}
void UPatrolBiasComponent::TickComponent(float Delta, ELevelTick Tick, FActorComponentTickFunction* Function)
{
    Super::TickComponent(Delta, Tick, Function);
    Observe();
}
FPatrolLogData UPatrolBiasComponent::Snapshot() const
{
    FPatrolLogData Data = Selection;
    const auto* Settings = GetDefault<UAITelemetrySettings>();
    Data.bEnabled = Settings->bEnablePatrolBias;
    Data.Threshold = Settings->PatrolContactThresholdSeconds;
    Data.CooldownSeconds = Settings->PatrolChaseCooldownSeconds;
    Data.ContactGap = Contact.Gap(GetWorld()->GetTimeSeconds());
    Data.CooldownRemaining = FMath::Max(0., Contact.CooldownUntil - GetWorld()->GetTimeSeconds());
    return Data;
}
void UPatrolBiasComponent::Log(const FString& Kind, const FString& Reason) const
{
    if (UAITelemetrySubsystem* Session = GetWorld()->GetSubsystem<UAITelemetrySubsystem>())
    {
        const FPatrolLogData Data = Snapshot();
        AAIController* AI = Controller();
        Session->Event(AI ? AI->GetPawn() : nullptr, TrackedPlayer.Get(), Kind, TEXT(""), Reason,
            Contact.bSightActive, false, 0, nullptr, &Data);
    }
}
void UPatrolBiasComponent::SetMode(const FString& Mode, const FString& Reason)
{
    if (Selection.Mode == Mode) return;
    Selection.Mode = Mode;
    Log(Mode == TEXT("zone") ? TEXT("PatrolBiasEnabled") : TEXT("PatrolBiasDisabled"), Reason);
}
bool UPatrolBiasComponent::SelectTarget(FVector& OutTarget)
{
    Observe();
    AAIController* AI = Controller();
    APawn* Pawn = AI ? AI->GetPawn() : nullptr;
    if (!Pawn) return false;
    UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    ANavigationData* NavData = Nav ? Nav->GetNavDataForProps(AI->GetNavAgentPropertiesRef(), Pawn->GetActorLocation()) : nullptr;
    Selection.bHasTarget = false;
    Selection.Zone.Reset();
    bPendingMove = false;
    const auto* Settings = GetDefault<UAITelemetrySettings>();
    APatrolBiasZone* Zone = nullptr;
    FString Fallback = TEXT("disabled_or_contact_or_cooldown");
    if (NavData && Settings->bEnablePatrolBias && TrackedPlayer.IsValid() &&
        Contact.Eligible(GetWorld()->GetTimeSeconds(), Settings->PatrolContactThresholdSeconds))
    {
        // Pick the smallest region; ZoneId breaks ties consistently for overlapping authored zones.
        double Smallest = TNumericLimits<double>::Max();
        for (TActorIterator<APatrolBiasZone> It(GetWorld()); It; ++It)
        {
            if (!It->Contains(TrackedPlayer->GetActorLocation())) continue;
            const double Size = It->Bounds->GetScaledBoxExtent().GetAbsMax();
            if (!Zone || Size < Smallest || (Size == Smallest && It->ZoneId.LexicalLess(Zone->ZoneId)))
            { Zone = *It; Smallest = Size; }
        }
        Fallback = Zone ? TEXT("no_reachable_zone_candidate") : TEXT("player_outside_zones");
        if (Zone)
        {
            Selection.Zone = Zone->ZoneId.ToString();
            TArray<int32> Order;
            for (int32 Index = 0; Index < Zone->CandidateOffsets.Num(); ++Index) Order.Add(Index);
            for (int32 Index = Order.Num() - 1; Index > 0; --Index) Order.Swap(Index, FMath::RandRange(0, Index));
            const auto Filter = UNavigationQueryFilter::GetQueryFilter(*NavData, AI, AI->GetDefaultNavigationFilterClass());
            for (int32 Index : Order)
            {
                FNavLocation Projected;
                if (!Nav->ProjectPointToNavigation(Zone->Candidate(Index), Projected, FVector(120., 120., 800.), NavData, Filter) ||
                    !Zone->Contains(Projected.Location)) continue;
                FPathFindingQuery Query(AI, *NavData, Pawn->GetActorLocation(), Projected.Location, Filter);
                Query.SetAllowPartialPaths(false);
                const FPathFindingResult Path = Nav->FindPathSync(AI->GetNavAgentPropertiesRef(), Query);
                if (!Path.IsSuccessful() || !Path.Path.IsValid() || Path.Path->IsPartial()) continue;
                OutTarget = Projected.Location;
                Selection.Target = OutTarget;
                Selection.bHasTarget = true;
                SetMode(TEXT("zone"), TEXT("contact_gap_threshold"));
                bPendingMove = true;
                PendingSelection = Selection;
                Log(TEXT("PatrolTargetSelected"), TEXT("reachable_zone_candidate"));
                return true;
            }
        }
    }
    if (!NavData) Fallback = TEXT("navigation_unavailable");
    SetMode(TEXT("normal"), Fallback);
    Selection.Zone.Reset();
    FNavLocation Random;
    if (Nav && NavData && Nav->GetRandomReachablePointInRadius(Pawn->GetActorLocation(), 1000.f, Random, NavData))
    {
        OutTarget = Random.Location;
        Selection.Target = OutTarget;
        Selection.bHasTarget = true;
        bPendingMove = true;
        PendingSelection = Selection;
        Log(TEXT("PatrolTargetSelected"), Fallback);
        return true;
    }
    Log(TEXT("PatrolTargetSelectionFailed"), Fallback);
    return false;
}
void UPatrolBiasComponent::MoveFinished(const FString& Result)
{
    if (!bPendingMove) return;
    if (UAITelemetrySubsystem* Session = GetWorld()->GetSubsystem<UAITelemetrySubsystem>())
    {
        FPatrolLogData Data = Snapshot();
        Data.Mode = PendingSelection.Mode;
        Data.Zone = PendingSelection.Zone;
        Data.Target = PendingSelection.Target;
        Data.bHasTarget = PendingSelection.bHasTarget;
        AAIController* AI = Controller();
        Session->Event(AI ? AI->GetPawn() : nullptr, TrackedPlayer.Get(), TEXT("PatrolMoveFinished"), TEXT(""), Result,
            Contact.bSightActive, false, 0, nullptr, &Data);
    }
    bPendingMove = false;
}
void UPatrolBiasComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    MoveFinished(TEXT("interrupted_session_end"));
    if (Perception.IsValid()) Perception->OnTargetPerceptionUpdated.RemoveDynamic(this, &UPatrolBiasComponent::PerceptionUpdated);
    Super::EndPlay(Reason);
}

UBTTask_SelectPatrolWithBias::UBTTask_SelectPatrolWithBias()
{
    NodeName = TEXT("Select Patrol (Contact Gap / Zone)");
    BlackboardKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_SelectPatrolWithBias, BlackboardKey));
    BlackboardKey.SelectedKeyName = TEXT("PatrolLocation");
}
EBTNodeResult::Type UBTTask_SelectPatrolWithBias::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    AAIController* AI = OwnerComp.GetAIOwner();
    UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
    FVector Target;
    bool bFound = false;
    if (UPatrolBiasComponent* Bias = UPatrolBiasComponent::Ensure(AI)) bFound = Bias->SelectTarget(Target);
    else if (AI && AI->GetPawn())
    {
        // Shared BT outside Day_4 keeps the original local random patrol.
        if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(AI->GetWorld()))
        {
            FNavLocation Point;
            bFound = Nav->GetRandomReachablePointInRadius(AI->GetPawn()->GetActorLocation(), 1000.f, Point);
            Target = Point.Location;
        }
    }
    if (!Blackboard || !bFound || BlackboardKey.GetSelectedKeyID() == FBlackboard::InvalidKey) return EBTNodeResult::Failed;
    Blackboard->SetValueAsVector(BlackboardKey.SelectedKeyName, Target);
    return EBTNodeResult::Succeeded;
}
UBTTask_PatrolMoveWithLog::UBTTask_PatrolMoveWithLog(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
    NodeName = TEXT("Patrol Move To (Logged)");
    bNotifyTaskFinished = true;
}
void UBTTask_PatrolMoveWithLog::OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult)
{
    if (AAIController* AI = OwnerComp.GetAIOwner())
        if (UPatrolBiasComponent* Bias = AI->FindComponentByClass<UPatrolBiasComponent>())
            Bias->MoveFinished(TaskResult == EBTNodeResult::Succeeded ? TEXT("arrived") :
                TaskResult == EBTNodeResult::Aborted ? TEXT("aborted") : TEXT("failed"));
    Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);
}
bool UPatrolBiasWorldSubsystem::DoesSupportWorldType(EWorldType::Type Type) const { return Type == EWorldType::Game || Type == EWorldType::PIE; }
void UPatrolBiasWorldSubsystem::Tick(float Delta)
{
    if (!GetWorld() || !GetWorld()->HasBegunPlay() || GetWorld()->GetNetMode() == NM_Client) return;
    const bool bActive = IsTelemetryTargetActive(GetWorld());
    if (bTargetActive && !bActive)
        for (TActorIterator<AAIController> It(GetWorld()); It; ++It)
            if (UPatrolBiasComponent* Bias = It->FindComponentByClass<UPatrolBiasComponent>()) Bias->DestroyComponent();
    if (!bTargetActive && bActive) NextScan = 0.;
    bTargetActive = bActive;
    if (!bActive || GetWorld()->GetTimeSeconds() < NextScan) return;
    NextScan = GetWorld()->GetTimeSeconds() + 0.25;
    for (TActorIterator<AAIController> It(GetWorld()); It; ++It)
        for (const UClass* Class = It->GetClass(); Class; Class = Class->GetSuperClass())
            if (Class->GetPathName() == GetDefault<UAITelemetrySettings>()->ControllerClassPath)
            { UPatrolBiasComponent::Ensure(*It); break; }
}
TStatId UPatrolBiasWorldSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UPatrolBiasWorldSubsystem, STATGROUP_Tickables); }
