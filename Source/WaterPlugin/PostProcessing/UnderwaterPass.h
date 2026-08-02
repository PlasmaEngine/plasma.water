#pragma once

#include <WaterPlugin/WaterPluginDLL.h>
#include <RendererCore/Pipeline/RenderPipelinePass.h>
#include <RendererCore/Shader/ConstantBufferStorage.h>
#include <RendererCore/Shader/ShaderResource.h>

/// \brief Post-process pass that applies underwater effects when the camera is below the water surface.
/// 
/// This pass calculates the water height at the camera position using the same wave function
/// as OceanSurface, and applies fog, distortion, caustics, and light shafts when submerged.
class PL_WATER_DLL plUnderwaterPass : public plRenderPipelinePass
{
  PL_ADD_DYNAMIC_REFLECTION(plUnderwaterPass, plRenderPipelinePass);

public:
  plUnderwaterPass();
  ~plUnderwaterPass();

  virtual plStatus AddRenderPasses(const plViewData& viewData, const plCamera& camera, plRenderGraph& ref_graph, const plArrayPtr<const plRenderPipelinePinConnection> inputs, plArrayPtr<plRenderPipelinePinConnection> outputs) override;

  virtual plResult Serialize(plStreamWriter& inout_stream) const override;
  virtual plResult Deserialize(plStreamReader& inout_stream) override;

protected:
  plRenderPipelineNodeInputPin m_PinColorInput;
  plRenderPipelineNodeInputPin m_PinDepthInput;
  plRenderPipelineNodeOutputPin m_PinOutput;

  // Water surface parameters - must match OceanSurface material
  float m_fWaterBaseHeight = 0.0f;
  float m_fWaveSpeed = 1.0f;
  float m_fWaveAmplitude = 1.5f;
  float m_fWaveFrequency = 0.15f;
  float m_fWaveDrag = 0.38f;
  plInt32 m_iWaveIterations = 24;

  // Fog settings
  plColor m_FogColor = plColor(0.02f, 0.08f, 0.15f, 1.0f);
  plVec3 m_vWaterAbsorption = plVec3(0.45f, 0.09f, 0.06f);
  float m_fFogDensity = 0.15f;
  float m_fMaxUnderwaterDepth = 50.0f;

  // Distortion settings
  float m_fDistortionStrength = 1.0f;
  float m_fDistortionSpeed = 1.0f;
  float m_fChromaticAberration = 0.01f;

  // Caustics
  float m_fCausticScale = 0.5f;
  float m_fCausticSpeed = 1.0f;
  float m_fCausticIntensity = 0.3f;

  // Light shafts
  float m_fLightShaftIntensity = 0.2f;
  float m_fLightShaftFalloff = 0.5f;
  float m_fVignetteStrength = 0.3f;

  // Migration bridge: when modern water frame data is present it supplies optical/light values. The serialized
  // inline values above remain the fallback profile for old scenes and custom pipelines.
  bool m_bUseWaterFrameData = true;

  plConstantBufferStorageHandle m_hConstantBuffer;
  plShaderResourceHandle m_hShader;
};
