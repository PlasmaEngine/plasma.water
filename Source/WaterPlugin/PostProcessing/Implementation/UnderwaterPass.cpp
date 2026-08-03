#include <WaterPlugin/WaterPluginPCH.h>

#include <Foundation/IO/TypeVersionContext.h>
#include <WaterPlugin/PostProcessing/UnderwaterPass.h>
#include <RendererCore/Pipeline/RenderPipeline.h>
#include <RendererCore/Pipeline/View.h>
#include <RendererCore/RenderContext/RenderContext.h>
#include <RendererCore/RenderGraph/RenderGraph.h>
#include <RendererCore/RenderGraph/RenderGraphContext.h>
#include <RendererCore/RenderGraph/RenderGraphPassBuilder.h>

#include <RendererFoundation/Resources/RenderTargetView.h>
#include <RendererFoundation/Resources/Texture.h>
#include <WaterPlugin/Rendering/WaterFrameData.h>

#include <Shaders/Pipeline/UnderwaterConstants.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUnderwaterPass, 2, plRTTIDefaultAllocator<plUnderwaterPass>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Color", m_PinColorInput),
    PL_MEMBER_PROPERTY("Depth", m_PinDepthInput),
    PL_MEMBER_PROPERTY("Output", m_PinOutput),
    
    // Water surface group
    PL_MEMBER_PROPERTY("WaterBaseHeight", m_fWaterBaseHeight)->AddAttributes(new plGroupAttribute("Water Surface")),
    PL_MEMBER_PROPERTY("WaveSpeed", m_fWaveSpeed)->AddAttributes(new plDefaultValueAttribute(1.0f)),
    PL_MEMBER_PROPERTY("WaveAmplitude", m_fWaveAmplitude)->AddAttributes(new plDefaultValueAttribute(1.5f)),
    PL_MEMBER_PROPERTY("WaveFrequency", m_fWaveFrequency)->AddAttributes(new plDefaultValueAttribute(0.15f)),
    PL_MEMBER_PROPERTY("WaveDrag", m_fWaveDrag)->AddAttributes(new plDefaultValueAttribute(0.38f)),
    PL_MEMBER_PROPERTY("WaveIterations", m_iWaveIterations)->AddAttributes(new plDefaultValueAttribute(24), new plClampValueAttribute(4, 48)),
    
    // Fog group
    PL_MEMBER_PROPERTY("FogColor", m_FogColor)->AddAttributes(new plGroupAttribute("Fog"), new plDefaultValueAttribute(plColor(0.02f, 0.08f, 0.15f))),
    PL_MEMBER_PROPERTY("WaterAbsorption", m_vWaterAbsorption)->AddAttributes(new plDefaultValueAttribute(plVec3(0.45f, 0.09f, 0.06f))),
    PL_MEMBER_PROPERTY("FogDensity", m_fFogDensity)->AddAttributes(new plDefaultValueAttribute(0.15f), new plClampValueAttribute(0.0f, 2.0f)),
    PL_MEMBER_PROPERTY("MaxUnderwaterDepth", m_fMaxUnderwaterDepth)->AddAttributes(new plDefaultValueAttribute(50.0f)),
    
    // Distortion group
    PL_MEMBER_PROPERTY("DistortionStrength", m_fDistortionStrength)->AddAttributes(new plGroupAttribute("Distortion"), new plDefaultValueAttribute(1.0f), new plClampValueAttribute(0.0f, 5.0f)),
    PL_MEMBER_PROPERTY("DistortionSpeed", m_fDistortionSpeed)->AddAttributes(new plDefaultValueAttribute(1.0f)),
    PL_MEMBER_PROPERTY("ChromaticAberration", m_fChromaticAberration)->AddAttributes(new plDefaultValueAttribute(0.01f), new plClampValueAttribute(0.0f, 0.1f)),
    
    // Caustics group
    PL_MEMBER_PROPERTY("CausticScale", m_fCausticScale)->AddAttributes(new plGroupAttribute("Caustics"), new plDefaultValueAttribute(0.5f)),
    PL_MEMBER_PROPERTY("CausticSpeed", m_fCausticSpeed)->AddAttributes(new plDefaultValueAttribute(1.0f)),
    PL_MEMBER_PROPERTY("CausticIntensity", m_fCausticIntensity)->AddAttributes(new plDefaultValueAttribute(0.3f), new plClampValueAttribute(0.0f, 2.0f)),
    
    // Light shafts group
    PL_MEMBER_PROPERTY("LightShaftIntensity", m_fLightShaftIntensity)->AddAttributes(new plGroupAttribute("Light Shafts"), new plDefaultValueAttribute(0.2f), new plClampValueAttribute(0.0f, 2.0f)),
    PL_MEMBER_PROPERTY("LightShaftFalloff", m_fLightShaftFalloff)->AddAttributes(new plDefaultValueAttribute(0.5f)),
    PL_MEMBER_PROPERTY("VignetteStrength", m_fVignetteStrength)->AddAttributes(new plDefaultValueAttribute(0.3f), new plClampValueAttribute(0.0f, 1.0f)),
    PL_MEMBER_PROPERTY("UseWaterFrameData", m_bUseWaterFrameData)->AddAttributes(new plDefaultValueAttribute(true)),
  }
  PL_END_PROPERTIES;
  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("Post Processing/Underwater"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plUnderwaterPass::plUnderwaterPass()
  : plRenderPipelinePass("UnderwaterPass", true)
{
  m_hShader = plResourceManager::LoadResource<plShaderResource>("Shaders/Pipeline/UnderwaterPostProcess.plShader");
  PL_ASSERT_DEV(m_hShader.IsValid(), "Could not load underwater shader!");

  m_hConstantBuffer = plRenderContext::CreateConstantBufferStorage<plUnderwaterConstants>();
}

plUnderwaterPass::~plUnderwaterPass()
{
  plRenderContext::DeleteConstantBufferStorage(m_hConstantBuffer);
}

plStatus plUnderwaterPass::AddRenderPasses(const plViewData& viewData, const plCamera& camera, plRenderGraph& ref_graph, const plArrayPtr<const plRenderPipelinePinConnection> inputs, plArrayPtr<plRenderPipelinePinConnection> outputs)
{
  plRenderGraphTextureHandle hColorInput = inputs[m_PinColorInput.m_uiInputIndex].m_TextureHandle;
  if (hColorInput.IsInvalidated())
    return plStatus(plFmt("Color input: Not connected"));

  plRenderGraphTextureHandle hDepthInput = inputs[m_PinDepthInput.m_uiInputIndex].m_TextureHandle;

  const plGALTextureCreationDescription colorInputDesc = ref_graph.GetTextureDesc(hColorInput);

  plGALDevice* pDevice = plGALDevice::GetDefaultDevice();
  const plGALRenderTargets& renderTargets = viewData.GetActiveRenderTargets();
  const plGALTexture* pTexture = pDevice->GetTexture(renderTargets.m_hRTs[0]);
  if (pTexture == nullptr)
    return plStatus(plFmt("View does not have a valid color target"));

  const plGALTextureCreationDescription& rtDesc = pTexture->GetDescription();
  plGALTextureCreationDescription outputDesc;
  outputDesc.SetAsRenderTarget(colorInputDesc.m_uiWidth, colorInputDesc.m_uiHeight, rtDesc.m_Format);
  outputDesc.m_Type = colorInputDesc.m_Type;
  outputDesc.m_uiArraySize = colorInputDesc.m_uiArraySize;
  plRenderGraphTextureHandle hOutput = ref_graph.CreateTexture(outputDesc);
  outputs[m_PinOutput.m_uiOutputIndex].m_TextureHandle = hOutput;

  auto pass = ref_graph.AddGraphicsPass("Underwater");
  pass.AddColorTarget(hOutput);
  pass.ReadTexture(hColorInput, {}, plGALResourceState::ShaderRead, plGALShaderStageFlags::PixelShader);
  if (!hDepthInput.IsInvalidated())
    pass.ReadTexture(hDepthInput, {}, plGALResourceState::DepthStencilRead, plGALShaderStageFlags::PixelShader);
  pass.SetStereoscopic(camera.IsStereoscopic());
  pass.SetExecuteCallback([=](const plRenderGraphContext& ctx)
    {
    const plRenderViewContext& renderViewContext = *ctx.GetUserData<plRenderViewContext>();
    renderViewContext.UpdateViewport();

    // Update constant buffer with current settings
    {
      plUnderwaterConstants* constants = plRenderContext::GetConstantBufferData<plUnderwaterConstants>(m_hConstantBuffer);

      // Water surface parameters (from pass properties)
      constants->WaterBaseHeight = m_fWaterBaseHeight;
      constants->WaveSpeed = m_fWaveSpeed;
      constants->WaveAmplitude = m_fWaveAmplitude;
      constants->WaveFrequency = m_fWaveFrequency;
      constants->WaveDrag = m_fWaveDrag;
      constants->WaveIterations = m_iWaveIterations;
      constants->FogDensity = m_fFogDensity;
      constants->MaxUnderwaterDepth = m_fMaxUnderwaterDepth;

      // Modern worlds publish an immutable per-view optical profile. Old scenes retain their serialized inline
      // settings as a version-safe fallback when no frame data is present.
      plWaterFrameData* pWaterFrameData = nullptr;
      if (m_bUseWaterFrameData)
      {
        if (plWaterFrameDataProvider* pProvider = GetPipeline()->GetFrameDataProvider<plWaterFrameDataProvider>())
          pWaterFrameData = pProvider->GetData(renderViewContext);
      }

      if (pWaterFrameData != nullptr && pWaterFrameData->m_bCameraUnderwater)
      {
        constants->FogColor = plColor(pWaterFrameData->m_vScattering.x, pWaterFrameData->m_vScattering.y, pWaterFrameData->m_vScattering.z, 1.0f);
        constants->WaterAbsorption = plVec4(pWaterFrameData->m_vAbsorption.x, pWaterFrameData->m_vAbsorption.y, pWaterFrameData->m_vAbsorption.z, 0.0f);
        constants->MaxUnderwaterDepth = pWaterFrameData->m_fMaxUnderwaterDistance;
      }
      else
      {
        constants->FogColor = m_FogColor;
        constants->WaterAbsorption = plVec4(m_vWaterAbsorption.x, m_vWaterAbsorption.y, m_vWaterAbsorption.z, 0.0f);
      }

      // Distortion
      constants->DistortionStrength = m_fDistortionStrength;
      constants->DistortionSpeed = m_fDistortionSpeed;
      constants->ChromaticAberration = m_fChromaticAberration;
      constants->CausticScale = m_fCausticScale;

      // Caustics and light shafts
      constants->CausticSpeed = m_fCausticSpeed;
      constants->CausticIntensity = m_fCausticIntensity;
      constants->LightShaftIntensity = m_fLightShaftIntensity;
      constants->LightShaftFalloff = m_fLightShaftFalloff;

      constants->VignetteStrength = m_fVignetteStrength;

      const plVec3 sunDir = pWaterFrameData != nullptr ? pWaterFrameData->m_vMainLightDirection.GetNormalized() : plVec3(0.3f, 0.2f, 0.93f).GetNormalized();
      constants->SunDirection = plVec4(sunDir.x, sunDir.y, sunDir.z, 0.0f);
    }

    renderViewContext.m_pRenderContext->BindShader(m_hShader);
    renderViewContext.m_pRenderContext->BindNullMeshBuffer(plGALPrimitiveTopology::Triangles, 1);

    // Bind resources using bind group (per-slot recording context, not the default instance - see multithreaded recording).
    plBindGroupBuilder& bindGroup = renderViewContext.m_pRenderContext->GetBindGroup();
    bindGroup.BindBuffer("plUnderwaterConstants", m_hConstantBuffer);
    bindGroup.BindTexture("SceneColorTexture", ctx.ResolveTexture(hColorInput));

    if (!hDepthInput.IsInvalidated())
    {
      bindGroup.BindTexture("SceneDepthTexture", ctx.ResolveTexture(hDepthInput));
    }

    // Draw fullscreen triangle
    renderViewContext.m_pRenderContext->DrawMeshBuffer().IgnoreResult(); });

  return plStatus(PL_SUCCESS);
}

plResult plUnderwaterPass::Serialize(plStreamWriter& inout_stream) const
{
  PL_SUCCEED_OR_RETURN(SUPER::Serialize(inout_stream));
  
  inout_stream << m_fWaterBaseHeight;
  inout_stream << m_fWaveSpeed;
  inout_stream << m_fWaveAmplitude;
  inout_stream << m_fWaveFrequency;
  inout_stream << m_fWaveDrag;
  inout_stream << m_iWaveIterations;
  
  inout_stream << m_FogColor;
  inout_stream << m_vWaterAbsorption;
  inout_stream << m_fFogDensity;
  inout_stream << m_fMaxUnderwaterDepth;
  
  inout_stream << m_fDistortionStrength;
  inout_stream << m_fDistortionSpeed;
  inout_stream << m_fChromaticAberration;
  
  inout_stream << m_fCausticScale;
  inout_stream << m_fCausticSpeed;
  inout_stream << m_fCausticIntensity;
  
  inout_stream << m_fLightShaftIntensity;
  inout_stream << m_fLightShaftFalloff;
  inout_stream << m_fVignetteStrength;
  inout_stream << m_bUseWaterFrameData;
  
  return PL_SUCCESS;
}

plResult plUnderwaterPass::Deserialize(plStreamReader& inout_stream)
{
  PL_SUCCEED_OR_RETURN(SUPER::Deserialize(inout_stream));
  const plUInt32 uiVersion = plTypeVersionReadContext::GetContext()->GetTypeVersion(GetStaticRTTI());
  
  inout_stream >> m_fWaterBaseHeight;
  inout_stream >> m_fWaveSpeed;
  inout_stream >> m_fWaveAmplitude;
  inout_stream >> m_fWaveFrequency;
  inout_stream >> m_fWaveDrag;
  inout_stream >> m_iWaveIterations;
  
  inout_stream >> m_FogColor;
  inout_stream >> m_vWaterAbsorption;
  inout_stream >> m_fFogDensity;
  inout_stream >> m_fMaxUnderwaterDepth;
  
  inout_stream >> m_fDistortionStrength;
  inout_stream >> m_fDistortionSpeed;
  inout_stream >> m_fChromaticAberration;
  
  inout_stream >> m_fCausticScale;
  inout_stream >> m_fCausticSpeed;
  inout_stream >> m_fCausticIntensity;
  
  inout_stream >> m_fLightShaftIntensity;
  inout_stream >> m_fLightShaftFalloff;
  inout_stream >> m_fVignetteStrength;

  if (uiVersion >= 2)
    inout_stream >> m_bUseWaterFrameData;
  else
    m_bUseWaterFrameData = true;
  
  return PL_SUCCESS;
}

PL_STATICLINK_FILE(WaterPlugin, WaterPlugin_PostProcessing_Implementation_UnderwaterPass);
