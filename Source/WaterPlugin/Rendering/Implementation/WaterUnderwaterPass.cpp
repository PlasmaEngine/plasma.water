#include <WaterPlugin/WaterPluginPCH.h>

#include <Foundation/IO/TypeVersionContext.h>
#include <RendererCore/Pipeline/RenderPipeline.h>
#include <RendererCore/Pipeline/View.h>
#include <RendererCore/RenderContext/BindGroupBuilder.h>
#include <RendererCore/RenderContext/RenderContext.h>
#include <RendererCore/RenderGraph/RenderGraph.h>
#include <RendererCore/RenderGraph/RenderGraphContext.h>
#include <RendererCore/RenderGraph/RenderGraphPassBuilder.h>
#include <RendererFoundation/Device/Device.h>
#include <WaterPlugin/Rendering/WaterFrameData.h>
#include <WaterPlugin/Rendering/WaterRenderData.h>
#include <WaterPlugin/Rendering/WaterUnderwaterPass.h>

#include <Shaders/Water/WaterUnderwaterConstants.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plWaterUnderwaterPass, 1, plRTTIDefaultAllocator<plWaterUnderwaterPass>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Color", m_PinColor),
    PL_MEMBER_PROPERTY("Depth", m_PinDepth),
    PL_MEMBER_PROPERTY("WaterExitDepth", m_PinWaterExitDepth),
    PL_MEMBER_PROPERTY("Output", m_PinOutput),
    PL_ENUM_MEMBER_PROPERTY("Quality", plWaterQualityTier, m_Quality),
    PL_MEMBER_PROPERTY("Density", m_fDensity)->AddAttributes(new plDefaultValueAttribute(0.08f), new plClampValueAttribute(0.0f, 2.0f)),
    PL_MEMBER_PROPERTY("DistortionStrength", m_fDistortionStrength)->AddAttributes(new plDefaultValueAttribute(0.003f), new plClampValueAttribute(0.0f, 0.1f)),
    PL_MEMBER_PROPERTY("CausticIntensity", m_fCausticIntensity)->AddAttributes(new plDefaultValueAttribute(0.35f), new plClampValueAttribute(0.0f, 4.0f)),
    PL_MEMBER_PROPERTY("LightShaftIntensity", m_fLightShaftIntensity)->AddAttributes(new plDefaultValueAttribute(0.2f), new plClampValueAttribute(0.0f, 4.0f)),
  }
  PL_END_PROPERTIES;
  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("Post Processing/Water"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plWaterUnderwaterPass::plWaterUnderwaterPass()
  : plRenderPipelinePass("WaterUnderwaterPass", true)
{
  m_hShader = plResourceManager::LoadResource<plShaderResource>("Shaders/Water/WaterUnderwater.plShader");
  m_hConstants = plRenderContext::CreateConstantBufferStorage<plWaterUnderwaterConstants>();
}

plWaterUnderwaterPass::~plWaterUnderwaterPass()
{
  plRenderContext::DeleteConstantBufferStorage(m_hConstants);
}

plStatus plWaterUnderwaterPass::AddRenderPasses(const plViewData& viewData, const plCamera& camera, plRenderGraph& ref_graph, const plArrayPtr<const plRenderPipelinePinConnection> inputs, plArrayPtr<plRenderPipelinePinConnection> outputs)
{
  const plRenderGraphTextureHandle hColor = inputs[m_PinColor.m_uiInputIndex].m_TextureHandle;
  const plRenderGraphTextureHandle hDepth = inputs[m_PinDepth.m_uiInputIndex].m_TextureHandle;
  const plRenderGraphTextureHandle hWaterExitDepth = inputs[m_PinWaterExitDepth.m_uiInputIndex].m_TextureHandle;

  if (hColor.IsInvalidated())
    return plStatus(plFmt("Color: Not connected"));
  if (hDepth.IsInvalidated())
    return plStatus(plFmt("Depth: Not connected"));

  const plGALTextureCreationDescription inputDesc = ref_graph.GetTextureDesc(hColor);
  plGALTextureCreationDescription outputDesc = inputDesc;
  outputDesc.m_bAllowRenderTargetView = true;
  outputDesc.m_bAllowShaderResourceView = true;
  const plRenderGraphTextureHandle hOutput = ref_graph.CreateTexture(outputDesc);
  outputs[m_PinOutput.m_uiOutputIndex].m_TextureHandle = hOutput;

  const bool bHasExitDepth = !hWaterExitDepth.IsInvalidated();
  const plRenderGraphTextureHandle hExitDepthOrSceneDepth = bHasExitDepth ? hWaterExitDepth : hDepth;
  const plWaterQualitySettings quality = plWaterQualitySettings::Resolve(m_Quality, ref_graph.GetDevice()->GetCapabilities());

  auto pass = ref_graph.AddGraphicsPass("Water Underwater Composite");
  pass.AddColorTarget(hOutput);
  pass.ReadTexture(hColor, {}, plGALResourceState::ShaderRead, plGALShaderStageFlags::PixelShader);
  pass.ReadTexture(hDepth, {}, plGALResourceState::ShaderRead, plGALShaderStageFlags::PixelShader);
  pass.ReadTexture(hExitDepthOrSceneDepth, {}, plGALResourceState::ShaderRead, plGALShaderStageFlags::PixelShader);
  pass.SetStereoscopic(camera.IsStereoscopic());
  pass.SetExecuteCallback([=](const plRenderGraphContext& ctx) {
    const plRenderViewContext& renderViewContext = *ctx.GetUserData<plRenderViewContext>();
    renderViewContext.UpdateViewport();

    plWaterFrameData* pFrameData = nullptr;
    if (plWaterFrameDataProvider* pProvider = GetPipeline()->GetFrameDataProvider<plWaterFrameDataProvider>())
    {
      pFrameData = pProvider->GetData(renderViewContext);
    }

    const bool bHasExtractedWaterData = pFrameData != nullptr && pFrameData->m_bHasWaterData;
    bool bUnderwater = bHasExtractedWaterData && pFrameData->m_bCameraUnderwater;
    plVec3 vAbsorption = bHasExtractedWaterData ? pFrameData->m_vAbsorption : plVec3(0.15f, 0.06f, 0.025f);
    plVec3 vScattering = bHasExtractedWaterData ? pFrameData->m_vScattering : plVec3(0.015f, 0.035f, 0.055f);
    const bool bHasMainLight = pFrameData != nullptr && !pFrameData->m_vMainLightColorIntensity.IsZero(0.0001f);
    plVec3 vLightDirection = bHasMainLight ? pFrameData->m_vMainLightDirection : plVec3(0.0f, 0.0f, 1.0f);
    vLightDirection.NormalizeIfNotZero(plVec3(0.3f, 0.2f, 0.93f).GetNormalized()).IgnoreResult();
    const plColor lightColor = bHasMainLight ? pFrameData->m_MainLightColor : plColor::Black;
    float fCameraDepth = bHasExtractedWaterData ? pFrameData->m_fCameraWaterDepth : 0.0f;
    float fMaxDistance = bHasExtractedWaterData ? pFrameData->m_fMaxUnderwaterDistance : 80.0f;
    float fFogDensity = bHasExtractedWaterData ? pFrameData->m_fUnderwaterFogDensity : m_fDensity;
    float fScatteringIntensity = bHasExtractedWaterData ? pFrameData->m_fUnderwaterScattering : 1.0f;
    float fHaze = bHasExtractedWaterData ? pFrameData->m_fUnderwaterHaze : 0.0f;
    float fDistortionStrength = bHasExtractedWaterData ? m_fDistortionStrength * (pFrameData->m_fUnderwaterDistortion / 0.18f) : m_fDistortionStrength;
    float fCausticIntensity = bHasExtractedWaterData ? pFrameData->m_fUnderwaterCaustics : m_fCausticIntensity;
    float fLightShaftIntensity = bHasExtractedWaterData ? pFrameData->m_fUnderwaterLightShafts : m_fLightShaftIntensity;
    float fMeniscus = bHasExtractedWaterData ? pFrameData->m_fUnderwaterMeniscus : 0.0f;
    float fSimulationTime = pFrameData != nullptr ? pFrameData->m_fSimulationTime : 0.0f;

    // Keep ocean underwater rendering functional in custom pipelines that omitted the optional frame-data
    // extractor. Finite rivers and lakes still use the extractor for exact spline containment.
    if (!bHasExtractedWaterData && renderViewContext.m_pCamera != nullptr)
    {
      const float fCameraHeight = renderViewContext.m_pCamera->GetPosition().z;
      const plRenderDataBatchList waterBatches = GetPipeline()->GetRenderDataBatchesWithCategory(plWaterRenderData::WaterSurface);
      for (plUInt32 uiBatch = 0; uiBatch < waterBatches.GetBatchCount() && !bUnderwater; ++uiBatch)
      {
        for (auto it = waterBatches.GetBatch(uiBatch).GetIterator<plWaterRenderData>(); it.IsValid(); ++it)
        {
          if (it->m_uiGeometryType != 0u || fCameraHeight >= it->m_fBaseHeight)
            continue;

          bUnderwater = true;
          fCameraDepth = it->m_fBaseHeight - fCameraHeight;
          vAbsorption = it->m_vAbsorption;
          vScattering = it->m_vScattering;
          fFogDensity = it->m_fUnderwaterFogDensity;
          fDistortionStrength = m_fDistortionStrength * (it->m_fUnderwaterDistortion / 0.18f);
          fCausticIntensity = it->m_fUnderwaterCaustics;
          fLightShaftIntensity = it->m_fUnderwaterLightShafts;
          fSimulationTime = it->m_fSimulationTime;
          break;
        }
      }
    }

    // Debug render modes describe the underlying geometry or material channel. Applying fog, distortion and
    // shafts afterwards makes the water and every object seen through it report the wrong diagnostic values.
    if (renderViewContext.m_pViewData->m_ViewRenderMode != plViewRenderMode::None)
      bUnderwater = false;

    if (!bHasMainLight)
      fLightShaftIntensity = 0.0f;

    plWaterUnderwaterConstants* constants = plRenderContext::GetConstantBufferData<plWaterUnderwaterConstants>(m_hConstants);
    constants->WaterAbsorptionAndDensity = plVec4(vAbsorption.x, vAbsorption.y, vAbsorption.z, fFogDensity);
    constants->WaterScatteringAndIntensity = plVec4(vScattering.x, vScattering.y, vScattering.z, 1.0f);
    constants->WaterMainLightDirectionAndShaftIntensity = plVec4(vLightDirection.x, vLightDirection.y, vLightDirection.z, quality.m_bUseUnderwaterLightShafts ? fLightShaftIntensity : 0.0f);
    constants->WaterMainLightColor = lightColor;
    constants->WaterCameraParams = plVec4(fCameraDepth, fMaxDistance, bUnderwater ? 1.0f : 0.0f, bHasExitDepth ? 1.0f : 0.0f);
    constants->WaterEffectParams = plVec4(fDistortionStrength, quality.m_bUseUnderwaterCaustics ? fCausticIntensity : 0.0f, quality.m_bUseUnderwaterLightShafts ? fLightShaftIntensity : 0.0f, fSimulationTime);
    constants->WaterProfileParams = plVec4(fScatteringIntensity, fHaze, fMeniscus, 0.0f);
    constants->WaterUnderwaterParams = plVec4U32(static_cast<plUInt32>(quality.m_Tier), plMath::Max(inputDesc.m_uiArraySize, 1u), pFrameData != nullptr ? pFrameData->m_uiActiveBodyId : 0u, 0u);

    plBindGroupBuilder& bindGroup = renderViewContext.m_pRenderContext->GetBindGroup();
    bindGroup.BindBuffer("plWaterUnderwaterConstants", m_hConstants);
    bindGroup.BindTexture("WaterUnderwaterSceneColor", ctx.ResolveTexture(hColor));
    bindGroup.BindTexture("WaterUnderwaterSceneDepth", ctx.ResolveTexture(hDepth));
    bindGroup.BindTexture("WaterUnderwaterExitDepth", ctx.ResolveTexture(hExitDepthOrSceneDepth));
    renderViewContext.m_pRenderContext->BindShader(m_hShader);
    renderViewContext.m_pRenderContext->BindNullMeshBuffer(plGALPrimitiveTopology::Triangles, 1);
    renderViewContext.m_pRenderContext->DrawMeshBuffer().IgnoreResult();
  });

  return plStatus(PL_SUCCESS);
}

plResult plWaterUnderwaterPass::Serialize(plStreamWriter& inout_stream) const
{
  PL_SUCCEED_OR_RETURN(SUPER::Serialize(inout_stream));
  inout_stream << m_Quality;
  inout_stream << m_fDensity;
  inout_stream << m_fDistortionStrength;
  inout_stream << m_fCausticIntensity;
  inout_stream << m_fLightShaftIntensity;
  return PL_SUCCESS;
}

plResult plWaterUnderwaterPass::Deserialize(plStreamReader& inout_stream)
{
  PL_SUCCEED_OR_RETURN(SUPER::Deserialize(inout_stream));
  inout_stream >> m_Quality;
  inout_stream >> m_fDensity;
  inout_stream >> m_fDistortionStrength;
  inout_stream >> m_fCausticIntensity;
  inout_stream >> m_fLightShaftIntensity;
  return PL_SUCCESS;
}

PL_STATICLINK_FILE(WaterPlugin, WaterPlugin_Rendering_Implementation_WaterUnderwaterPass);
