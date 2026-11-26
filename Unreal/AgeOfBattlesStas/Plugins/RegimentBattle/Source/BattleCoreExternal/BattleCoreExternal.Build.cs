using System;
using System.IO;
using UnrealBuildTool;

public class BattleCoreExternal : ModuleRules
{
    public BattleCoreExternal(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;
        if (Target.Platform != UnrealTargetPlatform.Win64)
        {
            throw new BuildException("Regiment Battle currently supplies a Win64 BattleCore build.");
        }

        string root = Environment.GetEnvironmentVariable("BATTLE_CORE_ROOT");
        if (String.IsNullOrWhiteSpace(root))
        {
            string parent = Path.GetFullPath(Path.Combine(Target.ProjectFile.Directory.FullName, "..", ".."));
            string bundledCore = Path.Combine(parent, "BattleCore");
            root = File.Exists(Path.Combine(bundledCore, "CMakeLists.txt"))
                ? bundledCore : Path.Combine(parent, "September 4", "BattleCore");
        }
        string library = Path.Combine(root, "out", "build", "ue-msvc", "Release", "battle_core.lib");
        if (!File.Exists(library))
        {
            throw new BuildException(
                "Missing {0}. Build battle_core with MSVC into out/build/ue-msvc first.", library);
        }

        string tools = Path.Combine(root, "out", "build", "ue-msvc", "Release", "battle_io.lib");
        if (!File.Exists(tools))
        {
            throw new BuildException("Missing {0}. Build the BattleCore battle_io target with MSVC first.", tools);
        }
        PublicSystemIncludePaths.Add(Path.Combine(root, "include"));
        PublicSystemIncludePaths.Add(Path.Combine(root, "tools"));
        PublicAdditionalLibraries.Add(library);
        PublicAdditionalLibraries.Add(tools);
    }
}
