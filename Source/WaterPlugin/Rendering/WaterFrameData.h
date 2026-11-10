#pragma once

#include <Foundation/Containers/StaticArray.h>
#include <Foundation/Math/Color.h>
#include <GameEngine/Interfaces/WaterWorldModule.h>
#include <RendererCore/Pipeline/FrameDataProvider.h>
#include <RendererCore/Pipeline/RenderData.h>
#include <WaterPlugin/WaterPluginDLL.h>

#include <Shaders/Water/WaterProfileData.h>

/// \brief Fills one entry of the profile table from a profile. See plWaterWorldModule::FillGpuProfileTable.
PL_WATER_DLL void plWaterFillGpuProfile(plWaterGpuProfile& out_entry, const struct plWaterProfileResourceDescriptor& profile, plWaterBodyType::Enum type, float fFlowSpeed);

/// \brief Immutable per-view bridge produced during extraction by the water world module.
class PL_WATER_DLL plWaterFrameData : public plRenderData
{
  PL_ADD_DYNAMIC_REFLECTION(plWaterFrameData, plRenderData);

public:
  /// The GPU upload budget tops out at 512 (Ultra). Keep interaction payloads inline
  /// so render-graph callbacks never retain a pointer into an extractor worker's
  /// transient allocator.
  static constexpr plUInt32 MaxInteractions = 512;

  plGALBufferHandle m_hBodyData;
  plGALBufferHandle m_hProfileData;
  plGALTextureHandle m_hSpectrumDisplacement;
  plGALTextureHandle m_hInteractionAtlas;
  plStaticArray<plWaterInteraction, MaxInteractions> m_Interactions;

  /// The profile of every water body of the world, indexed by body. plWaterSurfacePass uploads it once per frame.
  plWaterGpuProfile m_Profiles[WATER_MAX_PROFILES];
  plUInt32 m_uiProfileCount = 0;

  plVec3 m_vAbsorption = plVec3(0.15f, 0.06f, 0.025f);
  plVec3 m_vScattering = plVec3(0.015f, 0.035f, 0.055f);
  plVec3 m_vMainLightDirection = plVec3(0.3f, 0.2f, 0.93f); ///< World-space direction from the scene toward the light.
  /// Center of the currently valid 100 m interaction window (camera XY). The interaction atlas wraps world
  /// space with a 100 m period; this anchor decides which region of it may be written and sampled this frame.
  /// Zero fallback keeps the legacy origin-centered window.
  plVec2 m_vInteractionWindowCenter = plVec2::MakeZero();
  /// Analytic fallback for caustics when the surface is outside the camera frustum or hidden by its receiver.
  /// The rasterized world-space field takes precedence wherever actual water geometry is available.
  plVec2 m_vCausticFallbackCenter = plVec2::MakeZero();
  plVec2 m_vCausticFallbackWindDirection = plVec2(1.0f, 0.0f);
  plVec4 m_vCausticFallbackBounds = plVec4::MakeZero();     ///< min.xy, max.zw in world space.
  plVec4 m_vCausticFallbackWaveParams = plVec4::MakeZero(); ///< amplitude, wavelength, speed, choppiness.
  plColor m_MainLightColor = plColor::White;
  /// Physical light color * intensity of the dominant directional light, for analytically lighting the
  /// water surface tint (m_MainLightColor is the underwater-composite-scaled variant).
  plVec3 m_vMainLightColorIntensity = plVec3::MakeZero();
  float m_fCameraWaterDepth = 0.0f;
  float m_fMaxUnderwaterDistance = 80.0f;
  float m_fSimulationTime = 0.0f;
  float m_fUnderwaterFogDensity = 0.06f;
  float m_fUnderwaterScattering = 0.65f;
  float m_fUnderwaterHaze = 0.25f;
  float m_fUnderwaterDistortion = 0.18f;
  float m_fUnderwaterCaustics = 0.25f;
  float m_fReflectedCaustics = 0.15f;
  float m_fCausticFallbackSurfaceHeight = 0.0f;
  float m_fUnderwaterLightShafts = 0.12f;
  float m_fUnderwaterMeniscus = 0.1f;
  plUInt32 m_uiActiveBodyId = plInvalidIndex;
  bool m_bCameraUnderwater = false;
  bool m_bHasWaterData = false;
  bool m_bCausticFallbackValid = false;
  bool m_bWorldSimulationEnabled = false;
  /// True for main/editor views. Secondary views (shadows, reflections, thumbnails) run their own water sim
  /// and must not emit user-facing diagnostics that look like they came from the viewport the user watches.
  bool m_bPrimaryView = true;
};

class PL_WATER_DLL plWaterFrameDataProvider : public plFrameDataProvider<plWaterFrameData>
{
  PL_ADD_DYNAMIC_REFLECTION(plWaterFrameDataProvider, plFrameDataProviderBase);

protected:
  virtual void* UpdateData(const plRenderViewContext& renderViewContext, const plExtractedRenderData& extractedData) override;

private:
  plWaterFrameData m_FallbackData;
};
