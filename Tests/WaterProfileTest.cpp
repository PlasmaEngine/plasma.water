#include <GameEngineTest/GameEngineTestPCH.h>

#include <Foundation/IO/MemoryStream.h>
#include <Foundation/Reflection/ReflectionUtils.h>
#include <WaterPlugin/Resources/WaterProfileResource.h>

namespace
{
  enum WaterProfileConsumer : plUInt32
  {
    SurfaceShading = PL_BIT(0),
    GpuSimulation = PL_BIT(1),
    CpuSurfaceQuery = PL_BIT(2),
    FiniteGeometry = PL_BIT(3),
    UnderwaterComposition = PL_BIT(4),
    PhysicsQuery = PL_BIT(5),
    TextureBinding = PL_BIT(6),
  };

  struct VisiblePropertyConsumer
  {
    const char* m_szProperty;
    plUInt32 m_uiConsumers;
    const char* m_szImplementationPath;
  };

  constexpr VisiblePropertyConsumer s_VisiblePropertyConsumers[] = {
    {"ShallowColor", SurfaceShading, "plWaterRenderData -> WaterSurfaceModern"},
    {"DeepColor", SurfaceShading, "plWaterRenderData -> WaterSurfaceModern"},
    {"Roughness", SurfaceShading, "plWaterRenderData -> WaterSurfaceModern"},
    {"ReflectionStrength", SurfaceShading, "plWaterRenderData -> WaterSurfaceModern"},
    {"EnvironmentReflectionWeight", SurfaceShading, "plWaterRenderData -> WaterSurfaceModern"},
    {"MicroNormalScale", SurfaceShading, "plWaterRenderData -> WaterSurfaceModern"},
    {"MicroNormalStrength", SurfaceShading, "plWaterRenderData -> WaterSurfaceModern"},
    {"SunGlitterStrength", SurfaceShading, "plWaterRenderData -> WaterSurfaceModern"},

    {"SpectrumMode", GpuSimulation, "plWaterProfileResourceDescriptor -> plWaterGpuSimulation"},
    {"WaveSeed", CpuSurfaceQuery | GpuSimulation, "plWaterWorldModule::EvaluateWaves / plWaterGpuSimulation"},
    {"WindDirection", CpuSurfaceQuery | GpuSimulation | SurfaceShading, "EvaluateWaves / GPU spectrum / micro normals"},
    {"WindSpeed", GpuSimulation | SurfaceShading, "GPU spectrum / WaterSurfaceModern"},
    {"WaveAmplitude", CpuSurfaceQuery | GpuSimulation | SurfaceShading, "EvaluateWaves / GPU spectrum / WaterSurfaceModern"},
    {"BaseWavelength", CpuSurfaceQuery | GpuSimulation | SurfaceShading, "EvaluateWaves / GPU spectrum / WaterSurfaceModern"},
    {"WaveSteepness", CpuSurfaceQuery | GpuSimulation | SurfaceShading, "EvaluateWaves / GPU spectrum / WaterSurfaceModern"},
    {"CpuWaveCount", CpuSurfaceQuery, "plWaterWorldModule::EvaluateWaves"},
    {"GpuCascadeCount", GpuSimulation, "plWaterSurfacePass -> plWaterGpuSimulation"},
    {"SmallestGpuWavelength", GpuSimulation, "plWaterSurfacePass -> plWaterGpuSimulation"},
    {"EnableLakeSpectrum", GpuSimulation, "plWaterBodyComponent GPU-spectrum eligibility"},
    {"FlowScale", CpuSurfaceQuery | SurfaceShading, "surface query flow / render flow"},

    {"FiniteMeshCellSize", FiniteGeometry, "plWaterWorldModule::PrepareBody"},
    {"MaxRiverCrossSubdivisions", FiniteGeometry, "plWaterGeometry::BuildRiverRibbon"},
    {"ShoreWaveFadeDistance", FiniteGeometry | CpuSurfaceQuery, "finite mesh attenuation / finite-body wave query"},
    {"RiverCurvatureRefinement", FiniteGeometry, "plWaterGeometry::BuildRiverRibbon"},

    {"Absorption", SurfaceShading | UnderwaterComposition, "surface constants / plWaterFrameDataExtractor"},
    {"Scattering", SurfaceShading | UnderwaterComposition, "surface constants / plWaterFrameDataExtractor"},
    {"IndexOfRefraction", SurfaceShading, "plWaterRenderData -> WaterSurfaceModern"},
    {"RefractionStrength", SurfaceShading, "plWaterRenderData -> WaterSurfaceModern"},
    {"RefractionDepth", SurfaceShading, "plWaterRenderData -> WaterSurfaceModern"},
    {"ShoreFadeDepth", SurfaceShading, "plWaterRenderData -> WaterSurfaceModern"},
    {"RefractionEdgeRejection", SurfaceShading, "plWaterRenderData -> WaterSurfaceModern"},

    {"FoamStrength", SurfaceShading, "plWaterRenderData -> WaterSurfaceModern"},
    {"FoamCrestThreshold", SurfaceShading, "plWaterRenderData -> WaterSurfaceModern"},
    {"FoamShoreDepth", SurfaceShading, "plWaterRenderData -> WaterSurfaceModern"},
    {"FoamColor", SurfaceShading, "plWaterRenderData -> WaterSurfaceModern"},
    {"FoamRoughness", SurfaceShading, "plWaterRenderData -> WaterSurfaceModern"},
    {"FoamBreakupScale", SurfaceShading, "plWaterRenderData -> WaterSurfaceModern"},
    {"RippleMaxAmplitude", SurfaceShading, "plWaterRenderData -> WaterRenderer SurfaceParams.y -> WaterSurfaceModern interaction clamp"},

    {"UnderwaterFogDensity", UnderwaterComposition, "plWaterFrameDataExtractor -> plWaterUnderwaterPass"},
    {"UnderwaterScattering", UnderwaterComposition, "plWaterFrameDataExtractor -> plWaterUnderwaterPass"},
    {"UnderwaterHaze", UnderwaterComposition, "plWaterFrameDataExtractor -> plWaterUnderwaterPass"},
    {"UnderwaterDistortion", UnderwaterComposition, "plWaterFrameDataExtractor -> plWaterUnderwaterPass"},
    {"UnderwaterCaustics", UnderwaterComposition, "plWaterFrameDataExtractor -> plWaterUnderwaterPass"},
    {"ReflectedCaustics", SurfaceShading, "plWaterBodyComponent -> plWaterRenderData / plWaterFrameDataExtractor -> caustic composite"},
    {"UnderwaterLightShafts", UnderwaterComposition, "plWaterFrameDataExtractor -> plWaterUnderwaterPass"},
    {"UnderwaterMeniscus", UnderwaterComposition, "plWaterFrameDataExtractor -> plWaterUnderwaterPass"},

    {"Density", PhysicsQuery, "plWaterWorldModule::TrySampleSurface"},
    {"LinearDrag", PhysicsQuery, "plWaterWorldModule::TrySampleSurface"},
    {"AngularDrag", PhysicsQuery, "plWaterWorldModule::TrySampleSurface"},
    {"NormalTexture", SurfaceShading | TextureBinding, "FillCommonRenderData / FillSecondaryProfile"},
    {"FoamTexture", SurfaceShading | TextureBinding, "FillCommonRenderData / FillSecondaryProfile"},
  };

  const VisiblePropertyConsumer* FindVisiblePropertyConsumer(plStringView sProperty)
  {
    for (const VisiblePropertyConsumer& consumer : s_VisiblePropertyConsumers)
    {
      if (sProperty == consumer.m_szProperty)
        return &consumer;
    }
    return nullptr;
  }

  void FillDistinctProfile(plWaterProfileResourceDescriptor& profile)
  {
    profile.m_sSurfaceMaterial = "LegacySurfaceMaterial";
    profile.m_SpectrumMode = plWaterWaveSpectrumMode::Phillips;
    profile.m_uiWaveSeed = 17;
    profile.m_vWindDirection = plVec2(0.6f, 0.8f);
    profile.m_fWindSpeed = 7.25f;
    profile.m_fWaveAmplitude = 0.42f;
    profile.m_fBaseWavelength = 12.0f;
    profile.m_fWaveSteepness = 0.47f;
    profile.m_uiCpuWaveCount = 12;
    profile.m_uiGpuCascadeCount = 2;
    profile.m_uiGpuCascadeResolution = 512;
    profile.m_fSmallestGpuWavelength = 0.25f;
    profile.m_bEnableLakeSpectrum = true;
    profile.m_fFlowScale = 1.3f;

    profile.m_fFiniteMeshCellSize = 0.75f;
    profile.m_uiMaxRiverCrossSubdivisions = 24;
    profile.m_fShoreWaveFadeDistance = 2.5f;
    profile.m_fRiverCurvatureRefinement = 0.7f;

    profile.m_ShallowColor = plColor(0.12f, 0.28f, 0.37f, 0.43f);
    profile.m_DeepColor = plColor(0.01f, 0.04f, 0.09f, 0.87f);
    profile.m_vAbsorption = plVec3(0.31f, 0.08f, 0.03f);
    profile.m_vScattering = plVec3(0.02f, 0.05f, 0.07f);
    profile.m_fIndexOfRefraction = 1.4f;
    profile.m_fRoughness = 0.17f;
    profile.m_fReflectionStrength = 0.8f;
    profile.m_fEnvironmentReflectionWeight = 0.65f;
    profile.m_fMicroNormalScale = 1.4f;
    profile.m_fMicroNormalStrength = 0.3f;
    profile.m_fSunGlitterStrength = 0.45f;
    profile.m_fRefractionEdgeRejection = 0.35f;
    profile.m_fLegacyReflectionHistoryWeight = 0.61f;
    profile.m_fLegacyReflectionTraceDistance = 73.0f;
    profile.m_fRefractionStrength = 0.02f;
    profile.m_fRefractionDepth = 4.5f;
    profile.m_fShoreFadeDepth = 0.8f;

    profile.m_fFoamStrength = 0.7f;
    profile.m_fFoamCrestThreshold = 0.66f;
    profile.m_fFoamShoreDepth = 0.44f;
    profile.m_fFoamInteractionStrength = 0.33f;
    profile.m_FoamColor = plColor(0.81f, 0.85f, 0.9f, 0.57f);
    profile.m_fFoamRoughness = 0.8f;
    profile.m_fFoamBreakupScale = 0.22f;

    profile.m_fUnderwaterFogDensity = 0.08f;
    profile.m_fUnderwaterScattering = 0.7f;
    profile.m_fUnderwaterHaze = 0.3f;
    profile.m_fUnderwaterDistortion = 0.2f;
    profile.m_fUnderwaterCaustics = 0.3f;
    profile.m_fReflectedCaustics = 0.22f;
    profile.m_fUnderwaterLightShafts = 0.18f;
    profile.m_fUnderwaterMeniscus = 0.15f;

    profile.m_fDensity = 998.0f;
    profile.m_fLinearDrag = 0.4f;
    profile.m_fAngularDrag = 0.07f;

    profile.m_fRipplePropagationSpeed = 2.5f;
    profile.m_fRippleDamping = 0.96f;
    profile.m_fRippleMaxAmplitude = 0.16f;
    profile.m_sNormalTexture = "DistinctNormalTexture";
    profile.m_sFoamTexture = "DistinctFoamTexture";
  }

  void WriteProfileVersion(plStreamWriter& stream, const plWaterProfileResourceDescriptor& profile, plUInt8 uiVersion, bool bZeroV4Extension = false)
  {
    stream << uiVersion;

    stream << profile.m_uiWaveSeed;
    stream << profile.m_vWindDirection;
    stream << profile.m_fWindSpeed;
    stream << profile.m_fWaveAmplitude;
    stream << profile.m_fBaseWavelength;
    stream << profile.m_fWaveSteepness;
    stream << profile.m_uiCpuWaveCount;

    stream << profile.m_ShallowColor;
    stream << profile.m_DeepColor;
    stream << profile.m_vAbsorption;
    stream << profile.m_vScattering;
    stream << profile.m_fIndexOfRefraction;
    stream << profile.m_fRoughness;
    stream << profile.m_fFoamStrength;
    stream << profile.m_fUnderwaterFogDensity;

    stream << profile.m_fDensity;
    stream << profile.m_fLinearDrag;
    stream << profile.m_fAngularDrag;

    stream << profile.m_fRipplePropagationSpeed;
    stream << profile.m_fRippleDamping;
    stream << profile.m_fRippleMaxAmplitude;
    stream << profile.m_sNormalTexture;
    stream << profile.m_sFoamTexture;

    if (uiVersion >= 2)
    {
      stream << profile.m_sSurfaceMaterial;
      stream << profile.m_SpectrumMode;
      stream << profile.m_uiGpuCascadeCount;
      stream << profile.m_uiGpuCascadeResolution;
      stream << profile.m_fSmallestGpuWavelength;
      stream << profile.m_bEnableLakeSpectrum;
      stream << profile.m_fFlowScale;
      stream << profile.m_fReflectionStrength;
      stream << profile.m_fLegacyReflectionHistoryWeight;
      stream << profile.m_fLegacyReflectionTraceDistance;
      stream << profile.m_fRefractionStrength;
      stream << profile.m_fRefractionDepth;
      stream << profile.m_fShoreFadeDepth;
      stream << profile.m_fFoamCrestThreshold;
      stream << profile.m_fFoamShoreDepth;
      stream << profile.m_fFoamInteractionStrength;
      stream << profile.m_fUnderwaterScattering;
      stream << profile.m_fUnderwaterHaze;
      stream << profile.m_fUnderwaterDistortion;
      stream << profile.m_fUnderwaterCaustics;
      stream << profile.m_fUnderwaterLightShafts;
      stream << profile.m_fUnderwaterMeniscus;
    }

    if (uiVersion >= 3)
    {
      stream << profile.m_fFiniteMeshCellSize;
      stream << profile.m_uiMaxRiverCrossSubdivisions;
      stream << profile.m_fShoreWaveFadeDistance;
      stream << profile.m_fRiverCurvatureRefinement;
    }

    if (uiVersion >= 4)
    {
      if (bZeroV4Extension)
      {
        stream << 0.0f;
        stream << 0.0f;
        stream << 0.0f;
        stream << 0.0f;
        stream << 0.0f;
        stream << plColor::Black;
        stream << 0.0f;
        stream << 0.0f;
      }
      else
      {
        stream << profile.m_fEnvironmentReflectionWeight;
        stream << profile.m_fMicroNormalScale;
        stream << profile.m_fMicroNormalStrength;
        stream << profile.m_fSunGlitterStrength;
        stream << profile.m_fRefractionEdgeRejection;
        stream << profile.m_FoamColor;
        stream << profile.m_fFoamRoughness;
        stream << profile.m_fFoamBreakupScale;
      }
    }

    if (uiVersion >= 5)
    {
      stream << profile.m_fReflectedCaustics;
    }
  }

  plWaterProfileResourceDescriptor LoadProfileVersion(const plWaterProfileResourceDescriptor& source, plUInt8 uiVersion, bool bZeroV4Extension = false)
  {
    plDefaultMemoryStreamStorage storage;
    plMemoryStreamWriter writer(&storage);
    WriteProfileVersion(writer, source, uiVersion, bZeroV4Extension);

    plMemoryStreamReader reader(&storage);
    plWaterProfileResourceDescriptor result;
    result.Load(reader);
    return result;
  }

  void TestProfilesEqual(const plWaterProfileResourceDescriptor& expected, const plWaterProfileResourceDescriptor& actual)
  {
    PL_TEST_BOOL(plReflectionUtils::IsEqual(&expected, &actual, plGetStaticRTTI<plWaterProfileResourceDescriptor>()));
    PL_TEST_FLOAT(expected.m_fLegacyReflectionHistoryWeight, actual.m_fLegacyReflectionHistoryWeight, 0.0f);
    PL_TEST_FLOAT(expected.m_fLegacyReflectionTraceDistance, actual.m_fLegacyReflectionTraceDistance, 0.0f);
  }

  bool IsExpectedHiddenProperty(plStringView sName)
  {
    // RippleMaxAmplitude is exposed and consumed (per-body surface clamp); propagation/damping remain
    // reserved for the paged atlas allocator and are governed by the Water.Interaction.* cvars.
    return sName == "SurfaceMaterial" || sName == "GpuCascadeResolution" || sName == "FoamInteractionStrength" ||
           sName == "RipplePropagationSpeed" || sName == "RippleDamping";
  }

  bool IsVisiblePropertyWithoutClamp(plStringView sName)
  {
    return sName == "ShallowColor" || sName == "DeepColor" || sName == "SpectrumMode" || sName == "WaveSeed" || sName == "WindDirection" ||
           sName == "EnableLakeSpectrum" || sName == "FoamColor" || sName == "NormalTexture" || sName == "FoamTexture";
  }

  bool IsPropertyWithPhysicalUnit(plStringView sName)
  {
    return sName == "WindSpeed" || sName == "WaveAmplitude" || sName == "BaseWavelength" || sName == "SmallestGpuWavelength" ||
           sName == "FiniteMeshCellSize" || sName == "ShoreWaveFadeDistance" || sName == "Absorption" || sName == "Scattering" ||
           sName == "RefractionDepth" || sName == "ShoreFadeDepth" || sName == "FoamShoreDepth" || sName == "FoamBreakupScale" ||
           sName == "UnderwaterFogDensity" || sName == "Density";
  }
} // namespace

PL_CREATE_SIMPLE_TEST(Water, Profile)
{
  PL_TEST_BLOCK(plTestBlock::Enabled, "Every visible property has an end-to-end consumer contract")
  {
    constexpr plUInt32 uiAllConsumers = SurfaceShading | GpuSimulation | CpuSurfaceQuery | FiniteGeometry |
                                        UnderwaterComposition | PhysicsQuery | TextureBinding;
    const plRTTI* pRtti = plGetStaticRTTI<plWaterProfileResourceDescriptor>();
    plUInt32 uiVisiblePropertyCount = 0;

    for (const plAbstractProperty* pProperty : pRtti->GetProperties())
    {
      if (pProperty->GetAttributeByType<plHiddenAttribute>() != nullptr)
        continue;

      ++uiVisiblePropertyCount;
      const VisiblePropertyConsumer* pConsumer = FindVisiblePropertyConsumer(pProperty->GetPropertyName());
      PL_TEST_BOOL(pConsumer != nullptr);
      if (pConsumer != nullptr)
      {
        PL_TEST_BOOL(pConsumer->m_uiConsumers != 0);
        PL_TEST_BOOL((pConsumer->m_uiConsumers & ~uiAllConsumers) == 0);
        PL_TEST_BOOL(!plStringUtils::IsNullOrEmpty(pConsumer->m_szImplementationPath));
      }
    }

    PL_TEST_INT(uiVisiblePropertyCount, PL_ARRAY_SIZE(s_VisiblePropertyConsumers));
    for (plUInt32 i = 0; i < PL_ARRAY_SIZE(s_VisiblePropertyConsumers); ++i)
    {
      const plAbstractProperty* pProperty = pRtti->FindPropertyByName(s_VisiblePropertyConsumers[i].m_szProperty);
      PL_TEST_BOOL(pProperty != nullptr);
      if (pProperty != nullptr)
        PL_TEST_BOOL(pProperty->GetAttributeByType<plHiddenAttribute>() == nullptr);

      for (plUInt32 j = i + 1; j < PL_ARRAY_SIZE(s_VisiblePropertyConsumers); ++j)
        PL_TEST_BOOL(!plStringUtils::IsEqual(s_VisiblePropertyConsumers[i].m_szProperty, s_VisiblePropertyConsumers[j].m_szProperty));
    }
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Reflected authoring contract")
  {
    const plRTTI* pRtti = plGetStaticRTTI<plWaterProfileResourceDescriptor>();
    PL_TEST_INT(pRtti->GetTypeVersion(), 6);

    const plWaterProfileResourceDescriptor defaults;
    PL_TEST_STRING(defaults.m_sSurfaceMaterial, "");
    PL_TEST_INT(defaults.m_SpectrumMode.GetValue(), plWaterWaveSpectrumMode::Analytic);
    PL_TEST_FLOAT(defaults.m_fReflectedCaustics, 0.15f, 0.0f);
    PL_TEST_STRING(defaults.m_sNormalTexture, "{ 0753ed83-0a62-42dc-a964-c0458ba1d74e }");
    PL_TEST_STRING(defaults.m_sFoamTexture, "{ 7cdd0fa2-f12a-437e-9f40-d14fc11f7ec0 }");
    PL_TEST_BOOL(defaults.Validate().Succeeded());

    plUInt32 uiHiddenPropertyCount = 0;
    for (const plAbstractProperty* pProperty : pRtti->GetProperties())
    {
      const bool bExpectedHidden = IsExpectedHiddenProperty(pProperty->GetPropertyName());
      const bool bIsHidden = pProperty->GetAttributeByType<plHiddenAttribute>() != nullptr;
      PL_TEST_BOOL(bIsHidden == bExpectedHidden);

      if (bIsHidden)
      {
        ++uiHiddenPropertyCount;
        continue;
      }

      PL_TEST_BOOL(pProperty->GetCategory() == plPropertyCategory::Member);
      PL_TEST_BOOL(pProperty->GetAttributeByType<plGroupAttribute>() != nullptr);
      PL_TEST_BOOL(pProperty->GetAttributeByType<plDefaultValueAttribute>() != nullptr);
      if (!IsVisiblePropertyWithoutClamp(pProperty->GetPropertyName()))
        PL_TEST_BOOL(pProperty->GetAttributeByType<plClampValueAttribute>() != nullptr);
      if (IsPropertyWithPhysicalUnit(pProperty->GetPropertyName()))
        PL_TEST_BOOL(pProperty->GetAttributeByType<plSuffixAttribute>() != nullptr);
      if (pProperty->GetCategory() == plPropertyCategory::Member)
      {
        const auto* pMember = static_cast<const plAbstractMemberProperty*>(pProperty);
        const plVariant nativeDefault = plReflectionUtils::GetMemberPropertyValue(pMember, &defaults);
        const plVariant reflectedDefault = plReflectionUtils::GetDefaultValue(pProperty);
        // A mismatch here after changing a default can also mean a STALE WaterPlugin build: the descriptor's
        // implicit ctor is dllexported, and incremental builds have been seen keeping an old ctor COMDAT while
        // recompiling the reflection block. Clean Intermediate/.../WaterPluginModule if values look impossible.
        PL_TEST_BOOL_MSG(nativeDefault == reflectedDefault, "Property '%s': native %f vs reflected %f",
          pProperty->GetPropertyName(),
          nativeDefault.IsNumber() ? nativeDefault.ConvertTo<double>() : -999.0,
          reflectedDefault.IsNumber() ? reflectedDefault.ConvertTo<double>() : -999.0);
      }
    }
    PL_TEST_INT(uiHiddenPropertyCount, 5);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Version 5 round-trip preserves visible and hidden slots")
  {
    plWaterProfileResourceDescriptor source;
    FillDistinctProfile(source);

    plDefaultMemoryStreamStorage storage;
    plMemoryStreamWriter writer(&storage);
    source.Save(writer);

    plMemoryStreamReader reader(&storage);
    plWaterProfileResourceDescriptor roundTripped;
    roundTripped.Load(reader);
    TestProfilesEqual(source, roundTripped);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Version 1 migration")
  {
    plWaterProfileResourceDescriptor source;
    FillDistinctProfile(source);
    const plWaterProfileResourceDescriptor defaults;
    const plWaterProfileResourceDescriptor loaded = LoadProfileVersion(source, 1);

    PL_TEST_INT(loaded.m_uiWaveSeed, source.m_uiWaveSeed);
    PL_TEST_STRING(loaded.m_sNormalTexture, source.m_sNormalTexture);
    PL_TEST_STRING(loaded.m_sFoamTexture, source.m_sFoamTexture);
    PL_TEST_FLOAT(loaded.m_fDensity, source.m_fDensity, 0.0f);
    PL_TEST_STRING(loaded.m_sSurfaceMaterial, defaults.m_sSurfaceMaterial);
    PL_TEST_FLOAT(loaded.m_fFiniteMeshCellSize, defaults.m_fFiniteMeshCellSize, 0.0f);
    PL_TEST_FLOAT(loaded.m_fEnvironmentReflectionWeight, defaults.m_fEnvironmentReflectionWeight, 0.0f);
    PL_TEST_FLOAT(loaded.m_fReflectedCaustics, defaults.m_fReflectedCaustics, 0.0f);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Version 2 migration")
  {
    plWaterProfileResourceDescriptor source;
    FillDistinctProfile(source);
    const plWaterProfileResourceDescriptor defaults;
    const plWaterProfileResourceDescriptor loaded = LoadProfileVersion(source, 2);

    PL_TEST_STRING(loaded.m_sSurfaceMaterial, source.m_sSurfaceMaterial);
    PL_TEST_INT(loaded.m_uiGpuCascadeResolution, source.m_uiGpuCascadeResolution);
    PL_TEST_FLOAT(loaded.m_fShoreFadeDepth, source.m_fShoreFadeDepth, 0.0f);
    PL_TEST_FLOAT(loaded.m_fFiniteMeshCellSize, defaults.m_fFiniteMeshCellSize, 0.0f);
    PL_TEST_FLOAT(loaded.m_fEnvironmentReflectionWeight, defaults.m_fEnvironmentReflectionWeight, 0.0f);
    PL_TEST_FLOAT(loaded.m_fReflectedCaustics, defaults.m_fReflectedCaustics, 0.0f);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Version 3 migration")
  {
    plWaterProfileResourceDescriptor source;
    FillDistinctProfile(source);
    const plWaterProfileResourceDescriptor defaults;
    const plWaterProfileResourceDescriptor loaded = LoadProfileVersion(source, 3);

    PL_TEST_FLOAT(loaded.m_fFiniteMeshCellSize, source.m_fFiniteMeshCellSize, 0.0f);
    PL_TEST_INT(loaded.m_uiMaxRiverCrossSubdivisions, source.m_uiMaxRiverCrossSubdivisions);
    PL_TEST_FLOAT(loaded.m_fRiverCurvatureRefinement, source.m_fRiverCurvatureRefinement, 0.0f);
    PL_TEST_FLOAT(loaded.m_fEnvironmentReflectionWeight, defaults.m_fEnvironmentReflectionWeight, 0.0f);
    PL_TEST_BOOL(loaded.m_FoamColor == defaults.m_FoamColor);
    PL_TEST_FLOAT(loaded.m_fReflectedCaustics, defaults.m_fReflectedCaustics, 0.0f);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Early version 4 zero-extension migration")
  {
    plWaterProfileResourceDescriptor source;
    FillDistinctProfile(source);
    const plWaterProfileResourceDescriptor defaults;
    const plWaterProfileResourceDescriptor loaded = LoadProfileVersion(source, 4, true);

    PL_TEST_FLOAT(loaded.m_fReflectionStrength, source.m_fReflectionStrength, 0.0f);
    PL_TEST_FLOAT(loaded.m_fEnvironmentReflectionWeight, defaults.m_fEnvironmentReflectionWeight, 0.0f);
    PL_TEST_FLOAT(loaded.m_fMicroNormalScale, defaults.m_fMicroNormalScale, 0.0f);
    PL_TEST_FLOAT(loaded.m_fSunGlitterStrength, defaults.m_fSunGlitterStrength, 0.0f);
    PL_TEST_BOOL(loaded.m_FoamColor == defaults.m_FoamColor);
    PL_TEST_FLOAT(loaded.m_fFoamRoughness, defaults.m_fFoamRoughness, 0.0f);
    PL_TEST_FLOAT(loaded.m_fFoamBreakupScale, defaults.m_fFoamBreakupScale, 0.0f);
    PL_TEST_FLOAT(loaded.m_fReflectedCaustics, defaults.m_fReflectedCaustics, 0.0f);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Version 4 migration defaults reflected caustics")
  {
    plWaterProfileResourceDescriptor source;
    FillDistinctProfile(source);
    const plWaterProfileResourceDescriptor defaults;
    const plWaterProfileResourceDescriptor loaded = LoadProfileVersion(source, 4);

    PL_TEST_FLOAT(loaded.m_fUnderwaterCaustics, source.m_fUnderwaterCaustics, 0.0f);
    PL_TEST_FLOAT(loaded.m_fReflectedCaustics, defaults.m_fReflectedCaustics, 0.0f);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Runtime sanitization")
  {
    plWaterProfileResourceDescriptor source;
    source.m_SpectrumMode = static_cast<plWaterWaveSpectrumMode::Enum>(255);
    source.m_uiCpuWaveCount = 0;
    source.m_fBaseWavelength = -1.0f;
    source.m_fDensity = -1.0f;
    source.m_uiGpuCascadeCount = 255;
    source.m_uiGpuCascadeResolution = 1;
    source.m_fFiniteMeshCellSize = 0.01f;
    source.m_uiMaxRiverCrossSubdivisions = 1;
    source.m_fShoreWaveFadeDistance = 0.0f;
    source.m_fRiverCurvatureRefinement = 2.0f;
    source.m_fEnvironmentReflectionWeight = 5.0f;
    source.m_fMicroNormalScale = 0.0f;
    source.m_fMicroNormalStrength = 3.0f;
    source.m_fSunGlitterStrength = 5.0f;
    source.m_fRefractionEdgeRejection = 2.0f;
    source.m_fFoamRoughness = 0.0f;
    source.m_fFoamBreakupScale = 0.0f;
    source.m_fReflectedCaustics = 5.0f;

    const plWaterProfileResourceDescriptor loaded = LoadProfileVersion(source, 5);
    PL_TEST_INT(loaded.m_SpectrumMode.GetValue(), plWaterWaveSpectrumMode::Analytic);
    PL_TEST_INT(loaded.m_uiCpuWaveCount, 1);
    PL_TEST_FLOAT(loaded.m_fBaseWavelength, 0.1f, 0.0f);
    PL_TEST_FLOAT(loaded.m_fDensity, 1.0f, 0.0f);
    PL_TEST_INT(loaded.m_uiGpuCascadeCount, 4);
    PL_TEST_INT(loaded.m_uiGpuCascadeResolution, 1);
    PL_TEST_FLOAT(loaded.m_fFiniteMeshCellSize, 0.1f, 0.0f);
    PL_TEST_INT(loaded.m_uiMaxRiverCrossSubdivisions, 2);
    PL_TEST_FLOAT(loaded.m_fShoreWaveFadeDistance, 0.01f, 0.0f);
    PL_TEST_FLOAT(loaded.m_fRiverCurvatureRefinement, 1.0f, 0.0f);
    PL_TEST_FLOAT(loaded.m_fEnvironmentReflectionWeight, 4.0f, 0.0f);
    PL_TEST_FLOAT(loaded.m_fMicroNormalScale, 0.05f, 0.0f);
    PL_TEST_FLOAT(loaded.m_fMicroNormalStrength, 2.0f, 0.0f);
    PL_TEST_FLOAT(loaded.m_fSunGlitterStrength, 4.0f, 0.0f);
    PL_TEST_FLOAT(loaded.m_fRefractionEdgeRejection, 1.0f, 0.0f);
    PL_TEST_FLOAT(loaded.m_fFoamRoughness, 0.02f, 0.0f);
    PL_TEST_FLOAT(loaded.m_fFoamBreakupScale, 0.001f, 0.0f);
    PL_TEST_FLOAT(loaded.m_fReflectedCaustics, 4.0f, 0.0f);
    PL_TEST_BOOL(loaded.Validate().Succeeded());
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Validation and non-finite sanitization")
  {
    const plWaterProfileResourceDescriptor defaults;
    plWaterProfileResourceDescriptor profile;
    profile.m_vWindDirection.SetZero();
    profile.m_fWindSpeed = plMath::NaN<float>();
    profile.m_fSmallestGpuWavelength = plMath::Infinity<float>();
    profile.m_ShallowColor = plColor::MakeNaN();
    profile.m_vAbsorption = plVec3::MakeNaN();
    profile.m_fShoreFadeDepth = -1.0f;
    profile.m_fReflectedCaustics = plMath::NaN<float>();
    profile.m_fDensity = plMath::NaN<float>();

    PL_TEST_BOOL(profile.Validate().Failed());
    profile.Sanitize();
    PL_TEST_BOOL(profile.Validate().Succeeded());
    PL_TEST_BOOL(profile.m_vWindDirection == defaults.m_vWindDirection);
    PL_TEST_FLOAT(profile.m_fWindSpeed, defaults.m_fWindSpeed, 0.0f);
    PL_TEST_FLOAT(profile.m_fSmallestGpuWavelength, defaults.m_fSmallestGpuWavelength, 0.0f);
    PL_TEST_BOOL(profile.m_ShallowColor == defaults.m_ShallowColor);
    PL_TEST_BOOL(profile.m_vAbsorption == defaults.m_vAbsorption);
    PL_TEST_FLOAT(profile.m_fShoreFadeDepth, 0.01f, 0.0f);
    PL_TEST_FLOAT(profile.m_fReflectedCaustics, defaults.m_fReflectedCaustics, 0.0f);
    PL_TEST_FLOAT(profile.m_fDensity, defaults.m_fDensity, 0.0f);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Deprecated slots are preserved but not validated")
  {
    plWaterProfileResourceDescriptor profile;
    profile.m_sSurfaceMaterial = "LegacyOnly";
    profile.m_SpectrumMode = plWaterWaveSpectrumMode::JONSWAP;
    profile.m_uiGpuCascadeResolution = 1;
    profile.m_fRipplePropagationSpeed = -100.0f;
    profile.m_fRippleDamping = -1.0f;
    PL_TEST_BOOL(profile.Validate().Succeeded());

    // RippleMaxAmplitude is a live, visible property and IS validated.
    profile.m_fRippleMaxAmplitude = -10.0f;
    PL_TEST_BOOL(profile.Validate().Failed());
  }
}
