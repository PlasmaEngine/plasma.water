using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class WaterPluginTarget : TargetRules
{
    public WaterPluginTarget(BuildContext context) : base(context)
    {
        Type = TargetType.SharedLibrary;
        OutputName = "plWaterPlugin";
        OutputDirectory = PlasmaPackageSdk.PackageBinaryDirectory(context);

        UsePCHFiles = true;
        UseUnityBuild = true;
        UseAdaptiveUnityBuild = true;
        UseIncrementalLinking = true;

        ExtraModules.Add("WaterPluginModule");
    }
}
