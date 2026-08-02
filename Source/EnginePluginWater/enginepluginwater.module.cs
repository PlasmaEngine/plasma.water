using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class EnginePluginWaterModule : ModuleRules
{
    public EnginePluginWaterModule(BuildContext context) : base(context)
    {
        PlasmaPackageSdk.ConfigurePackageModule(this, context, "Source/EnginePluginWater",
            "EnginePluginWaterPCH.h", "BUILDSYSTEM_BUILDING_ENGINEPLUGINWATER_LIB");

        PlasmaPackageSdk.ConfigureEditorSdkConsumer(this, context);
        PlasmaPackageSdk.AddPackagePluginImport(this, context, "Source/WaterPlugin", "plWaterPlugin");
    }
}
