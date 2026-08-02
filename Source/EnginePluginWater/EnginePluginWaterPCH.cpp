#include <EnginePluginWater/EnginePluginWaterPCH.h>

PL_STATICLINK_LIBRARY(EnginePluginWater)
{
  if (bReturn)
    return;

  PL_STATICLINK_REFERENCE(EnginePluginWater_WaterProfileAsset_WaterProfileContext);
  PL_STATICLINK_REFERENCE(EnginePluginWater_WaterProfileAsset_WaterProfileView);
}
