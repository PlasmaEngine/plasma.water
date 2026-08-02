#include <WaterPlugin/WaterPluginPCH.h>

#include <Foundation/Utilities/AssetFileHeader.h>
#include <WaterPlugin/Resources/WaterProfileResource.h>

namespace
{
  constexpr plUInt32 s_uiMaxWaterProfileStringBytes = 4096;

  namespace WaterProfileLimits
  {
    // Wind speed is effectively a surface-motion multiplier (it scales the spectrum phase time);
    // 1 m/s is the calm baseline, storms sit around 3. 15 is already extreme.
    constexpr float WindSpeedMax = 15.0f;
    constexpr float WaveAmplitudeMax = 10.0f;
    constexpr float BaseWavelengthMin = 0.1f;
    constexpr float BaseWavelengthMax = 500.0f;
    constexpr float SmallestGpuWavelengthMin = 0.05f;
    constexpr float SmallestGpuWavelengthMax = 10.0f;
    constexpr float FlowScaleMax = 4.0f;
    constexpr float FiniteMeshCellSizeMin = 0.1f;
    constexpr float FiniteMeshCellSizeMax = 20.0f;
    constexpr float ShoreDistanceMin = 0.01f;
    constexpr float ShoreDistanceMax = 100.0f;
    constexpr float OpticalCoefficientMax = 10.0f;
    constexpr float IndexOfRefractionMax = 2.5f;
    constexpr float RefractionStrengthMax = 0.03f;
    constexpr float RefractionDepthMax = 100.0f;
    constexpr float EffectStrengthMax = 4.0f;
    constexpr float MicroNormalScaleMin = 0.05f;
    constexpr float MicroNormalScaleMax = 20.0f;
    constexpr float MicroNormalStrengthMax = 2.0f;
    constexpr float FoamRoughnessMin = 0.02f;
    constexpr float FoamBreakupScaleMin = 0.001f;
    constexpr float FoamBreakupScaleMax = 10.0f;
    constexpr float UnderwaterFogDensityMax = 10.0f;
    // Water is ~1000; heavy fantasy fluids stay well below mercury (~13600). Drag coefficients above 10 freeze bodies in place.
    constexpr float DensityMin = 1.0f;
    constexpr float DensityMax = 20000.0f;
    constexpr float DragMax = 10.0f;
  } // namespace WaterProfileLimits

  void SanitizeFloat(float& inout_fValue, float fDefault, float fMinimum, float fMaximum)
  {
    if (!plMath::IsFinite(inout_fValue))
      inout_fValue = fDefault;
    inout_fValue = plMath::Clamp(inout_fValue, fMinimum, fMaximum);
  }

  void SanitizeColor(plColor& inout_color, const plColor& defaultColor)
  {
    if (!inout_color.IsValid())
      inout_color = defaultColor;

    inout_color.r = plMath::Saturate(inout_color.r);
    inout_color.g = plMath::Saturate(inout_color.g);
    inout_color.b = plMath::Saturate(inout_color.b);
    inout_color.a = plMath::Saturate(inout_color.a);
  }

  bool IsFiniteInRange(float fValue, float fMinimum, float fMaximum)
  {
    return plMath::IsFinite(fValue) && fValue >= fMinimum && fValue <= fMaximum;
  }

  void SanitizeVec3(plVec3& inout_value, const plVec3& defaultValue, float fMinimum, float fMaximum)
  {
    if (!inout_value.IsValid())
      inout_value = defaultValue;

    inout_value.x = plMath::Clamp(inout_value.x, fMinimum, fMaximum);
    inout_value.y = plMath::Clamp(inout_value.y, fMinimum, fMaximum);
    inout_value.z = plMath::Clamp(inout_value.z, fMinimum, fMaximum);
  }

  bool IsVec3InRange(const plVec3& value, float fMinimum, float fMaximum)
  {
    return value.IsValid() && IsFiniteInRange(value.x, fMinimum, fMaximum) &&
           IsFiniteInRange(value.y, fMinimum, fMaximum) && IsFiniteInRange(value.z, fMinimum, fMaximum);
  }

  bool IsUnitColor(const plColor& color)
  {
    return color.IsValid() && color.r >= 0.0f && color.r <= 1.0f && color.g >= 0.0f && color.g <= 1.0f &&
           color.b >= 0.0f && color.b <= 1.0f && color.a >= 0.0f && color.a <= 1.0f;
  }

  template <typename StringType>
  bool ReadBoundedProfileString(plStreamReader& stream, StringType& out_sValue, plStringView sFieldName)
  {
    plUInt32 uiByteCount = 0;
    if (stream.ReadDWordValue(&uiByteCount).Failed())
    {
      plLog::Error("Water profile ended while reading the '{}' string length.", sFieldName);
      return false;
    }

    if (uiByteCount > s_uiMaxWaterProfileStringBytes)
    {
      plLog::Error("Water profile '{}' string declares {} bytes (maximum {}). The transformed profile is corrupt or was written by an incompatible plugin build.",
        sFieldName, uiByteCount, s_uiMaxWaterProfileStringBytes);
      return false;
    }

    plHybridArray<char, 256> bytes;
    bytes.SetCountUninitialized(uiByteCount);
    if (uiByteCount > 0 && stream.ReadBytes(bytes.GetData(), uiByteCount) != uiByteCount)
    {
      plLog::Error("Water profile ended while reading the '{}' string payload.", sFieldName);
      return false;
    }

    out_sValue = plStringView(bytes.GetData(), bytes.GetData() + uiByteCount);
    return true;
  }

  void ResetProfileDescriptor(plWaterProfileResourceDescriptor& descriptor)
  {
    // Use an lvalue copy. Never destroy/reconstruct a resource payload while other resource readers may hold it.
    const plWaterProfileResourceDescriptor defaults;
    descriptor = defaults;
  }
} // namespace

// clang-format off
PL_BEGIN_STATIC_REFLECTED_ENUM(plWaterWaveSpectrumMode, 1)
  PL_ENUM_CONSTANTS(plWaterWaveSpectrumMode::Analytic, plWaterWaveSpectrumMode::Phillips, plWaterWaveSpectrumMode::JONSWAP)
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plWaterSeedWidgetAttribute, 1, plRTTIDefaultAllocator<plWaterSeedWidgetAttribute>)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plWaterWindDirectionWidgetAttribute, 1, plRTTIDefaultAllocator<plWaterWindDirectionWidgetAttribute>)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_STATIC_REFLECTED_TYPE(plWaterProfileResourceDescriptor, plNoBase, 6, plRTTIDefaultAllocator<plWaterProfileResourceDescriptor>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("SurfaceMaterial", m_sSurfaceMaterial)->AddAttributes(new plDefaultValueAttribute(""), new plHiddenAttribute()),
    PL_MEMBER_PROPERTY("ShallowColor", m_ShallowColor)->AddAttributes(new plGroupAttribute("Surface", 2.0f), new plDefaultValueAttribute(plColor(0.08f, 0.34f, 0.42f, 0.65f)), new plExposeColorAlphaAttribute()),
    PL_MEMBER_PROPERTY("DeepColor", m_DeepColor)->AddAttributes(new plGroupAttribute("Surface", 2.0f), new plDefaultValueAttribute(plColor(0.008f, 0.035f, 0.075f, 0.95f)), new plExposeColorAlphaAttribute()),
    PL_MEMBER_PROPERTY("Roughness", m_fRoughness)->AddAttributes(new plGroupAttribute("Surface", 2.0f), new plDefaultValueAttribute(0.09f), new plClampValueAttribute(0.0f, 1.0f)),
    PL_MEMBER_PROPERTY("ReflectionStrength", m_fReflectionStrength)->AddAttributes(new plGroupAttribute("Surface", 2.0f), new plDefaultValueAttribute(1.0f), new plClampValueAttribute(0.0f, WaterProfileLimits::EffectStrengthMax)),
    PL_MEMBER_PROPERTY("EnvironmentReflectionWeight", m_fEnvironmentReflectionWeight)->AddAttributes(new plGroupAttribute("Surface", 2.0f), new plDefaultValueAttribute(1.0f), new plClampValueAttribute(0.0f, WaterProfileLimits::EffectStrengthMax)),
    PL_MEMBER_PROPERTY("MicroNormalScale", m_fMicroNormalScale)->AddAttributes(new plGroupAttribute("Surface", 2.0f), new plDefaultValueAttribute(1.0f), new plClampValueAttribute(WaterProfileLimits::MicroNormalScaleMin, WaterProfileLimits::MicroNormalScaleMax)),
    PL_MEMBER_PROPERTY("MicroNormalStrength", m_fMicroNormalStrength)->AddAttributes(new plGroupAttribute("Surface", 2.0f), new plDefaultValueAttribute(0.22f), new plClampValueAttribute(0.0f, WaterProfileLimits::MicroNormalStrengthMax)),
    PL_MEMBER_PROPERTY("SunGlitterStrength", m_fSunGlitterStrength)->AddAttributes(new plGroupAttribute("Surface", 2.0f), new plDefaultValueAttribute(0.35f), new plClampValueAttribute(0.0f, WaterProfileLimits::EffectStrengthMax)),

    PL_ENUM_MEMBER_PROPERTY("SpectrumMode", plWaterWaveSpectrumMode, m_SpectrumMode)->AddAttributes(new plGroupAttribute("Wave Spectrum", 1.0f), new plDefaultValueAttribute(plWaterWaveSpectrumMode::Analytic)),
    PL_MEMBER_PROPERTY("WaveSeed", m_uiWaveSeed)->AddAttributes(new plGroupAttribute("Wave Spectrum", 1.0f), new plDefaultValueAttribute(plVariant(static_cast<plUInt32>(0xC0FFEEu))), new plWaterSeedWidgetAttribute()),
    PL_MEMBER_PROPERTY("WindDirection", m_vWindDirection)->AddAttributes(new plGroupAttribute("Wave Spectrum", 1.0f), new plDefaultValueAttribute(plVec2(1.0f, 0.0f)), new plWaterWindDirectionWidgetAttribute()),
    PL_MEMBER_PROPERTY("WindSpeed", m_fWindSpeed)->AddAttributes(new plGroupAttribute("Wave Spectrum", 1.0f), new plDefaultValueAttribute(1.0f), new plClampValueAttribute(0.0f, WaterProfileLimits::WindSpeedMax), new plSuffixAttribute(" m/s")),
    PL_MEMBER_PROPERTY("WaveAmplitude", m_fWaveAmplitude)->AddAttributes(new plGroupAttribute("Wave Spectrum", 1.0f), new plDefaultValueAttribute(0.28f), new plClampValueAttribute(0.0f, WaterProfileLimits::WaveAmplitudeMax), new plSuffixAttribute(" m")),
    PL_MEMBER_PROPERTY("BaseWavelength", m_fBaseWavelength)->AddAttributes(new plGroupAttribute("Wave Spectrum", 1.0f), new plDefaultValueAttribute(18.0f), new plClampValueAttribute(WaterProfileLimits::BaseWavelengthMin, WaterProfileLimits::BaseWavelengthMax), new plSuffixAttribute(" m")),
    PL_MEMBER_PROPERTY("WaveSteepness", m_fWaveSteepness)->AddAttributes(new plGroupAttribute("Wave Spectrum", 1.0f), new plDefaultValueAttribute(0.32f), new plClampValueAttribute(0.0f, 1.0f)),
    PL_MEMBER_PROPERTY("CpuWaveCount", m_uiCpuWaveCount)->AddAttributes(new plGroupAttribute("Wave Spectrum", 1.0f), new plDefaultValueAttribute(8), new plClampValueAttribute(1, 16)),
    PL_MEMBER_PROPERTY("GpuCascadeCount", m_uiGpuCascadeCount)->AddAttributes(new plGroupAttribute("Wave Spectrum", 1.0f), new plDefaultValueAttribute(3), new plClampValueAttribute(0, 4)),
    PL_MEMBER_PROPERTY("GpuCascadeResolution", m_uiGpuCascadeResolution)->AddAttributes(new plDefaultValueAttribute(256), new plHiddenAttribute()),
    PL_MEMBER_PROPERTY("SmallestGpuWavelength", m_fSmallestGpuWavelength)->AddAttributes(new plGroupAttribute("Wave Spectrum", 1.0f), new plDefaultValueAttribute(0.5f), new plClampValueAttribute(WaterProfileLimits::SmallestGpuWavelengthMin, WaterProfileLimits::SmallestGpuWavelengthMax), new plSuffixAttribute(" m")),
    PL_MEMBER_PROPERTY("EnableLakeSpectrum", m_bEnableLakeSpectrum)->AddAttributes(new plGroupAttribute("Wave Spectrum", 1.0f), new plDefaultValueAttribute(false)),
    PL_MEMBER_PROPERTY("FlowScale", m_fFlowScale)->AddAttributes(new plGroupAttribute("Wave Spectrum", 1.0f), new plDefaultValueAttribute(1.0f), new plClampValueAttribute(0.0f, WaterProfileLimits::FlowScaleMax)),

    PL_MEMBER_PROPERTY("FiniteMeshCellSize", m_fFiniteMeshCellSize)->AddAttributes(new plGroupAttribute("Finite Mesh", 6.0f), new plDefaultValueAttribute(0.25f), new plClampValueAttribute(WaterProfileLimits::FiniteMeshCellSizeMin, WaterProfileLimits::FiniteMeshCellSizeMax), new plSuffixAttribute(" m")),
    PL_MEMBER_PROPERTY("MaxRiverCrossSubdivisions", m_uiMaxRiverCrossSubdivisions)->AddAttributes(new plGroupAttribute("Finite Mesh", 6.0f), new plDefaultValueAttribute(32), new plClampValueAttribute(2, 128)),
    PL_MEMBER_PROPERTY("ShoreWaveFadeDistance", m_fShoreWaveFadeDistance)->AddAttributes(new plGroupAttribute("Finite Mesh", 6.0f), new plDefaultValueAttribute(2.0f), new plClampValueAttribute(WaterProfileLimits::ShoreDistanceMin, WaterProfileLimits::ShoreDistanceMax), new plSuffixAttribute(" m")),
    PL_MEMBER_PROPERTY("RiverCurvatureRefinement", m_fRiverCurvatureRefinement)->AddAttributes(new plGroupAttribute("Finite Mesh", 6.0f), new plDefaultValueAttribute(1.0f), new plClampValueAttribute(0.0f, 1.0f)),

    PL_MEMBER_PROPERTY("Absorption", m_vAbsorption)->AddAttributes(new plGroupAttribute("Optics", 3.0f), new plDefaultValueAttribute(plVec3(0.35f, 0.07f, 0.025f)), new plClampValueAttribute(plVec3::MakeZero(), plVec3(WaterProfileLimits::OpticalCoefficientMax)), new plSuffixAttribute(" 1/m")),
    PL_MEMBER_PROPERTY("Scattering", m_vScattering)->AddAttributes(new plGroupAttribute("Optics", 3.0f), new plDefaultValueAttribute(plVec3(0.015f, 0.045f, 0.065f)), new plClampValueAttribute(plVec3::MakeZero(), plVec3(WaterProfileLimits::OpticalCoefficientMax)), new plSuffixAttribute(" 1/m")),
    PL_MEMBER_PROPERTY("IndexOfRefraction", m_fIndexOfRefraction)->AddAttributes(new plGroupAttribute("Optics", 3.0f), new plDefaultValueAttribute(1.333f), new plClampValueAttribute(1.0f, WaterProfileLimits::IndexOfRefractionMax)),
    PL_MEMBER_PROPERTY("RefractionStrength", m_fRefractionStrength)->AddAttributes(new plGroupAttribute("Optics", 3.0f), new plDefaultValueAttribute(0.012f), new plClampValueAttribute(0.0f, WaterProfileLimits::RefractionStrengthMax)),
    PL_MEMBER_PROPERTY("RefractionDepth", m_fRefractionDepth)->AddAttributes(new plGroupAttribute("Optics", 3.0f), new plDefaultValueAttribute(6.0f), new plClampValueAttribute(WaterProfileLimits::ShoreDistanceMin, WaterProfileLimits::RefractionDepthMax), new plSuffixAttribute(" m")),
    PL_MEMBER_PROPERTY("ShoreFadeDepth", m_fShoreFadeDepth)->AddAttributes(new plGroupAttribute("Optics", 3.0f), new plDefaultValueAttribute(1.2f), new plClampValueAttribute(WaterProfileLimits::ShoreDistanceMin, WaterProfileLimits::ShoreDistanceMax), new plSuffixAttribute(" m")),
    PL_MEMBER_PROPERTY("RefractionEdgeRejection", m_fRefractionEdgeRejection)->AddAttributes(new plGroupAttribute("Optics", 3.0f), new plDefaultValueAttribute(0.65f), new plClampValueAttribute(0.0f, 1.0f)),

    PL_MEMBER_PROPERTY("FoamStrength", m_fFoamStrength)->AddAttributes(new plGroupAttribute("Foam & Shoreline", 4.0f), new plDefaultValueAttribute(0.65f), new plClampValueAttribute(0.0f, WaterProfileLimits::EffectStrengthMax)),
    PL_MEMBER_PROPERTY("FoamCrestThreshold", m_fFoamCrestThreshold)->AddAttributes(new plGroupAttribute("Foam & Shoreline", 4.0f), new plDefaultValueAttribute(0.78f), new plClampValueAttribute(0.0f, 1.0f)),
    PL_MEMBER_PROPERTY("FoamShoreDepth", m_fFoamShoreDepth)->AddAttributes(new plGroupAttribute("Foam & Shoreline", 4.0f), new plDefaultValueAttribute(0.75f), new plClampValueAttribute(WaterProfileLimits::ShoreDistanceMin, WaterProfileLimits::ShoreDistanceMax), new plSuffixAttribute(" m")),
    PL_MEMBER_PROPERTY("FoamInteractionStrength", m_fFoamInteractionStrength)->AddAttributes(new plDefaultValueAttribute(0.8f), new plHiddenAttribute()),
    PL_MEMBER_PROPERTY("FoamColor", m_FoamColor)->AddAttributes(new plGroupAttribute("Foam & Shoreline", 4.0f), new plDefaultValueAttribute(plColor(0.85f, 0.9f, 0.92f, 1.0f)), new plExposeColorAlphaAttribute()),
    PL_MEMBER_PROPERTY("FoamRoughness", m_fFoamRoughness)->AddAttributes(new plGroupAttribute("Foam & Shoreline", 4.0f), new plDefaultValueAttribute(0.72f), new plClampValueAttribute(WaterProfileLimits::FoamRoughnessMin, 1.0f)),
    PL_MEMBER_PROPERTY("FoamBreakupScale", m_fFoamBreakupScale)->AddAttributes(new plGroupAttribute("Foam & Shoreline", 4.0f), new plDefaultValueAttribute(0.18f), new plClampValueAttribute(WaterProfileLimits::FoamBreakupScaleMin, WaterProfileLimits::FoamBreakupScaleMax), new plSuffixAttribute(" 1/m")),

    PL_MEMBER_PROPERTY("UnderwaterFogDensity", m_fUnderwaterFogDensity)->AddAttributes(new plGroupAttribute("Underwater", 5.0f), new plDefaultValueAttribute(0.06f), new plClampValueAttribute(0.0f, WaterProfileLimits::UnderwaterFogDensityMax), new plSuffixAttribute(" 1/m")),
    PL_MEMBER_PROPERTY("UnderwaterScattering", m_fUnderwaterScattering)->AddAttributes(new plGroupAttribute("Underwater", 5.0f), new plDefaultValueAttribute(0.65f), new plClampValueAttribute(0.0f, WaterProfileLimits::EffectStrengthMax)),
    PL_MEMBER_PROPERTY("UnderwaterHaze", m_fUnderwaterHaze)->AddAttributes(new plGroupAttribute("Underwater", 5.0f), new plDefaultValueAttribute(0.25f), new plClampValueAttribute(0.0f, 1.0f)),
    PL_MEMBER_PROPERTY("UnderwaterDistortion", m_fUnderwaterDistortion)->AddAttributes(new plGroupAttribute("Underwater", 5.0f), new plDefaultValueAttribute(0.18f), new plClampValueAttribute(0.0f, 1.0f)),
    // Keep the reflected property name for existing object graphs; localization presents it as Transmitted Caustics.
    PL_MEMBER_PROPERTY("UnderwaterCaustics", m_fUnderwaterCaustics)->AddAttributes(new plGroupAttribute("Caustics", 5.0f), new plDefaultValueAttribute(0.25f), new plClampValueAttribute(0.0f, WaterProfileLimits::EffectStrengthMax)),
    PL_MEMBER_PROPERTY("ReflectedCaustics", m_fReflectedCaustics)->AddAttributes(new plGroupAttribute("Caustics", 5.0f), new plDefaultValueAttribute(0.15f), new plClampValueAttribute(0.0f, WaterProfileLimits::EffectStrengthMax)),
    PL_MEMBER_PROPERTY("UnderwaterLightShafts", m_fUnderwaterLightShafts)->AddAttributes(new plGroupAttribute("Underwater", 5.0f), new plDefaultValueAttribute(0.12f), new plClampValueAttribute(0.0f, WaterProfileLimits::EffectStrengthMax)),
    PL_MEMBER_PROPERTY("UnderwaterMeniscus", m_fUnderwaterMeniscus)->AddAttributes(new plGroupAttribute("Underwater", 5.0f), new plDefaultValueAttribute(0.1f), new plClampValueAttribute(0.0f, 1.0f)),

    PL_MEMBER_PROPERTY("Density", m_fDensity)->AddAttributes(new plGroupAttribute("Physics", 7.0f), new plDefaultValueAttribute(1000.0f), new plClampValueAttribute(WaterProfileLimits::DensityMin, WaterProfileLimits::DensityMax), new plSuffixAttribute(" kg/m^3")),
    PL_MEMBER_PROPERTY("LinearDrag", m_fLinearDrag)->AddAttributes(new plGroupAttribute("Physics", 7.0f), new plDefaultValueAttribute(0.3f), new plClampValueAttribute(0.0f, WaterProfileLimits::DragMax)),
    PL_MEMBER_PROPERTY("AngularDrag", m_fAngularDrag)->AddAttributes(new plGroupAttribute("Physics", 7.0f), new plDefaultValueAttribute(0.05f), new plClampValueAttribute(0.0f, WaterProfileLimits::DragMax)),

    // Propagation speed and damping stay hidden: the interaction atlas is one global wave field, so they are
    // governed by the Water.Interaction.* cvars until the paged allocator gives texels profile ownership.
    PL_MEMBER_PROPERTY("RipplePropagationSpeed", m_fRipplePropagationSpeed)->AddAttributes(new plDefaultValueAttribute(3.0f), new plHiddenAttribute()),
    PL_MEMBER_PROPERTY("RippleDamping", m_fRippleDamping)->AddAttributes(new plDefaultValueAttribute(0.985f), new plHiddenAttribute()),
    PL_MEMBER_PROPERTY("RippleMaxAmplitude", m_fRippleMaxAmplitude)->AddAttributes(new plGroupAttribute("Foam & Shoreline", 4.0f), new plDefaultValueAttribute(0.2f), new plClampValueAttribute(0.0f, 10.0f), new plSuffixAttribute(" m")),

    PL_MEMBER_PROPERTY("NormalTexture", m_sNormalTexture)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_Texture_2D"), new plGroupAttribute("Textures", 8.0f), new plDefaultValueAttribute("{ 0753ed83-0a62-42dc-a964-c0458ba1d74e }")),
    PL_MEMBER_PROPERTY("FoamTexture", m_sFoamTexture)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_Texture_2D"), new plGroupAttribute("Textures", 8.0f), new plDefaultValueAttribute("{ 7cdd0fa2-f12a-437e-9f40-d14fc11f7ec0 }")),
  }
  PL_END_PROPERTIES;
}
PL_END_STATIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plWaterProfileResource, 1, plRTTIDefaultAllocator<plWaterProfileResource>)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_RESOURCE_IMPLEMENT_COMMON_CODE(plWaterProfileResource);
// clang-format on

plUInt32 plGetWaterProfileDescriptorRuntimeAbiVersion()
{
  return PL_WATER_PROFILE_DESCRIPTOR_ABI_VERSION;
}

plUInt32 plGetWaterProfileDescriptorRuntimeSize()
{
  return sizeof(plWaterProfileResourceDescriptor);
}

plWaterProfileResource::plWaterProfileResource()
  : plResource(DoUpdate::OnAnyThread, 1)
{
}

PL_RESOURCE_IMPLEMENT_CREATEABLE(plWaterProfileResource, plWaterProfileResourceDescriptor)
{
  m_Descriptor = descriptor;
  m_Descriptor.Sanitize();

  plResourceLoadDesc result;
  result.m_uiQualityLevelsDiscardable = 0;
  result.m_uiQualityLevelsLoadable = 0;
  result.m_State = plResourceState::Loaded;
  return result;
}

plResourceLoadDesc plWaterProfileResource::UnloadData(Unload WhatToUnload)
{
  PL_IGNORE_UNUSED(WhatToUnload);
  ResetProfileDescriptor(m_Descriptor);

  plResourceLoadDesc result;
  result.m_uiQualityLevelsDiscardable = 0;
  result.m_uiQualityLevelsLoadable = 0;
  result.m_State = plResourceState::Unloaded;
  return result;
}

plResourceLoadDesc plWaterProfileResource::UpdateContent(plStreamReader* pStream)
{
  PL_LOG_BLOCK("plWaterProfileResource::UpdateContent", GetResourceIdOrDescription());

  plResourceLoadDesc result;
  result.m_uiQualityLevelsDiscardable = 0;
  result.m_uiQualityLevelsLoadable = 0;

  if (pStream == nullptr)
  {
    result.m_State = plResourceState::LoadedResourceMissing;
    return result;
  }

  plStringBuilder absolutePath;
  (*pStream) >> absolutePath;

  plAssetFileHeader assetHeader;
  assetHeader.Read(*pStream).IgnoreResult();
  m_Descriptor.Load(*pStream);

  result.m_State = plResourceState::Loaded;
  return result;
}

void plWaterProfileResource::UpdateMemoryUsage(MemoryUsage& out_memoryUsage)
{
  out_memoryUsage.m_uiMemoryCPU = sizeof(plWaterProfileResource) + sizeof(plWaterProfileResourceDescriptor) +
                                  m_Descriptor.m_sSurfaceMaterial.GetHeapMemoryUsage() + m_Descriptor.m_sNormalTexture.GetHeapMemoryUsage() +
                                  m_Descriptor.m_sFoamTexture.GetHeapMemoryUsage();
  out_memoryUsage.m_uiMemoryGPU = 0;
}

void plWaterProfileResourceDescriptor::Save(plStreamWriter& inout_stream) const
{
  constexpr plUInt8 uiVersion = 5;
  inout_stream << uiVersion;

  inout_stream << m_uiWaveSeed;
  inout_stream << m_vWindDirection;
  inout_stream << m_fWindSpeed;
  inout_stream << m_fWaveAmplitude;
  inout_stream << m_fBaseWavelength;
  inout_stream << m_fWaveSteepness;
  inout_stream << m_uiCpuWaveCount;

  inout_stream << m_ShallowColor;
  inout_stream << m_DeepColor;
  inout_stream << m_vAbsorption;
  inout_stream << m_vScattering;
  inout_stream << m_fIndexOfRefraction;
  inout_stream << m_fRoughness;
  inout_stream << m_fFoamStrength;
  inout_stream << m_fUnderwaterFogDensity;

  inout_stream << m_fDensity;
  inout_stream << m_fLinearDrag;
  inout_stream << m_fAngularDrag;

  inout_stream << m_fRipplePropagationSpeed;
  inout_stream << m_fRippleDamping;
  inout_stream << m_fRippleMaxAmplitude;
  inout_stream << m_sNormalTexture;
  inout_stream << m_sFoamTexture;

  inout_stream << m_sSurfaceMaterial;
  inout_stream << m_SpectrumMode;
  inout_stream << m_uiGpuCascadeCount;
  inout_stream << m_uiGpuCascadeResolution;
  inout_stream << m_fSmallestGpuWavelength;
  inout_stream << m_bEnableLakeSpectrum;
  inout_stream << m_fFlowScale;
  inout_stream << m_fReflectionStrength;
  inout_stream << m_fLegacyReflectionHistoryWeight;
  inout_stream << m_fLegacyReflectionTraceDistance;
  inout_stream << m_fRefractionStrength;
  inout_stream << m_fRefractionDepth;
  inout_stream << m_fShoreFadeDepth;
  inout_stream << m_fFoamCrestThreshold;
  inout_stream << m_fFoamShoreDepth;
  inout_stream << m_fFoamInteractionStrength;
  inout_stream << m_fUnderwaterScattering;
  inout_stream << m_fUnderwaterHaze;
  inout_stream << m_fUnderwaterDistortion;
  inout_stream << m_fUnderwaterCaustics;
  inout_stream << m_fUnderwaterLightShafts;
  inout_stream << m_fUnderwaterMeniscus;
  inout_stream << m_fFiniteMeshCellSize;
  inout_stream << m_uiMaxRiverCrossSubdivisions;
  inout_stream << m_fShoreWaveFadeDistance;
  inout_stream << m_fRiverCurvatureRefinement;
  inout_stream << m_fEnvironmentReflectionWeight;
  inout_stream << m_fMicroNormalScale;
  inout_stream << m_fMicroNormalStrength;
  inout_stream << m_fSunGlitterStrength;
  inout_stream << m_fRefractionEdgeRejection;
  inout_stream << m_FoamColor;
  inout_stream << m_fFoamRoughness;
  inout_stream << m_fFoamBreakupScale;
  inout_stream << m_fReflectedCaustics;
}

void plWaterProfileResourceDescriptor::Load(plStreamReader& inout_stream)
{
  ResetProfileDescriptor(*this);

  plUInt8 uiVersion = 0;
  inout_stream >> uiVersion;

  if (uiVersion < 1 || uiVersion > 5)
  {
    plLog::Error("Unsupported water profile version '{}'.", uiVersion);
    ResetProfileDescriptor(*this);
    return;
  }

  inout_stream >> m_uiWaveSeed;
  inout_stream >> m_vWindDirection;
  inout_stream >> m_fWindSpeed;
  inout_stream >> m_fWaveAmplitude;
  inout_stream >> m_fBaseWavelength;
  inout_stream >> m_fWaveSteepness;
  inout_stream >> m_uiCpuWaveCount;

  inout_stream >> m_ShallowColor;
  inout_stream >> m_DeepColor;
  inout_stream >> m_vAbsorption;
  inout_stream >> m_vScattering;
  inout_stream >> m_fIndexOfRefraction;
  inout_stream >> m_fRoughness;
  inout_stream >> m_fFoamStrength;
  inout_stream >> m_fUnderwaterFogDensity;

  inout_stream >> m_fDensity;
  inout_stream >> m_fLinearDrag;
  inout_stream >> m_fAngularDrag;

  inout_stream >> m_fRipplePropagationSpeed;
  inout_stream >> m_fRippleDamping;
  inout_stream >> m_fRippleMaxAmplitude;
  if (!ReadBoundedProfileString(inout_stream, m_sNormalTexture, "NormalTexture") ||
      !ReadBoundedProfileString(inout_stream, m_sFoamTexture, "FoamTexture"))
  {
    ResetProfileDescriptor(*this);
    return;
  }

  if (uiVersion >= 2)
  {
    if (!ReadBoundedProfileString(inout_stream, m_sSurfaceMaterial, "SurfaceMaterial"))
    {
      ResetProfileDescriptor(*this);
      return;
    }
    inout_stream >> m_SpectrumMode;
    inout_stream >> m_uiGpuCascadeCount;
    inout_stream >> m_uiGpuCascadeResolution;
    inout_stream >> m_fSmallestGpuWavelength;
    inout_stream >> m_bEnableLakeSpectrum;
    inout_stream >> m_fFlowScale;
    inout_stream >> m_fReflectionStrength;
    inout_stream >> m_fLegacyReflectionHistoryWeight;
    inout_stream >> m_fLegacyReflectionTraceDistance;
    inout_stream >> m_fRefractionStrength;
    inout_stream >> m_fRefractionDepth;
    inout_stream >> m_fShoreFadeDepth;
    inout_stream >> m_fFoamCrestThreshold;
    inout_stream >> m_fFoamShoreDepth;
    inout_stream >> m_fFoamInteractionStrength;
    inout_stream >> m_fUnderwaterScattering;
    inout_stream >> m_fUnderwaterHaze;
    inout_stream >> m_fUnderwaterDistortion;
    inout_stream >> m_fUnderwaterCaustics;
    inout_stream >> m_fUnderwaterLightShafts;
    inout_stream >> m_fUnderwaterMeniscus;
  }

  if (uiVersion >= 3)
  {
    inout_stream >> m_fFiniteMeshCellSize;
    inout_stream >> m_uiMaxRiverCrossSubdivisions;
    inout_stream >> m_fShoreWaveFadeDistance;
    inout_stream >> m_fRiverCurvatureRefinement;
  }

  if (uiVersion >= 4)
  {
    inout_stream >> m_fEnvironmentReflectionWeight;
    inout_stream >> m_fMicroNormalScale;
    inout_stream >> m_fMicroNormalStrength;
    inout_stream >> m_fSunGlitterStrength;
    inout_stream >> m_fRefractionEdgeRejection;
    inout_stream >> m_FoamColor;
    inout_stream >> m_fFoamRoughness;
    inout_stream >> m_fFoamBreakupScale;

    // Assets saved while the v4 reflected properties were first introduced may contain an all-zero extension block;
    // the editor object graph uses zero for absent members instead of the C++ member initializers. Migrate only that
    // unmistakable combination so an intentionally disabled individual effect remains valid.
    if (m_fEnvironmentReflectionWeight == 0.0f && m_fMicroNormalScale == 0.0f && m_fMicroNormalStrength == 0.0f &&
        m_fSunGlitterStrength == 0.0f && m_fRefractionEdgeRejection == 0.0f && m_FoamColor == plColor::Black &&
        m_fFoamRoughness == 0.0f && m_fFoamBreakupScale == 0.0f)
    {
      const plWaterProfileResourceDescriptor defaults;
      m_fEnvironmentReflectionWeight = defaults.m_fEnvironmentReflectionWeight;
      m_fMicroNormalScale = defaults.m_fMicroNormalScale;
      m_fMicroNormalStrength = defaults.m_fMicroNormalStrength;
      m_fSunGlitterStrength = defaults.m_fSunGlitterStrength;
      m_fRefractionEdgeRejection = defaults.m_fRefractionEdgeRejection;
      m_FoamColor = defaults.m_FoamColor;
      m_fFoamRoughness = defaults.m_fFoamRoughness;
      m_fFoamBreakupScale = defaults.m_fFoamBreakupScale;
    }
  }

  if (uiVersion >= 5)
  {
    inout_stream >> m_fReflectedCaustics;
  }

  Sanitize();
}

void plWaterProfileResourceDescriptor::Sanitize()
{
  const plWaterProfileResourceDescriptor defaults;

  if (m_SpectrumMode.GetValue() > plWaterWaveSpectrumMode::JONSWAP)
    m_SpectrumMode = defaults.m_SpectrumMode;

  if (!m_vWindDirection.IsValid() || m_vWindDirection.GetLengthSquared() < 0.0001f)
    m_vWindDirection = defaults.m_vWindDirection;

  SanitizeFloat(m_fWindSpeed, defaults.m_fWindSpeed, 0.0f, WaterProfileLimits::WindSpeedMax);
  SanitizeFloat(m_fWaveAmplitude, defaults.m_fWaveAmplitude, 0.0f, WaterProfileLimits::WaveAmplitudeMax);
  SanitizeFloat(m_fBaseWavelength, defaults.m_fBaseWavelength, WaterProfileLimits::BaseWavelengthMin, WaterProfileLimits::BaseWavelengthMax);
  SanitizeFloat(m_fWaveSteepness, defaults.m_fWaveSteepness, 0.0f, 1.0f);
  m_uiCpuWaveCount = plMath::Clamp<plUInt8>(m_uiCpuWaveCount, 1, 16);
  m_uiGpuCascadeCount = plMath::Clamp<plUInt8>(m_uiGpuCascadeCount, 0, 4);
  SanitizeFloat(m_fSmallestGpuWavelength, defaults.m_fSmallestGpuWavelength, WaterProfileLimits::SmallestGpuWavelengthMin,
    WaterProfileLimits::SmallestGpuWavelengthMax);
  m_fSmallestGpuWavelength = plMath::Min(m_fSmallestGpuWavelength, m_fBaseWavelength);
  SanitizeFloat(m_fFlowScale, defaults.m_fFlowScale, 0.0f, WaterProfileLimits::FlowScaleMax);

  SanitizeFloat(m_fFiniteMeshCellSize, defaults.m_fFiniteMeshCellSize, WaterProfileLimits::FiniteMeshCellSizeMin,
    WaterProfileLimits::FiniteMeshCellSizeMax);
  m_uiMaxRiverCrossSubdivisions = plMath::Clamp<plUInt16>(m_uiMaxRiverCrossSubdivisions, 2, 128);
  SanitizeFloat(m_fShoreWaveFadeDistance, defaults.m_fShoreWaveFadeDistance, WaterProfileLimits::ShoreDistanceMin,
    WaterProfileLimits::ShoreDistanceMax);
  SanitizeFloat(m_fRiverCurvatureRefinement, defaults.m_fRiverCurvatureRefinement, 0.0f, 1.0f);

  SanitizeColor(m_ShallowColor, defaults.m_ShallowColor);
  SanitizeColor(m_DeepColor, defaults.m_DeepColor);
  SanitizeVec3(m_vAbsorption, defaults.m_vAbsorption, 0.0f, WaterProfileLimits::OpticalCoefficientMax);
  SanitizeVec3(m_vScattering, defaults.m_vScattering, 0.0f, WaterProfileLimits::OpticalCoefficientMax);
  SanitizeFloat(m_fIndexOfRefraction, defaults.m_fIndexOfRefraction, 1.0f, WaterProfileLimits::IndexOfRefractionMax);
  SanitizeFloat(m_fRoughness, defaults.m_fRoughness, 0.0f, 1.0f);
  SanitizeFloat(m_fReflectionStrength, defaults.m_fReflectionStrength, 0.0f, WaterProfileLimits::EffectStrengthMax);
  SanitizeFloat(m_fEnvironmentReflectionWeight, defaults.m_fEnvironmentReflectionWeight, 0.0f, WaterProfileLimits::EffectStrengthMax);
  SanitizeFloat(m_fMicroNormalScale, defaults.m_fMicroNormalScale, WaterProfileLimits::MicroNormalScaleMin, WaterProfileLimits::MicroNormalScaleMax);
  SanitizeFloat(m_fMicroNormalStrength, defaults.m_fMicroNormalStrength, 0.0f, WaterProfileLimits::MicroNormalStrengthMax);
  SanitizeFloat(m_fSunGlitterStrength, defaults.m_fSunGlitterStrength, 0.0f, WaterProfileLimits::EffectStrengthMax);
  SanitizeFloat(m_fRefractionEdgeRejection, defaults.m_fRefractionEdgeRejection, 0.0f, 1.0f);
  SanitizeFloat(m_fRefractionStrength, defaults.m_fRefractionStrength, 0.0f, WaterProfileLimits::RefractionStrengthMax);
  SanitizeFloat(m_fRefractionDepth, defaults.m_fRefractionDepth, WaterProfileLimits::ShoreDistanceMin, WaterProfileLimits::RefractionDepthMax);
  SanitizeFloat(m_fShoreFadeDepth, defaults.m_fShoreFadeDepth, WaterProfileLimits::ShoreDistanceMin, WaterProfileLimits::ShoreDistanceMax);

  SanitizeFloat(m_fFoamStrength, defaults.m_fFoamStrength, 0.0f, WaterProfileLimits::EffectStrengthMax);
  SanitizeFloat(m_fFoamCrestThreshold, defaults.m_fFoamCrestThreshold, 0.0f, 1.0f);
  SanitizeFloat(m_fFoamShoreDepth, defaults.m_fFoamShoreDepth, WaterProfileLimits::ShoreDistanceMin, WaterProfileLimits::ShoreDistanceMax);
  SanitizeFloat(m_fFoamInteractionStrength, defaults.m_fFoamInteractionStrength, 0.0f, WaterProfileLimits::EffectStrengthMax);
  SanitizeColor(m_FoamColor, defaults.m_FoamColor);
  SanitizeFloat(m_fFoamRoughness, defaults.m_fFoamRoughness, WaterProfileLimits::FoamRoughnessMin, 1.0f);
  SanitizeFloat(m_fFoamBreakupScale, defaults.m_fFoamBreakupScale, WaterProfileLimits::FoamBreakupScaleMin,
    WaterProfileLimits::FoamBreakupScaleMax);

  SanitizeFloat(m_fUnderwaterFogDensity, defaults.m_fUnderwaterFogDensity, 0.0f, WaterProfileLimits::UnderwaterFogDensityMax);
  SanitizeFloat(m_fUnderwaterScattering, defaults.m_fUnderwaterScattering, 0.0f, WaterProfileLimits::EffectStrengthMax);
  SanitizeFloat(m_fUnderwaterHaze, defaults.m_fUnderwaterHaze, 0.0f, 1.0f);
  SanitizeFloat(m_fUnderwaterDistortion, defaults.m_fUnderwaterDistortion, 0.0f, 1.0f);
  SanitizeFloat(m_fUnderwaterCaustics, defaults.m_fUnderwaterCaustics, 0.0f, WaterProfileLimits::EffectStrengthMax);
  SanitizeFloat(m_fReflectedCaustics, defaults.m_fReflectedCaustics, 0.0f, WaterProfileLimits::EffectStrengthMax);
  SanitizeFloat(m_fUnderwaterLightShafts, defaults.m_fUnderwaterLightShafts, 0.0f, WaterProfileLimits::EffectStrengthMax);
  SanitizeFloat(m_fUnderwaterMeniscus, defaults.m_fUnderwaterMeniscus, 0.0f, 1.0f);

  SanitizeFloat(m_fDensity, defaults.m_fDensity, WaterProfileLimits::DensityMin, WaterProfileLimits::DensityMax);
  SanitizeFloat(m_fLinearDrag, defaults.m_fLinearDrag, 0.0f, WaterProfileLimits::DragMax);
  SanitizeFloat(m_fAngularDrag, defaults.m_fAngularDrag, 0.0f, WaterProfileLimits::DragMax);

  SanitizeFloat(m_fRippleMaxAmplitude, defaults.m_fRippleMaxAmplitude, 0.0f, 10.0f);
}

plStatus plWaterProfileResourceDescriptor::Validate() const
{
  plHybridArray<plWaterProfileIssue, 8> issues;
  Validate(issues);

  for (const plWaterProfileIssue& issue : issues)
  {
    if (issue.m_Severity == plWaterProfileIssue::Severity::Error)
      return plStatus(issue.m_sMessage.GetView());
  }

  return plStatus(PL_SUCCESS);
}

void plWaterProfileResourceDescriptor::Validate(plDynamicArray<plWaterProfileIssue>& out_issues) const
{
  auto AddIssue = [&out_issues](plWaterProfileIssue::Severity severity, plStringView sProperty, plStringView sMessage) {
    plWaterProfileIssue& issue = out_issues.ExpandAndGetRef();
    issue.m_Severity = severity;
    issue.m_sProperty = sProperty;
    issue.m_sMessage = sMessage;
  };
  auto AddError = [&AddIssue](plStringView sProperty, plStringView sMessage) { AddIssue(plWaterProfileIssue::Severity::Error, sProperty, sMessage); };

  if (m_SpectrumMode.GetValue() > plWaterWaveSpectrumMode::JONSWAP)
    AddError("SpectrumMode", "SpectrumMode is not a supported water spectrum model.");
  if (!m_vWindDirection.IsValid() || m_vWindDirection.GetLengthSquared() < 0.0001f)
    AddError("WindDirection", "WindDirection must be finite and non-zero.");
  if (!IsFiniteInRange(m_fWindSpeed, 0.0f, WaterProfileLimits::WindSpeedMax))
    AddError("WindSpeed", "WindSpeed must be between 0 and 15 m/s.");
  if (!IsFiniteInRange(m_fWaveAmplitude, 0.0f, WaterProfileLimits::WaveAmplitudeMax))
    AddError("WaveAmplitude", "WaveAmplitude must be between 0 and 10 metres.");
  if (!IsFiniteInRange(m_fBaseWavelength, WaterProfileLimits::BaseWavelengthMin, WaterProfileLimits::BaseWavelengthMax))
    AddError("BaseWavelength", "BaseWavelength must be between 0.1 and 500 metres.");
  if (!IsFiniteInRange(m_fWaveSteepness, 0.0f, 1.0f))
    AddError("WaveSteepness", "WaveSteepness must be between zero and one.");
  if (m_uiCpuWaveCount < 1 || m_uiCpuWaveCount > 16)
    AddError("CpuWaveCount", "CpuWaveCount must be between 1 and 16.");
  if (m_uiGpuCascadeCount > 4)
    AddError("GpuCascadeCount", "GpuCascadeCount cannot exceed 4.");
  if (!IsFiniteInRange(m_fSmallestGpuWavelength, WaterProfileLimits::SmallestGpuWavelengthMin,
        WaterProfileLimits::SmallestGpuWavelengthMax) ||
      m_fSmallestGpuWavelength > m_fBaseWavelength)
    AddError("SmallestGpuWavelength", "SmallestGpuWavelength must be between 0.05 metres and BaseWavelength.");
  if (!IsFiniteInRange(m_fFlowScale, 0.0f, WaterProfileLimits::FlowScaleMax))
    AddError("FlowScale", "FlowScale must be between zero and four.");

  if (!IsFiniteInRange(m_fFiniteMeshCellSize, WaterProfileLimits::FiniteMeshCellSizeMin, WaterProfileLimits::FiniteMeshCellSizeMax))
    AddError("FiniteMeshCellSize", "FiniteMeshCellSize must be between 0.1 and 20 metres.");
  if (m_uiMaxRiverCrossSubdivisions < 2 || m_uiMaxRiverCrossSubdivisions > 128)
    AddError("MaxRiverCrossSubdivisions", "MaxRiverCrossSubdivisions must be between 2 and 128.");
  if (!IsFiniteInRange(m_fShoreWaveFadeDistance, WaterProfileLimits::ShoreDistanceMin, WaterProfileLimits::ShoreDistanceMax))
    AddError("ShoreWaveFadeDistance", "ShoreWaveFadeDistance must be between 0.01 and 100 metres.");
  if (!IsFiniteInRange(m_fRiverCurvatureRefinement, 0.0f, 1.0f))
    AddError("RiverCurvatureRefinement", "RiverCurvatureRefinement must be between zero and one.");

  if (!IsUnitColor(m_ShallowColor))
    AddError("ShallowColor", "ShallowColor channels must be finite and between zero and one.");
  if (!IsUnitColor(m_DeepColor))
    AddError("DeepColor", "DeepColor channels must be finite and between zero and one.");
  if (!IsVec3InRange(m_vAbsorption, 0.0f, WaterProfileLimits::OpticalCoefficientMax))
    AddError("Absorption", "Absorption channels must be between zero and 10 inverse metres.");
  if (!IsVec3InRange(m_vScattering, 0.0f, WaterProfileLimits::OpticalCoefficientMax))
    AddError("Scattering", "Scattering channels must be between zero and 10 inverse metres.");
  if (!IsFiniteInRange(m_fIndexOfRefraction, 1.0f, WaterProfileLimits::IndexOfRefractionMax))
    AddError("IndexOfRefraction", "IndexOfRefraction must be between 1.0 and 2.5.");
  if (!IsFiniteInRange(m_fRoughness, 0.0f, 1.0f))
    AddError("Roughness", "Roughness must be between zero and one.");
  if (!IsFiniteInRange(m_fReflectionStrength, 0.0f, WaterProfileLimits::EffectStrengthMax))
    AddError("ReflectionStrength", "ReflectionStrength must be between zero and four.");
  if (!IsFiniteInRange(m_fEnvironmentReflectionWeight, 0.0f, WaterProfileLimits::EffectStrengthMax))
    AddError("EnvironmentReflectionWeight", "EnvironmentReflectionWeight must be between zero and four.");
  if (!IsFiniteInRange(m_fMicroNormalScale, WaterProfileLimits::MicroNormalScaleMin, WaterProfileLimits::MicroNormalScaleMax))
    AddError("MicroNormalScale", "MicroNormalScale must be between 0.05 and 20.");
  if (!IsFiniteInRange(m_fMicroNormalStrength, 0.0f, WaterProfileLimits::MicroNormalStrengthMax))
    AddError("MicroNormalStrength", "MicroNormalStrength must be between zero and two.");
  if (!IsFiniteInRange(m_fSunGlitterStrength, 0.0f, WaterProfileLimits::EffectStrengthMax))
    AddError("SunGlitterStrength", "SunGlitterStrength must be between zero and four.");
  if (!IsFiniteInRange(m_fRefractionStrength, 0.0f, WaterProfileLimits::RefractionStrengthMax))
    AddError("RefractionStrength", "RefractionStrength must be between zero and 0.03.");
  if (!IsFiniteInRange(m_fRefractionDepth, WaterProfileLimits::ShoreDistanceMin, WaterProfileLimits::RefractionDepthMax))
    AddError("RefractionDepth", "RefractionDepth must be between 0.01 and 100 metres.");
  if (!IsFiniteInRange(m_fShoreFadeDepth, WaterProfileLimits::ShoreDistanceMin, WaterProfileLimits::ShoreDistanceMax))
    AddError("ShoreFadeDepth", "ShoreFadeDepth must be between 0.01 and 100 metres.");
  if (!IsFiniteInRange(m_fRefractionEdgeRejection, 0.0f, 1.0f))
    AddError("RefractionEdgeRejection", "RefractionEdgeRejection must be between zero and one.");

  if (!IsFiniteInRange(m_fFoamStrength, 0.0f, WaterProfileLimits::EffectStrengthMax))
    AddError("FoamStrength", "FoamStrength must be between zero and four.");
  if (!IsFiniteInRange(m_fFoamInteractionStrength, 0.0f, WaterProfileLimits::EffectStrengthMax))
    AddError("FoamInteractionStrength", "FoamInteractionStrength must be between zero and four.");
  if (!IsFiniteInRange(m_fFoamCrestThreshold, 0.0f, 1.0f))
    AddError("FoamCrestThreshold", "FoamCrestThreshold must be between zero and one.");
  if (!IsFiniteInRange(m_fFoamShoreDepth, WaterProfileLimits::ShoreDistanceMin, WaterProfileLimits::ShoreDistanceMax))
    AddError("FoamShoreDepth", "FoamShoreDepth must be between 0.01 and 100 metres.");
  if (!IsUnitColor(m_FoamColor))
    AddError("FoamColor", "FoamColor channels must be finite and between zero and one.");
  if (!IsFiniteInRange(m_fFoamRoughness, WaterProfileLimits::FoamRoughnessMin, 1.0f))
    AddError("FoamRoughness", "FoamRoughness must be between 0.02 and one.");
  if (!IsFiniteInRange(m_fFoamBreakupScale, WaterProfileLimits::FoamBreakupScaleMin, WaterProfileLimits::FoamBreakupScaleMax))
    AddError("FoamBreakupScale", "FoamBreakupScale must be between 0.001 and 10 inverse metres.");

  if (!IsFiniteInRange(m_fUnderwaterFogDensity, 0.0f, WaterProfileLimits::UnderwaterFogDensityMax))
    AddError("UnderwaterFogDensity", "UnderwaterFogDensity must be between zero and 10 inverse metres.");
  if (!IsFiniteInRange(m_fUnderwaterScattering, 0.0f, WaterProfileLimits::EffectStrengthMax))
    AddError("UnderwaterScattering", "UnderwaterScattering must be between zero and four.");
  if (!IsFiniteInRange(m_fUnderwaterHaze, 0.0f, 1.0f))
    AddError("UnderwaterHaze", "UnderwaterHaze must be between zero and one.");
  if (!IsFiniteInRange(m_fUnderwaterDistortion, 0.0f, 1.0f))
    AddError("UnderwaterDistortion", "UnderwaterDistortion must be between zero and one.");
  if (!IsFiniteInRange(m_fUnderwaterMeniscus, 0.0f, 1.0f))
    AddError("UnderwaterMeniscus", "UnderwaterMeniscus must be between zero and one.");
  if (!IsFiniteInRange(m_fUnderwaterCaustics, 0.0f, WaterProfileLimits::EffectStrengthMax))
    AddError("UnderwaterCaustics", "UnderwaterCaustics must be between zero and four.");
  if (!IsFiniteInRange(m_fReflectedCaustics, 0.0f, WaterProfileLimits::EffectStrengthMax))
    AddError("ReflectedCaustics", "ReflectedCaustics must be between zero and four.");
  if (!IsFiniteInRange(m_fUnderwaterLightShafts, 0.0f, WaterProfileLimits::EffectStrengthMax))
    AddError("UnderwaterLightShafts", "UnderwaterLightShafts must be between zero and four.");

  if (!IsFiniteInRange(m_fDensity, WaterProfileLimits::DensityMin, WaterProfileLimits::DensityMax))
    AddError("Density", "Density must be between 1 and 20000 kg/m^3.");
  if (!IsFiniteInRange(m_fLinearDrag, 0.0f, WaterProfileLimits::DragMax))
    AddError("LinearDrag", "LinearDrag must be between zero and 10.");
  if (!IsFiniteInRange(m_fAngularDrag, 0.0f, WaterProfileLimits::DragMax))
    AddError("AngularDrag", "AngularDrag must be between zero and 10.");

  if (!IsFiniteInRange(m_fRippleMaxAmplitude, 0.0f, 10.0f))
    AddError("RippleMaxAmplitude", "RippleMaxAmplitude must be between 0 and 10 metres.");

  // Advisory tuning lints. These never block the asset transform; they flag combinations that render
  // technically valid but visually broken or wasteful results.
  plStringBuilder sMessage;

  if (IsFiniteInRange(m_fWaveSteepness, 0.0f, 1.0f) && IsFiniteInRange(m_fWaveAmplitude, 0.0f, WaterProfileLimits::WaveAmplitudeMax) &&
      m_fWaveSteepness > 0.85f && m_fWaveAmplitude > 1.0f)
  {
    sMessage.SetFormat("Steepness {} with amplitude {} m is near the wave-breaking limit; crests may self-intersect at high wind speeds.",
      plArgF(m_fWaveSteepness, 2), plArgF(m_fWaveAmplitude, 2));
    AddIssue(plWaterProfileIssue::Severity::Warning, "WaveSteepness", sMessage);
  }

  if (m_uiGpuCascadeCount > 1 && IsFiniteInRange(m_fSmallestGpuWavelength, WaterProfileLimits::SmallestGpuWavelengthMin, WaterProfileLimits::SmallestGpuWavelengthMax) &&
      m_fSmallestGpuWavelength > m_fBaseWavelength * 0.5f)
  {
    sMessage.SetFormat("SmallestGpuWavelength {} m covers less than one octave below BaseWavelength {} m; most of the {} cascades add no visible detail.",
      plArgF(m_fSmallestGpuWavelength, 2), plArgF(m_fBaseWavelength, 1), m_uiGpuCascadeCount);
    AddIssue(plWaterProfileIssue::Severity::Warning, "SmallestGpuWavelength", sMessage);
  }

  if (m_fFoamStrength > 0.0f && m_fFoamCrestThreshold > 0.9f && m_fWaveSteepness < 0.3f)
  {
    sMessage.SetFormat("FoamCrestThreshold {} with steepness {} means crest foam will rarely appear; lower the threshold or raise steepness.",
      plArgF(m_fFoamCrestThreshold, 2), plArgF(m_fWaveSteepness, 2));
    AddIssue(plWaterProfileIssue::Severity::Warning, "FoamCrestThreshold", sMessage);
  }
}

PL_STATICLINK_FILE(WaterPlugin, WaterPlugin_Resources_Implementation_WaterProfileResource);
