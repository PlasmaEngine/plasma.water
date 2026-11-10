// #pl-version 1

using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class PlasmaWaterWorkspace : WorkspaceRules
{
    public PlasmaWaterWorkspace(BuildContext context) : base(context)
    {
        TargetNames.Add("WaterPlugin");
        TargetNames.Add("EnginePluginWater");
        TargetNames.Add("EditorPluginWater");
        TargetNames.Add("WaterTest");
    }
}
