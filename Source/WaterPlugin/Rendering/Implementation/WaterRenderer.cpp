#include <WaterPlugin/WaterPluginPCH.h>

#include <Foundation/Configuration/CVar.h>
#include <RendererCore/Pipeline/RenderDataBatch.h>
#include <RendererCore/RenderContext/BindGroupBuilder.h>
#include <RendererCore/RenderContext/RenderContext.h>
#include <RendererFoundation/Device/Device.h>
#include <RendererFoundation/Resources/DynamicBuffer.h>
#include <WaterPlugin/Rendering/WaterGpuSimulation.h>
#include <WaterPlugin/Rendering/WaterRenderData.h>
#include <WaterPlugin/Rendering/WaterRenderer.h>
#include <WaterPlugin/Rendering/WaterSurfacePass.h>

#include <Shaders/Water/WaterDrawConstants.h>
#include <Shaders/Water/WaterJunctionConstants.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plWaterRenderer, 1, plRTTIDefaultAllocator<plWaterRenderer>)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plCVarInt cvar_WaterDebugJunctionPatches("Water.Debug.JunctionPatches", 0, plCVarFlags::Default,
  "Visualizes junction blend patches: 0 = off, 1 = blend gradient (occlusion clip active), 2 = blend gradient bypassing the occlusion clip.");

plCVarBool cvar_WaterDebugNoTessellation("Water.Debug.NoTessellation", false, plCVarFlags::Default,
  "Draws water without hull and domain stages on every tier. For finding out what tessellation costs.");

plWaterRenderer::plWaterRenderer()
{
  m_hShader = plResourceManager::LoadResource<plShaderResource>("Shaders/Water/WaterSurfaceModern.plShader");
  m_hJunctionConstants = plRenderContext::CreateConstantBufferStorage<plWaterJunctionConstants>();
}

plWaterRenderer::~plWaterRenderer()
{
  plRenderContext::DeleteConstantBufferStorage(m_hJunctionConstants);
}

void plWaterRenderer::GetSupportedRenderDataTypes(plHybridArray<const plRTTI*, 8>& ref_types) const
{
  ref_types.PushBack(plGetStaticRTTI<plWaterRenderData>());
}

void plWaterRenderer::GetSupportedRenderDataCategories(plHybridArray<plRenderData::Category, 8>& ref_categories) const
{
  ref_categories.PushBack(plWaterRenderData::WaterSurface);
}

void plWaterRenderer::RenderBatch(const plRenderViewContext& renderViewContext, const plRenderPipelinePass* pPass, const plRenderDataBatch& batch) const
{
  PL_LOCK(m_RenderMutex);

  plRenderContext* pContext = renderViewContext.m_pRenderContext;
  const plWaterSurfacePass* pWaterPass = pPass != nullptr && pPass->GetDynamicRTTI()->IsDerivedFrom<plWaterSurfacePass>()
                                           ? static_cast<const plWaterSurfacePass*>(pPass)
                                           : nullptr;

  for (auto it = batch.GetIterator<plWaterRenderData>(0, batch.GetDataCount()); it.IsValid(); ++it)
  {
    const plWaterRenderData* pRenderData = it;
    const plWaterQualitySettings* pQuality = pWaterPass != nullptr ? &pWaterPass->GetQualitySettings() : nullptr;
    const plGALDeviceCapabilities& capabilities = plGALDevice::GetDefaultDevice()->GetCapabilities();
    const bool bTessellationSupported = capabilities.m_bShaderStageSupported[plGALShaderStage::HullShader] &&
                                        capabilities.m_bShaderStageSupported[plGALShaderStage::DomainShader];
    const bool bSurfaceGeometry = pRenderData->m_uiGeometryType == 0u || pRenderData->m_uiGeometryType == 1u ||
                                  pRenderData->m_uiGeometryType == 2u || pRenderData->m_uiGeometryType == 5u;
    const bool bUseTessellation = pQuality != nullptr && pQuality->m_Tier != plWaterQualityTier::Low &&
                                  bTessellationSupported && bSurfaceGeometry && !cvar_WaterDebugNoTessellation;

    plVec4 vActiveInteractionBounds = plVec4::MakeZero();
    const bool bInteractionBoundsValid = pWaterPass != nullptr && pWaterPass->GetActiveInteractionBounds(vActiveInteractionBounds);
    plUInt32 uiCellsPerSide = pRenderData->m_uiCellsPerSide;
    if (pWaterPass != nullptr)
    {
      const plWaterQualitySettings& quality = *pQuality;
      if (pRenderData->m_uiGeometryType == 0)
      {
        if (pRenderData->m_uiLod >= quality.m_uiOceanRingCount)
          continue;

        const plUInt32 uiQualityCells = plMath::Max<plUInt32>(
          static_cast<plUInt32>(quality.m_uiOceanCellsPerRing) >> plMath::Min<plUInt8>(pRenderData->m_uiLod, 6), 2u);
        uiCellsPerSide = plMath::Min(uiCellsPerSide, uiQualityCells);

        // Capability fallback for devices without hull/domain stages. Tessellated devices keep the ordinary
        // base grid and generate interaction detail in the domain shader instead of multiplying every cell.
        const plVec2 vPatchMin(pRenderData->m_vPatchOrigin.x, pRenderData->m_vPatchOrigin.y);
        const plVec2 vPatchMax = vPatchMin + plVec2(pRenderData->m_fExtentU, pRenderData->m_fExtentV);
        const bool bTouchesInteraction = bInteractionBoundsValid &&
                                         vPatchMax.x >= vActiveInteractionBounds.x && vPatchMax.y >= vActiveInteractionBounds.y &&
                                         vPatchMin.x <= vActiveInteractionBounds.z && vPatchMin.y <= vActiveInteractionBounds.w;
        if (!bUseTessellation && bTouchesInteraction && pRenderData->m_uiLod <= 1)
        {
          const plUInt32 uiInteractionMultiplier = quality.m_Tier == plWaterQualityTier::Ultra ? 3u : 2u;
          uiCellsPerSide = plMath::Min(uiCellsPerSide * uiInteractionMultiplier, 192u);
        }
      }
    }

    if (pRenderData->m_hFiniteMesh.IsValid())
      pContext->SetShaderPermutationVariable("WATER_GEOMETRY_SOURCE", "WATER_GEOMETRY_SOURCE_FINITE");
    else
      pContext->SetShaderPermutationVariable("WATER_GEOMETRY_SOURCE", "WATER_GEOMETRY_SOURCE_PROCEDURAL");
    pContext->SetShaderPermutationVariable("WATER_USE_TESSELLATION",
      bUseTessellation ? plTempHashedString("TRUE") : plTempHashedString("FALSE"));
    pContext->BindShader(m_hShader);

    plBindGroupBuilder& bindGroup = pContext->GetBindGroup(PL_GAL_BIND_GROUP_DRAW_CALL);

    plUInt32 uiGpuProfileSlot = plInvalidIndex;
    plUInt32 uiSecondaryProfileSlot = plInvalidIndex;
    plUInt32 uiSpectrumLayerCount = 0;
    plVec3 vMainLightDirection = plVec3(0.0f, 0.0f, 1.0f);
    plVec3 vMainLightColorIntensity = plVec3::MakeZero();
    plVec4 vInteractionWindowParams = plVec4::MakeZero();
    if (pWaterPass != nullptr)
    {
      uiGpuProfileSlot = pWaterPass->GetGpuProfileSlot(pRenderData->m_uiProfileIndex);
      if (pRenderData->m_uiSecondaryProfileIndex != plInvalidIndex)
        uiSecondaryProfileSlot = pWaterPass->GetGpuProfileSlot(pRenderData->m_uiSecondaryProfileIndex);
      uiSpectrumLayerCount = pWaterPass->GetSpectrumLayerCount();
      vMainLightDirection = pWaterPass->GetMainLightDirection();
      vMainLightColorIntensity = pWaterPass->GetMainLightColorIntensity();
      vInteractionWindowParams = pWaterPass->GetInteractionWindowParams();
    }

    plWaterDrawConstants constants;
    constants.PatchOriginAndBaseHeight = plVec4(pRenderData->m_vPatchOrigin.x, pRenderData->m_vPatchOrigin.y, pRenderData->m_vPatchOrigin.z, pRenderData->m_fBaseHeight);
    constants.PatchAxisUAndExtent = plVec4(pRenderData->m_vPatchAxisU.x, pRenderData->m_vPatchAxisU.y, pRenderData->m_vPatchAxisU.z, pRenderData->m_fExtentU);
    constants.PatchAxisVAndExtent = plVec4(pRenderData->m_vPatchAxisV.x, pRenderData->m_vPatchAxisV.y, pRenderData->m_vPatchAxisV.z, pRenderData->m_fExtentV);
    constants.WaveParams = plVec4(pRenderData->m_fWaveAmplitude, pRenderData->m_fWaveLength, pRenderData->m_fWaveSpeed, pRenderData->m_fChoppiness);
    constants.FlowAndJunctionBlend = plVec4(pRenderData->m_vFlow.x, pRenderData->m_vFlow.y, pRenderData->m_fJunctionBlend, pRenderData->m_fRefractionStrength);
    const plUInt32 uiAvailableCascadeCount = uiSpectrumLayerCount / plWaterGpuSimulation::MaxSpectrumProfiles;
    const plUInt32 uiEffectiveCascadeCount = uiGpuProfileSlot != plInvalidIndex
                                               ? plMath::Min(pRenderData->m_uiGpuCascadeCount, uiAvailableCascadeCount)
                                               : 0u;
    if (uiGpuProfileSlot == plInvalidIndex)
      uiGpuProfileSlot = 0;
    constants.SurfaceParams = plVec4(pRenderData->m_fSimulationTime, pRenderData->m_fRippleMaxAmplitude,
      static_cast<float>(uiEffectiveCascadeCount), static_cast<float>(uiGpuProfileSlot));
    constants.PatchParams = plVec4U32(uiCellsPerSide, uiSpectrumLayerCount, static_cast<plUInt32>(cvar_WaterDebugJunctionPatches), pRenderData->m_uiGeometryType);
    const plUInt32 uiActiveUnderwaterBodyId = pWaterPass != nullptr ? pWaterPass->GetActiveUnderwaterBodyId() : plInvalidIndex;
    constants.VolumeFilterParams = plVec4U32(pRenderData->m_uiBodyId, pRenderData->m_uiSecondaryBodyId,
      uiActiveUnderwaterBodyId, uiActiveUnderwaterBodyId != plInvalidIndex ? 1u : 0u);

    plWaterJunctionConstants* junctionConstants = plRenderContext::GetConstantBufferData<plWaterJunctionConstants>(m_hJunctionConstants);
    junctionConstants->PrimaryAbsorptionAndRoughness = plVec4(pRenderData->m_vAbsorption.x, pRenderData->m_vAbsorption.y, pRenderData->m_vAbsorption.z, pRenderData->m_fRoughness);
    junctionConstants->PrimaryScatteringAndIor = plVec4(pRenderData->m_vScattering.x, pRenderData->m_vScattering.y, pRenderData->m_vScattering.z, pRenderData->m_fIndexOfRefraction);
    junctionConstants->PrimaryShallowColor = pRenderData->m_ShallowColor.GetAsVec4();
    junctionConstants->PrimaryDeepColor = pRenderData->m_DeepColor.GetAsVec4();
    junctionConstants->PrimaryOpticalParams = plVec4(pRenderData->m_fReflectionStrength, pRenderData->m_fEnvironmentReflectionWeight, pRenderData->m_fRefractionDepth, pRenderData->m_fShoreFadeDepth);
    junctionConstants->PrimaryFoamParams = plVec4(pRenderData->m_fFoamStrength, pRenderData->m_fFoamCrestThreshold, pRenderData->m_fFoamShoreDepth, 0.0f);
    junctionConstants->PrimaryWindDirection = plVec4(pRenderData->m_vWindDirection.x, pRenderData->m_vWindDirection.y, 0.0f, 0.0f);
    junctionConstants->SecondaryWaveParams = plVec4(pRenderData->m_fSecondaryWaveAmplitude, pRenderData->m_fSecondaryWaveLength,
      pRenderData->m_fSecondaryWaveSpeed, pRenderData->m_fSecondaryChoppiness);
    junctionConstants->SecondaryAbsorptionAndRoughness = plVec4(pRenderData->m_vSecondaryAbsorption.x, pRenderData->m_vSecondaryAbsorption.y,
      pRenderData->m_vSecondaryAbsorption.z, pRenderData->m_fSecondaryRoughness);
    junctionConstants->SecondaryScatteringAndIor = plVec4(pRenderData->m_vSecondaryScattering.x, pRenderData->m_vSecondaryScattering.y,
      pRenderData->m_vSecondaryScattering.z, pRenderData->m_fSecondaryIndexOfRefraction);
    junctionConstants->SecondaryShallowColor = pRenderData->m_SecondaryShallowColor.GetAsVec4();
    junctionConstants->SecondaryDeepColor = pRenderData->m_SecondaryDeepColor.GetAsVec4();
    junctionConstants->SecondaryOpticalParams = plVec4(pRenderData->m_fSecondaryReflectionStrength, pRenderData->m_fSecondaryEnvironmentReflectionWeight,
      pRenderData->m_fSecondaryRefractionDepth, pRenderData->m_fSecondaryShoreFadeDepth);
    junctionConstants->SecondaryFoamParams = plVec4(pRenderData->m_fSecondaryFoamStrength, pRenderData->m_fSecondaryFoamCrestThreshold,
      pRenderData->m_fSecondaryFoamShoreDepth, 0.0f);
    junctionConstants->SecondaryWindAndRefraction = plVec4(pRenderData->m_vSecondaryWindDirection.x, pRenderData->m_vSecondaryWindDirection.y,
      pRenderData->m_fSecondaryRefractionStrength, 0.0f);
    junctionConstants->JunctionCutout0 = plVec4(pRenderData->m_JunctionCutouts[0].m_vCenter.x, pRenderData->m_JunctionCutouts[0].m_vCenter.y,
      pRenderData->m_JunctionCutouts[0].m_vDirection.x, pRenderData->m_JunctionCutouts[0].m_vDirection.y);
    junctionConstants->JunctionCutout1 = plVec4(pRenderData->m_JunctionCutouts[0].m_fHalfLength, pRenderData->m_JunctionCutouts[0].m_fStartHalfWidth,
      pRenderData->m_JunctionCutouts[0].m_fEndHalfWidth, pRenderData->m_JunctionCutouts[0].m_fEnabled);
    junctionConstants->JunctionCutout2 = plVec4(pRenderData->m_JunctionCutouts[1].m_vCenter.x, pRenderData->m_JunctionCutouts[1].m_vCenter.y,
      pRenderData->m_JunctionCutouts[1].m_vDirection.x, pRenderData->m_JunctionCutouts[1].m_vDirection.y);
    junctionConstants->JunctionCutout3 = plVec4(pRenderData->m_JunctionCutouts[1].m_fHalfLength, pRenderData->m_JunctionCutouts[1].m_fStartHalfWidth,
      pRenderData->m_JunctionCutouts[1].m_fEndHalfWidth, pRenderData->m_JunctionCutouts[1].m_fEnabled);
    junctionConstants->JunctionFlow0 = plVec4(pRenderData->m_JunctionFlows[0].m_vOrigin.x, pRenderData->m_JunctionFlows[0].m_vOrigin.y,
      pRenderData->m_JunctionFlows[0].m_vDirection.x, pRenderData->m_JunctionFlows[0].m_vDirection.y);
    junctionConstants->JunctionFlow1 = plVec4(pRenderData->m_JunctionFlows[0].m_fLength, pRenderData->m_JunctionFlows[0].m_fHalfWidth,
      pRenderData->m_JunctionFlows[0].m_fSpeed, pRenderData->m_JunctionFlows[0].m_fDirectionSign);
    junctionConstants->JunctionFlow2 = plVec4(pRenderData->m_JunctionFlows[1].m_vOrigin.x, pRenderData->m_JunctionFlows[1].m_vOrigin.y,
      pRenderData->m_JunctionFlows[1].m_vDirection.x, pRenderData->m_JunctionFlows[1].m_vDirection.y);
    junctionConstants->JunctionFlow3 = plVec4(pRenderData->m_JunctionFlows[1].m_fLength, pRenderData->m_JunctionFlows[1].m_fHalfWidth,
      pRenderData->m_JunctionFlows[1].m_fSpeed, pRenderData->m_JunctionFlows[1].m_fDirectionSign);
    junctionConstants->MainLightDirection = plVec4(vMainLightDirection.x, vMainLightDirection.y, vMainLightDirection.z, 0.0f);
    junctionConstants->MainLightColorIntensity = plVec4(vMainLightColorIntensity.x, vMainLightColorIntensity.y, vMainLightColorIntensity.z, 0.0f);
    junctionConstants->InteractionWindowParams = vInteractionWindowParams;
    // Junction patches sample the SECONDARY body's spectrum toward their seam. Without this the patch edge
    // stands still while the adjoining ocean displaces with its GPU spectrum waves, opening a visible gap.
    const plUInt32 uiSecondaryCascadeCount = uiSecondaryProfileSlot != plInvalidIndex
                                               ? plMath::Min(pRenderData->m_uiSecondaryGpuCascadeCount, uiAvailableCascadeCount)
                                               : 0u;
    junctionConstants->JunctionSpillParams = plVec4(static_cast<float>(uiSecondaryProfileSlot != plInvalidIndex ? uiSecondaryProfileSlot : 0u),
      static_cast<float>(uiSecondaryCascadeCount), pRenderData->m_fMouthFoamStrength, pRenderData->m_fSecondaryRippleMaxAmplitude);
    junctionConstants->JunctionMaterialParams = plVec4(pRenderData->m_vSecondaryFlow.x, pRenderData->m_vSecondaryFlow.y,
      pRenderData->m_fJunctionMaterialMixStrength, static_cast<float>(pRenderData->m_uiJunctionMaterialFlowSlot));
    junctionConstants->PrimarySurfaceDetailParams = plVec4(pRenderData->m_fMicroNormalScale, pRenderData->m_fMicroNormalStrength,
      pRenderData->m_fSunGlitterStrength, pRenderData->m_fRefractionEdgeRejection);
    junctionConstants->PrimaryFoamMaterialParams = plVec4(pRenderData->m_fFoamRoughness, pRenderData->m_fFoamBreakupScale,
      pRenderData->m_hFoamTexture.IsValid() ? 1.0f : 0.0f, pRenderData->m_hNormalTexture.IsValid() ? 1.0f : 0.0f);
    junctionConstants->PrimaryFoamColor = pRenderData->m_FoamColor.GetAsVec4();
    junctionConstants->SecondarySurfaceDetailParams = plVec4(pRenderData->m_fSecondaryMicroNormalScale, pRenderData->m_fSecondaryMicroNormalStrength,
      pRenderData->m_fSecondarySunGlitterStrength, pRenderData->m_fSecondaryRefractionEdgeRejection);
    junctionConstants->SecondaryFoamMaterialParams = plVec4(pRenderData->m_fSecondaryFoamRoughness, pRenderData->m_fSecondaryFoamBreakupScale,
      pRenderData->m_hSecondaryFoamTexture.IsValid() ? 1.0f : 0.0f, pRenderData->m_hSecondaryNormalTexture.IsValid() ? 1.0f : 0.0f);
    junctionConstants->SecondaryFoamColor = pRenderData->m_SecondaryFoamColor.GetAsVec4();
    junctionConstants->CausticParams = plVec4(pRenderData->m_fUnderwaterCaustics, pRenderData->m_fReflectedCaustics,
      pRenderData->m_fSecondaryUnderwaterCaustics, pRenderData->m_fSecondaryReflectedCaustics);
    junctionConstants->WaterQualityParams = plVec4(
      pWaterPass != nullptr ? static_cast<float>(pWaterPass->GetQualitySettings().m_uiScreenSpaceReflectionSteps) : 0.0f, 0.0f, 0.0f, 0.0f);
    float fMaximumTessellation = 1.0f;
    float fTargetEdgePixels = 32.0f;
    float fInteractionTessellation = 1.0f;
    if (bUseTessellation)
    {
      if (pQuality->m_Tier == plWaterQualityTier::Ultra)
      {
        fMaximumTessellation = 8.0f;
        fTargetEdgePixels = 18.0f;
        fInteractionTessellation = 6.0f;
      }
      else if (pQuality->m_Tier == plWaterQualityTier::High)
      {
        fMaximumTessellation = 4.0f;
        fTargetEdgePixels = 22.0f;
        fInteractionTessellation = 4.0f;
      }
      else
      {
        fMaximumTessellation = 2.0f;
        fTargetEdgePixels = 28.0f;
        fInteractionTessellation = 2.0f;
      }
    }
    junctionConstants->TessellationParams = plVec4(fMaximumTessellation, fTargetEdgePixels,
      fInteractionTessellation, bUseTessellation && bInteractionBoundsValid ? 1.0f : 0.0f);
    junctionConstants->ActiveInteractionBounds = vActiveInteractionBounds;

    pContext->SetPushConstants("plWaterDrawConstants", constants);
    bindGroup.BindTexture("WaterNormalTexture", pRenderData->m_hNormalTexture, plResourceAcquireMode::AllowLoadingFallback);
    bindGroup.BindTexture("WaterNormalTextureSecondary", pRenderData->m_hSecondaryNormalTexture, plResourceAcquireMode::AllowLoadingFallback);
    bindGroup.BindTexture("WaterFoamTexture", pRenderData->m_hFoamTexture, plResourceAcquireMode::AllowLoadingFallback);
    bindGroup.BindTexture("WaterFoamTextureSecondary", pRenderData->m_hSecondaryFoamTexture, plResourceAcquireMode::AllowLoadingFallback);
    bindGroup.BindBuffer("plWaterJunctionConstants", m_hJunctionConstants);

    const plUInt32 uiPrimitiveCount = pRenderData->m_hFiniteMesh.IsValid()
                                        ? pRenderData->m_uiPrimitiveCount
                                        : uiCellsPerSide * uiCellsPerSide * 2;
    if (pRenderData->m_hFiniteMesh.IsValid())
      pContext->BindMeshBuffer(pRenderData->m_hFiniteMesh);
    else
      pContext->BindNullMeshBuffer(plGALPrimitiveTopology::Triangles, uiPrimitiveCount);
    pContext->DrawMeshBuffer().IgnoreResult();
  }
}

PL_STATICLINK_FILE(WaterPlugin, WaterPlugin_Rendering_Implementation_WaterRenderer);
