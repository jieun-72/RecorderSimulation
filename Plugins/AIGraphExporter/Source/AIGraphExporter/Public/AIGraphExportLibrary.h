#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Commandlets/Commandlet.h"
#include "AIGraphExportLibrary.generated.h"

/** Editor-only, read-only asset inspection. Only JSON files under Saved/AIExports are written. */
UCLASS()
class AIGRAPHEXPORTER_API UAIGraphExportLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Returns a JSON status with output paths and errors. Does not compile or save assets. */
    UFUNCTION(BlueprintCallable, Category="AI Graph Export")
    static FString ExportGhostGraphs();

    UFUNCTION(BlueprintCallable, Category="AI Graph Export")
    static FString ExportBlueprintGraph(const FString& AssetPath);

    UFUNCTION(BlueprintCallable, Category="AI Graph Export")
    static FString ExportBehaviorTree(const FString& AssetPath);
};

/** UnrealEditor-Cmd Project.uproject -run=AIGraphExport -unattended -nullrhi */
UCLASS()
class AIGRAPHEXPORTER_API UAIGraphExportCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UAIGraphExportCommandlet();
    virtual int32 Main(const FString& Params) override;
};
