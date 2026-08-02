#include <WaterPlugin/WaterPluginPCH.h>

#include <RendererCore/Pipeline/ExtractedRenderData.h>
#include <WaterPlugin/Rendering/WaterFrameData.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plWaterFrameData, 1, plRTTIDefaultAllocator<plWaterFrameData>)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plWaterFrameDataProvider, 1, plRTTIDefaultAllocator<plWaterFrameDataProvider>)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

void* plWaterFrameDataProvider::UpdateData(const plRenderViewContext& renderViewContext, const plExtractedRenderData& extractedData)
{
  PL_IGNORE_UNUSED(renderViewContext);

  if (const plWaterFrameData* pFrameData = extractedData.GetFrameData<plWaterFrameData>())
  {
    return const_cast<plWaterFrameData*>(pFrameData);
  }

  return &m_FallbackData;
}

PL_STATICLINK_FILE(WaterPlugin, WaterPlugin_Rendering_Implementation_WaterFrameData);
