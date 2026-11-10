using System.IO;
using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class WaterTestTarget : TargetRules
{
    public WaterTestTarget(BuildContext context) : base(context)
    {
        Type = TargetType.Executable;
        OutputName = "WaterTest";
        // Kept out of the package binary directory, which is what installs and publishes draw from. The
        // test finds the engine libraries through its library path and the package plugin through a
        // plugin search root.
        OutputDirectory = Path.Combine(context.ProjectRoot, "Bin", "Tests", $"{context.Platform}_x64_{context.Configuration}");
        UsePCHFiles = true;
        UseUnityBuild = false; // Test fixtures reuse translation-unit-local helper names.
        UseIncrementalLinking = true;
        GenerateDebugInfo = true;

        TargetDependencies.Add("WaterPlugin");
        ExtraModules.Add("WaterTestModule");
    }
}
