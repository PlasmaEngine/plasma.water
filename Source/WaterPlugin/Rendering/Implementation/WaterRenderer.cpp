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
#include <WaterPlugin/Utils/WaterGeometry.h>

#include <Shaders/Water/WaterDrawConstants.h>
#include <Shaders/Water/WaterJunctionConstants.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plWaterRenderer, 1, plRTTIDefaultAllocator<plWaterRenderer>)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plCVarInt cvar_WaterDebugJunctionPatches("Water.Debug.JunctionPatches", 0, plCVarFlags::Default,
  "Shows one term of the water shading instead of the picture: 0 = off, 1 = profile weights on rivers and lakes (what Water.Debug.Stitch shows), 3 and up = shading terms; see the end of WaterSurfaceModern.plShader.");
extern plCVarBool cvar_WaterDebugStitch;

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
    const bool bSurfaceGeometry = pRenderData->m_uiGeometryType != plWaterRenderData::GeometryVolume;
    const bool bUseTessellation = pQuality != nullptr && pQuality->m_Tier != plWaterQualityTier::Low &&
                                  bTessellationSupported && bSurfaceGeometry && !cvar_WaterDebugNoTessellation;

    plVec4 vActiveInteractionBounds = plVec4::MakeZero();
    const bool bInteractionBoundsValid = pWaterPass != nullptr && pWaterPass->GetActiveInteractionBounds(vActiveInteractionBounds);
    plUInt32 uiCellsPerSide = pRenderData->m_uiCellsPerSide;
    plUInt32 uiSeams = 0;
    if (pWaterPass != nullptr && pRenderData->m_uiGeometryType == plWaterRenderData::GeometryOceanPatch)
    {
      const plWaterQualitySettings& quality = *pQuality;
      if (pRenderData->m_uiLod >= quality.m_uiOceanRingCount)
        continue;

      // How fine a ring is drawn is decided for the ring as a whole: every patch of a ring has the same cells, so
      // patches of one ring always meet vertex to vertex, and a ring knows the cells of the ring outside it.
      // Without hull and domain stages, ripples need the extra vertices in the grid itself: the two inner rings are
      // refined while there are any.
      const plUInt32 uiRippleMultiplier = (!bUseTessellation && bInteractionBoundsValid) ? (quality.m_Tier == plWaterQualityTier::Ultra ? 4u : 2u) : 1u;
      auto ringCells = [&](plUInt32 uiRing)
      {
        return plWaterGeometry::GetOceanRingCells(pRenderData->m_uiOceanResolution, quality.m_uiOceanCellsPerRing, uiRing, uiRing <= 1 ? uiRippleMultiplier : 1u);
      };
      uiCellsPerSide = ringCells(pRenderData->m_uiLod);

      // On an edge that meets the next ring, the vertices move onto that ring's coarser vertices (see the vertex shader).
      const bool bHasOuterRing = pRenderData->m_uiLod + 1u < quality.m_uiOceanRingCount;
      const plUInt32 uiOuterMask = bHasOuterRing ? (pRenderData->m_uiOuterSeamMask & 0xFu) : 0u;
      const plUInt32 uiRatio = bHasOuterRing ? plWaterGeometry::GetOceanSeamRatio(uiCellsPerSide, ringCells(pRenderData->m_uiLod + 1u)) : 1u;
      uiSeams = (pRenderData->m_uiInnerSeamMask & 0xFu) | (uiOuterMask << 4) | (static_cast<plUInt32>(pRenderData->m_uiLod) << 8) | (uiRatio << 16);
    }

    if (pRenderData->m_hFiniteMesh.IsValid())
      pContext->SetShaderPermutationVariable("WATER_GEOMETRY_SOURCE", "WATER_GEOMETRY_SOURCE_FINITE");
    else
      pContext->SetShaderPermutationVariable("WATER_GEOMETRY_SOURCE", "WATER_GEOMETRY_SOURCE_PROCEDURAL");
    pContext->SetShaderPermutationVariable("WATER_USE_TESSELLATION",
      bUseTessellation ? plTempHashedString("TRUE") : plTempHashedString("FALSE"));
    pContext->BindShader(m_hShader);

    plBindGroupBuilder& bindGroup = pContext->GetBindGroup(PL_GAL_BIND_GROUP_DRAW_CALL);

    plUInt32 uiSpectrumLayerCount = 0;
    plUInt32 uiProfileCount = 1;
    plVec3 vMainLightDirection = plVec3(0.0f, 0.0f, 1.0f);
    plVec3 vMainLightColorIntensity = plVec3::MakeZero();
    plVec4 vInteractionWindowParams = plVec4::MakeZero();
    if (pWaterPass != nullptr)
    {
      uiSpectrumLayerCount = pWaterPass->GetSpectrumLayerCount();
      uiProfileCount = pWaterPass->GetProfileCount();
      vMainLightDirection = pWaterPass->GetMainLightDirection();
      vMainLightColorIntensity = pWaterPass->GetMainLightColorIntensity();
      vInteractionWindowParams = pWaterPass->GetInteractionWindowParams();
    }

    // Everything that belongs to a profile comes from the profile table. A draw only names its own entry; the
    // vertices of a river or lake name the pairs they blend.
    const plUInt32 uiProfile = pRenderData->m_uiProfileIndex;

    plWaterDrawConstants constants;
    constants.PatchOriginAndBaseHeight = plVec4(pRenderData->m_vPatchOrigin.x, pRenderData->m_vPatchOrigin.y, pRenderData->m_vPatchOrigin.z, pRenderData->m_fBaseHeight);
    constants.PatchAxisUAndExtent = plVec4(pRenderData->m_vPatchAxisU.x, pRenderData->m_vPatchAxisU.y, pRenderData->m_vPatchAxisU.z, pRenderData->m_fExtentU);
    constants.PatchAxisVAndExtent = plVec4(pRenderData->m_vPatchAxisV.x, pRenderData->m_vPatchAxisV.y, pRenderData->m_vPatchAxisV.z, pRenderData->m_fExtentV);
    // How far apart the draw's vertices are: the spectrum is read at that scale by the vertices, and the pixels add the rest.
    const float fVertexSpacing = pRenderData->m_uiGeometryType == plWaterRenderData::GeometryOceanPatch
                                   ? pRenderData->m_fExtentU / static_cast<float>(plMath::Max(uiCellsPerSide, 1u))
                                   : pRenderData->m_fVertexSpacing;
    constants.SurfaceParams = plVec4(pRenderData->m_fSimulationTime, fVertexSpacing, pWaterPass != nullptr ? static_cast<float>(pWaterPass->GetSpectrumResolution()) : 0.0f, 0.0f);
    // Water.Debug.Stitch shows the profile weights, which is view 1.
    const plUInt32 uiDebugMode = cvar_WaterDebugStitch ? 1u : static_cast<plUInt32>(plMath::Max(cvar_WaterDebugJunctionPatches.GetValue(), 0));
    constants.PatchParams = plVec4U32(uiCellsPerSide, uiSpectrumLayerCount, uiDebugMode, pRenderData->m_uiGeometryType);
    const plUInt32 uiActiveUnderwaterBodyId = pWaterPass != nullptr ? pWaterPass->GetActiveUnderwaterBodyId() : plInvalidIndex;
    constants.VolumeFilterParams = plVec4U32(pRenderData->m_uiBodyId, plInvalidIndex,
      uiActiveUnderwaterBodyId, uiActiveUnderwaterBodyId != plInvalidIndex ? 1u : 0u);
    constants.ProfileParams = plVec4U32(uiProfile, uiProfile, uiProfileCount, uiSeams);

    plWaterJunctionConstants* junctionConstants = plRenderContext::GetConstantBufferData<plWaterJunctionConstants>(m_hJunctionConstants);
    junctionConstants->JunctionCutout0 = plVec4(pRenderData->m_Cutouts[0].m_vCenter.x, pRenderData->m_Cutouts[0].m_vCenter.y,
      pRenderData->m_Cutouts[0].m_vDirection.x, pRenderData->m_Cutouts[0].m_vDirection.y);
    junctionConstants->JunctionCutout1 = plVec4(pRenderData->m_Cutouts[0].m_fHalfLength, pRenderData->m_Cutouts[0].m_fStartHalfWidth,
      pRenderData->m_Cutouts[0].m_fEndHalfWidth, pRenderData->m_Cutouts[0].m_fEnabled);
    junctionConstants->JunctionCutout2 = plVec4(pRenderData->m_Cutouts[1].m_vCenter.x, pRenderData->m_Cutouts[1].m_vCenter.y,
      pRenderData->m_Cutouts[1].m_vDirection.x, pRenderData->m_Cutouts[1].m_vDirection.y);
    junctionConstants->JunctionCutout3 = plVec4(pRenderData->m_Cutouts[1].m_fHalfLength, pRenderData->m_Cutouts[1].m_fStartHalfWidth,
      pRenderData->m_Cutouts[1].m_fEndHalfWidth, pRenderData->m_Cutouts[1].m_fEnabled);
    junctionConstants->MainLightDirection = plVec4(vMainLightDirection.x, vMainLightDirection.y, vMainLightDirection.z, 0.0f);
    junctionConstants->MainLightColorIntensity = plVec4(vMainLightColorIntensity.x, vMainLightColorIntensity.y, vMainLightColorIntensity.z, 0.0f);
    junctionConstants->InteractionWindowParams = vInteractionWindowParams;
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
    bindGroup.BindTexture("WaterFoamTexture", pRenderData->m_hFoamTexture, plResourceAcquireMode::AllowLoadingFallback);
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
