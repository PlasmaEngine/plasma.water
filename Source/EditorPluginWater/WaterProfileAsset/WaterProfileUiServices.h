#pragma once

#include <EditorPluginWater/EditorPluginWaterDLL.h>

#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/Math/Vec2.h>
#include <WaterPlugin/Resources/WaterProfileResource.h>

class plDocument;
struct plPropertyMetaStateEvent;

/// \brief Preview quality tier caps, mirrored by the preview view context and the performance estimate.
struct plWaterPreviewQualityTier
{
  const char* m_szName;
  plUInt32 m_uiMaxCascades;
  plUInt32 m_uiResolution;
  const char* m_szSimMode;
};

/// \brief Editor-side helpers shared between the Water Profile window, the property meta state handler,
/// and the diagnostics widgets.
class plWaterProfileUiServices
{
public:
  static constexpr plUInt32 QualityTierCount = 4;
  static const plWaterPreviewQualityTier s_QualityTiers[QualityTierCount];

  /// Estimated spectrum working set in bytes for a tier: resolution^2 x 8 B x effective cascades x 2 buffers.
  static plUInt64 GetSpectrumMemoryBytes(plUInt32 uiTier, plUInt32 uiAuthoredCascades);

  /// \name Preview body type registry
  /// The preview body type is window state, not asset data. The meta state handler reads it from here to dim
  /// properties the current body type ignores.
  ///@{
  static void SetPreviewBodyType(const plDocument* pDocument, plInt32 iBodyType);
  static plInt32 GetPreviewBodyType(const plDocument* pDocument);
  static void ClearPreviewBodyType(const plDocument* pDocument);
  ///@}

  /// Dims descriptor properties that the currently previewed body type ignores (e.g. Finite Mesh for oceans).
  static void PropertyMetaStateEventHandler(plPropertyMetaStateEvent& e);

  /// Wavelength range (min, max) covered by each authored GPU cascade: a log-even partition of
  /// [SmallestGpuWavelength, BaseWavelength], cascade 0 covering the longest wavelengths. Mirrors the
  /// cascade fraction lerp in WaterSpectrumUpdate.plShader.
  static void GetCascadeBands(const plWaterProfileResourceDescriptor& profile, plDynamicArray<plVec2>& out_bands);

  /// The exact wavelengths of the deterministic CPU waves; mirrors plWaterWorldModule::EvaluateWaves.
  static void GetCpuWavelengths(const plWaterProfileResourceDescriptor& profile, plDynamicArray<float>& out_wavelengths);
};
