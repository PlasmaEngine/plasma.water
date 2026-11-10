#include <WaterPlugin/WaterPluginPCH.h>

#include <Foundation/Configuration/CVar.h>
#include <Foundation/IO/TypeVersionContext.h>
#include <RendererCore/Lights/ContactShadowDataProvider.h>
#include <RendererCore/Pipeline/RenderPipeline.h>
#include <RendererCore/Pipeline/View.h>
#include <RendererCore/RenderContext/BindGroupBuilder.h>
#include <RendererCore/RenderContext/RenderContext.h>
#include <RendererCore/RenderGraph/RenderGraph.h>
#include <RendererCore/RenderGraph/RenderGraphContext.h>
#include <RendererCore/RenderGraph/RenderGraphPassBuilder.h>
#include <RendererFoundation/Resources/Texture.h>
#include <WaterPlugin/Rendering/WaterFrameData.h>
#include <WaterPlugin/Rendering/WaterRenderData.h>
#include <WaterPlugin/Rendering/WaterSurfacePass.h>

#include <Shaders/Water/WaterCausticFieldConstants.h>
#include <Shaders/Water/WaterCausticsConstants.h>
#include <Shaders/Water/WaterProfileData.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plWaterSurfacePass, 1, plRTTIDefaultAllocator<plWaterSurfacePass>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("ResolvedDepth", m_PinResolvedDepth),
    PL_MEMBER_PROPERTY("GBuffer1", m_PinGBuffer1),
    PL_MEMBER_PROPERTY("WaterDepth", m_PinWaterDepth),
    PL_MEMBER_PROPERTY("WaterExitDepth", m_PinWaterExitDepth),
    PL_ENUM_MEMBER_PROPERTY("Quality", plWaterQualityTier, m_Quality),
  }
  PL_END_PROPERTIES;
  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("Rendering/Water"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plCVarBool cvar_WaterDebugFailSurfacePass("Water.Debug.FailSurfacePass", false, plCVarFlags::Default,
  "Makes the water surface pass report a failure while its render graph is built. For checking that the rest of the frame still renders.");

plCVarBool cvar_WaterDebugNoCaustics("Water.Debug.NoCaustics", false, plCVarFlags::Default,
  "Leaves out the caustic field, trace and composite passes on every tier. For finding out what caustics cost.");
plCVarBool cvar_WaterDebugNoSpectrum("Water.Debug.NoSpectrum", false, plCVarFlags::Default,
  "Leaves out the wave spectrum and interaction passes on every tier. For finding out what they cost.");

namespace
{
  static plGALTextureCreationDescription MakeWaterTextureDesc(const plGALTextureCreationDescription& referenceDesc, plUInt32 uiWidth, plUInt32 uiHeight, plGALResourceFormat::Enum format)
  {
    plGALTextureCreationDescription desc;
    desc.m_uiWidth = uiWidth;
    desc.m_uiHeight = uiHeight;
    desc.m_uiArraySize = plMath::Max(referenceDesc.m_uiArraySize, 1u);
    desc.m_uiMipLevelCount = 1;
    desc.m_Type = plGALTextureType::Texture2DArray;
    desc.m_Format = format;
    desc.m_SampleCount = plGALMSAASampleCount::None;
    desc.m_bAllowRenderTargetView = true;
    desc.m_bAllowShaderResourceView = true;
    desc.m_bAllowUAV = false;
    desc.m_ResourceAccess.m_bImmutable = false;
    return desc;
  }
} // namespace

plWaterSurfacePass::plWaterSurfacePass()
  : plForwardRenderPass("WaterSurfacePass")
{
  m_hCausticsShader = plResourceManager::LoadResource<plShaderResource>("Shaders/Water/WaterCaustics.plShader");
  m_hCausticsCompositeShader = plResourceManager::LoadResource<plShaderResource>("Shaders/Water/WaterCausticsComposite.plShader");
  m_hCausticsConstants = plRenderContext::CreateConstantBufferStorage<plWaterCausticsConstants>();
  m_hCausticFieldConstants = plRenderContext::CreateConstantBufferStorage<plWaterCausticFieldConstants>();
}

plWaterSurfacePass::~plWaterSurfacePass()
{
  plRenderContext::DeleteConstantBufferStorage(m_hCausticsConstants);
  plRenderContext::DeleteConstantBufferStorage(m_hCausticFieldConstants);
  if (!m_hProfileTable.IsInvalidated())
  {
    plGALDevice::GetDefaultDevice()->DestroyBuffer(m_hProfileTable);
    m_hProfileTable.Invalidate();
  }
}

plStatus plWaterSurfacePass::AddRenderPasses(const plViewData& viewData, const plCamera& camera, plRenderGraph& ref_graph, const plArrayPtr<const plRenderPipelinePinConnection> inputs, plArrayPtr<plRenderPipelinePinConnection> outputs)
{
  const plRenderGraphTextureHandle hColor = inputs[m_PinColor.m_uiInputIndex].m_TextureHandle;
  const plRenderGraphTextureHandle hDepthStencil = inputs[m_PinDepthStencil.m_uiInputIndex].m_TextureHandle;
  const plRenderGraphTextureHandle hResolvedDepth = inputs[m_PinResolvedDepth.m_uiInputIndex].m_TextureHandle;
  const plRenderGraphTextureHandle hGBuffer1 = inputs[m_PinGBuffer1.m_uiInputIndex].m_TextureHandle;
  // The contact shadows pass publishes one result layer per traced light and eye. The sun, when it is traced, is
  // always slot 0, so the caustics read layer eye * slots-per-eye.
  plRenderGraphTextureHandle hScreenSpaceShadow;
  plUInt32 uiShadowLayersTotal = 0;
  if (plContactShadowDataProvider* pShadowProvider = GetPipeline()->GetFrameDataProvider<plContactShadowDataProvider>())
  {
    const plGALTextureHandle hShadowTexture = pShadowProvider->GetTexture();
    if (const plGALTexture* pShadowTexture = hShadowTexture.IsInvalidated() ? nullptr : ref_graph.GetDevice()->GetTexture(hShadowTexture))
    {
      hScreenSpaceShadow = ref_graph.ImportTexture(hShadowTexture);
      uiShadowLayersTotal = pShadowTexture->GetDescription().m_uiArraySize;
    }
  }

  if (hColor.IsInvalidated())
    return plStatus(plFmt("Color: Not connected"));
  if (hDepthStencil.IsInvalidated())
    return plStatus(plFmt("DepthStencil: Not connected"));
  if (hResolvedDepth.IsInvalidated())
    return plStatus(plFmt("ResolvedDepth: Not connected"));
  if (cvar_WaterDebugFailSurfacePass)
    return plStatus(plFmt("Water.Debug.FailSurfacePass is set"));

  outputs[m_PinColor.m_uiOutputIndex].m_TextureHandle = hColor;
  outputs[m_PinDepthStencil.m_uiOutputIndex].m_TextureHandle = hDepthStencil;

  // No water body was extracted for this view (recently): no targets, no wave simulation, no draws. The pass is in
  // every pipeline with a transparent pass once the package is installed, so a scene without water must not pay for it.
  // The content flags are part of the graph's fingerprint: the graph is rebuilt when water comes into view.
  if ((GetPipeline()->GetContentFlags() & PL_WATER_CONTENT_FLAG_SURFACES) == 0)
  {
    // Clear the handles but keep the connectivity, or a later build could not hand a texture through the connection.
    outputs[m_PinWaterDepth.m_uiOutputIndex] = plRenderPipelinePinConnection(outputs[m_PinWaterDepth.m_uiOutputIndex].m_Connectivity);
    outputs[m_PinWaterExitDepth.m_uiOutputIndex] = plRenderPipelinePinConnection(outputs[m_PinWaterExitDepth.m_uiOutputIndex].m_Connectivity);
    return plStatus(PL_SUCCESS);
  }

  const plGALTextureCreationDescription colorDesc = ref_graph.GetTextureDesc(hColor);
  const plUInt32 uiWidth = colorDesc.m_uiWidth;
  const plUInt32 uiHeight = colorDesc.m_uiHeight;
  const plUInt32 uiArraySize = plMath::Max(colorDesc.m_uiArraySize, 1u);

  plWaterQualitySettings quality = plWaterQualitySettings::Resolve(plWaterQualitySettings::SelectTier(*this, m_Quality, viewData), ref_graph.GetDevice()->GetCapabilities());
  if (cvar_WaterDebugNoCaustics)
  {
    quality.m_bUseTransmittedCaustics = false;
    quality.m_bUseReflectedCaustics = false;
    quality.m_bUseUnderwaterCaustics = false;
  }
  if (cvar_WaterDebugNoSpectrum)
  {
    quality.m_bUseSpectrum = false;
    quality.m_uiSpectrumCascadeCount = 0;
    quality.m_uiSpectrumResolution = 0;
    quality.m_uiInteractionAtlasSize = 0;
  }

  const plGALTextureCreationDescription waterDepthDesc = MakeWaterTextureDesc(colorDesc, uiWidth, uiHeight, plGALResourceFormat::RFloat);
  const plRenderGraphTextureHandle hWaterDepth = ref_graph.CreateTexture(waterDepthDesc);
  const plRenderGraphTextureHandle hWaterExitDepth = ref_graph.CreateTexture(waterDepthDesc);
  outputs[m_PinWaterDepth.m_uiOutputIndex].m_TextureHandle = hWaterDepth;
  outputs[m_PinWaterExitDepth.m_uiOutputIndex].m_TextureHandle = hWaterExitDepth;

  const bool bSimulationSettingsChanged = !m_bSimulationInitialized ||
                                          m_SimulationQuality.m_Tier != quality.m_Tier ||
                                          m_SimulationQuality.m_uiSpectrumCascadeCount != quality.m_uiSpectrumCascadeCount ||
                                          m_SimulationQuality.m_uiSpectrumResolution != quality.m_uiSpectrumResolution ||
                                          m_SimulationQuality.m_uiInteractionAtlasSize != quality.m_uiInteractionAtlasSize;
  if (bSimulationSettingsChanged)
  {
    m_bSimulationInitialized = m_GpuSimulation.Initialize(quality);
    m_SimulationQuality = quality;
  }

  plRenderGraphTextureHandle hSimulationSpectrum;
  plRenderGraphTextureHandle hSimulationSlope;
  plRenderGraphTextureHandle hSimulationInteraction;
  if (m_bSimulationInitialized)
  {
    RefreshGpuSpectrumProfiles();

    m_GpuSimulation.AddRenderPasses(ref_graph, GetPipeline(), plMakeDelegate(&plWaterSurfacePass::RefreshGpuSpectrumProfiles, this));

    if (!m_GpuSimulation.GetSpectrumDisplacement().IsInvalidated())
      hSimulationSpectrum = ref_graph.ImportTexture(m_GpuSimulation.GetSpectrumDisplacement());
    if (!m_GpuSimulation.GetSpectrumSlope().IsInvalidated())
      hSimulationSlope = ref_graph.ImportTexture(m_GpuSimulation.GetSpectrumSlope());
    if (!m_GpuSimulation.GetInteractionAtlas().IsInvalidated())
      hSimulationInteraction = ref_graph.ImportTexture(m_GpuSimulation.GetInteractionAtlas());
  }

  // The profile table: every water draw of this frame reads its profiles from it by index.
  if (m_hProfileTable.IsInvalidated())
  {
    plGALBufferCreationDescription tableDesc;
    tableDesc.m_uiStructSize = sizeof(plWaterGpuProfile);
    tableDesc.m_uiTotalSize = WATER_MAX_PROFILES * tableDesc.m_uiStructSize;
    tableDesc.m_BufferFlags = plGALBufferUsageFlags::StructuredBuffer | plGALBufferUsageFlags::ShaderResource;
    tableDesc.m_ResourceAccess.m_bImmutable = false;
    m_hProfileTable = ref_graph.GetDevice()->CreateBuffer(tableDesc);
    if (m_hProfileTable.IsInvalidated())
      return plStatus(plFmt("WaterSurfacePass: the profile table buffer could not be created"));
  }
  const plRenderGraphBufferHandle hProfileTable = ref_graph.ImportBuffer(m_hProfileTable);
  {
    // Executes every frame, also when the graph itself is served from the cache: profiles are edited live.
    auto pass = ref_graph.AddTransferPass("Water Profile Upload");
    pass.WriteBuffer(hProfileTable, plGALResourceState::CopyDestination);
    pass.SetExecuteCallback([=](const plRenderGraphContext& ctx) {
      const plRenderViewContext& renderViewContext = *ctx.GetUserData<plRenderViewContext>();
      FetchPerFrameWaterData(renderViewContext);
      UploadProfileTable(ctx, hProfileTable);
    });
  }
  const plBitflags<plGALShaderStageFlags> profileTableStages = plGALShaderStageFlags::VertexShader | plGALShaderStageFlags::DomainShader | plGALShaderStageFlags::PixelShader;

  // Water attributes are rendered without attaching the potentially multisampled opaque depth. The shader compares
  // against ResolvedDepth and discards occluded fragments, leaving single-sample attributes suitable for compute.
  {
    auto pass = ref_graph.AddGraphicsPass("Water Attributes");
    pass.AddColorTarget(hWaterDepth, {}, plGALRenderTargetLoadOp::Clear);
    pass.SetClearColor(0, plColor::Black);
    pass.ReadTexture(hResolvedDepth, {}, plGALResourceState::ShaderRead, plGALShaderStageFlags::PixelShader);
    pass.ReadBuffer(hProfileTable, plGALResourceState::ShaderRead, profileTableStages);
    if (!hSimulationSpectrum.IsInvalidated())
      pass.ReadTexture(hSimulationSpectrum, {}, plGALResourceState::ShaderRead,
        plGALShaderStageFlags::VertexShader | plGALShaderStageFlags::DomainShader | plGALShaderStageFlags::PixelShader);
    if (!hSimulationSlope.IsInvalidated())
      pass.ReadTexture(hSimulationSlope, {}, plGALResourceState::ShaderRead,
        plGALShaderStageFlags::VertexShader | plGALShaderStageFlags::DomainShader | plGALShaderStageFlags::PixelShader);
    if (!hSimulationInteraction.IsInvalidated())
      pass.ReadTexture(hSimulationInteraction, {}, plGALResourceState::ShaderRead,
        plGALShaderStageFlags::VertexShader | plGALShaderStageFlags::DomainShader);
    pass.SetStereoscopic(camera.IsStereoscopic());
    DeclareRenderObjectDependencies(ref_graph, pass);
    pass.SetExecuteCallback([=](const plRenderGraphContext& ctx) {
      const plRenderViewContext& renderViewContext = *ctx.GetUserData<plRenderViewContext>();
      renderViewContext.UpdateViewport();
      FetchPerFrameWaterData(renderViewContext);
      ClearRenderPassResources(renderViewContext);
      SetupPermutationVars(renderViewContext);
      if (quality.m_bUseScreenSpaceReflections)
        renderViewContext.m_pRenderContext->SetShaderPermutationVariable("WATER_USE_SSR", "TRUE");
      else
        renderViewContext.m_pRenderContext->SetShaderPermutationVariable("WATER_USE_SSR", "FALSE");
      renderViewContext.m_pRenderContext->SetShaderPermutationVariable("RENDER_PASS", "RENDER_PASS_FORWARD");
      renderViewContext.m_pRenderContext->SetShaderPermutationVariable("WATER_RENDER_PASS", "WATER_RENDER_PASS_ATTRIBUTES");
      renderViewContext.m_pRenderContext->GetBindGroup(PL_GAL_BIND_GROUP_RENDER_PASS).BindTexture("WaterOpaqueDepth", ctx.ResolveTexture(hResolvedDepth));
      renderViewContext.m_pRenderContext->GetBindGroup(PL_GAL_BIND_GROUP_RENDER_PASS).BindBuffer("WaterProfiles", ctx.ResolveBuffer(hProfileTable));
      renderViewContext.m_pRenderContext->GetBindGroup(PL_GAL_BIND_GROUP_RENDER_PASS).BindTexture("WaterSpectrumDisplacement", hSimulationSpectrum.IsInvalidated() ? plGALTextureHandle() : ctx.ResolveTexture(hSimulationSpectrum));
      renderViewContext.m_pRenderContext->GetBindGroup(PL_GAL_BIND_GROUP_RENDER_PASS).BindTexture("WaterSpectrumSlope", hSimulationSlope.IsInvalidated() ? plGALTextureHandle() : ctx.ResolveTexture(hSimulationSlope));
      renderViewContext.m_pRenderContext->GetBindGroup(PL_GAL_BIND_GROUP_RENDER_PASS).BindTexture("WaterInteractionAtlas", hSimulationInteraction.IsInvalidated() ? plGALTextureHandle() : ctx.ResolveTexture(hSimulationInteraction));
      RenderObjects(renderViewContext);
    });
  }

  // Finite-volume proxies are rendered separately so they cannot overwrite the top-surface attributes.
  // Back-to-front water sorting leaves the nearest positive exit as the last value for an underwater camera.
  {
    auto pass = ref_graph.AddGraphicsPass("Water Volume Exit Depth");
    pass.AddColorTarget(hWaterExitDepth, {}, plGALRenderTargetLoadOp::Clear);
    pass.SetClearColor(0, plColor::Black);
    pass.ReadBuffer(hProfileTable, plGALResourceState::ShaderRead, profileTableStages);
    if (!hSimulationSpectrum.IsInvalidated())
      pass.ReadTexture(hSimulationSpectrum, {}, plGALResourceState::ShaderRead,
        plGALShaderStageFlags::VertexShader | plGALShaderStageFlags::DomainShader | plGALShaderStageFlags::PixelShader);
    if (!hSimulationSlope.IsInvalidated())
      pass.ReadTexture(hSimulationSlope, {}, plGALResourceState::ShaderRead,
        plGALShaderStageFlags::VertexShader | plGALShaderStageFlags::DomainShader | plGALShaderStageFlags::PixelShader);
    if (!hSimulationInteraction.IsInvalidated())
      pass.ReadTexture(hSimulationInteraction, {}, plGALResourceState::ShaderRead,
        plGALShaderStageFlags::VertexShader | plGALShaderStageFlags::DomainShader);
    pass.SetStereoscopic(camera.IsStereoscopic());
    DeclareRenderObjectDependencies(ref_graph, pass);
    pass.SetExecuteCallback([=](const plRenderGraphContext& ctx) {
      const plRenderViewContext& renderViewContext = *ctx.GetUserData<plRenderViewContext>();
      if (plWaterFrameDataProvider* pProvider = GetPipeline()->GetFrameDataProvider<plWaterFrameDataProvider>())
      {
        if (const plWaterFrameData* pFrameData = pProvider->GetData(renderViewContext))
        {
          // Exit depth is only consumed by the underwater composite. The clear still executes, but avoiding
          // all finite-volume draws removes a complete geometry pass for the overwhelmingly common view.
          if (!pFrameData->m_bHasWaterData || !pFrameData->m_bCameraUnderwater)
            return;
        }
      }
      renderViewContext.UpdateViewport();
      FetchPerFrameWaterData(renderViewContext);
      ClearRenderPassResources(renderViewContext);
      SetupPermutationVars(renderViewContext);
      if (quality.m_bUseScreenSpaceReflections)
        renderViewContext.m_pRenderContext->SetShaderPermutationVariable("WATER_USE_SSR", "TRUE");
      else
        renderViewContext.m_pRenderContext->SetShaderPermutationVariable("WATER_USE_SSR", "FALSE");
      renderViewContext.m_pRenderContext->SetShaderPermutationVariable("RENDER_PASS", "RENDER_PASS_FORWARD");
      renderViewContext.m_pRenderContext->SetShaderPermutationVariable("WATER_RENDER_PASS", "WATER_RENDER_PASS_VOLUME");
      renderViewContext.m_pRenderContext->GetBindGroup(PL_GAL_BIND_GROUP_RENDER_PASS).BindBuffer("WaterProfiles", ctx.ResolveBuffer(hProfileTable));
      renderViewContext.m_pRenderContext->GetBindGroup(PL_GAL_BIND_GROUP_RENDER_PASS).BindTexture("WaterSpectrumDisplacement", hSimulationSpectrum.IsInvalidated() ? plGALTextureHandle() : ctx.ResolveTexture(hSimulationSpectrum));
      renderViewContext.m_pRenderContext->GetBindGroup(PL_GAL_BIND_GROUP_RENDER_PASS).BindTexture("WaterSpectrumSlope", hSimulationSlope.IsInvalidated() ? plGALTextureHandle() : ctx.ResolveTexture(hSimulationSlope));
      renderViewContext.m_pRenderContext->GetBindGroup(PL_GAL_BIND_GROUP_RENDER_PASS).BindTexture("WaterInteractionAtlas", hSimulationInteraction.IsInvalidated() ? plGALTextureHandle() : ctx.ResolveTexture(hSimulationInteraction));
      RenderObjects(renderViewContext);
    });
  }

  plRenderGraphTextureHandle hCausticLighting;
  // View diagnostics must show the selected material / lighting channel without a later additive water effect
  // changing opaque receivers underneath it. The ordinary surface draw still runs with RENDER_PASS_EDITOR or
  // RENDER_PASS_WIREFRAME and supplies the water-specific visualization.
  const bool bUseProjectedCaustics = viewData.m_ViewRenderMode == plViewRenderMode::None &&
                                      (quality.m_bUseTransmittedCaustics || quality.m_bUseReflectedCaustics);
  if (bUseProjectedCaustics)
  {
    const float fResolutionScale = plMath::Clamp(quality.m_fCausticResolutionScale, 0.25f, 1.0f);
    const plUInt32 uiCausticWidth = plMath::Max(1u, static_cast<plUInt32>(plMath::Ceil(uiWidth * fResolutionScale)));
    const plUInt32 uiCausticHeight = plMath::Max(1u, static_cast<plUInt32>(plMath::Ceil(uiHeight * fResolutionScale)));
    const plUInt32 uiFieldResolution = quality.m_Tier == plWaterQualityTier::Ultra ? 1024u : quality.m_Tier == plWaterQualityTier::High ? 512u : 256u;
    const float fFieldHalfExtent = quality.m_Tier == plWaterQualityTier::Ultra ? 192.0f : quality.m_Tier == plWaterQualityTier::High ? 128.0f : 96.0f;
    const float fFieldTexelSize = (fFieldHalfExtent * 2.0f) / static_cast<float>(uiFieldResolution);
    const float fMaxDistance = quality.m_Tier == plWaterQualityTier::Ultra ? 32.0f : quality.m_Tier == plWaterQualityTier::High ? 24.0f : 16.0f;

    plGALTextureCreationDescription fieldHeightDesc = MakeWaterTextureDesc(colorDesc, uiFieldResolution, uiFieldResolution, plGALResourceFormat::RFloat);
    fieldHeightDesc.m_uiArraySize = 1;
    const plRenderGraphTextureHandle hCausticFieldHeight = ref_graph.CreateTexture(fieldHeightDesc);
    plGALTextureCreationDescription fieldDataDesc = MakeWaterTextureDesc(colorDesc, uiFieldResolution, uiFieldResolution, plGALResourceFormat::RGBAHalf);
    fieldDataDesc.m_uiArraySize = 1;
    const plRenderGraphTextureHandle hCausticFieldData = ref_graph.CreateTexture(fieldDataDesc);

    // Render the real displaced surface into a camera-centered world-space field. Unlike the view-space water
    // attributes, this source remains available behind a ceiling/bridge and is shared by both stereo eyes.
    {
      auto fieldPass = ref_graph.AddGraphicsPass("Water Caustic Field");
      fieldPass.AddColorTarget(hCausticFieldHeight, {}, plGALRenderTargetLoadOp::Clear);
      fieldPass.AddColorTarget(hCausticFieldData, {}, plGALRenderTargetLoadOp::Clear);
      fieldPass.SetClearColor(0, plColor::Black);
      fieldPass.SetClearColor(1, plColor::Black);
      fieldPass.ReadBuffer(hProfileTable, plGALResourceState::ShaderRead, profileTableStages);
      if (!hSimulationSpectrum.IsInvalidated())
        fieldPass.ReadTexture(hSimulationSpectrum, {}, plGALResourceState::ShaderRead,
          plGALShaderStageFlags::VertexShader | plGALShaderStageFlags::DomainShader | plGALShaderStageFlags::PixelShader);
      if (!hSimulationSlope.IsInvalidated())
        fieldPass.ReadTexture(hSimulationSlope, {}, plGALResourceState::ShaderRead,
          plGALShaderStageFlags::VertexShader | plGALShaderStageFlags::DomainShader | plGALShaderStageFlags::PixelShader);
      if (!hSimulationInteraction.IsInvalidated())
        fieldPass.ReadTexture(hSimulationInteraction, {}, plGALResourceState::ShaderRead,
          plGALShaderStageFlags::VertexShader | plGALShaderStageFlags::DomainShader);
      fieldPass.SetStereoscopic(false);
      DeclareRenderObjectDependencies(ref_graph, fieldPass);
      fieldPass.SetExecuteCallback([=](const plRenderGraphContext& ctx) {
        const plRenderViewContext& renderViewContext = *ctx.GetUserData<plRenderViewContext>();
        FetchPerFrameWaterData(renderViewContext);
        RefreshGpuSpectrumProfiles();

        const bool bHasLight = !m_vMainLightColorIntensity.IsZero(0.0001f) && !m_vMainLightDirection.IsZero(0.0001f);
        const bool bFallbackValid = m_vCausticFallbackCenterHeightValidity.w > 0.5f;
        const bool bHasTransmittedSource = quality.m_bUseTransmittedCaustics &&
                                           (m_bHasTransmittedCausticSources || (bFallbackValid && m_vCausticFallbackStrengths.x > 0.0f));
        const bool bHasReflectedSource = quality.m_bUseReflectedCaustics &&
                                         (m_bHasReflectedCausticSources || (bFallbackValid && m_vCausticFallbackStrengths.y > 0.0f));
        if (!bHasLight || (!bHasTransmittedSource && !bHasReflectedSource))
          return;

        ClearRenderPassResources(renderViewContext);
        SetupPermutationVars(renderViewContext);
        if (quality.m_bUseScreenSpaceReflections)
          renderViewContext.m_pRenderContext->SetShaderPermutationVariable("WATER_USE_SSR", "TRUE");
        else
          renderViewContext.m_pRenderContext->SetShaderPermutationVariable("WATER_USE_SSR", "FALSE");
        renderViewContext.m_pRenderContext->SetShaderPermutationVariable("RENDER_PASS", "RENDER_PASS_FORWARD");
        renderViewContext.m_pRenderContext->SetShaderPermutationVariable("WATER_RENDER_PASS", "WATER_RENDER_PASS_CAUSTIC_FIELD");

        const plVec3 vCameraPosition = renderViewContext.m_pCamera != nullptr
                                         ? renderViewContext.m_pCamera->GetCenterPosition()
                                         : plVec3::MakeZero();
        const plVec2 vFieldCenter(plMath::Floor(vCameraPosition.x / fFieldTexelSize) * fFieldTexelSize,
          plMath::Floor(vCameraPosition.y / fFieldTexelSize) * fFieldTexelSize);
        m_vCausticFieldCenterAndExtent = plVec4(vFieldCenter.x, vFieldCenter.y, fFieldHalfExtent, 1.0f / fFieldHalfExtent);
        plWaterCausticFieldConstants* fieldConstants =
          plRenderContext::GetConstantBufferData<plWaterCausticFieldConstants>(m_hCausticFieldConstants);
        fieldConstants->WaterCausticFieldCenterAndExtent = m_vCausticFieldCenterAndExtent;

        plBindGroupBuilder& bindGroup = renderViewContext.m_pRenderContext->GetBindGroup(PL_GAL_BIND_GROUP_RENDER_PASS);
        bindGroup.BindBuffer("plWaterCausticFieldConstants", m_hCausticFieldConstants);
        bindGroup.BindBuffer("WaterProfiles", ctx.ResolveBuffer(hProfileTable));
        bindGroup.BindTexture("WaterSpectrumDisplacement", hSimulationSpectrum.IsInvalidated() ? plGALTextureHandle() : ctx.ResolveTexture(hSimulationSpectrum));
        bindGroup.BindTexture("WaterSpectrumSlope", hSimulationSlope.IsInvalidated() ? plGALTextureHandle() : ctx.ResolveTexture(hSimulationSlope));
        bindGroup.BindTexture("WaterInteractionAtlas", hSimulationInteraction.IsInvalidated() ? plGALTextureHandle() : ctx.ResolveTexture(hSimulationInteraction));
        RenderObjects(renderViewContext);
      });
    }

    plGALTextureCreationDescription causticDesc = MakeWaterTextureDesc(colorDesc, uiCausticWidth, uiCausticHeight, plGALResourceFormat::RGBAHalf);
    causticDesc.m_bAllowRenderTargetView = false;
    causticDesc.m_bAllowUAV = true;
    hCausticLighting = ref_graph.CreateTexture(causticDesc);

    {
      auto pass = ref_graph.AddComputePass("Water Caustic Trace");
      pass.ReadTexture(hResolvedDepth, {}, plGALResourceState::ComputeShaderRead, plGALShaderStageFlags::ComputeShader);
      pass.ReadTexture(hCausticFieldHeight, {}, plGALResourceState::ComputeShaderRead, plGALShaderStageFlags::ComputeShader);
      pass.ReadTexture(hCausticFieldData, {}, plGALResourceState::ComputeShaderRead, plGALShaderStageFlags::ComputeShader);
      if (!hGBuffer1.IsInvalidated())
        pass.ReadTexture(hGBuffer1, {}, plGALResourceState::ComputeShaderRead, plGALShaderStageFlags::ComputeShader);
      if (!hScreenSpaceShadow.IsInvalidated())
        pass.ReadTexture(hScreenSpaceShadow, {}, plGALResourceState::ComputeShaderRead, plGALShaderStageFlags::ComputeShader);
      pass.WriteTexture(hCausticLighting, {}, plGALResourceState::UnorderedAccessWrite, plGALShaderStageFlags::ComputeShader);
      pass.SetExecuteCallback([=](const plRenderGraphContext& ctx) {
        const plRenderViewContext& renderViewContext = *ctx.GetUserData<plRenderViewContext>();
        FetchPerFrameWaterData(renderViewContext);
        SetupPermutationVars(renderViewContext);

        const bool bHasLight = !m_vMainLightColorIntensity.IsZero(0.0001f) && !m_vMainLightDirection.IsZero(0.0001f);
        const bool bHasGBuffer1 = !hGBuffer1.IsInvalidated();
        bool bHasShadow = false;
        plUInt32 uiShadowSlotsPerEye = 0;
        if (!hScreenSpaceShadow.IsInvalidated())
        {
          // The texture is imported while the graph is built, but whether the sun owns slot 0 is only known once the
          // slot selection ran for this view. Without a traced sun the layers hold other lights' shadows.
          if (plContactShadowDataProvider* pShadowProvider = GetPipeline()->GetFrameDataProvider<plContactShadowDataProvider>())
          {
            const plContactShadowData* pShadowData = pShadowProvider->GetData(renderViewContext);
            uiShadowSlotsPerEye = uiShadowLayersTotal / uiArraySize;
            bHasShadow = pShadowData != nullptr && !pShadowProvider->GetTexture().IsInvalidated() && !pShadowData->m_Slots.IsEmpty() &&
                         pShadowData->m_Slots[0].m_bSun && uiShadowSlotsPerEye > 0;
          }
        }
        const bool bFallbackValid = m_vCausticFallbackCenterHeightValidity.w > 0.5f;
        const bool bHasTransmittedSource = quality.m_bUseTransmittedCaustics &&
                                           (m_bHasTransmittedCausticSources || (bFallbackValid && m_vCausticFallbackStrengths.x > 0.0f));
        const bool bHasReflectedSource = quality.m_bUseReflectedCaustics &&
                                         (m_bHasReflectedCausticSources || (bFallbackValid && m_vCausticFallbackStrengths.y > 0.0f));
        const bool bHasSource = bHasTransmittedSource || bHasReflectedSource;
        m_bCausticTraceExecuted = false;
        if (!bHasLight || !bHasSource)
          return;

        plWaterCausticsConstants* constants = plRenderContext::GetConstantBufferData<plWaterCausticsConstants>(m_hCausticsConstants);
        constants->WaterCausticLightDirectionAndValidity = plVec4(m_vMainLightDirection.x, m_vMainLightDirection.y,
          m_vMainLightDirection.z, bHasLight ? 1.0f : 0.0f);
        constants->WaterCausticLightColorAndScale = plVec4(m_vMainLightColorIntensity.x, m_vMainLightColorIntensity.y,
          m_vMainLightColorIntensity.z, 0.35f);
        constants->WaterCausticTextureParams = plVec4(1.0f / static_cast<float>(uiWidth), 1.0f / static_cast<float>(uiHeight),
          1.0f / static_cast<float>(uiCausticWidth), 1.0f / static_cast<float>(uiCausticHeight));
        // The inverse trace cannot reconstruct a footprint narrower than the rasterized source field. Matching
        // the kernel/tolerance to one field texel avoids sub-texel sparkle and stabilizes projected energy.
        constants->WaterCausticTraceParams = plVec4(fMaxDistance, fFieldTexelSize, bHasSource ? 1.0f : 0.0f, static_cast<float>(uiShadowSlotsPerEye));
        constants->WaterCausticFieldCenterAndExtent = m_vCausticFieldCenterAndExtent;
        constants->WaterCausticFallbackBounds = m_vCausticFallbackBounds;
        constants->WaterCausticFallbackCenterHeightValidity = m_vCausticFallbackCenterHeightValidity;
        constants->WaterCausticFallbackWaveParams = m_vCausticFallbackWaveParams;
        constants->WaterCausticFallbackStrengths = plVec4(m_vCausticFallbackStrengths.x, m_vCausticFallbackStrengths.y, 0.0f, 0.0f);
        constants->WaterCausticFallbackWindTimeTexel = plVec4(m_vCausticFallbackWindTime.x, m_vCausticFallbackWindTime.y,
          m_vCausticFallbackWindTime.z, fFieldTexelSize);
        constants->WaterCausticFeatureParams = plVec4(bHasGBuffer1 ? 1.0f : 0.0f, bHasShadow ? 1.0f : 0.0f,
          bHasTransmittedSource ? 1.0f : 0.0f, bHasReflectedSource ? 1.0f : 0.0f);
        constants->WaterCausticDispatchParams = plVec4U32(uiCausticWidth, uiCausticHeight, uiArraySize, quality.m_uiCausticRaySteps);

        plBindGroupBuilder& bindGroup = renderViewContext.m_pRenderContext->GetBindGroup();
        bindGroup.BindBuffer("plWaterCausticsConstants", m_hCausticsConstants);
        bindGroup.BindTexture("WaterCausticSceneDepth", ctx.ResolveTexture(hResolvedDepth));
        bindGroup.BindTexture("WaterCausticFieldHeight", ctx.ResolveTexture(hCausticFieldHeight));
        bindGroup.BindTexture("WaterCausticFieldData", ctx.ResolveTexture(hCausticFieldData));
        bindGroup.BindTexture("WaterCausticGBuffer1", bHasGBuffer1 ? ctx.ResolveTexture(hGBuffer1) : ctx.ResolveTexture(hResolvedDepth));
        bindGroup.BindTexture("WaterCausticShadow", bHasShadow ? ctx.ResolveTexture(hScreenSpaceShadow) : ctx.ResolveTexture(hResolvedDepth));
        bindGroup.BindTexture("WaterCausticOutput", ctx.ResolveTexture(hCausticLighting));
        renderViewContext.m_pRenderContext->BindShader(m_hCausticsShader);
        m_bCausticTraceExecuted = renderViewContext.m_pRenderContext->Dispatch(
                                                                      (uiCausticWidth + 7u) / 8u, (uiCausticHeight + 7u) / 8u, uiArraySize)
                                    .Succeeded();
      });
    }

    auto compositePass = ref_graph.AddGraphicsPass("Water Caustic Composite");
    compositePass.AddColorTarget(hColor);
    compositePass.ReadTexture(hCausticLighting, {}, plGALResourceState::ShaderRead, plGALShaderStageFlags::PixelShader);
    compositePass.ReadTexture(hResolvedDepth, {}, plGALResourceState::ShaderRead, plGALShaderStageFlags::PixelShader);
    compositePass.SetStereoscopic(camera.IsStereoscopic());
    compositePass.SetExecuteCallback([=](const plRenderGraphContext& ctx) {
      if (!m_bCausticTraceExecuted)
        return;

      const plRenderViewContext& renderViewContext = *ctx.GetUserData<plRenderViewContext>();
      renderViewContext.UpdateViewport();
      SetupPermutationVars(renderViewContext);
      plBindGroupBuilder& bindGroup = renderViewContext.m_pRenderContext->GetBindGroup();
      bindGroup.BindBuffer("plWaterCausticsConstants", m_hCausticsConstants);
      bindGroup.BindTexture("WaterCausticLighting", ctx.ResolveTexture(hCausticLighting));
      bindGroup.BindTexture("WaterCausticResolvedDepth", ctx.ResolveTexture(hResolvedDepth));
      renderViewContext.m_pRenderContext->BindShader(m_hCausticsCompositeShader);
      renderViewContext.m_pRenderContext->BindNullMeshBuffer(plGALPrimitiveTopology::Triangles, 1);
      renderViewContext.m_pRenderContext->DrawMeshBuffer().IgnoreResult();
    });
  }

  plGALTextureCreationDescription sceneColorDesc = colorDesc;
  sceneColorDesc.m_Type = plGALTextureType::Texture2DArray;
  sceneColorDesc.m_uiArraySize = uiArraySize;
  sceneColorDesc.m_uiMipLevelCount = 1;
  sceneColorDesc.m_SampleCount = plGALMSAASampleCount::None;
  const plRenderGraphTextureHandle hSceneColor = ref_graph.CreateTexture(sceneColorDesc);

  {
    auto pass = ref_graph.AddTransferPass("Water Copy Scene Color");
    pass.ReadTexture(hColor, {}, plGALResourceState::ResolveSource);
    pass.WriteTexture(hSceneColor, {}, plGALResourceState::ResolveDestination);
    pass.SetExecuteCallback([=](const plRenderGraphContext& ctx) {
      if (GetPipeline()->GetRenderDataBatchesWithCategory(plWaterRenderData::WaterSurface).GetBatchCount() == 0)
        return;

      for (plUInt32 uiSlice = 0; uiSlice < uiArraySize; ++uiSlice)
      {
        plGALTextureSubresource subresource;
        subresource.m_uiMipLevel = 0;
        subresource.m_uiArraySlice = uiSlice;
        ctx.GetCommandEncoder()->ResolveTexture(ctx.ResolveTexture(hSceneColor), subresource, ctx.ResolveTexture(hColor), subresource);
      }
    });
  }

  if (ref_graph.GetTextureDesc(hDepthStencil).m_SampleCount != colorDesc.m_SampleCount)
    return plStatus(plFmt("WaterSurfacePass: DepthStencil sample count must match the Color target"));

  {
    auto pass = ref_graph.AddGraphicsPass("Water Surface");
    pass.AddColorTarget(hColor);
    // Attach the opaque depth so hardware early-Z rejects occluded water before the pixel shader runs.
    // The shader renderstate keeps DepthWrite off, so the buffer contents are preserved for later passes.
    // (A true read-only depth attachment is not supported by the Vulkan backend yet.)
    pass.AddDepthStencilTarget(hDepthStencil);
    pass.ReadTexture(hSceneColor, {}, plGALResourceState::ShaderRead, plGALShaderStageFlags::PixelShader);
    pass.ReadBuffer(hProfileTable, plGALResourceState::ShaderRead, profileTableStages);
    pass.ReadTexture(hResolvedDepth, {}, plGALResourceState::ShaderRead, plGALShaderStageFlags::PixelShader);
    if (!hSimulationSpectrum.IsInvalidated())
      pass.ReadTexture(hSimulationSpectrum, {}, plGALResourceState::ShaderRead,
        plGALShaderStageFlags::VertexShader | plGALShaderStageFlags::DomainShader | plGALShaderStageFlags::PixelShader);
    if (!hSimulationSlope.IsInvalidated())
      pass.ReadTexture(hSimulationSlope, {}, plGALResourceState::ShaderRead,
        plGALShaderStageFlags::VertexShader | plGALShaderStageFlags::DomainShader | plGALShaderStageFlags::PixelShader);
    if (!hSimulationInteraction.IsInvalidated())
      pass.ReadTexture(hSimulationInteraction, {}, plGALResourceState::ShaderRead,
        plGALShaderStageFlags::VertexShader | plGALShaderStageFlags::DomainShader);
    pass.SetStereoscopic(camera.IsStereoscopic());
    DeclareRenderObjectDependencies(ref_graph, pass);
    pass.SetExecuteCallback([=](const plRenderGraphContext& ctx) {
      if (GetPipeline()->GetRenderDataBatchesWithCategory(plWaterRenderData::WaterSurface).GetBatchCount() == 0)
        return;

      const plRenderViewContext& renderViewContext = *ctx.GetUserData<plRenderViewContext>();
      renderViewContext.UpdateViewport();
      FetchPerFrameWaterData(renderViewContext);
      ClearRenderPassResources(renderViewContext);
      SetupPermutationVars(renderViewContext);
      SetupLighting(renderViewContext);
      if (quality.m_bUseScreenSpaceReflections)
        renderViewContext.m_pRenderContext->SetShaderPermutationVariable("WATER_USE_SSR", "TRUE");
      else
        renderViewContext.m_pRenderContext->SetShaderPermutationVariable("WATER_USE_SSR", "FALSE");
      renderViewContext.m_pRenderContext->SetShaderPermutationVariable("WATER_RENDER_PASS", "WATER_RENDER_PASS_SURFACE");
      plBindGroupBuilder& bindGroup = renderViewContext.m_pRenderContext->GetBindGroup(PL_GAL_BIND_GROUP_RENDER_PASS);
      bindGroup.BindTexture("WaterSceneColor", ctx.ResolveTexture(hSceneColor));
      bindGroup.BindBuffer("WaterProfiles", ctx.ResolveBuffer(hProfileTable));
      bindGroup.BindTexture("WaterOpaqueDepth", ctx.ResolveTexture(hResolvedDepth));
      bindGroup.BindTexture("WaterSpectrumDisplacement", hSimulationSpectrum.IsInvalidated() ? plGALTextureHandle() : ctx.ResolveTexture(hSimulationSpectrum));
      bindGroup.BindTexture("WaterSpectrumSlope", hSimulationSlope.IsInvalidated() ? plGALTextureHandle() : ctx.ResolveTexture(hSimulationSlope));
      bindGroup.BindTexture("WaterInteractionAtlas", hSimulationInteraction.IsInvalidated() ? plGALTextureHandle() : ctx.ResolveTexture(hSimulationInteraction));
      RenderObjects(renderViewContext);
    });
  }

  return plStatus(PL_SUCCESS);
}

void plWaterSurfacePass::RefreshGpuSpectrumProfiles()
{
  if (GetPipeline() == nullptr)
    return;

  if (m_bSimulationInitialized)
    m_GpuSimulation.ClearSpectrumProfiles();
  m_GpuProfileIds.Clear();
  m_GpuProfileSlots.Clear();
  m_bHasTransmittedCausticSources = false;
  m_bHasReflectedCausticSources = false;
  plHybridArray<plUInt32, plWaterGpuSimulation::MaxSpectrumProfiles> registeredProfiles;
  const plRenderDataBatchList waterBatches = GetPipeline()->GetRenderDataBatchesWithCategory(plWaterRenderData::WaterSurface);
  for (plUInt32 uiBatch = 0; uiBatch < waterBatches.GetBatchCount(); ++uiBatch)
  {
    for (auto it = waterBatches.GetBatch(uiBatch).GetIterator<plWaterRenderData>(); it.IsValid(); ++it)
    {
      m_bHasTransmittedCausticSources |= it->m_fUnderwaterCaustics > 0.0f;
      m_bHasReflectedCausticSources |= it->m_fReflectedCaustics > 0.0f;

      if (!m_bSimulationInitialized || it->m_uiGpuCascadeCount == 0 ||
          registeredProfiles.GetCount() >= plWaterGpuSimulation::MaxSpectrumProfiles)
        continue;

      bool bAlreadyRegistered = false;
      for (plUInt32 uiProfile : registeredProfiles)
        bAlreadyRegistered |= uiProfile == it->m_uiProfileIndex;
      if (bAlreadyRegistered)
        continue;

      plWaterProfileResourceDescriptor profile;
      profile.m_fWaveAmplitude = it->m_fWaveAmplitude;
      profile.m_fBaseWavelength = it->m_fWaveLength;
      profile.m_fWindSpeed = it->m_fWaveSpeed;
      profile.m_fWaveSteepness = it->m_fChoppiness;
      profile.m_vWindDirection = it->m_vWindDirection.IsZero() ? plVec2(1.0f, 0.0f) : it->m_vWindDirection.GetNormalized();
      profile.m_uiWaveSeed = it->m_uiWaveSeed;
      profile.m_SpectrumMode = static_cast<plWaterWaveSpectrumMode::Enum>(it->m_uiSpectrumMode);
      profile.m_fSmallestGpuWavelength = plMath::Max(it->m_fSmallestGpuWavelength, 0.01f);
      profile.m_fFoamCrestThreshold = it->m_fFoamCrestThreshold;
      profile.m_uiGpuCascadeCount = static_cast<plUInt8>(plMath::Min<plUInt32>(it->m_uiGpuCascadeCount, m_SimulationQuality.m_uiSpectrumCascadeCount));
      profile.m_uiGpuCascadeResolution = m_SimulationQuality.m_uiSpectrumResolution;
      // Compact live profiles into the lowest slots so the spectrum pass can trim trailing array layers.
      const plUInt32 uiGpuSlot = m_GpuSimulation.RegisterSpectrumProfile(profile);
      if (uiGpuSlot != plInvalidIndex)
      {
        m_GpuProfileIds.PushBack(it->m_uiProfileIndex);
        m_GpuProfileSlots.PushBack(uiGpuSlot);
      }
      registeredProfiles.PushBack(it->m_uiProfileIndex);
    }
  }
}

void plWaterSurfacePass::UploadProfileTable(const plRenderGraphContext& ctx, plRenderGraphBufferHandle hProfileTable)
{
  // Which profiles have a slot in the spectrum simulation is decided from this frame's draws.
  RefreshGpuSpectrumProfiles();

  plWaterGpuProfile table[WATER_MAX_PROFILES];
  plUInt32 uiCount = 0;
  if (plWaterFrameDataProvider* pProvider = GetPipeline()->GetFrameDataProvider<plWaterFrameDataProvider>())
  {
    const plRenderViewContext& renderViewContext = *ctx.GetUserData<plRenderViewContext>();
    if (const plWaterFrameData* pFrameData = pProvider->GetData(renderViewContext))
    {
      uiCount = plMath::Min<plUInt32>(pFrameData->m_uiProfileCount, WATER_MAX_PROFILES);
      plMemoryUtils::RawByteCopy(table, pFrameData->m_Profiles, sizeof(plWaterGpuProfile) * uiCount);
    }
  }
  if (uiCount == 0)
  {
    // A pipeline without the water extractor still draws: every index then reads the default profile.
    plWaterFillGpuProfile(table[0], plWaterProfileResourceDescriptor(), plWaterBodyType::Ocean, 0.0f);
    uiCount = 1;
  }

  const plUInt32 uiAvailableCascades = GetSpectrumLayerCount() / plWaterGpuSimulation::MaxSpectrumProfiles;
  for (plUInt32 i = 0; i < uiCount; ++i)
  {
    const plUInt32 uiSlot = GetGpuProfileSlot(i);
    const plUInt32 uiWanted = static_cast<plUInt32>(table[i].SpectrumParams.y);
    table[i].SpectrumParams.x = uiSlot != plInvalidIndex ? static_cast<float>(uiSlot) : 0.0f;
    table[i].SpectrumParams.y = uiSlot != plInvalidIndex ? static_cast<float>(plMath::Min(uiWanted, uiAvailableCascades)) : 0.0f;
  }
  m_uiProfileCount = uiCount;

  // AheadOfTime: the default update mode only delivers to transient constant buffers.
  ctx.GetCommandEncoder()->UpdateBuffer(ctx.ResolveBuffer(hProfileTable), 0,
    plArrayPtr<const plUInt8>(reinterpret_cast<const plUInt8*>(table), uiCount * sizeof(plWaterGpuProfile)), plGALUpdateMode::AheadOfTime);
}

void plWaterSurfacePass::FetchPerFrameWaterData(const plRenderViewContext& renderViewContext)
{
  // Fetch per-frame values from the extracted water frame data. This must NOT happen in AddRenderPasses:
  // with the render-graph cache active (cvar Rendering.Caching.RenderGraph) AddRenderPasses is skipped on
  // cache hits and any value captured there goes permanently stale.
  m_vMainLightColorIntensity = plVec3::MakeZero();
  m_vMainLightDirection = plVec3(0.0f, 0.0f, 1.0f);
  m_vCausticFallbackBounds = plVec4::MakeZero();
  m_vCausticFallbackCenterHeightValidity = plVec4::MakeZero();
  m_vCausticFallbackWaveParams = plVec4::MakeZero();
  m_vCausticFallbackWindTime = plVec4::MakeZero();
  m_vCausticFallbackStrengths = plVec2::MakeZero();
  m_uiActiveUnderwaterBodyId = plInvalidIndex;

  // One-texel inner margin keeps bilinear reads from bleeding across the 100 m wrap seam.
  const float fHalfExtent = m_SimulationQuality.m_uiInteractionAtlasSize > 0
                              ? 50.0f - 100.0f / static_cast<float>(m_SimulationQuality.m_uiInteractionAtlasSize)
                              : 0.0f;
  m_vInteractionWindowParams = plVec4(0.0f, 0.0f, fHalfExtent,
    m_bSimulationInitialized && m_GpuSimulation.HasInteractionData() ? 1.0f : 0.0f);

  if (plWaterFrameDataProvider* pProvider = GetPipeline()->GetFrameDataProvider<plWaterFrameDataProvider>())
  {
    if (const plWaterFrameData* pFrameData = pProvider->GetData(renderViewContext))
    {
      m_vMainLightDirection = pFrameData->m_vMainLightDirection;
      m_vMainLightColorIntensity = pFrameData->m_vMainLightColorIntensity;
      if (pFrameData->m_bHasWaterData && pFrameData->m_bCameraUnderwater)
        m_uiActiveUnderwaterBodyId = pFrameData->m_uiActiveBodyId;
      m_vInteractionWindowParams.x = pFrameData->m_vInteractionWindowCenter.x;
      m_vInteractionWindowParams.y = pFrameData->m_vInteractionWindowCenter.y;
      m_vCausticFallbackBounds = pFrameData->m_vCausticFallbackBounds;
      m_vCausticFallbackCenterHeightValidity = plVec4(pFrameData->m_vCausticFallbackCenter.x,
        pFrameData->m_vCausticFallbackCenter.y, pFrameData->m_fCausticFallbackSurfaceHeight,
        pFrameData->m_bCausticFallbackValid ? 1.0f : 0.0f);
      m_vCausticFallbackWaveParams = pFrameData->m_vCausticFallbackWaveParams;
      m_vCausticFallbackWindTime = plVec4(pFrameData->m_vCausticFallbackWindDirection.x,
        pFrameData->m_vCausticFallbackWindDirection.y, pFrameData->m_fSimulationTime, 0.0f);
      m_vCausticFallbackStrengths = plVec2(pFrameData->m_fUnderwaterCaustics, pFrameData->m_fReflectedCaustics);
    }
  }
}

plStatus plWaterSurfacePass::AddRenderPassesInactive(const plViewData& viewData, const plCamera& camera, plRenderGraph& ref_graph, const plArrayPtr<const plRenderPipelinePinConnection> inputs, plArrayPtr<plRenderPipelinePinConnection> outputs)
{
  PL_IGNORE_UNUSED(viewData);
  PL_IGNORE_UNUSED(camera);
  PL_IGNORE_UNUSED(ref_graph);

  outputs[m_PinColor.m_uiOutputIndex] = inputs[m_PinColor.m_uiInputIndex];
  outputs[m_PinDepthStencil.m_uiOutputIndex] = inputs[m_PinDepthStencil.m_uiInputIndex];
  // Keep the connectivity: a connection without it drops the texture the pass hands over once it is active again.
  outputs[m_PinWaterDepth.m_uiOutputIndex] = plRenderPipelinePinConnection(outputs[m_PinWaterDepth.m_uiOutputIndex].m_Connectivity);
  outputs[m_PinWaterExitDepth.m_uiOutputIndex] = plRenderPipelinePinConnection(outputs[m_PinWaterExitDepth.m_uiOutputIndex].m_Connectivity);
  return plStatus(PL_SUCCESS);
}

void plWaterSurfacePass::DeclareRenderObjectDependencies(plRenderGraph& ref_graph, plRenderGraphPassBuilder& ref_pass)
{
  DeclareRendererDependenciesForCategory(plWaterRenderData::WaterSurface, ref_graph, ref_pass);
}

void plWaterSurfacePass::RenderObjects(const plRenderViewContext& renderViewContext)
{
  RenderDataWithCategory(renderViewContext, plWaterRenderData::WaterSurface);
}

plUInt32 plWaterSurfacePass::GetGpuProfileSlot(plUInt32 uiProfileIndex) const
{
  for (plUInt32 i = 0; i < m_GpuProfileIds.GetCount(); ++i)
  {
    if (m_GpuProfileIds[i] == uiProfileIndex)
      return m_GpuProfileSlots[i];
  }
  return plInvalidIndex;
}

plResult plWaterSurfacePass::Serialize(plStreamWriter& inout_stream) const
{
  PL_SUCCEED_OR_RETURN(SUPER::Serialize(inout_stream));
  inout_stream << m_Quality;
  return PL_SUCCESS;
}

plResult plWaterSurfacePass::Deserialize(plStreamReader& inout_stream)
{
  PL_SUCCEED_OR_RETURN(SUPER::Deserialize(inout_stream));
  inout_stream >> m_Quality;
  return PL_SUCCESS;
}

PL_STATICLINK_FILE(WaterPlugin, WaterPlugin_Rendering_Implementation_WaterSurfacePass);
