using UnrealBuildTool;
public class AITelemetryEditor : ModuleRules
{
    public AITelemetryEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "AITelemetry", "AIModule", "UnrealEd", "Json", "AIGraph", "NavigationSystem" });
    }
}
