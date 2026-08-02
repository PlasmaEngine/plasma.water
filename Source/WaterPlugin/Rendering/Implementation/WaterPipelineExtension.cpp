#include <WaterPlugin/WaterPluginPCH.h>

#include <WaterPlugin/Rendering/WaterPipelineExtension.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plWaterPipelineExtension, 1, plRTTIDefaultAllocator<plWaterPipelineExtension>)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plResult plWaterPipelineExtension::Apply(plRenderPipeline& ref_pipeline) const
{
  // Pipeline assets are author-owned. Water never adds passes, extractors, or
  // connections at load time; projects opt in by placing the water nodes themselves.
  PL_IGNORE_UNUSED(ref_pipeline);
  return PL_SUCCESS;
}

PL_STATICLINK_FILE(WaterPlugin, WaterPlugin_Rendering_Implementation_WaterPipelineExtension);
