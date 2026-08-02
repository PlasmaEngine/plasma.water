#pragma once

#include <RendererCore/Pipeline/RenderPipelinePass.h>
#include <RendererCore/Shader/ConstantBufferStorage.h>
#include <RendererCore/Shader/ShaderResource.h>
#include <WaterPlugin/Rendering/WaterQuality.h>
#include <WaterPlugin/WaterPluginDLL.h>

/// \brief Network-aware underwater HDR post effect. It consumes an optional finite-volume exit depth.
class PL_WATER_DLL plWaterUnderwaterPass : public plRenderPipelinePass
{
  PL_ADD_DYNAMIC_REFLECTION(plWaterUnderwaterPass, plRenderPipelinePass);

public:
  plWaterUnderwaterPass();
  ~plWaterUnderwaterPass();

  virtual plStatus AddRenderPasses(const plViewData& viewData, const plCamera& camera, plRenderGraph& ref_graph, const plArrayPtr<const plRenderPipelinePinConnection> inputs, plArrayPtr<plRenderPipelinePinConnection> outputs) override;
  virtual plResult Serialize(plStreamWriter& inout_stream) const override;
  virtual plResult Deserialize(plStreamReader& inout_stream) override;

private:
  plRenderPipelineNodeInputPin m_PinColor;
  plRenderPipelineNodeInputPin m_PinDepth;
  plRenderPipelineNodeInputPin m_PinWaterExitDepth;
  plRenderPipelineNodeOutputPin m_PinOutput;

  plEnum<plWaterQualityTier> m_Quality = plWaterQualityTier::High;
  float m_fDensity = 0.08f;
  float m_fDistortionStrength = 0.003f;
  float m_fCausticIntensity = 0.35f;
  float m_fLightShaftIntensity = 0.2f;

  plShaderResourceHandle m_hShader;
  plConstantBufferStorageHandle m_hConstants;
};
