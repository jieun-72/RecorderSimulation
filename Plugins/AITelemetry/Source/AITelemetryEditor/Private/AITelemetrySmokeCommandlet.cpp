#include "AITelemetrySmokeCommandlet.h"
#include "AITelemetry.h"
#include "PatrolBias.h"
#include "Components/BoxComponent.h"
#include "AIController.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeManager.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTTaskNode.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "UObject/Package.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AIPerceptionSystem.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISenseConfig_Sight.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, AITelemetryEditor)

UAITelemetrySmokeCommandlet::UAITelemetrySmokeCommandlet()
{
    IsClient = false; IsServer = false; IsEditor = true; LogToConsole = true; ShowErrorCount = false;
}

int32 UAITelemetrySmokeCommandlet::Main(const FString& Params)
{
    int32 Failures = 0;
    auto Verify = [&Failures](bool Condition, const TCHAR* Message)
    {
        if (!Condition) { ++Failures; UE_LOG(LogTemp, Error, TEXT("TELEMETRY_TEST_FAILED %s"), Message); }
    };
    FPatrolContactClock Clock;
    Clock.Observe(39., false, 10.);
    Verify(!Clock.Eligible(39., 40.) && Clock.Eligible(40., 40.), TEXT("bias contact threshold boundary"));
    Clock.bSightActive = true;
    Clock.Observe(100., false, 10.);
    Verify(!Clock.Eligible(100., 0.) && Clock.LastContact == 100., TEXT("ongoing sight refreshes contact"));
    Clock.bSightActive = false;
    Clock.Observe(101., true, 10.);
    Clock.Observe(102., false, 10.);
    Verify(!Clock.Eligible(111.99, 0.) && Clock.Eligible(112., 0.), TEXT("chase exit cooldown boundary"));
    Verify(!Clock.Eligible(120., 40.) && Clock.Eligible(141., 40.), TEXT("contact threshold still applies after cooldown"));
    FPatrolContactClock Transition;
    Transition.ObserveState(10., TEXT("Chase"), 10.);
    Transition.ObserveState(11., TEXT("Unknown"), 10.);
    Verify(Transition.bWasChasing && Transition.CooldownUntil == 0., TEXT("unknown transition does not falsely end chase"));
    Transition.ObserveState(12., TEXT("Patrol"), 10.);
    Verify(!Transition.bWasChasing && Transition.CooldownUntil == 22., TEXT("known chase exit starts cooldown"));
    // Overlap and repeats must count one encounter until ALL participating senses end.
    FAIEncounterState State;
    Verify(State.Update(false, true, 0.) == 1, TEXT("first hearing starts encounter"));
    Verify(State.Update(false, true, 1.) == 0, TEXT("repeat hearing does not count again"));
    Verify(State.Update(true, true, 1.5) == 0, TEXT("sight plus hearing stays one encounter"));
    Verify(State.ExpireHearing(4., 3.) == 0 && !State.bHearing && State.bSight, TEXT("hearing timeout does not end active sight"));
    Verify(State.Update(true, false, 5.) == -1, TEXT("all senses lost ends encounter"));
    Verify(State.Update(true, true, 6.) == 1 && State.EncounterCount == 2, TEXT("redetection starts second encounter"));
    FAIEncounterState Native;
    Native.Update(false, true, 0.);
    Verify(Native.ExpireHearing(100., 0.) == 0 && Native.bHearing, TEXT("native mode never invents hearing expiry"));
    FAIEncounterState HearingOnly;
    HearingOnly.Update(false, true, 0.);
    Verify(HearingOnly.ExpireHearing(2.99, 3.) == 0, TEXT("quiet boundary not prematurely expired"));
    Verify(HearingOnly.ExpireHearing(3., 3.) == -1, TEXT("quiet boundary expiry"));
    Verify(HearingOnly.Update(false, true, 4.) == 1 && HearingOnly.EncounterCount == 2, TEXT("new noise after silence counts again"));

    // Validate classification against the REAL saved tree, without running or rebuilding it.
    UBehaviorTree* Tree = LoadObject<UBehaviorTree>(nullptr, TEXT("/Game/BP_Object/AIGhost/BT_Ghost"));
    Verify(Tree && Tree->RootNode && Tree->RootNode->Children.Num() == 4, TEXT("actual BT structure"));
    if (Tree && Tree->RootNode && Tree->RootNode->Children.Num() == 4)
    {
        const TCHAR* Expected[] = { TEXT("Chase"), TEXT("InvestigateLastSeen"), TEXT("Investigate"), TEXT("Patrol") };
        for (int32 Branch = 0; Branch < 4; ++Branch)
        {
            const UBTCompositeNode* Composite = Tree->RootNode->Children[Branch].ChildComposite;
            Verify(Composite != nullptr, TEXT("composite branch exists"));
            if (Composite)
                for (const FBTCompositeChild& Child : Composite->Children)
                    Verify(ClassifyTelemetryBranch(Child.ChildTask, Tree) == Expected[Branch], TEXT("all real tasks including Wait classified by branch"));
        }
    }
    Verify(ClassifyTelemetryBranch(nullptr, Tree) == TEXT("Unknown"), TEXT("inactive BT is not guessed as patrol"));

    // CSV end-to-end smoke uses transient actors; never loads/saves the gameplay map.
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("AITelemetrySmokeWorld"));
    Verify(World != nullptr, TEXT("temporary game world"));
    if (!World) return 1;
    FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
    WorldContext.SetCurrentWorld(World);
    World->CreateAISystem();
    UAIPerceptionSystem* PerceptionSystem = UAIPerceptionSystem::GetCurrent(World);
    Verify(PerceptionSystem != nullptr, TEXT("temporary world Perception system"));
    if (PerceptionSystem)
    {
        PerceptionSystem->RegisterSenseClass(UAISense_Sight::StaticClass());
        PerceptionSystem->RegisterSenseClass(UAISense_Hearing::StaticClass());
    }
    World->InitializeActorsForPlay(FURL());
    // Use the engine's real duplication/initialization path used during gameplay.
    UBehaviorTreeManager* Manager = UBehaviorTreeManager::GetCurrent(World);
    UBTCompositeNode* RuntimeRoot = nullptr;
    uint16 RuntimeMemorySize = 0;
    Verify(Manager && Tree && Manager->LoadTree(*Tree, RuntimeRoot, RuntimeMemorySize), TEXT("engine runtime tree loaded"));
    Verify(RuntimeRoot && RuntimeRoot != Tree->RootNode, TEXT("runtime root differs from saved asset root"));
    if (RuntimeRoot && RuntimeRoot->Children.Num() == 4)
    {
        const TCHAR* Expected[] = { TEXT("Chase"), TEXT("InvestigateLastSeen"), TEXT("Investigate"), TEXT("Patrol") };
        for (int32 Branch = 0; Branch < 4; ++Branch)
        {
            const UBTCompositeNode* Composite = RuntimeRoot->Children[Branch].ChildComposite;
            Verify(Composite != nullptr, TEXT("runtime composite exists"));
            if (!Composite) continue;
            for (const FBTCompositeChild& Child : Composite->Children)
                Verify(ClassifyTelemetryBranch(Child.ChildTask, Tree) == Expected[Branch], TEXT("engine duplicated tasks and Wait classified"));
        }
        Verify(ClassifyTelemetryBranch(RuntimeRoot, Tree) == TEXT("Unknown"), TEXT("root itself is not classified as a behavior"));
    }
    UAITelemetrySubsystem* Session = World->GetSubsystem<UAITelemetrySubsystem>();
    Verify(Session != nullptr, TEXT("game subsystem created"));
    if (Session)
    {
        Verify(Session->StartRecording(TEXT("smoke")), TEXT("session starts"));
        APawn* Ghost = World->SpawnActor<APawn>();
        APawn* Player = World->SpawnActor<APawn>();
        for (APawn* Pawn : { Ghost, Player })
        {
            USceneComponent* Root = NewObject<USceneComponent>(Pawn);
            Pawn->SetRootComponent(Root);
            Root->RegisterComponent();
        }
        Ghost->SetActorLocation(FVector(123., -456., 78.));
        Player->SetActorLocation(FVector(321., -654., 87.));
        // Exercise real auto-attachment and delegate delivery on transient native AI.
        // Change only this process's config object; never persist settings/assets.
        UAITelemetrySettings* Settings = GetMutableDefault<UAITelemetrySettings>();
        const FString SavedControllerPath = Settings->ControllerClassPath;
        Settings->ControllerClassPath = AAIController::StaticClass()->GetPathName();
        APlayerController* PC = World->SpawnActor<APlayerController>();
        PC->Possess(Player);
        AAIController* AI = World->SpawnActor<AAIController>();
        AI->Possess(Ghost);
        UAIPerceptionComponent* Perception = NewObject<UAIPerceptionComponent>(AI);
        AI->AddInstanceComponent(Perception);
        UAISenseConfig_Sight* Sight = NewObject<UAISenseConfig_Sight>(Perception);
        UAISenseConfig_Hearing* Hearing = NewObject<UAISenseConfig_Hearing>(Perception);
        Perception->ConfigureSense(*Sight);
        Perception->ConfigureSense(*Hearing);
        Perception->RegisterComponent();
        UAITelemetryComponent* Logger = AI->FindComponentByClass<UAITelemetryComponent>();
        Verify(Logger != nullptr, TEXT("automatic runtime component attachment"));
        if (Logger)
        {
            Logger->Observe();
            FAIStimulus HearingSuccess(*GetDefault<UAISense_Hearing>(), 1.f, Player->GetActorLocation(), Ghost->GetActorLocation(), FAIStimulus::SensingSucceeded);
            FAIStimulus SightSuccess(*GetDefault<UAISense_Sight>(), 1.f, Player->GetActorLocation(), Ghost->GetActorLocation(), FAIStimulus::SensingSucceeded);
            FAIStimulus HearingLost = HearingSuccess; HearingLost.MarkNoLongerSensed();
            FAIStimulus SightLost = SightSuccess; SightLost.MarkNoLongerSensed();
            Perception->OnTargetPerceptionUpdated.Broadcast(Player, HearingSuccess);
            Perception->OnTargetPerceptionUpdated.Broadcast(Player, HearingSuccess);
            Perception->OnTargetPerceptionUpdated.Broadcast(Player, SightSuccess);
            Perception->OnTargetPerceptionUpdated.Broadcast(Player, HearingLost);
            Perception->OnTargetPerceptionUpdated.Broadcast(Player, SightLost);
            Perception->OnTargetPerceptionUpdated.Broadcast(Player, SightSuccess);
            // Same stimuli for a non-player must produce no logging event.
            Perception->OnTargetPerceptionUpdated.Broadcast(Ghost, HearingSuccess);
        }
        Settings->ControllerClassPath = SavedControllerPath;
        Session->Sample(Ghost, Player, TEXT("Patrol"), TEXT("SyntheticSmokeNode"), false, false);
        Session->Event(Ghost, Player, TEXT("EncounterStart"), TEXT("Hearing"), TEXT("synthetic,quoted \"smoke\"\nmultiline"), false, true, 1);
        Session->Sample(Ghost, nullptr, TEXT("Unknown"), TEXT("SyntheticMissingPlayer"), false, false);
        APatrolBiasZone* Zone = World->SpawnActor<APatrolBiasZone>();
        Zone->SetActorLocation(FVector(500., 600., 400.));
        Zone->ZoneId = TEXT("smoke_zone");
        Zone->CandidateOffsets.Add(FVector(100., 100., -100.));
        Verify(Zone->Contains(Zone->Candidate(0)) && !Zone->Contains(FVector(1500., 600., 400.)), TEXT("zone candidate transform and half open bounds"));
        Verify(Zone->Bounds->GetCollisionEnabled() == ECollisionEnabled::NoCollision, TEXT("zones do not obstruct gameplay"));
        FPatrolLogData BiasLog;
        BiasLog.bEnabled = true;
        BiasLog.Mode = TEXT("zone");
        BiasLog.Zone = Zone->ZoneId.ToString();
        BiasLog.Target = Zone->Candidate(0);
        BiasLog.bHasTarget = true;
        BiasLog.ContactGap = 45.;
        Session->Event(Ghost, Player, TEXT("PatrolTargetSelected"), TEXT(""), TEXT("synthetic_bias_smoke"), false, false, 0, nullptr, &BiasLog);
        Session->StopRecording(TEXT("smoke_complete"));
        const FString Directory = Session->GetRunDirectory();
        FString Samples, Events;
        Verify(FFileHelper::LoadFileToString(Samples, *FPaths::Combine(Directory, TEXT("samples.csv"))) && Samples.Contains(TEXT("123.000000,-456.000000,78.000000")), TEXT("sample positions serialized"));
        Verify(FFileHelper::LoadFileToString(Events, *FPaths::Combine(Directory, TEXT("events.csv"))) && Events.Contains(TEXT("SessionEnd")), TEXT("end flush includes session end"));
        UE_LOG(LogTemp, Display, TEXT("TELEMETRY_SMOKE_DIRECTORY %s"), *Directory);
        // Main-style world: merely loading a sublevel must not start recording.
        const FString SavedTarget = Settings->TargetMap;
        const bool SavedEnabled = Settings->bEnabled;
        Settings->bEnabled = true;
        Settings->TargetMap = TEXT("/Temp/Day_4StreamingSmoke");
        ULevel* Streamed = NewObject<ULevel>(CreatePackage(TEXT("/Temp/UEDPIE_0_Day_4StreamingSmoke")), TEXT("TestLevel"));
        World->AddLevel(Streamed);
        World->BeginPlay();
        // This transient world has no GameMode/GameState to set the play flag.
        World->SetBegunPlay(true);
        Streamed->bIsVisible = false;
        Session->Tick(0.f);
        Verify(!Session->IsRecording(), TEXT("hidden streamed target does not record"));
        Streamed->bIsVisible = true;
        Session->Tick(0.f);
        Verify(Session->IsRecording(), TEXT("visible PIE streamed target starts recording"));
        const FString FirstStreamRun = Session->GetRunDirectory();
        Streamed->bIsVisible = false;
        Session->Tick(0.f);
        Verify(!Session->IsRecording(), TEXT("hidden target stops recording"));
        FString StreamEvents;
        Verify(FFileHelper::LoadFileToString(StreamEvents, *FPaths::Combine(FirstStreamRun, TEXT("events.csv"))) &&
            StreamEvents.Contains(TEXT("SessionEnd")) && StreamEvents.Contains(TEXT("target_level_inactive")), TEXT("stream exit flushes end event"));
        Streamed->bIsVisible = true;
        Session->Tick(0.f);
        Verify(Session->IsRecording() && Session->GetRunDirectory() != FirstStreamRun, TEXT("stream reentry creates new session"));
        World->RemoveLevel(Streamed);
        Session->Tick(0.f);
        Verify(!Session->IsRecording(), TEXT("unloaded target stops recording"));
        Settings->TargetMap = SavedTarget;
        Settings->bEnabled = SavedEnabled;
    }
    World->DestroyWorld(false);
    GEngine->DestroyWorldContext(World);
    UE_LOG(LogTemp, Display, TEXT("TELEMETRY_SMOKE_FAILURES %d"), Failures);
    return Failures == 0 ? 0 : 1;
}
