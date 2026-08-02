using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class WaterPluginModule : ModuleRules
{
    public WaterPluginModule(BuildContext context) : base(context)
    {
        PlasmaPackageSdk.ConfigurePackageModule(this, context, "Source/WaterPlugin", "WaterPluginPCH.h",
            "BUILDSYSTEM_BUILDING_WATERPLUGIN_LIB");
    }
}
