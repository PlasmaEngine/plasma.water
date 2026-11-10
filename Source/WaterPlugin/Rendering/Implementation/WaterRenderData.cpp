#include <WaterPlugin/WaterPluginPCH.h>

#include <RendererCore/Pipeline/SortingFunctions.h>
#include <WaterPlugin/Rendering/WaterRenderData.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plWaterRenderData, 1, plRTTIDefaultAllocator<plWaterRenderData>)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

// Back to front within a layer, and the layers in order. Water does not write depth, so what is drawn last shows:
// a river's apron has to come after the ocean it lies on, from wherever it is looked at.
static plUInt64 plWaterSortingKey(const plRenderData* pRenderData, const plCamera& camera)
{
  const plUInt64 uiBackToFront = plRenderSortingFunctions::BackToFrontThenByRenderData(pRenderData, camera);
  const plUInt64 uiLayer = static_cast<const plWaterRenderData*>(pRenderData)->m_uiSortLayer & 0x3u;
  return (uiBackToFront >> 2) | (uiLayer << 62);
}

plRenderData::Category plWaterRenderData::WaterSurface =
  plRenderData::RegisterCategory("WaterSurface", &plWaterSortingKey);

PL_STATICLINK_FILE(WaterPlugin, WaterPlugin_Rendering_Implementation_WaterRenderData);
