#pragma once

#include <Core/ResourceManager/Resource.h>
#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/Math/Color.h>
#include <Foundation/Math/Vec2.h>
#include <Foundation/Math/Vec3.h>
#include <Foundation/Reflection/Implementation/PropertyAttributes.h>
#include <Foundation/Strings/String.h>
#include <Foundation/Types/Status.h>
#include <WaterPlugin/WaterPluginDLL.h>

using plWaterProfileResourceHandle = plTypedResourceHandle<class plWaterProfileResource>;

/// Increment whenever plWaterProfileResourceDescriptor changes size or member order. Editor code compares this
/// against the loaded runtime DLL before allowing the reflection mirror to write into the native descriptor.
inline constexpr plUInt32 PL_WATER_PROFILE_DESCRIPTOR_ABI_VERSION = 5;

PL_WATER_DLL plUInt32 plGetWaterProfileDescriptorRuntimeAbiVersion();
PL_WATER_DLL plUInt32 plGetWaterProfileDescriptorRuntimeSize();

struct PL_WATER_DLL plWaterWaveSpectrumMode
{
  using StorageType = plUInt8;

  enum Enum
  {
    Analytic,
    Phillips,
    JONSWAP,

    Default = Analytic
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_WATER_DLL, plWaterWaveSpectrumMode);

/// \brief One validation finding for a water profile. Errors block asset transform, warnings are advisory.
struct PL_WATER_DLL plWaterProfileIssue
{
  enum class Severity : plUInt8
  {
    Warning,
    Error,
  };

  Severity m_Severity = Severity::Error;
  plString m_sProperty; ///< Reflected property name the issue anchors to. May be empty for cross-property findings.
  plString m_sMessage;
};

/// \brief Marks the WaveSeed property so the editor shows a reroll button next to the standard spin box.
class PL_WATER_DLL plWaterSeedWidgetAttribute : public plTypeWidgetAttribute
{
  PL_ADD_DYNAMIC_REFLECTION(plWaterSeedWidgetAttribute, plTypeWidgetAttribute);
};

/// \brief Marks the WindDirection property so the editor shows a compass dial next to the standard spin boxes.
class PL_WATER_DLL plWaterWindDirectionWidgetAttribute : public plTypeWidgetAttribute
{
  PL_ADD_DYNAMIC_REFLECTION(plWaterWindDirectionWidgetAttribute, plTypeWidgetAttribute);
};

/// \brief Typed, renderer-independent settings shared by water rendering, queries and physics.
struct PL_WATER_DLL plWaterProfileResourceDescriptor
{
  /// Deprecated v2-v4 serialization slot. The modern renderer is driven entirely by this profile.
  plString m_sSurfaceMaterial;

  // Deterministic analytic surface used by both CPU queries and the base GPU displacement.
  plEnum<plWaterWaveSpectrumMode> m_SpectrumMode;
  plUInt32 m_uiWaveSeed = 0xC0FFEEu;
  plVec2 m_vWindDirection = plVec2(1.0f, 0.0f);
  float m_fWindSpeed = 1.0f;
  float m_fWaveAmplitude = 0.28f;
  float m_fBaseWavelength = 18.0f;
  float m_fWaveSteepness = 0.32f;
  plUInt8 m_uiCpuWaveCount = 8;
  plUInt8 m_uiGpuCascadeCount = 3;
  plUInt16 m_uiGpuCascadeResolution = 256;
  float m_fSmallestGpuWavelength = 0.5f;
  bool m_bEnableLakeSpectrum = false;
  float m_fFlowScale = 1.0f;

  // Shared finite-body geometry controls. Oceans ignore these values.
  float m_fFiniteMeshCellSize = 0.25f;
  plUInt16 m_uiMaxRiverCrossSubdivisions = 32;
  float m_fShoreWaveFadeDistance = 2.0f;
  float m_fRiverCurvatureRefinement = 1.0f;

  // Surface optics.
  plColor m_ShallowColor = plColor(0.08f, 0.34f, 0.42f, 0.65f);
  plColor m_DeepColor = plColor(0.008f, 0.035f, 0.075f, 0.95f);
  plVec3 m_vAbsorption = plVec3(0.35f, 0.07f, 0.025f);
  plVec3 m_vScattering = plVec3(0.015f, 0.045f, 0.065f);
  float m_fIndexOfRefraction = 1.333f;
  float m_fRoughness = 0.09f;
  float m_fReflectionStrength = 1.0f;
  float m_fEnvironmentReflectionWeight = 1.0f;
  float m_fMicroNormalScale = 1.0f;
  float m_fMicroNormalStrength = 0.22f;
  float m_fSunGlitterStrength = 0.35f;
  float m_fRefractionEdgeRejection = 0.65f;
  // Retained only to consume v2/v3 binary profiles. Never exposed or used by rendering.
  float m_fLegacyReflectionHistoryWeight = 0.75f;
  float m_fLegacyReflectionTraceDistance = 100.0f;
  float m_fRefractionStrength = 0.012f;
  float m_fRefractionDepth = 6.0f;
  float m_fShoreFadeDepth = 1.2f;

  float m_fFoamStrength = 0.65f;
  float m_fFoamCrestThreshold = 0.78f;
  float m_fFoamShoreDepth = 0.75f;
  // Retained only for binary/profile compatibility. Interactions no longer add a pixel-stage foam mask.
  float m_fFoamInteractionStrength = 0.8f;
  plColor m_FoamColor = plColor(0.85f, 0.9f, 0.92f, 1.0f);
  float m_fFoamRoughness = 0.72f;
  float m_fFoamBreakupScale = 0.18f;

  float m_fUnderwaterFogDensity = 0.06f;
  float m_fUnderwaterScattering = 0.65f;
  float m_fUnderwaterHaze = 0.25f;
  float m_fUnderwaterDistortion = 0.18f;
  /// Transmitted/refracted caustic intensity. The legacy member and serialized property name are retained for compatibility.
  float m_fUnderwaterCaustics = 0.25f;
  float m_fReflectedCaustics = 0.15f;
  float m_fUnderwaterLightShafts = 0.12f;
  float m_fUnderwaterMeniscus = 0.1f;

  // Gameplay/physics values returned with every deterministic surface result.
  float m_fDensity = 1000.0f;
  float m_fLinearDrag = 0.3f;
  float m_fAngularDrag = 0.05f;

  // Cosmetic GPU interaction controls. CPU queries deliberately do not sample these ripples.
  // Propagation speed and damping are reserved for the paged atlas allocator (texels currently have no
  // profile ownership); the live global knobs are the Water.Interaction.* cvars. MaxAmplitude clamps the
  // per-body surface displacement and IS consumed.
  float m_fRipplePropagationSpeed = 3.0f;
  float m_fRippleDamping = 0.985f;
  float m_fRippleMaxAmplitude = 0.2f;

  plString m_sNormalTexture = "{ 0753ed83-0a62-42dc-a964-c0458ba1d74e }";
  plString m_sFoamTexture = "{ 7cdd0fa2-f12a-437e-9f40-d14fc11f7ec0 }";

  void Save(plStreamWriter& inout_stream) const;
  void Load(plStreamReader& inout_stream);

  /// Clamps untrusted/transformed data to the same finite ranges exposed by reflection.
  void Sanitize();

  /// Validates all authored properties. Deprecated compatibility slots are intentionally ignored.
  plStatus Validate() const;

  /// Collects every validation error plus advisory tuning warnings, instead of stopping at the first failure.
  void Validate(plDynamicArray<plWaterProfileIssue>& out_issues) const;
};

PL_DECLARE_REFLECTABLE_TYPE(PL_WATER_DLL, plWaterProfileResourceDescriptor);

/// \brief Loadable water profile resource used by all water body types.
class PL_WATER_DLL plWaterProfileResource : public plResource
{
  PL_ADD_DYNAMIC_REFLECTION(plWaterProfileResource, plResource);
  PL_RESOURCE_DECLARE_COMMON_CODE(plWaterProfileResource);
  PL_RESOURCE_DECLARE_CREATEABLE(plWaterProfileResource, plWaterProfileResourceDescriptor);

public:
  plWaterProfileResource();

  const plWaterProfileResourceDescriptor& GetDescriptor() const { return m_Descriptor; }

private:
  virtual plResourceLoadDesc UnloadData(Unload WhatToUnload) override;
  virtual plResourceLoadDesc UpdateContent(plStreamReader* pStream) override;
  virtual void UpdateMemoryUsage(MemoryUsage& out_memoryUsage) override;

  plWaterProfileResourceDescriptor m_Descriptor;
};
