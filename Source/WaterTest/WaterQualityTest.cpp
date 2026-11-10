#include <WaterTest/WaterTestPCH.h>

#include <RendererFoundation/Device/DeviceCapabilities.h>
#include <RendererFoundation/Resources/ResourceFormats.h>
#include <WaterPlugin/Rendering/WaterQuality.h>

namespace
{
  plGALDeviceCapabilities MakeWaterCapabilities(bool bCompute, bool bHalfTextureRw = true, bool bHalfRenderTarget = true)
  {
    plGALDeviceCapabilities capabilities;
    capabilities.m_bShaderStageSupported[plGALShaderStage::ComputeShader] = bCompute;
    capabilities.m_FormatSupport.SetCount(plGALResourceFormat::ENUM_COUNT);
    if (bHalfTextureRw)
      capabilities.m_FormatSupport[plGALResourceFormat::RGBAHalf].Add(
        plGALResourceFormatSupport::Texture | plGALResourceFormatSupport::TextureRW);
    if (bHalfRenderTarget)
      capabilities.m_FormatSupport[plGALResourceFormat::RGBAHalf].Add(
        plGALResourceFormatSupport::Texture | plGALResourceFormatSupport::RenderTarget);
    return capabilities;
  }
} // namespace

PL_CREATE_SIMPLE_TEST(Water, Quality)
{
  const plGALDeviceCapabilities desktopCapabilities = MakeWaterCapabilities(true);

  PL_TEST_BLOCK(plTestBlock::Enabled, "Tier budgets")
  {
    const plWaterQualitySettings low = plWaterQualitySettings::Resolve(plWaterQualityTier::Low, desktopCapabilities);
    PL_TEST_INT(low.m_uiOceanCellsPerRing, 32);
    PL_TEST_INT(low.m_uiOceanRingCount, 4);
    PL_TEST_INT(low.m_uiInteractionAtlasSize, 512);
    PL_TEST_INT(low.m_uiInteractionEventsPerTile, 4);
    PL_TEST_BOOL(!low.m_bUseSpectrum);
    PL_TEST_BOOL(!low.m_bUseScreenSpaceReflections);
    PL_TEST_BOOL(!low.m_bUseTransmittedCaustics);
    PL_TEST_BOOL(!low.m_bUseReflectedCaustics);
    PL_TEST_BOOL(!low.m_bUseUnderwaterCaustics);
    PL_TEST_FLOAT(low.m_fCausticResolutionScale, 0.0f, 0.0f);
    PL_TEST_INT(low.m_uiCausticRaySteps, 0);
    PL_TEST_INT(low.m_uiScreenSpaceReflectionSteps, 0);

    const plWaterQualitySettings balanced = plWaterQualitySettings::Resolve(plWaterQualityTier::Balanced, desktopCapabilities);
    PL_TEST_INT(balanced.m_uiOceanCellsPerRing, 48);
    PL_TEST_INT(balanced.m_uiOceanRingCount, 5);
    PL_TEST_INT(balanced.m_uiInteractionAtlasSize, 512);
    PL_TEST_INT(balanced.m_uiInteractionEventsPerTile, 8);
    PL_TEST_BOOL(!balanced.m_bUseScreenSpaceReflections);
    PL_TEST_BOOL(balanced.m_bUseTransmittedCaustics);
    PL_TEST_BOOL(balanced.m_bUseReflectedCaustics);
    PL_TEST_BOOL(balanced.m_bUseUnderwaterCaustics);
    PL_TEST_FLOAT(balanced.m_fCausticResolutionScale, 0.5f, 0.0f);
    PL_TEST_INT(balanced.m_uiCausticRaySteps, 4);
    PL_TEST_INT(balanced.m_uiScreenSpaceReflectionSteps, 0);

    const plWaterQualitySettings high = plWaterQualitySettings::Resolve(plWaterQualityTier::High, desktopCapabilities);
    PL_TEST_INT(high.m_uiOceanCellsPerRing, 64);
    PL_TEST_INT(high.m_uiOceanRingCount, 6);
    PL_TEST_INT(high.m_uiSpectrumResolution, 128);
    PL_TEST_INT(high.m_uiInteractionAtlasSize, 1024);
    PL_TEST_INT(high.m_uiInteractionEventsPerTile, 12);
    PL_TEST_BOOL(high.m_bUseScreenSpaceReflections);
    PL_TEST_BOOL(high.m_bUseTransmittedCaustics);
    PL_TEST_BOOL(high.m_bUseReflectedCaustics);
    PL_TEST_BOOL(high.m_bUseUnderwaterCaustics);
    PL_TEST_FLOAT(high.m_fCausticResolutionScale, 0.5f, 0.0f);
    PL_TEST_INT(high.m_uiCausticRaySteps, 6);
    PL_TEST_INT(high.m_uiScreenSpaceReflectionSteps, 4);

    const plWaterQualitySettings ultra = plWaterQualitySettings::Resolve(plWaterQualityTier::Ultra, desktopCapabilities);
    PL_TEST_INT(ultra.m_uiOceanCellsPerRing, 64);
    PL_TEST_INT(ultra.m_uiOceanRingCount, 12);
    PL_TEST_INT(ultra.m_uiSpectrumResolution, 256);
    PL_TEST_INT(ultra.m_uiInteractionAtlasSize, 2048);
    PL_TEST_INT(ultra.m_uiInteractionEventsPerTile, 16);
    PL_TEST_BOOL(ultra.m_bUseScreenSpaceReflections);
    PL_TEST_FLOAT(ultra.m_fCausticResolutionScale, 1.0f, 0.0f);
    PL_TEST_INT(ultra.m_uiCausticRaySteps, 8);
    PL_TEST_INT(ultra.m_uiScreenSpaceReflectionSteps, 6);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Capability fallback")
  {
    const plGALDeviceCapabilities limitedCapabilities = MakeWaterCapabilities(false);
    const plWaterQualitySettings high = plWaterQualitySettings::Resolve(plWaterQualityTier::High, limitedCapabilities);
    PL_TEST_BOOL(!high.m_bUseSpectrum);
    PL_TEST_INT(high.m_uiSpectrumCascadeCount, 0);
    PL_TEST_INT(high.m_uiInteractionAtlasSize, 0);
    PL_TEST_BOOL(!high.m_bUseTransmittedCaustics);
    PL_TEST_BOOL(!high.m_bUseReflectedCaustics);
    PL_TEST_BOOL(!high.m_bUseUnderwaterCaustics);
    PL_TEST_FLOAT(high.m_fCausticResolutionScale, 0.0f, 0.0f);
    PL_TEST_INT(high.m_uiCausticRaySteps, 0);
    PL_TEST_INT(high.m_uiScreenSpaceReflectionSteps, 4);

    const plGALDeviceCapabilities noHalfUavCapabilities = MakeWaterCapabilities(true, false, true);
    const plWaterQualitySettings noHalfUav = plWaterQualitySettings::Resolve(plWaterQualityTier::High, noHalfUavCapabilities);
    PL_TEST_BOOL(!noHalfUav.m_bUseSpectrum);
    PL_TEST_INT(noHalfUav.m_uiInteractionAtlasSize, 0);
    PL_TEST_BOOL(!noHalfUav.m_bUseTransmittedCaustics);
    PL_TEST_BOOL(!noHalfUav.m_bUseReflectedCaustics);

    const plGALDeviceCapabilities noHalfRtCapabilities = MakeWaterCapabilities(true, true, false);
    const plWaterQualitySettings noHalfRt = plWaterQualitySettings::Resolve(plWaterQualityTier::High, noHalfRtCapabilities);
    PL_TEST_BOOL(noHalfRt.m_bUseSpectrum);
    PL_TEST_INT(noHalfRt.m_uiInteractionAtlasSize, 1024);
    PL_TEST_BOOL(!noHalfRt.m_bUseTransmittedCaustics);
    PL_TEST_BOOL(!noHalfRt.m_bUseReflectedCaustics);
  }
}
