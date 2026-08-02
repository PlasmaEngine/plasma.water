using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class EnginePluginWaterTarget : TargetRules
{
    public EnginePluginWaterTarget(BuildContext context) : base(context)
    {
        Type = TargetType.SharedLibrary;
        OutputName = "plEnginePluginWater";
        OutputDirectory = PlasmaPackageSdk.PackageBinaryDirectory(context);

        UsePCHFiles = true;
        UseUnityBuild = true;
        UseAdaptiveUnityBuild = true;
        UseIncrementalLinking = true;

        TargetDependencies.Add("WaterPlugin");

        ExtraModules.Add("EnginePluginWaterModule");
    }
}
