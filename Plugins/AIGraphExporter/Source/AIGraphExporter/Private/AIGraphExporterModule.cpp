#include "AIGraphExportLibrary.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"

class FAIGraphExporterModule : public IModuleInterface
{
    IConsoleObject* ExportCommand = nullptr;
public:
    virtual void StartupModule() override
    {
        ExportCommand = IConsoleManager::Get().RegisterConsoleCommand(
            TEXT("AIExports.ExportGhostGraphs"),
            TEXT("Read AIC_Ghost and BT_Ghost and write JSON to Saved/AIExports. Never saves assets."),
            FConsoleCommandDelegate::CreateLambda([]()
            {
                UE_LOG(LogTemp, Display, TEXT("%s"), *UAIGraphExportLibrary::ExportGhostGraphs());
            }), ECVF_Default);
    }
    virtual void ShutdownModule() override
    {
        if (ExportCommand)
        {
            IConsoleManager::Get().UnregisterConsoleObject(ExportCommand);
            ExportCommand = nullptr;
        }
    }
};

IMPLEMENT_MODULE(FAIGraphExporterModule, AIGraphExporter)
