using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class WaterPluginModule : ModuleRules
{
    public WaterPluginModule(BuildContext context) : base(context)
    {
        PlasmaPackageSdk.ConfigurePackageModule(this, context, "Source/WaterPlugin", "WaterPluginPCH.h",
            "BUILDSYSTEM_BUILDING_WATERPLUGIN_LIB");

        // The constant-buffer headers are shared between C++ and HLSL, so both have to reach them
        // by the same name. Assets/ carries this package's own; the SDK's Data/Base carries the
        // engine's, which water's passes still include.
        PrivateIncludePaths.Add(System.IO.Path.Combine(context.ProjectRoot, "Assets"));
        PrivateIncludePaths.Add(System.IO.Path.Combine(PlasmaPackageSdk.Root, "Data", "Base"));
    }
}
