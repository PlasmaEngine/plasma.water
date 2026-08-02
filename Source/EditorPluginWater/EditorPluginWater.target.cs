using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class EditorPluginWaterTarget : TargetRules
{
    public EditorPluginWaterTarget(BuildContext context) : base(context)
    {
        Type = TargetType.SharedLibrary;
        OutputName = "plEditorPluginWater";
        OutputDirectory = PlasmaPackageSdk.PackageBinaryDirectory(context);

        UsePCHFiles = true;
        UseUnityBuild = true;
        UseAdaptiveUnityBuild = true;
        UseIncrementalLinking = true;

        TargetDependencies.Add("WaterPlugin");

        ExtraModules.Add("EditorPluginWaterModule");
    }
}
