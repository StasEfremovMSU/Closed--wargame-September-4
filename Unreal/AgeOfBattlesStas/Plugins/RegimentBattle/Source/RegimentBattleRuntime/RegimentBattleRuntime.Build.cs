using UnrealBuildTool;

public class RegimentBattleRuntime : ModuleRules
{
    public RegimentBattleRuntime(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "MassEntity" });
        PrivateDependencyModuleNames.AddRange(new[] { "BattleCoreExternal", "MassCore", "Slate", "SlateCore", "InputCore", "Json",
            "GeometryCore", "GeometryFramework", "MeshDescription", "StaticMeshDescription" });
        if (Target.bBuildEditor)
            PrivateDependencyModuleNames.Add("ToolMenus");
    }
}
