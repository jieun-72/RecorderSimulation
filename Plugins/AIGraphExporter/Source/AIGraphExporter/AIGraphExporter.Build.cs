using UnrealBuildTool;

public class AIGraphExporter : ModuleRules
{
    public AIGraphExporter(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new[] { "AIModule", "BlueprintGraph", "Json", "UnrealEd" });
    }
}
