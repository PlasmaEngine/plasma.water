#include <WaterPlugin/WaterPluginPCH.h>

#include <RendererCore/Pipeline/SortingFunctions.h>
#include <WaterPlugin/Rendering/WaterRenderData.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plWaterRenderData, 1, plRTTIDefaultAllocator<plWaterRenderData>)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plUInt64 plWaterRenderData::GetSortingKey(const plRenderData* pRenderData, const plCamera& camera)
{
  const plUInt64 uiBackToFront = plRenderSortingFunctions::BackToFrontThenByRenderData(pRenderData, camera);
  const plUInt64 uiLayer = static_cast<const plWaterRenderData*>(pRenderData)->m_uiSortLayer & 0x3u;
  return (uiBackToFront >> 2) | (uiLayer << 62);
}

plRenderData::Category plWaterRenderData::WaterSurface =
  plRenderData::RegisterCategory("WaterSurface", &plWaterRenderData::GetSortingKey);

PL_STATICLINK_FILE(WaterPlugin, WaterPlugin_Rendering_Implementation_WaterRenderData);
