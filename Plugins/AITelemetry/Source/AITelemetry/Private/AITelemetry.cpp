#include "AITelemetry.h"
#include "PatrolBias.h"
#include "AIController.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTNode.h"
#include "BehaviorTree/Decorators/BTDecorator_BlackboardBase.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AIPerceptionSystem.h"
#include "Perception/AISenseConfig.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AISense_Sight.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, AITelemetry)
DEFINE_LOG_CATEGORY_STATIC(LogAITelemetry, Log, All);

namespace Telemetry
{
FString Csv(FString Value) { Value.ReplaceInline(TEXT("\""), TEXT("\"\"")); return TEXT("\"") + Value + TEXT("\""); }
FString Number(double Value) { return FString::Printf(TEXT("%.6f"), Value); }
FString Location(const AActor* Actor)
{
    if (!IsValid(Actor)) return TEXT(",,"); // Missing positions stay blank, never become world origin.
    const FVector P = Actor->GetActorLocation();
    return Number(P.X) + TEXT(",") + Number(P.Y) + TEXT(",") + Number(P.Z);
}
bool Append(const FString& Text, const FString& File)
{
    return Text.IsEmpty() || FFileHelper::SaveStringToFile(Text, *File,
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
}
FString NormalizeMap(FString Package)
{
    const FString Short = FPackageName::GetShortName(Package);
    if (Short.StartsWith(TEXT("UEDPIE_")))
    {
        int32 Separator = INDEX_NONE;
        if (Short.FindChar(TEXT('_'), Separator))
        {
            Separator = Short.Find(TEXT("_"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Separator + 1);
            if (Separator != INDEX_NONE) Package = FPackageName::GetLongPackagePath(Package) + TEXT("/") + Short.Mid(Separator + 1);
        }
    }
    return Package;
}
FString WorldMap(const UWorld* World) { return NormalizeMap(World->GetOutermost()->GetName()); }
}

UAITelemetrySettings::UAITelemetrySettings() { CategoryName = TEXT("Plugins"); }

int32 FAIEncounterState::Update(bool bIsSight, bool bSuccess, double Now)
{
    const bool Before = IsDetected();
    if (bIsSight) bSight = bSuccess;
    else { bHearing = bSuccess; if (bSuccess) LastHearingSuccess = Now; }
    const bool After = IsDetected();
    if (!Before && After) { ++EncounterCount; return 1; }
    return Before && !After ? -1 : 0;
}

int32 FAIEncounterState::ExpireHearing(double Now, double QuietSeconds)
{
    if (QuietSeconds > 0. && bHearing && LastHearingSuccess >= 0. && Now - LastHearingSuccess >= QuietSeconds)
        return Update(false, false, Now);
    return 0;
}

FString ClassifyTelemetryBranch(const UBTNode* ActiveNode, const UBehaviorTree* Tree)
{
    if (!ActiveNode || !Tree || !Tree->RootNode) return TEXT("Unknown");
    // Mapping is intentionally restricted to the inspected tree, and validated by key settings.
    if (Tree->GetPathName() != TEXT("/Game/BP_Object/AIGhost/BT_Ghost.BT_Ghost")) return TEXT("Unknown");
    const UBTNode* Branch = ActiveNode;
    TSet<const UBTNode*> Seen;
    // Runtime nodes belong to the manager's duplicated tree, not the asset root.
    while (Branch && Branch->GetParentNode() && Branch->GetParentNode()->GetParentNode())
    {
        if (Seen.Contains(Branch)) return TEXT("Unknown");
        Seen.Add(Branch);
        Branch = Branch->GetParentNode();
    }
    const UBTCompositeNode* Root = Branch ? Branch->GetParentNode() : nullptr;
    if (!Root || Root->GetTreeAsset() != Tree || Root->GetClass() != Tree->RootNode->GetClass()) return TEXT("Unknown");
    const auto& Children = Root->Children;
    const bool bHasLastSeenBranch = Children.Num() == 4;
    if (!bHasLastSeenBranch && Children.Num() != 3) return TEXT("Unknown");
    const TArray<FName> ExpectedKeys = bHasLastSeenBranch
        ? TArray<FName>{ TEXT("TargetActor"), TEXT("LastSeenLocation"), TEXT("NoiseLocation"), TEXT("TargetActor") }
        : TArray<FName>{ TEXT("TargetActor"), TEXT("NoiseLocation"), TEXT("TargetActor") };
    const TArray<FString> States = bHasLastSeenBranch
        ? TArray<FString>{ TEXT("Chase"), TEXT("InvestigateLastSeen"), TEXT("Investigate"), TEXT("Patrol") }
        : TArray<FString>{ TEXT("Chase"), TEXT("Investigate"), TEXT("Patrol") };
    for (int32 Index = 0; Index < Children.Num(); ++Index)
    {
        if (Children[Index].Decorators.Num() != 1) return TEXT("Unknown");
        const UBTDecorator_BlackboardBase* Decorator = Cast<UBTDecorator_BlackboardBase>(Children[Index].Decorators[0]);
        if (!Decorator || Decorator->GetSelectedBlackboardKey() != ExpectedKeys[Index]) return TEXT("Unknown");
    }
    for (int32 Index = 0; Index < Children.Num(); ++Index)
    {
        const UBTNode* SavedBranch = Children[Index].ChildComposite.Get();
        if (SavedBranch && Branch == SavedBranch)
            return States[Index];
    }
    // Runtime instanced nodes can be copies; use indices only when unambiguous.
    int32 Match = INDEX_NONE;
    for (int32 Index = 0; Index < Children.Num(); ++Index)
    {
        const UBTNode* SavedBranch = Children[Index].ChildComposite.Get();
        if (SavedBranch && Branch->GetExecutionIndex() == SavedBranch->GetExecutionIndex())
        {
            if (Match != INDEX_NONE) return TEXT("Unknown");
            Match = Index;
        }
    }
    if (Match != INDEX_NONE) return States[Match];
    return TEXT("Unknown");
}

UAITelemetryComponent::UAITelemetryComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickInterval = 0.1f;
}
AAIController* UAITelemetryComponent::Controller() const
{
    if (AAIController* AI = Cast<AAIController>(GetOwner())) return AI;
    if (const APawn* Pawn = Cast<APawn>(GetOwner())) return Cast<AAIController>(Pawn->GetController());
    return nullptr;
}
void UAITelemetryComponent::BeginPlay() { Super::BeginPlay(); Observe(); }
void UAITelemetryComponent::TickComponent(float Delta, ELevelTick Tick, FActorComponentTickFunction* Function)
{
    Super::TickComponent(Delta, Tick, Function);
    Observe();
}
void UAITelemetryComponent::TryBind()
{
    AAIController* AI = Controller();
    UAIPerceptionComponent* Current = AI ? AI->FindComponentByClass<UAIPerceptionComponent>() : nullptr;
    if (Perception.Get() == Current) return;
    if (Perception.IsValid()) Perception->OnTargetPerceptionUpdated.RemoveDynamic(this, &UAITelemetryComponent::PerceptionUpdated);
    Perception = Current;
    if (Current)
    {
        Current->OnTargetPerceptionUpdated.AddUniqueDynamic(this, &UAITelemetryComponent::PerceptionUpdated);
        // Initial snapshot can recover a state already active when the logger was attached.
        // It is explicitly labelled; its true original onset time is not inferred.
        AActor* Target = UGameplayStatics::GetPlayerPawn(this, 0);
        FActorPerceptionBlueprintInfo Info;
        if (Target && Current->GetActorsPerception(Target, Info))
            for (const FAIStimulus& Stimulus : Info.LastSensedStimuli)
            {
                if (!Stimulus.WasSuccessfullySensed() || Stimulus.IsExpired()) continue;
                const TSubclassOf<UAISense> Sense = UAIPerceptionSystem::GetSenseClassForStimulus(this, Stimulus);
                if (Sense != UAISense_Sight::StaticClass() && Sense != UAISense_Hearing::StaticClass()) continue;
                Player = Target;
                const bool bSight = Sense == UAISense_Sight::StaticClass();
                const int32 Transition = Encounter.Update(bSight, true, GetWorld()->GetTimeSeconds());
                WriteEvent(TEXT("InitialSenseSnapshot"), bSight ? TEXT("Sight") : TEXT("Hearing"), TEXT("original_onset_unknown"), &Stimulus);
                EmitTransition(Transition, bSight ? TEXT("Sight") : TEXT("Hearing"), TEXT("initial_snapshot_onset_unknown"));
            }
    }
}
void UAITelemetryComponent::Observe()
{
    UAITelemetrySubsystem* Session = GetWorld() ? GetWorld()->GetSubsystem<UAITelemetrySubsystem>() : nullptr;
    if (!Session || !Session->IsRecording()) return;
    HearingQuietSeconds = Session->HearingQuiet();
    TryBind();
    AAIController* AI = Controller();
    if (!AI || !AI->GetPawn()) return;
    AActor* Target = UGameplayStatics::GetPlayerPawn(this, 0);
    if (Encounter.IsDetected() && (Player.IsStale() || Player.Get() != Target))
    {
        Encounter.bSight = Encounter.bHearing = false;
        WriteEvent(TEXT("EncounterEnd"), TEXT("All"), TEXT("player_changed"));
    }
    Player = Target;
    const double Now = GetWorld()->GetTimeSeconds();
    const bool bHearingBefore = Encounter.bHearing;
    const int32 Transition = Encounter.ExpireHearing(Now, HearingQuietSeconds);
    if (bHearingBefore && !Encounter.bHearing) WriteEvent(TEXT("HearingQuietTimeout"), TEXT("Hearing"), TEXT("logger_only_timeout"));
    EmitTransition(Transition, TEXT("Hearing"), TEXT("logger_only_timeout"));
    if (!bAnnounced && Perception.IsValid())
    {
        FString Detail = TEXT("read_only;hearing_quiet_seconds=") + Telemetry::Number(HearingQuietSeconds);
        for (const auto Sense : { UAISense::GetSenseID<UAISense_Sight>(), UAISense::GetSenseID<UAISense_Hearing>() })
            if (const UAISenseConfig* Config = Perception->GetSenseConfig(Sense))
                Detail += TEXT(";") + Config->GetClass()->GetName() + TEXT("_effective_max_age=") + Telemetry::Number(Config->GetMaxAge());
        WriteEvent(TEXT("LoggerAttached"), TEXT(""), Detail);
        bAnnounced = true;
    }
    if (Now >= NextSample)
    {
        const UBehaviorTreeComponent* BT = Cast<UBehaviorTreeComponent>(AI->GetBrainComponent());
        const UBTNode* Active = BT ? BT->GetActiveNode() : nullptr;
        Session->Sample(AI->GetPawn(), Target, ClassifyTelemetryBranch(Active, BT ? BT->GetCurrentTree() : nullptr),
            Active ? Active->GetPathName() : FString(), Encounter.bSight, Encounter.bHearing);
        NextSample = Now + Session->SampleInterval();
    }
}
void UAITelemetryComponent::PerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
    UAITelemetrySubsystem* Session = GetWorld()->GetSubsystem<UAITelemetrySubsystem>();
    if (!Session || !Session->IsRecording() || Actor != UGameplayStatics::GetPlayerPawn(this, 0)) return;
    const TSubclassOf<UAISense> Sense = UAIPerceptionSystem::GetSenseClassForStimulus(this, Stimulus);
    if (Sense != UAISense_Sight::StaticClass() && Sense != UAISense_Hearing::StaticClass()) return;
    Player = Actor;
    // Apply a quiet gap before a new success, even if no sampling tick fell in that gap.
    EmitTransition(Encounter.ExpireHearing(GetWorld()->GetTimeSeconds(), Session->HearingQuiet()), TEXT("Hearing"), TEXT("logger_only_timeout"));
    const bool bSight = Sense == UAISense_Sight::StaticClass();
    const bool bSuccess = Stimulus.WasSuccessfullySensed() && !Stimulus.IsExpired();
    const int32 Transition = Encounter.Update(bSight, bSuccess, GetWorld()->GetTimeSeconds());
    const FString SenseName = bSight ? TEXT("Sight") : TEXT("Hearing");
    WriteEvent(bSuccess ? TEXT("SenseSuccess") : TEXT("SenseLost"), SenseName, TEXT("perception_callback"), &Stimulus);
    EmitTransition(Transition, SenseName, TEXT("perception_callback"));
}
void UAITelemetryComponent::EmitTransition(int32 Transition, const FString& Sense, const FString& Reason)
{
    if (Transition != 0) WriteEvent(Transition > 0 ? TEXT("EncounterStart") : TEXT("EncounterEnd"), Sense, Reason);
}
void UAITelemetryComponent::WriteEvent(const FString& Kind, const FString& Sense, const FString& Reason, const FAIStimulus* Stimulus)
{
    if (UAITelemetrySubsystem* Session = GetWorld()->GetSubsystem<UAITelemetrySubsystem>())
    {
        const AAIController* AI = Controller();
        Session->Event(AI ? AI->GetPawn() : GetOwner(), Player.Get(), Kind, Sense, Reason,
            Encounter.bSight, Encounter.bHearing, Encounter.EncounterCount, Stimulus);
    }
}
void UAITelemetryComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    WriteEvent(TEXT("LoggerDetached"), TEXT(""), UEnum::GetValueAsString(Reason));
    if (Perception.IsValid()) Perception->OnTargetPerceptionUpdated.RemoveDynamic(this, &UAITelemetryComponent::PerceptionUpdated);
    Super::EndPlay(Reason);
}

bool UAITelemetrySubsystem::DoesSupportWorldType(EWorldType::Type Type) const { return Type == EWorldType::Game || Type == EWorldType::PIE; }
void UAITelemetrySubsystem::OnWorldBeginPlay(UWorld& World)
{
    UpdateRecording();
}
bool UAITelemetrySubsystem::IsTargetActive() const
{
    return IsTelemetryTargetActive(GetWorld());
}
bool IsTelemetryTargetActive(const UWorld* World)
{
    const FString& Target = GetDefault<UAITelemetrySettings>()->TargetMap;
    if (!World) return false;
    if (Telemetry::WorldMap(World) == Target) return true;
    for (const ULevel* Level : World->GetLevels())
        if (Level && Level->bIsVisible && Telemetry::NormalizeMap(Level->GetOutermost()->GetName()) == Target)
            return true;
    return false;
}
void UAITelemetrySubsystem::UpdateRecording()
{
    if (!GetWorld() || !GetWorld()->HasBegunPlay()) return;
    // Explicit smoke sessions are controlled by the commandlet.
    if (bRecording && RunKind != TEXT("playtest")) return;
    const bool bActive = GetDefault<UAITelemetrySettings>()->bEnabled && IsTargetActive();
    if (bActive && !bRecording) StartRecording();
    else if (!bActive && bRecording) StopRecording(TEXT("target_level_inactive"));
}
bool UAITelemetrySubsystem::StartRecording(const FString& Kind)
{
    if (bRecording || !GetWorld() || GetWorld()->GetNetMode() == NM_Client) return false;
    const UAITelemetrySettings* Settings = GetDefault<UAITelemetrySettings>();
    SampleSeconds = FMath::Max(0.05f, Settings->SampleIntervalSeconds);
    HearingSeconds = FMath::Max(0.f, Settings->HearingQuietSeconds);
    Condition = Settings->Condition; Tester = Settings->TesterId; Build = Settings->BuildLabel; RunKind = Kind;
    Map = Telemetry::WorldMap(GetWorld());
    if (Kind == TEXT("playtest")) Map = Settings->TargetMap;
    NextScan = NextFlush = 0.;
    StartTime = GetWorld()->GetTimeSeconds();
    RunId = FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%S")) + TEXT("_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    RunDirectory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AITelemetry"), RunId));
    SamplesFile = FPaths::Combine(RunDirectory, TEXT("samples.csv"));
    EventsFile = FPaths::Combine(RunDirectory, TEXT("events.csv"));
    const FString Common = TEXT("run_id,run_kind,condition,tester_id,build_label,map,elapsed_s,ghost_id,player_id,ghost_x_cm,ghost_y_cm,ghost_z_cm,player_x_cm,player_y_cm,player_z_cm,");
    if (!IFileManager::Get().MakeDirectory(*RunDirectory, true) ||
        !FFileHelper::SaveStringToFile(Common + TEXT("state,active_node,sight_detected,hearing_detected,sample_interval_s,hearing_quiet_s\n"), *SamplesFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) ||
        !FFileHelper::SaveStringToFile(Common + TEXT("event_kind,sense,reason,sight_detected,hearing_detected,encounter_count,stimulus_x_cm,stimulus_y_cm,stimulus_z_cm,stimulus_age_s,hearing_quiet_s,patrol_bias_enabled,patrol_mode,patrol_zone,target_x_cm,target_y_cm,target_z_cm,contact_threshold_s,chase_cooldown_s,contact_gap_s,cooldown_remaining_s\n"), *EventsFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        UE_LOG(LogAITelemetry, Error, TEXT("Failed to create CSV files: %s"), *RunDirectory);
        return false;
    }
    bRecording = true;
    FPatrolLogData PatrolSettings;
    PatrolSettings.bEnabled = Settings->bEnablePatrolBias;
    PatrolSettings.Threshold = Settings->PatrolContactThresholdSeconds;
    PatrolSettings.CooldownSeconds = Settings->PatrolChaseCooldownSeconds;
    Event(nullptr, nullptr, TEXT("SessionStart"), TEXT(""), TEXT("engine=") + FEngineVersion::Current().ToString(), false, false, 0, nullptr, &PatrolSettings);
    SpawnHandle = GetWorld()->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &UAITelemetrySubsystem::Attach));
    for (TActorIterator<AAIController> It(GetWorld()); It; ++It) Attach(*It);
    Flush();
    UE_LOG(LogAITelemetry, Display, TEXT("Recording %s -> %s"), *RunKind, *RunDirectory);
    return true;
}
double UAITelemetrySubsystem::Elapsed() const { return GetWorld() ? FMath::Max(0., GetWorld()->GetTimeSeconds() - StartTime) : 0.; }
FString UAITelemetrySubsystem::Prefix(AActor* Ghost, const AActor* Target) const
{
    return Telemetry::Csv(RunId) + TEXT(",") + Telemetry::Csv(RunKind) + TEXT(",") + Telemetry::Csv(Condition) + TEXT(",") +
        Telemetry::Csv(Tester) + TEXT(",") + Telemetry::Csv(Build) + TEXT(",") + Telemetry::Csv(Map) + TEXT(",") + Telemetry::Number(Elapsed()) + TEXT(",") +
        Telemetry::Csv(IsValid(Ghost) ? Ghost->GetPathName() : FString()) + TEXT(",") + Telemetry::Csv(IsValid(Target) ? Target->GetPathName() : FString()) + TEXT(",") +
        Telemetry::Location(Ghost) + TEXT(",") + Telemetry::Location(Target) + TEXT(",");
}
void UAITelemetrySubsystem::Sample(AActor* Ghost, const AActor* Target, const FString& State, const FString& Active, bool bSight, bool bHearing)
{
    if (!bRecording) return;
    SamplesPending += Prefix(Ghost, Target) + Telemetry::Csv(State) + TEXT(",") + Telemetry::Csv(Active) + TEXT(",") +
        FString::Printf(TEXT("%d,%d,%.6f,%.6f\n"), bSight, bHearing, SampleSeconds, HearingSeconds);
}
void UAITelemetrySubsystem::Event(AActor* Ghost, const AActor* Target, const FString& Kind, const FString& Sense,
    const FString& Reason, bool bSight, bool bHearing, int32 Count, const FAIStimulus* Stimulus, const FPatrolLogData* Patrol)
{
    if (!bRecording) return;
    const FString StimulusFields = Stimulus ? Telemetry::Number(Stimulus->StimulusLocation.X) + TEXT(",") + Telemetry::Number(Stimulus->StimulusLocation.Y) + TEXT(",") +
        Telemetry::Number(Stimulus->StimulusLocation.Z) + TEXT(",") + Telemetry::Number(Stimulus->GetAge()) : TEXT(",,,");
    FString PatrolFields = TEXT(",,,,,,,,,,");
    if (Patrol)
    {
        const FString Destination = Patrol->bHasTarget ? Telemetry::Number(Patrol->Target.X) + TEXT(",") +
            Telemetry::Number(Patrol->Target.Y) + TEXT(",") + Telemetry::Number(Patrol->Target.Z) : TEXT(",,");
        PatrolFields = FString::Printf(TEXT(",%d,"), Patrol->bEnabled) + Telemetry::Csv(Patrol->Mode) + TEXT(",") +
            Telemetry::Csv(Patrol->Zone) + TEXT(",") + Destination + TEXT(",") + Telemetry::Number(Patrol->Threshold) + TEXT(",") +
            Telemetry::Number(Patrol->CooldownSeconds) + TEXT(",") + Telemetry::Number(Patrol->ContactGap) + TEXT(",") +
            Telemetry::Number(Patrol->CooldownRemaining);
    }
    EventsPending += Prefix(Ghost, Target) + Telemetry::Csv(Kind) + TEXT(",") + Telemetry::Csv(Sense) + TEXT(",") + Telemetry::Csv(Reason) + TEXT(",") +
        FString::Printf(TEXT("%d,%d,%d,"), bSight, bHearing, Count) + StimulusFields + TEXT(",") + Telemetry::Number(HearingSeconds) + PatrolFields + TEXT("\n");
}
void UAITelemetrySubsystem::Attach(AActor* Actor)
{
    if (!bRecording || !IsValid(Actor) || !Actor->IsA<AAIController>()) return;
    bool bMatches = false;
    for (const UClass* Class = Actor->GetClass(); Class; Class = Class->GetSuperClass())
        if (Class->GetPathName() == GetDefault<UAITelemetrySettings>()->ControllerClassPath) { bMatches = true; break; }
    if (!bMatches || Actor->FindComponentByClass<UAITelemetryComponent>()) return;
    UAITelemetryComponent* Logger = NewObject<UAITelemetryComponent>(Actor);
    Actor->AddInstanceComponent(Logger);
    Logger->RegisterComponent();
    Logger->Observe();
}
void UAITelemetrySubsystem::Tick(float Delta)
{
    UpdateRecording();
    if (!bRecording) return;
    const double Now = GetWorld()->GetTimeSeconds();
    if (Now >= NextScan)
    {
        for (TActorIterator<AAIController> It(GetWorld()); It; ++It) Attach(*It);
        NextScan = Now + 0.25;
    }
    if (Now >= NextFlush) { Flush(); NextFlush = Now + 5.; }
}
TStatId UAITelemetrySubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UAITelemetrySubsystem, STATGROUP_Tickables); }
void UAITelemetrySubsystem::Flush()
{
    if (Telemetry::Append(SamplesPending, SamplesFile)) SamplesPending.Reset();
    else UE_LOG(LogAITelemetry, Error, TEXT("Could not append samples: %s"), *SamplesFile);
    if (Telemetry::Append(EventsPending, EventsFile)) EventsPending.Reset();
    else UE_LOG(LogAITelemetry, Error, TEXT("Could not append events: %s"), *EventsFile);
}
void UAITelemetrySubsystem::StopRecording(const FString& Reason)
{
    if (!bRecording) return;
    for (TActorIterator<AAIController> It(GetWorld()); It; ++It)
        if (UPatrolBiasComponent* Bias = It->FindComponentByClass<UPatrolBiasComponent>())
            Bias->MoveFinished(TEXT("interrupted_session_end"));
    Event(nullptr, nullptr, TEXT("SessionEnd"), TEXT(""), Reason, false, false, 0);
    Flush();
    GetWorld()->RemoveOnActorSpawnedHandler(SpawnHandle);
    bRecording = false;
    // Fresh components reset encounter counts and perception bindings on re-entry.
    for (TActorIterator<AAIController> It(GetWorld()); It; ++It)
        if (UAITelemetryComponent* Logger = It->FindComponentByClass<UAITelemetryComponent>())
            Logger->DestroyComponent();
    UE_LOG(LogAITelemetry, Display, TEXT("Saved session -> %s"), *RunDirectory);
}
void UAITelemetrySubsystem::Deinitialize() { StopRecording(TEXT("world_deinitialize")); Super::Deinitialize(); }
