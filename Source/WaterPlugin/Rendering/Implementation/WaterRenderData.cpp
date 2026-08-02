#include <WaterPlugin/WaterPluginPCH.h>

#include <RendererCore/Pipeline/SortingFunctions.h>
#include <WaterPlugin/Rendering/WaterRenderData.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plWaterRenderData, 1, plRTTIDefaultAllocator<plWaterRenderData>)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plRenderData::Category plWaterRenderData::WaterSurface =
  plRenderData::RegisterCategory("WaterSurface", &plRenderSortingFunctions::BackToFrontThenByRenderData);

PL_STATICLINK_FILE(WaterPlugin, WaterPlugin_Rendering_Implementation_WaterRenderData);
