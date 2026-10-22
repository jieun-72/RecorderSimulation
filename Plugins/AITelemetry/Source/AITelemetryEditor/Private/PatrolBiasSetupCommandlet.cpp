#include "PatrolBiasSetupCommandlet.h"
#include "PatrolBias.h"
#include "AITelemetry.h"
#include "AIGraphNode.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "Components/BoxComponent.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

namespace PatrolSetup
{
bool Save(UObject* Asset)
{
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    Args.SaveFlags = SAVE_NoError;
    UPackage* Package = Asset->GetOutermost();
    const FString File = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
    return UPackage::SavePackage(Package, Asset, *File, Args);
}
bool Backup(const FString& Package, const FString& Extension, const FString& Directory)
{
    const FString Source = FPackageName::LongPackageNameToFilename(Package, Extension);
    const FString Dest = FPaths::Combine(Directory, FPackageName::GetShortName(Package) + Extension);
    return IFileManager::Get().Copy(*Dest, *Source, false, false) == COPY_OK;
}
int32 ReplaceGraphReference(UEdGraph* Graph, UObject* Old, UObject* Replacement)
{
    int32 Count = 0;
    if (Graph)
        for (UEdGraphNode* Node : Graph->Nodes)
            if (UAIGraphNode* AI = Cast<UAIGraphNode>(Node))
                if (AI->NodeInstance == Old) { AI->NodeInstance = Replacement; ++Count; }
    return Count;
}
}
UPatrolBiasSetupCommandlet::UPatrolBiasSetupCommandlet()
{
    IsClient = false; IsServer = false; IsEditor = true; LogToConsole = true; ShowErrorCount = false;
}
int32 UPatrolBiasSetupCommandlet::Main(const FString& Params)
{
    const bool bVerifyOnly = Params.Contains(TEXT("verify"));
    const FString TreePackage = TEXT("/Game/BP_Object/AIGhost/BT_Ghost");
    const FString MapPackage = TEXT("/Game/Day_4");
    UBehaviorTree* Tree = LoadObject<UBehaviorTree>(nullptr, *TreePackage);
    if (!Tree || !Tree->RootNode || Tree->RootNode->Children.Num() != 3) return 1;
    UBTCompositeNode* Patrol = Tree->RootNode->Children[2].ChildComposite;
    if (!Patrol || Patrol->Children.Num() != 3) return 1;
    UBTTaskNode* Select = Patrol->Children[0].ChildTask;
    UBTTask_MoveTo* Move = Cast<UBTTask_MoveTo>(Patrol->Children[1].ChildTask);
    const bool bInstalled = Select && Select->IsA<UBTTask_SelectPatrolWithBias>() && Move && Move->IsA<UBTTask_PatrolMoveWithLog>();
    if (!bInstalled && (!Select || Select->GetClass()->GetPathName() !=
        TEXT("/Game/BP_Object/AIGhost/BTTask_FindRandomPatrol.BTTask_FindRandomPatrol_C") || !Move)) return 1;
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(MapPackage);
    if (!World || !World->PersistentLevel) return 1;
    FString BackupDirectory;
    if (!bVerifyOnly)
    {
        BackupDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PatrolBiasBackups"),
            FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%S")));
        if (!IFileManager::Get().MakeDirectory(*BackupDirectory, true) ||
            !PatrolSetup::Backup(TreePackage, FPackageName::GetAssetPackageExtension(), BackupDirectory) ||
            !PatrolSetup::Backup(MapPackage, FPackageName::GetMapPackageExtension(), BackupDirectory)) return 1;
        UE_LOG(LogTemp, Display, TEXT("PATROL_BIAS_BACKUP %s"), *BackupDirectory);
        if (!bInstalled)
        {
            UBTTask_SelectPatrolWithBias* NewSelect = NewObject<UBTTask_SelectPatrolWithBias>(Tree, NAME_None, RF_Transactional);
            UBTTask_PatrolMoveWithLog* NewMove = NewObject<UBTTask_PatrolMoveWithLog>(Tree, NAME_None, RF_Transactional);
            // Preserve every reflected engine MoveTo property, including its Blackboard selector.
            for (TFieldIterator<FProperty> It(UBTTask_MoveTo::StaticClass()); It; ++It)
                It->CopyCompleteValue_InContainer(NewMove, Move);
            NewSelect->InitializeFromAsset(*Tree);
            NewSelect->InitializeNode(Patrol, Select->GetExecutionIndex(), 0, Select->GetTreeDepth());
            NewMove->InitializeFromAsset(*Tree);
            NewMove->InitializeNode(Patrol, Move->GetExecutionIndex(), 0, Move->GetTreeDepth());
            if (PatrolSetup::ReplaceGraphReference(Cast<UEdGraph>(Tree->BTGraph), Select, NewSelect) != 1 ||
                PatrolSetup::ReplaceGraphReference(Cast<UEdGraph>(Tree->BTGraph), Move, NewMove) != 1) return 1;
            Patrol->Children[0].ChildTask = NewSelect;
            Patrol->Children[1].ChildTask = NewMove;
            Tree->MarkPackageDirty();
            if (!PatrolSetup::Save(Tree)) return 1;
        }
        // Initial broad map regions, not claimed to be room boundaries.
        // Bounds cover the inspected gameplay coordinates; artists can resize them.
        const double XEdges[] = { -6200., -2600., 1200., 4400. };
        const double YEdges[] = { -9000., -6500., -3000. };
        for (int32 X = 0; X < 3; ++X)
            for (int32 Y = 0; Y < 2; ++Y)
            {
                const FName Id(*FString::Printf(TEXT("Day4_Region_%d_%d"), X, Y));
                APatrolBiasZone* Zone = nullptr;
                for (AActor* Actor : World->PersistentLevel->Actors)
                    if (APatrolBiasZone* Existing = Cast<APatrolBiasZone>(Actor))
                        if (Existing->ZoneId == Id) { Zone = Existing; break; }
                if (Zone) continue; // Repeat setup preserves authored edits.
                FActorSpawnParameters Spawn;
                Spawn.OverrideLevel = World->PersistentLevel;
                Zone = World->SpawnActor<APatrolBiasZone>(APatrolBiasZone::StaticClass(),
                    FVector((XEdges[X] + XEdges[X+1]) / 2., (YEdges[Y] + YEdges[Y+1]) / 2., 400.), FRotator::ZeroRotator, Spawn);
                if (!Zone) return 1;
                Zone->ZoneId = Id;
                Zone->SetActorLabel(Id.ToString());
                Zone->SetFolderPath(TEXT("AI/PatrolBiasZones"));
                Zone->Bounds->SetBoxExtent(FVector((XEdges[X+1] - XEdges[X]) / 2., (YEdges[Y+1] - YEdges[Y]) / 2., 800.));
                for (double PX = XEdges[X] + 300.; PX < XEdges[X+1]; PX += 600.)
                    for (double PY = YEdges[Y] + 300.; PY < YEdges[Y+1]; PY += 600.)
                        Zone->CandidateOffsets.Add(FVector(PX, PY, 250.) - Zone->GetActorLocation());
            }
        World->MarkPackageDirty();
        if (!UEditorLoadingAndSavingUtils::SaveMap(World, MapPackage)) return 1;
    }
    int32 Zones = 0;
    for (AActor* Actor : World->PersistentLevel->Actors)
        if (APatrolBiasZone* Zone = Cast<APatrolBiasZone>(Actor))
        {
            if (Zone->ZoneId.IsNone() || Zone->CandidateOffsets.IsEmpty() ||
                Zone->Bounds->GetCollisionEnabled() != ECollisionEnabled::NoCollision) return 1;
            for (int32 Index = 0; Index < Zone->CandidateOffsets.Num(); ++Index)
                if (!Zone->Contains(Zone->Candidate(Index))) return 1;
            ++Zones;
        }
    if (Zones < 6 || !Patrol->Children[0].ChildTask->IsA<UBTTask_SelectPatrolWithBias>() ||
        !Patrol->Children[1].ChildTask->IsA<UBTTask_PatrolMoveWithLog>()) return 1;
    UE_LOG(LogTemp, Display, TEXT("PATROL_BIAS_SETUP_OK zones=%d verify_only=%d"), Zones, bVerifyOnly);
    return 0;
}
