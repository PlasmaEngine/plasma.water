#pragma once

#include <Foundation/Threading/Mutex.h>
#include <RendererCore/Pipeline/Renderer.h>
#include <RendererCore/Shader/ConstantBufferStorage.h>
#include <RendererCore/Shader/ShaderResource.h>
#include <WaterPlugin/WaterPluginDLL.h>

class PL_WATER_DLL plWaterRenderer : public plRenderer
{
  PL_ADD_DYNAMIC_REFLECTION(plWaterRenderer, plRenderer);
  PL_DISALLOW_COPY_AND_ASSIGN(plWaterRenderer);

public:
  plWaterRenderer();
  ~plWaterRenderer();

  virtual void GetSupportedRenderDataTypes(plHybridArray<const plRTTI*, 8>& ref_types) const override;
  virtual void GetSupportedRenderDataCategories(plHybridArray<plRenderData::Category, 8>& ref_categories) const override;
  virtual void RenderBatch(const plRenderViewContext& renderViewContext, const plRenderPipelinePass* pPass, const plRenderDataBatch& batch) const override;

private:
  mutable plMutex m_RenderMutex;
  plShaderResourceHandle m_hShader;
  plConstantBufferStorageHandle m_hJunctionConstants;
};
