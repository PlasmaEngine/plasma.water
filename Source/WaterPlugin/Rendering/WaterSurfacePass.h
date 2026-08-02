#pragma once

#include <RendererCore/Pipeline/Passes/ForwardRenderPass.h>
#include <RendererCore/Shader/ConstantBufferStorage.h>
#include <RendererCore/Shader/ShaderResource.h>
#include <WaterPlugin/Rendering/WaterGpuSimulation.h>
#include <WaterPlugin/Rendering/WaterQuality.h>
#include <WaterPlugin/WaterPluginDLL.h>

/// \brief Renders water attributes and the final water surface before ordinary transparency.
class PL_WATER_DLL plWaterSurfacePass : public plForwardRenderPass
{
  PL_ADD_DYNAMIC_REFLECTION(plWaterSurfacePass, plForwardRenderPass);

public:
  plWaterSurfacePass();
  ~plWaterSurfacePass();

  virtual plStatus AddRenderPasses(const plViewData& viewData, const plCamera& camera, plRenderGraph& ref_graph, const plArrayPtr<const plRenderPipelinePinConnection> inputs, plArrayPtr<plRenderPipelinePinConnection> outputs) override;
  virtual plStatus AddRenderPassesInactive(const plViewData& viewData, const plCamera& camera, plRenderGraph& ref_graph, const plArrayPtr<const plRenderPipelinePinConnection> inputs, plArrayPtr<plRenderPipelinePinConnection> outputs) override;
  virtual plResult Serialize(plStreamWriter& inout_stream) const override;
  virtual plResult Deserialize(plStreamReader& inout_stream) override;

  /// Returns the registered simulation slot, or plInvalidIndex when the profile must use analytic waves.
  plUInt32 GetGpuProfileSlot(plUInt32 uiProfileIndex) const;

  /// \brief Number of array layers in the spectrum displacement texture, 0 when the GPU simulation is unavailable.
  plUInt32 GetSpectrumLayerCount() const
  {
    return (m_bSimulationInitialized && m_GpuSimulation.HasValidSpectrum() && !m_GpuSimulation.GetSpectrumDisplacement().IsInvalidated())
             ? plWaterGpuSimulation::MaxSpectrumProfiles * m_SimulationQuality.m_uiSpectrumCascadeCount
             : 0;
  }

  /// \brief Dominant directional light of the current frame, used to light the water tint analytically.
  /// Screen-space and virtual shadow maps are derived from the opaque depth buffer and report water
  /// as fully shadowed, so the water body color cannot rely on the clustered lighting result.
  plVec3 GetMainLightDirection() const { return m_vMainLightDirection; }
  plVec3 GetMainLightColorIntensity() const { return m_vMainLightColorIntensity; }

  /// \brief Resolved settings used by renderers attached to this pass.
  const plWaterQualitySettings& GetQualitySettings() const { return m_SimulationQuality; }

  /// \brief Currently valid interaction-atlas window: xy = world-space center (camera anchor),
  /// z = inner half extent in meters, w = one while the atlas contains live data. The atlas wraps world space with a 100 m period;
  /// samples outside this window must read as 'no interaction'.
  plVec4 GetInteractionWindowParams() const { return m_vInteractionWindowParams; }

  bool GetActiveInteractionBounds(plVec4& out_bounds) const { return m_GpuSimulation.GetActiveInteractionBounds(out_bounds); }

  /// The finite water body containing the camera. The volume exit-depth shader uses this to reject
  /// unrelated bodies; plInvalidIndex means that no underwater body is active for this view.
  plUInt32 GetActiveUnderwaterBodyId() const { return m_uiActiveUnderwaterBodyId; }

protected:
  virtual void DeclareRenderObjectDependencies(plRenderGraph& ref_graph, plRenderGraphPassBuilder& ref_pass) override;
  virtual void RenderObjects(const plRenderViewContext& renderViewContext) override;

private:
  void FetchPerFrameWaterData(const plRenderViewContext& renderViewContext);
  void RefreshGpuSpectrumProfiles();

  plRenderPipelineNodeInputPin m_PinResolvedDepth;
  plRenderPipelineNodeInputPin m_PinGBuffer1;
  plRenderPipelineNodeOutputPin m_PinWaterDepth;
  plRenderPipelineNodeOutputPin m_PinWaterExitDepth;

  plEnum<plWaterQualityTier> m_Quality = plWaterQualityTier::High;

  plWaterGpuSimulation m_GpuSimulation;
  plWaterQualitySettings m_SimulationQuality;
  bool m_bSimulationInitialized = false;
  plStaticArray<plUInt32, plWaterGpuSimulation::MaxSpectrumProfiles> m_GpuProfileIds;
  plStaticArray<plUInt32, plWaterGpuSimulation::MaxSpectrumProfiles> m_GpuProfileSlots;

  plVec3 m_vMainLightDirection = plVec3(0.0f, 0.0f, 1.0f);
  plVec3 m_vMainLightColorIntensity = plVec3::MakeZero();
  plVec4 m_vInteractionWindowParams = plVec4::MakeZero();
  plUInt32 m_uiActiveUnderwaterBodyId = plInvalidIndex;
  plVec4 m_vCausticFieldCenterAndExtent = plVec4::MakeZero();
  plVec4 m_vCausticFallbackBounds = plVec4::MakeZero();
  plVec4 m_vCausticFallbackCenterHeightValidity = plVec4::MakeZero();
  plVec4 m_vCausticFallbackWaveParams = plVec4::MakeZero();
  plVec4 m_vCausticFallbackWindTime = plVec4::MakeZero();
  plVec2 m_vCausticFallbackStrengths = plVec2::MakeZero();
  bool m_bHasTransmittedCausticSources = false;
  bool m_bHasReflectedCausticSources = false;
  bool m_bCausticTraceExecuted = false;

  plShaderResourceHandle m_hCausticsShader;
  plShaderResourceHandle m_hCausticsCompositeShader;
  plConstantBufferStorageHandle m_hCausticsConstants;
  plConstantBufferStorageHandle m_hCausticFieldConstants;
};
