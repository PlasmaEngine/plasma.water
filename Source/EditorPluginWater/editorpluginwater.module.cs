using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class EditorPluginWaterModule : ModuleRules
{
    public EditorPluginWaterModule(BuildContext context) : base(context)
    {
        PlasmaPackageSdk.ConfigurePackageModule(this, context, "Source/EditorPluginWater",
            "EditorPluginWaterPCH.h", "BUILDSYSTEM_BUILDING_EDITORPLUGINWATER_LIB");

        PlasmaPackageSdk.ConfigureEditorSdkConsumer(this, context);
        PlasmaPackageSdk.AddPackagePluginImport(this, context, "Source/WaterPlugin", "plWaterPlugin");

        // The Water Profile docks are engine-drawn: EditorUi and the retained UI runtime.
        PlasmaPackageSdk.AddImportLibrary(this, context, "plEditorUi");
        PlasmaPackageSdk.AddImportLibrary(this, context, "plGameUiPlugin");
    }
}
