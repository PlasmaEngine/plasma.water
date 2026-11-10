#include <WaterPlugin/WaterPluginPCH.h>

#include <Foundation/Configuration/CVar.h>
#include <RendererCore/Pipeline/RenderPipeline.h>
#include <RendererCore/Pipeline/RenderPipelinePass.h>
#include <RendererCore/Pipeline/ViewData.h>
#include <RendererFoundation/Device/DeviceCapabilities.h>
#include <RendererFoundation/RendererFoundationDLL.h>
#include <RendererFoundation/Resources/ResourceFormats.h>
#include <WaterPlugin/Rendering/WaterQuality.h>

// clang-format off
PL_BEGIN_STATIC_REFLECTED_ENUM(plWaterQualityTier, 1)
  PL_ENUM_CONSTANTS(plWaterQualityTier::Low, plWaterQualityTier::Balanced, plWaterQualityTier::High, plWaterQualityTier::Ultra)
PL_END_STATIC_REFLECTED_ENUM;
// clang-format on

plCVarInt cvar_WaterQuality("Water.Quality", plWaterQualityTier::High, plCVarFlags::Save,
  "Quality of water in pipelines that get their water passes from the engine: 0 = Low, 1 = Balanced, 2 = High, 3 = Ultra. A water pass placed by hand in a pipeline asset uses its own Quality property.");

plWaterQualityTier::Enum plWaterQualitySettings::SelectTier(const plRenderPipelinePass& pass, plWaterQualityTier::Enum nodeTier, const plViewData& viewData)
{
  const plRenderPipeline* pPipeline = pass.GetPipeline();
  if (pPipeline == nullptr || !pPipeline->IsPassInjected(&pass))
    return nodeTier;

  // A probe or a render target shows water small or blurred. It gets the surface and nothing that costs a pass of its own.
  if (viewData.m_CameraUsageHint == plCameraUsageHint::Reflection || viewData.m_CameraUsageHint == plCameraUsageHint::RenderTarget)
    return plWaterQualityTier::Low;

  return static_cast<plWaterQualityTier::Enum>(plMath::Clamp<plInt32>(cvar_WaterQuality.GetValue(), plWaterQualityTier::Low, plWaterQualityTier::Ultra));
}

plWaterQualitySettings plWaterQualitySettings::Resolve(plWaterQualityTier::Enum requestedTier, const plGALDeviceCapabilities& capabilities)
{
  plWaterQualitySettings result;
  result.m_Tier = requestedTier;

  switch (requestedTier)
  {
    case plWaterQualityTier::Low:
      result.m_uiOceanCellsPerRing = 32;
      result.m_uiOceanRingCount = 4;
      result.m_uiSpectrumCascadeCount = 0;
      result.m_uiSpectrumResolution = 0;
      result.m_uiInteractionAtlasSize = 512;
      result.m_uiInteractionEventBudget = 32;
      result.m_uiInteractionEventsPerTile = 4;
      result.m_uiCausticRaySteps = 0;
      result.m_uiScreenSpaceReflectionSteps = 0;
      result.m_fCausticResolutionScale = 0.0f;
      result.m_bUseSpectrum = false;
      result.m_bUseScreenSpaceReflections = false;
      result.m_bUseTransmittedCaustics = false;
      result.m_bUseReflectedCaustics = false;
      result.m_bUseUnderwaterCaustics = false;
      result.m_bUseUnderwaterLightShafts = false;
      break;

    case plWaterQualityTier::Balanced:
      result.m_uiOceanCellsPerRing = 48;
      result.m_uiOceanRingCount = 5;
      result.m_uiSpectrumCascadeCount = 2;
      result.m_uiSpectrumResolution = 128;
      result.m_uiInteractionAtlasSize = 512;
      result.m_uiInteractionEventBudget = 64;
      result.m_uiInteractionEventsPerTile = 8;
      result.m_uiCausticRaySteps = 4;
      result.m_uiScreenSpaceReflectionSteps = 0;
      result.m_fCausticResolutionScale = 0.5f;
      result.m_bUseScreenSpaceReflections = false;
      result.m_bUseUnderwaterLightShafts = false;
      break;

    case plWaterQualityTier::Ultra:
      result.m_uiOceanCellsPerRing = 64;
      // Match the component's authored maximum so Ultra preserves all existing clipmap coverage.
      result.m_uiOceanRingCount = 12;
      result.m_uiSpectrumCascadeCount = 4;
      result.m_uiSpectrumResolution = 256;
      result.m_uiInteractionAtlasSize = 2048;
      result.m_uiInteractionEventBudget = 512;
      result.m_uiInteractionEventsPerTile = 16;
      result.m_uiCausticRaySteps = 8;
      result.m_uiScreenSpaceReflectionSteps = 6;
      result.m_fCausticResolutionScale = 1.0f;
      break;

    case plWaterQualityTier::High:
    default:
      break;
  }

  const bool bComputeSupported = capabilities.m_bShaderStageSupported[plGALShaderStage::ComputeShader];
  const bool bHalfTextureRwSupported = capabilities.m_FormatSupport.GetCount() > plGALResourceFormat::RGBAHalf &&
                                       capabilities.m_FormatSupport[plGALResourceFormat::RGBAHalf].AreAllSet(
                                         plGALResourceFormatSupport::Texture | plGALResourceFormatSupport::TextureRW);
  const bool bHalfRenderTargetSupported = capabilities.m_FormatSupport.GetCount() > plGALResourceFormat::RGBAHalf &&
                                          capabilities.m_FormatSupport[plGALResourceFormat::RGBAHalf].AreAllSet(
                                            plGALResourceFormatSupport::Texture | plGALResourceFormatSupport::RenderTarget);
  if (!bComputeSupported || !bHalfTextureRwSupported)
  {
    result.m_bUseSpectrum = false;
    result.m_uiSpectrumCascadeCount = 0;
    result.m_uiSpectrumResolution = 0;
    result.m_uiInteractionAtlasSize = 0;
  }

  if (!bComputeSupported || !bHalfTextureRwSupported || !bHalfRenderTargetSupported)
  {
    result.m_bUseTransmittedCaustics = false;
    result.m_bUseReflectedCaustics = false;
    result.m_uiCausticRaySteps = 0;
    result.m_fCausticResolutionScale = 0.0f;
  }

  result.m_bUseUnderwaterCaustics = result.m_bUseTransmittedCaustics;

#if PL_ENABLED(PL_PLATFORM_ANDROID)
  // Mobile is a separately tuned low preset: no GPU spectrum, projected caustics, or shafts. Do not inherit the desktop
  // Balanced preset merely because the caller persisted High/Ultra on another platform.
  if (result.m_Tier != plWaterQualityTier::Low)
  {
    return Resolve(plWaterQualityTier::Low, capabilities);
  }
#endif

  return result;
}

PL_STATICLINK_FILE(WaterPlugin, WaterPlugin_Rendering_Implementation_WaterQuality);
