#pragma once

#include <WaterPlugin/WaterPluginDLL.h>

struct plGALDeviceCapabilities;
struct plViewData;
class plRenderPipelinePass;

/// \brief Reported by every water body that extracts a surface, so the water passes only build their targets, run the
/// wave simulation and draw while a view has water in it (plRenderPipeline::GetContentFlags).
constexpr plUInt32 PL_WATER_CONTENT_FLAG_SURFACES = PL_BIT(25);

struct PL_WATER_DLL plWaterQualityTier
{
  using StorageType = plUInt8;

  enum Enum
  {
    Low,
    Balanced,
    High,
    Ultra,

    Default = High,
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_WATER_DLL, plWaterQualityTier);

/// \brief Fully resolved water settings. Capability resolution can only lower a requested feature.
struct PL_WATER_DLL plWaterQualitySettings
{
  static plWaterQualitySettings Resolve(plWaterQualityTier::Enum requestedTier, const plGALDeviceCapabilities& capabilities);

  /// \brief The tier a water pass asks for in one view.
  ///
  /// A pass that the engine placed (see plRenderPipelineInjections) has no node to carry a property, so it takes the
  /// cvar Water.Quality, held at Low for reflection and render-target views. A pass that an author placed in a
  /// pipeline asset keeps the Quality property of its node.
  static plWaterQualityTier::Enum SelectTier(const plRenderPipelinePass& pass, plWaterQualityTier::Enum nodeTier, const plViewData& viewData);

  plWaterQualityTier::Enum m_Tier = plWaterQualityTier::High;
  plUInt16 m_uiOceanCellsPerRing = 64;
  plUInt8 m_uiOceanRingCount = 6;
  plUInt8 m_uiSpectrumCascadeCount = 3;
  plUInt16 m_uiSpectrumResolution = 128;
  plUInt16 m_uiInteractionAtlasSize = 1024;
  plUInt16 m_uiInteractionEventBudget = 256;
  plUInt8 m_uiInteractionEventsPerTile = 12;
  plUInt8 m_uiCausticRaySteps = 6;
  plUInt8 m_uiScreenSpaceReflectionSteps = 4;
  float m_fCausticResolutionScale = 0.5f;
  bool m_bUseSpectrum = true;
  bool m_bUseScreenSpaceReflections = true;
  bool m_bUseTransmittedCaustics = true;
  bool m_bUseReflectedCaustics = true;
  /// Compatibility flag for the underwater post-process. Kept in sync with transmitted caustics by Resolve().
  bool m_bUseUnderwaterCaustics = true;
  bool m_bUseUnderwaterLightShafts = true;
};
