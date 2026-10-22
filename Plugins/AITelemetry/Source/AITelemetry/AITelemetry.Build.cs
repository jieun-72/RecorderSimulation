using UnrealBuildTool;
public class AITelemetry : ModuleRules
{
    public AITelemetry(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "AIModule", "DeveloperSettings", "NavigationSystem" });
    }
}
