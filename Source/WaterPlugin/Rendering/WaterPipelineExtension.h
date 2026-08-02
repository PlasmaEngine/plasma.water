#pragma once

#include <RendererCore/Pipeline/RenderPipelineExtension.h>
#include <WaterPlugin/WaterPluginDLL.h>

/// \brief Reserved Water pipeline extension point. Pipeline assets remain entirely author-owned.
class PL_WATER_DLL plWaterPipelineExtension : public plRenderPipelineExtension
{
  PL_ADD_DYNAMIC_REFLECTION(plWaterPipelineExtension, plRenderPipelineExtension);

public:
  virtual plResult Apply(plRenderPipeline& ref_pipeline) const override;
};
