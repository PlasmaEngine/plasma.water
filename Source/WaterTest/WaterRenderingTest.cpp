#include <WaterTest/WaterTestPCH.h>

#include <Core/ResourceManager/ResourceManager.h>
#include <Foundation/Configuration/Plugin.h>
#include <Foundation/IO/FileSystem/FileSystem.h>
#include <Foundation/Reflection/ReflectionUtils.h>
#include <Foundation/System/EnvironmentVariableUtils.h>
#include <WaterTest/WaterRenderingTest.h>
#include <GameEngine/Interfaces/WaterWorldModule.h>
#include <RendererCore/Components/SplineComponent.h>
#include <RendererCore/Lights/ClusteredDataExtractor.h>
#include <RendererCore/Lights/DirectionalLightComponent.h>
#include <RendererCore/Lights/PointLightComponent.h>
#include <RendererCore/Meshes/MeshResource.h>
#include <RendererCore/Pipeline/Extractor.h>
#include <RendererCore/Pipeline/Passes/CopyTexturePass.h>
#include <RendererCore/Pipeline/Passes/OpaqueForwardRenderPass.h>
#include <RendererCore/Pipeline/Passes/SourcePass.h>
#include <RendererCore/Pipeline/Passes/TargetPass.h>
#include <RendererCore/Pipeline/Passes/TonemapPass.h>
#include <RendererCore/Pipeline/RenderPipeline.h>
#include <RendererCore/Pipeline/View.h>
#include <RendererCore/RenderWorld/RenderWorld.h>
#include <Texture/Image/ImageConversion.h>
#include <WaterPlugin/Components/WaterBodyComponent.h>
#include <WaterPlugin/Components/WaterInteractorComponent.h>
#include <WaterPlugin/Components/WaterJunctionComponent.h>
#include <WaterPlugin/Rendering/WaterFrameDataExtractor.h>
#include <WaterPlugin/Rendering/WaterSurfacePass.h>
#include <WaterPlugin/Rendering/WaterUnderwaterPass.h>
#include <WaterPlugin/Resources/WaterProfileResource.h>
#include <WaterPlugin/World/WaterWorldModule.h>

namespace
{
  void SetPassProperty(plReflectedClass* pObject, const char* szProperty, const plVariant& value)
  {
    const plAbstractProperty* pProperty = pObject->GetDynamicRTTI()->FindPropertyByName(szProperty);
    PL_ASSERT_DEV(pProperty != nullptr && pProperty->GetCategory() == plPropertyCategory::Member, "Property '{}' is not a member property", szProperty);
    plReflectionUtils::SetMemberPropertyValue(static_cast<const plAbstractMemberProperty*>(pProperty), pObject, value);
  }

  plMeshResourceHandle CreateCausticReceiverMesh(bool bFaceDown)
  {
    plMeshResourceDescriptor descriptor;
    plMeshBufferResourceDescriptor& meshBuffer = descriptor.MeshBufferDesc();
    meshBuffer.AddCommonStreams();
    meshBuffer.AllocateStreams(4, plGALPrimitiveTopology::Triangles, 2);

    const plVec3 positions[4] = {
      plVec3(-16.0f, -16.0f, 0.0f),
      plVec3(16.0f, -16.0f, 0.0f),
      plVec3(16.0f, 16.0f, 0.0f),
      plVec3(-16.0f, 16.0f, 0.0f),
    };
    const plVec2 texCoords[4] = {
      plVec2(0.0f, 0.0f),
      plVec2(1.0f, 0.0f),
      plVec2(1.0f, 1.0f),
      plVec2(0.0f, 1.0f),
    };
    const plVec3 normal = bFaceDown ? -plVec3::MakeAxisZ() : plVec3::MakeAxisZ();
    for (plUInt32 i = 0; i < 4; ++i)
    {
      meshBuffer.SetPosition(i, positions[i]);
      meshBuffer.SetNormal(i, normal);
      meshBuffer.SetTangent(i, plVec4(1.0f, 0.0f, 0.0f, 1.0f));
      meshBuffer.SetTexCoord0(i, texCoords[i]);
    }

    if (bFaceDown)
    {
      meshBuffer.SetTriangleIndices(0, 0, 2, 1);
      meshBuffer.SetTriangleIndices(1, 0, 3, 2);
    }
    else
    {
      meshBuffer.SetTriangleIndices(0, 0, 1, 2);
      meshBuffer.SetTriangleIndices(1, 0, 2, 3);
    }

    descriptor.AddSubMesh(2, 0, 0);
    descriptor.SetMaterial(0, "{ 7c71fc2d-2a1a-4938-9305-f3a0f26d73cc }"); // LitStandard
    descriptor.ComputeBounds();
    return plResourceManager::CreateResource<plMeshResource>(
      bFaceDown ? "GameEngineTest/Water/CausticReceiverDown" : "GameEngineTest/Water/CausticReceiverUp",
      std::move(descriptor), "Deterministic projected-caustic receiver");
  }

  plRenderPipelineResourceHandle CreateWaterTestPipeline()
  {
    plUniquePtr<plRenderPipeline> pPipeline = PL_DEFAULT_NEW(plRenderPipeline);

    plSourcePass* pColorSource = nullptr;
    {
      plUniquePtr<plSourcePass> pPass = PL_DEFAULT_NEW(plSourcePass, "WaterTestColorSource");
      pColorSource = pPass.Borrow();
      SetPassProperty(pColorSource, "Format", static_cast<plInt64>(plSourceFormat::Color4Channel16BitFloat));
      SetPassProperty(pColorSource, "Clear", true);
      SetPassProperty(pColorSource, "ClearColor", plColor::Black);
      pPipeline->AddPass(std::move(pPass));
    }

    plSourcePass* pDepthStencilSource = nullptr;
    {
      plUniquePtr<plSourcePass> pPass = PL_DEFAULT_NEW(plSourcePass, "WaterTestDepthStencilSource");
      pDepthStencilSource = pPass.Borrow();
      SetPassProperty(pDepthStencilSource, "Format", static_cast<plInt64>(plSourceFormat::Depth24BitStencil8Bit));
      SetPassProperty(pDepthStencilSource, "Clear", true);
      pPipeline->AddPass(std::move(pPass));
    }

    plOpaqueForwardRenderPass* pOpaque = nullptr;
    {
      plUniquePtr<plOpaqueForwardRenderPass> pPass = PL_DEFAULT_NEW(plOpaqueForwardRenderPass, "WaterTestOpaqueReceiver");
      pOpaque = pPass.Borrow();
      pPipeline->AddPass(std::move(pPass));
    }

    // Water reads resolved depth while preserving the opaque depth-stencil attachment for early-Z. A real copy is
    // necessary for the projected-caustic fixtures: unlike the original water-only tests, their receiver depth is
    // populated by the opaque pass and must reach both the caustic trace and the water surface.
    plCopyTexturePass* pResolvedDepthCopy = nullptr;
    {
      plUniquePtr<plCopyTexturePass> pPass = PL_DEFAULT_NEW(plCopyTexturePass);
      pResolvedDepthCopy = pPass.Borrow();
      pPipeline->AddPass(std::move(pPass));
    }

    plWaterSurfacePass* pWaterSurface = nullptr;
    {
      plUniquePtr<plWaterSurfacePass> pPass = PL_DEFAULT_NEW(plWaterSurfacePass);
      pWaterSurface = pPass.Borrow();
      // High, not Low, for editor parity: Low disables the GPU spectrum entirely and runs a single interaction
      // substep, but the production failures lived exactly in that extra machinery - the spectrum pass and the
      // interaction steps sharing one frame (shared constant storage degenerates to last-write-wins across
      // passes) and multiple substeps per frame (events must apply exactly once). Test profiles keep
      // GpuCascadeCount 0, so the spectrum contributes nothing visually and captures stay deterministic.
      SetPassProperty(pWaterSurface, "Quality", static_cast<plInt64>(plWaterQualityTier::High));
      pPipeline->AddPass(std::move(pPass));
    }

    plWaterUnderwaterPass* pUnderwater = nullptr;
    {
      plUniquePtr<plWaterUnderwaterPass> pPass = PL_DEFAULT_NEW(plWaterUnderwaterPass);
      pUnderwater = pPass.Borrow();
      SetPassProperty(pUnderwater, "Quality", static_cast<plInt64>(plWaterQualityTier::High));
      pPipeline->AddPass(std::move(pPass));
    }

    plTonemapPass* pTonemap = nullptr;
    {
      plUniquePtr<plTonemapPass> pPass = PL_DEFAULT_NEW(plTonemapPass);
      pTonemap = pPass.Borrow();
      pPipeline->AddPass(std::move(pPass));
    }

    plTargetPass* pTarget = nullptr;
    {
      plUniquePtr<plTargetPass> pPass = PL_DEFAULT_NEW(plTargetPass, "WaterTestTarget");
      pTarget = pPass.Borrow();
      pPipeline->AddPass(std::move(pPass));
    }

    pPipeline->AddExtractor(PL_DEFAULT_NEW(plVisibleObjectsExtractor));
    pPipeline->AddExtractor(PL_DEFAULT_NEW(plClusteredDataExtractor));
    pPipeline->AddExtractor(PL_DEFAULT_NEW(plWaterFrameDataExtractor));

    const bool bConnected =
      pPipeline->Connect(pColorSource, "Output", pOpaque, "Color") &&
      pPipeline->Connect(pDepthStencilSource, "Output", pOpaque, "DepthStencil") &&
      pPipeline->Connect(pOpaque, "Color", pWaterSurface, "Color") &&
      pPipeline->Connect(pOpaque, "DepthStencil", pResolvedDepthCopy, "Input") &&
      pPipeline->Connect(pOpaque, "DepthStencil", pWaterSurface, "DepthStencil") &&
      pPipeline->Connect(pResolvedDepthCopy, "Output", pWaterSurface, "ResolvedDepth") &&
      pPipeline->Connect(pWaterSurface, "Color", pUnderwater, "Color") &&
      pPipeline->Connect(pWaterSurface, "DepthStencil", pUnderwater, "Depth") &&
      pPipeline->Connect(pWaterSurface, "WaterExitDepth", pUnderwater, "WaterExitDepth") &&
      pPipeline->Connect(pUnderwater, "Output", pTonemap, "Color") &&
      pPipeline->Connect(pTonemap, "Output", pTarget, "Color0");

    if (!bConnected)
    {
      plLog::Error("Failed to connect the deterministic water rendering test pipeline");
      return {};
    }

    plRenderPipelineResourceDescriptor descriptor;
    descriptor.CreateFromRenderPipeline(pPipeline.Borrow());
    return plResourceManager::CreateResource<plRenderPipelineResource>(
      "GameEngineTest/Water/RuntimePipeline", std::move(descriptor), "Deterministic water rendering test pipeline");
  }
} // namespace

static plGameEngineTestWaterRendering s_GameEngineTestWaterRendering;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plWaterRenderingTestGameState, 1, plRTTIDefaultAllocator<plWaterRenderingTestGameState>)
PL_END_DYNAMIC_REFLECTED_TYPE;

void plWaterRenderingTestGameState::SetRenderPipeline(const plRenderPipelineResourceHandle& hPipeline)
{
  plView* pView = nullptr;
  if (plRenderWorld::TryGetView(m_hMainView, pView))
  {
    pView->SetRenderPipelineResource(hPipeline);
  }
}

bool plWaterRenderingTestGameState::SetViewRenderMode(plViewRenderMode::Enum mode)
{
  plView* pView = nullptr;
  if (!plRenderWorld::TryGetView(m_hMainView, pView))
    return false;

  pView->SetViewRenderMode(mode);
  return true;
}

plWaterTestApplication::plWaterTestApplication(const char* szProjectDirName)
  : plGameEngineTestApplication(szProjectDirName)
{
}

plWaterRenderingTestApplication::plWaterRenderingTestApplication()
  : plWaterTestApplication("Water")
{
}

static plString s_sSdkBinaryDirectory;
static plString s_sPackagePluginDirectory;
static plString s_sPackageAssetsDirectory;

void plWaterTestApplication::SetPackageDirectories(plStringView sSdkBinaryDirectory, plStringView sPluginDirectory, plStringView sAssetsDirectory)
{
  s_sSdkBinaryDirectory = sSdkBinaryDirectory;
  s_sPackagePluginDirectory = sPluginDirectory;
  s_sPackageAssetsDirectory = sAssetsDirectory;
}

void plWaterTestApplication::Init_AddPluginSearchRoots()
{
  plGameEngineTestApplication::Init_AddPluginSearchRoots();

  if (!s_sSdkBinaryDirectory.IsEmpty())
  {
    plPlugin::AddPluginSearchRoot(s_sSdkBinaryDirectory);
  }
  if (!s_sPackagePluginDirectory.IsEmpty())
  {
    plPlugin::AddPluginSearchRoot(s_sPackagePluginDirectory);
  }
}

void plWaterTestApplication::Init_FileSystem_ConfigureDataDirs()
{
  plGameEngineTestApplication::Init_FileSystem_ConfigureDataDirs();

  if (!s_sPackageAssetsDirectory.IsEmpty())
  {
    plFileSystem::AddDataDirectory(s_sPackageAssetsDirectory, "WaterTest", "plasma.water", plDataDirUsage::ReadOnly).AssertSuccess();
  }
}

plUniquePtr<plGameStateBase> plWaterRenderingTestApplication::CreateGameState()
{
  return PL_DEFAULT_NEW(plWaterRenderingTestGameState);
}

void plWaterRenderingTestApplication::BeforeHighLevelSystemsShutdown()
{
  m_hWaterObject.Invalidate();
  m_hRiverObject.Invalidate();
  m_hJunctionObject.Invalidate();
  m_hCausticsDisabledProfile.Invalidate();
  m_hCausticsEnabledProfile.Invalidate();
  m_hRenderPipeline.Invalidate();
  plGameEngineTestApplication::BeforeHighLevelSystemsShutdown();
}

plResult plWaterRenderingTestApplication::SetupScene(SceneVariant variant)
{
  const bool bLakeJunction = variant == SceneVariant::LakeJunction || variant == SceneVariant::FiniteDebugModes;
  const bool bSurfaceDebugModes = variant == SceneVariant::OceanDebugModes || variant == SceneVariant::FiniteDebugModes;
  const bool bReflectedCaustics = variant == SceneVariant::ReflectedCaustics ||
                                  variant == SceneVariant::OceanReflectedCausticsSuppressed;
  const bool bProjectedCaustics = variant == SceneVariant::TransmittedCaustics || bReflectedCaustics;

  if (!m_hRenderPipeline.IsValid())
  {
    m_hRenderPipeline = CreateWaterTestPipeline();
  }
  if (!m_hRenderPipeline.IsValid())
    return PL_FAILURE;

  plWaterProfileResourceDescriptor profile;
  profile.m_uiWaveSeed = 0x57415445u;
  profile.m_uiGpuCascadeCount = 0; // Keep reference captures independent of compute scheduling and GPU FFT precision.
  profile.m_uiCpuWaveCount = 6;
  profile.m_fWaveAmplitude = 0.22f;
  profile.m_fBaseWavelength = 16.0f;
  profile.m_fWaveSteepness = 0.25f;
  profile.m_ShallowColor = plColor(0.075f, 0.31f, 0.36f, 0.72f);
  profile.m_DeepColor = plColor(0.008f, 0.035f, 0.07f, 0.94f);
  profile.m_FoamColor = plColor(0.82f, 0.87f, 0.88f, 0.75f);
  profile.m_sNormalTexture.Clear(); // Exercise deterministic analytic-normal fallback.
  profile.m_sFoamTexture.Clear();   // Exercise deterministic procedural foam breakup.

  m_hWaterObject.Invalidate();
  m_hRiverObject.Invalidate();
  m_hJunctionObject.Invalidate();
  m_hCausticsDisabledProfile.Invalidate();
  m_hCausticsEnabledProfile.Invalidate();

  const char* szProfileName = "GameEngineTest/Water/AboveWaterProfile";
  if (variant == SceneVariant::Underwater)
  {
    szProfileName = "GameEngineTest/Water/UnderwaterProfile";
  }
  else if (variant == SceneVariant::FlatLitWater)
  {
    // A perfectly flat surface: interaction ripples become the only surface motion, so an image diff against a
    // pre-interaction baseline isolates exactly the displacement/normal response the interaction sim produces.
    szProfileName = "GameEngineTest/Water/FlatLitProfile";
    profile.m_fWaveAmplitude = 0.0f;
    profile.m_fMicroNormalStrength = 0.0f;
    profile.m_fSunGlitterStrength = 0.35f;
    profile.m_fRefractionStrength = 0.03f;
    profile.m_fRefractionDepth = 4.0f;
    profile.m_fUnderwaterCaustics = 0.0f;
    profile.m_fReflectedCaustics = 0.0f;
  }
  else if (bProjectedCaustics)
  {
    // Static, spatially varying waves make the A/B captures differ only by caustic strength. In particular, no
    // simulation-time drift can make a moving water surface look like a projected-caustic response.
    szProfileName = variant == SceneVariant::TransmittedCaustics
                      ? "GameEngineTest/Water/TransmittedCausticsDisabled"
                      : variant == SceneVariant::ReflectedCaustics
                          ? "GameEngineTest/Water/ReflectedCausticsDisabled"
                          : "GameEngineTest/Water/OceanReflectedCausticsDisabled";
    profile.m_fWindSpeed = 0.0f;
    profile.m_fWaveAmplitude = 0.4f;
    // Match each lobe's focal distance while keeping the shortest analytic component resolvable by High's
    // 0.5 m caustic field. The reflected path bends roughly twice as strongly and therefore needs the broader
    // spectrum at its shallower receiver plane.
    profile.m_fBaseWavelength = variant == SceneVariant::TransmittedCaustics ? 8.0f : 16.0f;
    // Steepness only adds horizontal Gerstner displacement; it does not strengthen the analytic normal gradient.
    // Keeping it zero prevents the rasterized source field from folding away from the inverse trace.
    profile.m_fWaveSteepness = 0.0f;
    profile.m_uiCpuWaveCount = 8;
    profile.m_fFoamStrength = 0.0f;
    profile.m_fMicroNormalStrength = 0.0f;
    profile.m_fSunGlitterStrength = 0.0f;
    profile.m_fUnderwaterCaustics = 0.0f;
    profile.m_fReflectedCaustics = 0.0f;
  }
  else if (bLakeJunction)
  {
    szProfileName = variant == SceneVariant::FiniteDebugModes
                      ? "GameEngineTest/Water/FiniteDebugProfile"
                      : "GameEngineTest/Water/LakeJunctionProfile";
    profile.m_fWaveAmplitude = 0.0f;
    profile.m_fMicroNormalStrength = 0.0f;
    profile.m_fSunGlitterStrength = 0.0f;
    profile.m_fUnderwaterCaustics = 0.0f;
    profile.m_fReflectedCaustics = 0.0f;
  }
  else if (variant == SceneVariant::OceanDebugModes)
  {
    szProfileName = "GameEngineTest/Water/OceanDebugProfile";
  }

  if (bSurfaceDebugModes)
  {
    // Keep diagnostic channels comfortably above 8-bit quantization so the runtime regression can distinguish
    // them from the ordinary optical result without changing production defaults.
    profile.m_fRoughness = 0.65f;
    profile.m_ShallowColor = plColor(0.18f, 0.45f, 0.55f, 0.85f);
    profile.m_DeepColor = plColor(0.03f, 0.12f, 0.24f, 0.95f);
    profile.m_fUnderwaterCaustics = 0.0f;
    profile.m_fReflectedCaustics = 0.0f;
  }

  plWaterProfileResourceHandle hProfile;
  if (bProjectedCaustics)
  {
    plWaterProfileResourceDescriptor enabledProfile = profile;
    if (variant == SceneVariant::TransmittedCaustics)
      enabledProfile.m_fUnderwaterCaustics = 4.0f;
    else
      enabledProfile.m_fReflectedCaustics = 4.0f;

    profile.Sanitize();
    enabledProfile.Sanitize();
    m_hCausticsDisabledProfile = plResourceManager::CreateResource<plWaterProfileResource>(
      szProfileName, std::move(profile), "Projected caustics disabled test profile");
    m_hCausticsEnabledProfile = plResourceManager::CreateResource<plWaterProfileResource>(
      variant == SceneVariant::TransmittedCaustics
        ? "GameEngineTest/Water/TransmittedCausticsEnabled"
        : variant == SceneVariant::ReflectedCaustics
            ? "GameEngineTest/Water/ReflectedCausticsEnabled"
            : "GameEngineTest/Water/OceanReflectedCausticsEnabled",
      std::move(enabledProfile), "Projected caustics enabled test profile");
    hProfile = m_hCausticsDisabledProfile;
  }
  else
  {
    profile.Sanitize();
    hProfile = plResourceManager::CreateResource<plWaterProfileResource>(
      szProfileName, std::move(profile), "Water rendering test profile");
  }

  {
    PL_LOCK(m_pWorld->GetWriteMarker());
    m_pWorld->Clear();
    m_pWorld->GetRandomNumberGenerator().Initialize(0x57415445u);
    m_pWorld->GetClock().Reset(false);

    plGameObjectDesc objectDesc;
    const bool bLake = variant == SceneVariant::ReflectedCaustics || bLakeJunction;
    objectDesc.m_sName.Assign(bLake ? "DeterministicLake" : "DeterministicOcean");
    objectDesc.m_LocalPosition = plVec3::MakeZero();

    plGameObject* pWaterObject = nullptr;
    m_hWaterObject = m_pWorld->CreateObject(objectDesc, pWaterObject);

    if (bLake)
    {
      plSpline spline;
      spline.m_bClosed = true;
      const plVec3 reflectedPositions[] = {
        plVec3(-18.0f, -18.0f, 0.0f), plVec3(18.0f, -18.0f, 0.0f),
        plVec3(18.0f, 18.0f, 0.0f), plVec3(-18.0f, 18.0f, 0.0f)};
      const plVec3 junctionPositions[] = {
        plVec3(-8.0f, -10.0f, 0.0f), plVec3(14.0f, -10.0f, 0.0f),
        plVec3(14.0f, 10.0f, 0.0f), plVec3(-8.0f, 10.0f, 0.0f)};
      const plArrayPtr<const plVec3> positions = bLakeJunction
                                                   ? plMakeArrayPtr(junctionPositions)
                                                   : plMakeArrayPtr(reflectedPositions);
      for (const plVec3& position : positions)
      {
        auto& point = spline.m_ControlPoints.ExpandAndGetRef();
        point.SetPosition(plSimdVec4f(position.x, position.y, position.z, 1.0f));
        point.SetTangentModeIn(plSplineTangentMode::Linear);
        point.SetTangentModeOut(plSplineTangentMode::Linear);
      }
      spline.CalculateUpDirAndAutoTangents();
      ++spline.m_uiChangeCounter;
      plSplineComponent* pSpline = nullptr;
      plSplineComponent::CreateComponent(pWaterObject, pSpline);
      pSpline->SetSpline(std::move(spline));
    }

    plWaterBodyComponent* pWater = nullptr;
    m_pWorld->GetOrCreateComponentManager<plWaterBodyComponentManager>()->CreateComponent(pWaterObject, pWater);
    pWater->SetBodyType(bLake ? plWaterBodyType::Lake : plWaterBodyType::Ocean);
    pWater->SetProfile(hProfile);
    pWater->SetDefaultDepth(8.0f);
    pWater->SetOceanExtent(256.0f);
    pWater->SetOceanPatchSize(32.0f);
    pWater->SetOceanRingCount(4);
    pWater->SetPatchResolution(
      bProjectedCaustics ? 64 : 24);

    if (bLakeJunction)
    {
      plGameObjectDesc riverDesc;
      riverDesc.m_sName.Assign("DeterministicRiver");
      plGameObject* pRiverObject = nullptr;
      m_hRiverObject = m_pWorld->CreateObject(riverDesc, pRiverObject);

      plSpline riverSpline;
      riverSpline.m_bClosed = false;
      const plVec3 riverPositions[] = {plVec3(-24.0f, 0.0f, 0.0f), plVec3(-8.0f, 0.0f, 0.0f)};
      for (const plVec3& position : riverPositions)
      {
        auto& point = riverSpline.m_ControlPoints.ExpandAndGetRef();
        point.SetPosition(plSimdVec4f(position.x, position.y, position.z, 1.0f));
        point.SetTangentModeIn(plSplineTangentMode::Linear);
        point.SetTangentModeOut(plSplineTangentMode::Linear);
      }
      riverSpline.CalculateUpDirAndAutoTangents();
      ++riverSpline.m_uiChangeCounter;
      plSplineComponent* pRiverSpline = nullptr;
      plSplineComponent::CreateComponent(pRiverObject, pRiverSpline);
      pRiverSpline->SetSpline(std::move(riverSpline));

      plWaterBodyComponent* pRiver = nullptr;
      m_pWorld->GetOrCreateComponentManager<plWaterBodyComponentManager>()->CreateComponent(pRiverObject, pRiver);
      pRiver->SetBodyType(plWaterBodyType::River);
      pRiver->SetProfile(hProfile);
      pRiver->SetDefaultWidth(2.5f);
      pRiver->SetDefaultDepth(4.0f);
      pRiver->SetPatchResolution(24);
    }

    if (variant == SceneVariant::FlatLitWater || bProjectedCaustics)
    {
      plGameObjectDesc sunDesc;
      sunDesc.m_sName.Assign("TestSun");
      if (variant == SceneVariant::FlatLitWater)
      {
        sunDesc.m_LocalRotation = plQuat::MakeFromEulerAngles(
          plAngle::MakeFromDegree(0.0f), plAngle::MakeFromDegree(45.0f), plAngle::MakeFromDegree(20.0f));
      }
      else
      {
        const plVec3 vToLight = plVec3(0.55f, 0.15f, 0.82f).GetNormalized();
        sunDesc.m_LocalRotation = plQuat::MakeShortestRotation(plVec3(-1.0f, 0.0f, 0.0f), vToLight);
      }
      plGameObject* pSunObject = nullptr;
      m_pWorld->CreateObject(sunDesc, pSunObject);
      plDirectionalLightComponent* pSun = nullptr;
      plDirectionalLightComponent::CreateComponent(pSunObject, pSun);
      pSun->SetIntensity(variant == SceneVariant::FlatLitWater ? 3.0f : 60.0f);

      if (variant == SceneVariant::FlatLitWater)
      {
        // Mirror the editor's EDIT mode: world simulation off, the water module ticks from wall clock. The
        // interaction lifecycle must behave identically there - it once froze in exactly this configuration.
        m_pWorld->SetWorldSimulationEnabled(false);
      }
    }

    if (variant == SceneVariant::FlatLitWater)
    {
      // Refraction needs spatial detail to move. A submerged checkerboard makes the interaction displacement and
      // normal response observable without relying on exaggerated glitter or foam brightness.
      const plMeshResourceHandle hReceiverMesh = CreateCausticReceiverMesh(false);
      for (plInt32 y = -3; y <= 3; ++y)
      {
        for (plInt32 x = -3; x <= 3; ++x)
        {
          plGameObjectDesc receiverDesc;
          receiverDesc.m_sName.Assign("InteractionRefractionChecker");
          receiverDesc.m_LocalPosition = plVec3(x * 2.0f, y * 2.0f, -2.0f);
          receiverDesc.m_LocalScaling = plVec3(0.0625f, 0.0625f, 1.0f);

          plGameObject* pReceiverObject = nullptr;
          m_pWorld->CreateObject(receiverDesc, pReceiverObject);
          plMeshComponent* pReceiver = nullptr;
          m_pWorld->GetOrCreateComponentManager<plMeshComponentManager>()->CreateComponent(pReceiverObject, pReceiver);
          pReceiver->SetMesh(hReceiverMesh);
          pReceiver->SetColor((x + y) % 2 == 0 ? plColor(0.04f, 0.04f, 0.04f) : plColor(0.45f, 0.45f, 0.45f));
        }
      }
    }

    if (bProjectedCaustics)
    {
      const bool bReflected = bReflectedCaustics;
      plGameObjectDesc receiverDesc;
      receiverDesc.m_sName.Assign(bReflected ? "ReflectedCausticReceiver" : "TransmittedCausticReceiver");
      receiverDesc.m_LocalPosition = plVec3(0.0f, 0.0f, bReflected ? 1.5f : -3.0f);
      plGameObject* pReceiverObject = nullptr;
      m_pWorld->CreateObject(receiverDesc, pReceiverObject);
      plMeshComponent* pReceiver = nullptr;
      m_pWorld->GetOrCreateComponentManager<plMeshComponentManager>()->CreateComponent(pReceiverObject, pReceiver);
      pReceiver->SetMesh(CreateCausticReceiverMesh(bReflected));
      pReceiver->SetColor(plColor(0.05f, 0.05f, 0.05f));

      if (bReflected)
      {
        // The reflected receiver faces away from the sun by construction. A local fill light keeps the comparison
        // on already-lit pixels while the directional light remains the sole source used by the caustic trace.
        plGameObjectDesc fillDesc;
        fillDesc.m_sName.Assign("ReflectedReceiverFill");
        fillDesc.m_LocalPosition = plVec3(0.0f, 0.0f, 0.5f);
        plGameObject* pFillObject = nullptr;
        m_pWorld->CreateObject(fillDesc, pFillObject);
        plPointLightComponent* pFill = nullptr;
        plPointLightComponent::CreateComponent(pFillObject, pFill);
        pFill->SetIntensity(30.0f);
        pFill->SetRange(16.0f);
      }
    }
  }

  plWaterRenderingTestGameState* pGameState = plDynamicCast<plWaterRenderingTestGameState*>(GetActiveGameState());
  if (pGameState == nullptr)
    return PL_FAILURE;

  pGameState->SetRenderPipeline(m_hRenderPipeline);
  pGameState->SetViewRenderMode(plViewRenderMode::None);

  plCamera* pCamera = pGameState->GetMainCamera();
  pCamera->SetCameraMode(plCameraMode::PerspectiveFixedFovY, 55.0f, 0.1f, 500.0f);
  if (variant == SceneVariant::Underwater)
  {
    pCamera->LookAt(plVec3(-8.0f, 0.0f, -1.5f), plVec3(6.0f, 0.0f, 0.4f), plVec3(0.0f, 0.0f, 1.0f));
  }
  else if (variant == SceneVariant::TransmittedCaustics)
  {
    // Observe the receiver from inside the water. Looking through the surface would make its transmission and
    // deep-water extinction dominate the A/B result, masking whether projected light reached the receiver.
    pCamera->LookAt(plVec3(-8.0f, 0.0f, -1.0f), plVec3(2.0f, 0.0f, -3.0f), plVec3(0.0f, 0.0f, 1.0f));
  }
  else if (bReflectedCaustics)
  {
    pCamera->LookAt(plVec3(-3.0f, 0.0f, 0.75f), plVec3(0.0f, 0.0f, 1.5f), plVec3(0.0f, 0.0f, 1.0f));
  }
  else if (bLakeJunction)
  {
    pCamera->LookAt(plVec3(-22.0f, -20.0f, 24.0f), plVec3(-1.0f, 0.0f, 0.0f), plVec3(0.0f, 0.0f, 1.0f));
  }
  else
  {
    pCamera->LookAt(plVec3(-14.0f, 0.0f, 7.0f), plVec3(4.0f, 0.0f, -0.5f), plVec3(0.0f, 0.0f, 1.0f));
  }

  return PL_SUCCESS;
}

bool plWaterRenderingTestApplication::SetViewRenderMode(plViewRenderMode::Enum mode)
{
  plWaterRenderingTestGameState* pGameState = plDynamicCast<plWaterRenderingTestGameState*>(GetActiveGameState());
  return pGameState != nullptr && pGameState->SetViewRenderMode(mode);
}

bool plWaterRenderingTestApplication::EnableLakeJunction()
{
  PL_LOCK(m_pWorld->GetWriteMarker());
  if (!m_hWaterObject.IsInvalidated() && !m_hRiverObject.IsInvalidated() && m_hJunctionObject.IsInvalidated())
  {
    plGameObjectDesc junctionDesc;
    junctionDesc.m_sName.Assign("DeterministicLakeJunction");
    junctionDesc.m_LocalPosition = plVec3(-8.0f, 0.0f, 0.0f);
    plGameObject* pJunctionObject = nullptr;
    m_hJunctionObject = m_pWorld->CreateObject(junctionDesc, pJunctionObject);
    plWaterJunctionComponent* pJunction = nullptr;
    m_pWorld->GetOrCreateComponentManager<plWaterJunctionComponentManager>()->CreateComponent(pJunctionObject, pJunction);
    pJunction->SetBodyA(m_hRiverObject);
    pJunction->SetBodyB(m_hWaterObject);
    pJunction->SetAnchorA(plWaterAttachmentAnchor::RiverEnd);
    pJunction->SetAnchorB(plWaterAttachmentAnchor::LakeBoundary);
    pJunction->SetSnapRadius(3.0f);
    return true;
  }
  return false;
}

bool plWaterRenderingTestApplication::HasValidLakeJunction()
{
  PL_LOCK(m_pWorld->GetWriteMarker());

  plDynamicArray<plWaterJunctionRenderSnapshot> snapshots;
  m_pWorld->GetOrCreateModule<plWaterWorldModule>()->CopyJunctionRenderSnapshots(snapshots);
  for (const plWaterJunctionRenderSnapshot& snapshot : snapshots)
  {
    const bool bHasInflowField = snapshot.m_SecondaryType == plWaterBodyType::Lake &&
                                 snapshot.m_fFlowFieldLength >= 3.0f && snapshot.m_fFlowFieldHalfWidth > 0.0f &&
                                 snapshot.m_fFlowFieldSpeed > 0.0f && snapshot.m_fFlowFieldDirectionSign > 0.0f &&
                                 snapshot.m_vFlowFieldDirection.GetLengthSquared() > 0.9f;
    const bool bHasExactLakeStitch = snapshot.m_uiStitchedRiverVertexCount >= 3 && snapshot.m_uiStitchedLakeVertexCount >= 2 &&
                                     snapshot.m_fMaximumLakeSeamError <= 0.00001f &&
                                     snapshot.m_fLakeMaterialConvergenceDistance > 0.0f &&
                                     (snapshot.m_vStitchedLakeLeft - snapshot.m_vStitchedLakeRight).GetLengthSquared() > 0.01f;
    const bool bHasRenderableSurface = snapshot.m_bMergedIntoPrimary
                                         ? snapshot.m_hMergedSurfaceMesh.IsValid() && snapshot.m_uiMergedSurfacePrimitiveCount > 0
                                         : snapshot.m_hSurfaceMesh.IsValid() && snapshot.m_uiSurfacePrimitiveCount > 0;
    if (snapshot.m_bValid && bHasRenderableSurface && bHasInflowField && bHasExactLakeStitch)
      return true;
  }
  return false;
}

bool plWaterRenderingTestApplication::SetProjectedCausticsEnabled(bool bEnabled)
{
  PL_LOCK(m_pWorld->GetWriteMarker());

  plGameObject* pWaterObject = nullptr;
  if (!m_pWorld->TryGetObject(m_hWaterObject, pWaterObject))
    return false;

  plWaterBodyComponent* pWater = nullptr;
  if (!pWaterObject->TryGetComponentOfBaseType(pWater))
    return false;

  const plWaterProfileResourceHandle& hProfile = bEnabled ? m_hCausticsEnabledProfile : m_hCausticsDisabledProfile;
  if (!hProfile.IsValid())
    return false;

  pWater->SetProfile(hProfile);
  return true;
}

plUInt64 plWaterRenderingTestApplication::GetWaterSimulationTick()
{
  PL_LOCK(m_pWorld->GetWriteMarker());
  if (const plWaterWorldModuleInterface* pModule = m_pWorld->GetOrCreateModule<plWaterWorldModuleInterface>())
    return pModule->GetSimulationTick();
  return 0;
}

void plWaterRenderingTestApplication::InjectWaterInteraction(const plVec3& vPosition, bool bSplash, float fStrengthOverride /*= -1.0f*/)
{
  PL_LOCK(m_pWorld->GetWriteMarker());

  plWaterWorldModuleInterface* pModule = m_pWorld->GetOrCreateModule<plWaterWorldModuleInterface>();
  if (pModule == nullptr)
  {
    plLog::Error("WaterTest: plWaterWorldModuleInterface did not resolve - interaction not injected");
    return;
  }

  plWaterInteraction interaction;
  interaction.m_vPosition = vPosition;
  interaction.m_vDirection = plVec3(0, 0, -1);
  interaction.m_Type = bSplash ? plWaterInteractionType::Splash : plWaterInteractionType::Continuous;
  interaction.m_fRadius = 0.5f;
  interaction.m_fStrength = fStrengthOverride >= 0.0f ? fStrengthOverride : (bSplash ? 1.0f : 0.3f);
  interaction.m_uiSimulationTick = pModule->GetSimulationTick();
  pModule->AddInteraction(interaction);
}

plGameObjectHandle plWaterRenderingTestApplication::CreateWaterInteractor(const plVec3& vPosition)
{
  PL_LOCK(m_pWorld->GetWriteMarker());

  plGameObjectDesc desc;
  desc.m_sName.Assign("WaterProber");
  desc.m_LocalPosition = vPosition;
  desc.m_bDynamic = true;

  plGameObject* pObject = nullptr;
  const plGameObjectHandle hObject = m_pWorld->CreateObject(desc, pObject);
  plWaterInteractorComponent* pInteractor = nullptr;
  m_pWorld->GetOrCreateComponentManager<plWaterInteractorComponentManager>()->CreateComponent(pObject, pInteractor);
  return hObject;
}

void plWaterRenderingTestApplication::SetObjectPosition(const plGameObjectHandle& hObject, const plVec3& vPosition)
{
  PL_LOCK(m_pWorld->GetWriteMarker());
  plGameObject* pObject = nullptr;
  if (m_pWorld->TryGetObject(hObject, pObject))
    pObject->SetGlobalPosition(vPosition);
}

void plWaterRenderingTestApplication::CountInteractionsSince(plUInt64& ref_uiCursor, plUInt32& out_uiTotal, plUInt32& out_uiSplashes)
{
  out_uiTotal = 0;
  out_uiSplashes = 0;

  PL_LOCK(m_pWorld->GetReadMarker());
  const plWaterWorldModule* pModule = m_pWorld->GetModuleReadOnly<plWaterWorldModule>();
  if (pModule == nullptr)
    return;

  plDynamicArray<plWaterInteraction> interactions;
  ref_uiCursor = pModule->CopyInteractionsSince(ref_uiCursor, interactions);
  out_uiTotal = interactions.GetCount();
  for (const plWaterInteraction& interaction : interactions)
  {
    if (interaction.m_Type == plWaterInteractionType::Splash)
      ++out_uiSplashes;
  }
}

const char* plGameEngineTestWaterRendering::GetTestName() const
{
  return "Water Rendering Tests";
}

plGameEngineTestApplication* plGameEngineTestWaterRendering::CreateApplication()
{
  m_pOwnApplication = PL_DEFAULT_NEW(plWaterRenderingTestApplication);
  return m_pOwnApplication;
}

void plGameEngineTestWaterRendering::SetupSubTests()
{
  AddSubTest("No Light Above Water", SubTests::NoLightAboveWater);
  AddSubTest("No Light Underwater", SubTests::NoLightUnderwater);
  AddSubTest("Interaction Displacement", SubTests::InteractionDisplacement);
  AddSubTest("Transmitted Caustics", SubTests::TransmittedCaustics);
  AddSubTest("Reflected Caustics", SubTests::ReflectedCaustics);
  AddSubTest("Ocean Reflected Caustics Suppressed", SubTests::OceanReflectedCausticsSuppressed);
  AddSubTest("Lake Junction Continuity", SubTests::LakeJunctionContinuity);
  AddSubTest("Ocean Debug Modes", SubTests::OceanDebugModes);
  AddSubTest("Finite Water Debug Modes", SubTests::FiniteDebugModes);
}

plResult plGameEngineTestWaterRendering::InitializeSubTest(plInt32 iIdentifier)
{
  PL_SUCCEED_OR_RETURN(SUPER::InitializeSubTest(iIdentifier));

  m_iFrame = -1;
  m_iLifecyclePhase = 0;
  m_uiPhaseStartTick = 0;
  m_uiLastStreamInjectTick = 0;
  m_fFirstWakeChangedFraction = 0.0f;
  m_fStreamChangedFraction = 0.0f;
  m_BaselineImage.Clear();
  if (iIdentifier == SubTests::NoLightAboveWater)
    return m_pOwnApplication->SetupScene(plWaterRenderingTestApplication::SceneVariant::AboveWater);
  if (iIdentifier == SubTests::NoLightUnderwater)
    return m_pOwnApplication->SetupScene(plWaterRenderingTestApplication::SceneVariant::Underwater);
  if (iIdentifier == SubTests::InteractionDisplacement)
    return m_pOwnApplication->SetupScene(plWaterRenderingTestApplication::SceneVariant::FlatLitWater);
  if (iIdentifier == SubTests::TransmittedCaustics)
    return m_pOwnApplication->SetupScene(plWaterRenderingTestApplication::SceneVariant::TransmittedCaustics);
  if (iIdentifier == SubTests::ReflectedCaustics)
    return m_pOwnApplication->SetupScene(plWaterRenderingTestApplication::SceneVariant::ReflectedCaustics);
  if (iIdentifier == SubTests::OceanReflectedCausticsSuppressed)
    return m_pOwnApplication->SetupScene(plWaterRenderingTestApplication::SceneVariant::OceanReflectedCausticsSuppressed);
  if (iIdentifier == SubTests::LakeJunctionContinuity)
    return m_pOwnApplication->SetupScene(plWaterRenderingTestApplication::SceneVariant::LakeJunction);
  if (iIdentifier == SubTests::OceanDebugModes)
    return m_pOwnApplication->SetupScene(plWaterRenderingTestApplication::SceneVariant::OceanDebugModes);
  if (iIdentifier == SubTests::FiniteDebugModes)
  {
    PL_SUCCEED_OR_RETURN(m_pOwnApplication->SetupScene(plWaterRenderingTestApplication::SceneVariant::FiniteDebugModes));
    return m_pOwnApplication->EnableLakeJunction() ? PL_SUCCESS : PL_FAILURE;
  }

  return PL_FAILURE;
}

bool plGameEngineTestWaterRendering::AssertNonBlackWaterImage() const
{
  plImage rgbaImage;
  if (plImageConversion::Convert(m_pOwnApplication->GetLastScreenshot(), rgbaImage, plImageFormat::R8G8B8A8_UNORM).Failed())
  {
    PL_TEST_FAILURE("Water screenshot could not be converted to RGBA8", "");
    return false;
  }

  const plUInt32 uiMinX = rgbaImage.GetWidth() / 4;
  const plUInt32 uiMaxX = rgbaImage.GetWidth() * 3 / 4;
  const plUInt32 uiMinY = rgbaImage.GetHeight() / 4;
  const plUInt32 uiMaxY = rgbaImage.GetHeight() * 3 / 4;

  double fLuminanceSum = 0.0;
  float fPeakLuminance = 0.0f;
  plUInt64 uiPixelCount = 0;
  for (plUInt32 y = uiMinY; y < uiMaxY; ++y)
  {
    for (plUInt32 x = uiMinX; x < uiMaxX; ++x)
    {
      const plUInt8* pPixel = rgbaImage.GetPixelPointer<plUInt8>(0, 0, 0, x, y);
      const float r = pPixel[0] / 255.0f;
      const float g = pPixel[1] / 255.0f;
      const float b = pPixel[2] / 255.0f;
      const float fLuminance = 0.2126f * r + 0.7152f * g + 0.0722f * b;
      fLuminanceSum += fLuminance;
      fPeakLuminance = plMath::Max(fPeakLuminance, fLuminance);
      ++uiPixelCount;
    }
  }

  const float fMeanLuminance = uiPixelCount > 0 ? static_cast<float>(fLuminanceSum / static_cast<double>(uiPixelCount)) : 0.0f;
  PL_TEST_BOOL_MSG(fMeanLuminance > 0.001f,
    "No-light water must retain non-black energy (mean center luminance {}, peak {})", fMeanLuminance, fPeakLuminance);
  PL_TEST_BOOL_MSG(fPeakLuminance > 0.003f,
    "No-light water must contain visible pixels (mean center luminance {}, peak {})", fMeanLuminance, fPeakLuminance);
  return fMeanLuminance > 0.001f && fPeakLuminance > 0.003f;
}

namespace
{
  struct ProjectedCausticDifference
  {
    float m_fBaselineMeanLuminance = 0.0f;
    float m_fMeanPositiveDelta = 0.0f;
    float m_fPeakPositiveDelta = 0.0f;
    float m_fPositiveChangedFraction = 0.0f;
  };

  ProjectedCausticDifference ComputeProjectedCausticDifference(const plImage& disabled, const plImage& enabled)
  {
    ProjectedCausticDifference result;
    const plUInt32 uiMinX = disabled.GetWidth() / 5;
    const plUInt32 uiMaxX = disabled.GetWidth() * 4 / 5;
    const plUInt32 uiMinY = disabled.GetHeight() / 5;
    const plUInt32 uiMaxY = disabled.GetHeight() * 4 / 5;

    double fBaselineSum = 0.0;
    double fPositiveDeltaSum = 0.0;
    plUInt64 uiPositiveChangedPixels = 0;
    plUInt64 uiPixelCount = 0;
    for (plUInt32 y = uiMinY; y < uiMaxY; ++y)
    {
      for (plUInt32 x = uiMinX; x < uiMaxX; ++x)
      {
        const plUInt8* pDisabled = disabled.GetPixelPointer<plUInt8>(0, 0, 0, x, y);
        const plUInt8* pEnabled = enabled.GetPixelPointer<plUInt8>(0, 0, 0, x, y);
        const float fDisabledLuminance =
          (0.2126f * pDisabled[0] + 0.7152f * pDisabled[1] + 0.0722f * pDisabled[2]) / 255.0f;
        const float fEnabledLuminance =
          (0.2126f * pEnabled[0] + 0.7152f * pEnabled[1] + 0.0722f * pEnabled[2]) / 255.0f;
        const float fPositiveDelta = plMath::Max(fEnabledLuminance - fDisabledLuminance, 0.0f);
        fBaselineSum += fDisabledLuminance;
        fPositiveDeltaSum += fPositiveDelta;
        result.m_fPeakPositiveDelta = plMath::Max(result.m_fPeakPositiveDelta, fPositiveDelta);
        if (fPositiveDelta > 2.0f / 255.0f)
          ++uiPositiveChangedPixels;
        ++uiPixelCount;
      }
    }

    if (uiPixelCount > 0)
    {
      result.m_fBaselineMeanLuminance = static_cast<float>(fBaselineSum / static_cast<double>(uiPixelCount));
      result.m_fMeanPositiveDelta = static_cast<float>(fPositiveDeltaSum / static_cast<double>(uiPixelCount));
      result.m_fPositiveChangedFraction = static_cast<float>(uiPositiveChangedPixels) / static_cast<float>(uiPixelCount);
    }
    return result;
  }

  // Fraction of noticeably changed pixels in the image center (where the test ripples live).
  float ComputeCenterChangedFraction(const plImage& a, const plImage& b)
  {
    const plUInt32 uiMinX = a.GetWidth() / 4;
    const plUInt32 uiMaxX = a.GetWidth() * 3 / 4;
    const plUInt32 uiMinY = a.GetHeight() / 4;
    const plUInt32 uiMaxY = a.GetHeight() * 3 / 4;

    plUInt32 uiChangedPixels = 0;
    plUInt64 uiPixelCount = 0;
    for (plUInt32 y = uiMinY; y < uiMaxY; ++y)
    {
      for (plUInt32 x = uiMinX; x < uiMaxX; ++x)
      {
        const plUInt8* pA = a.GetPixelPointer<plUInt8>(0, 0, 0, x, y);
        const plUInt8* pB = b.GetPixelPointer<plUInt8>(0, 0, 0, x, y);
        const plInt32 iDiff = plMath::Abs(static_cast<plInt32>(pA[0]) - pB[0]) +
                              plMath::Abs(static_cast<plInt32>(pA[1]) - pB[1]) +
                              plMath::Abs(static_cast<plInt32>(pA[2]) - pB[2]);
        if (iDiff > 12)
          ++uiChangedPixels;
        ++uiPixelCount;
      }
    }
    return uiPixelCount > 0 ? static_cast<float>(uiChangedPixels) / static_cast<float>(uiPixelCount) : 0.0f;
  }

  // Broad saturation catches unit/conversion errors in the HDR light path without rejecting a legitimate,
  // spatially small sun glint. In particular, plColorLinearUB channels must not reach the shader as 0..255 floats.
  float ComputeCenterNearWhiteFraction(const plImage& image)
  {
    const plUInt32 uiMinX = image.GetWidth() / 4;
    const plUInt32 uiMaxX = image.GetWidth() * 3 / 4;
    const plUInt32 uiMinY = image.GetHeight() / 4;
    const plUInt32 uiMaxY = image.GetHeight() * 3 / 4;

    plUInt64 uiNearWhitePixels = 0;
    plUInt64 uiPixelCount = 0;
    for (plUInt32 y = uiMinY; y < uiMaxY; ++y)
    {
      for (plUInt32 x = uiMinX; x < uiMaxX; ++x)
      {
        const plUInt8* pPixel = image.GetPixelPointer<plUInt8>(0, 0, 0, x, y);
        const float fLuminance = (0.2126f * pPixel[0] + 0.7152f * pPixel[1] + 0.0722f * pPixel[2]) / 255.0f;
        if (fLuminance > 0.9f)
          ++uiNearWhitePixels;
        ++uiPixelCount;
      }
    }

    return uiPixelCount > 0 ? static_cast<float>(uiNearWhitePixels) / static_cast<float>(uiPixelCount) : 0.0f;
  }

  struct WaterCoverageLoss
  {
    plUInt64 m_uiBaselineWaterPixels = 0;
    plUInt64 m_uiLostWaterPixels = 0;
  };

  WaterCoverageLoss ComputeWaterCoverageLoss(const plImage& baseline, const plImage& junction)
  {
    WaterCoverageLoss result;
    for (plUInt32 y = 0; y < baseline.GetHeight(); ++y)
    {
      for (plUInt32 x = 0; x < baseline.GetWidth(); ++x)
      {
        const plUInt8* pBaseline = baseline.GetPixelPointer<plUInt8>(0, 0, 0, x, y);
        const plUInt8* pJunction = junction.GetPixelPointer<plUInt8>(0, 0, 0, x, y);
        const float fBaselineLuminance =
          (0.2126f * pBaseline[0] + 0.7152f * pBaseline[1] + 0.0722f * pBaseline[2]) / 255.0f;
        const float fJunctionLuminance =
          (0.2126f * pJunction[0] + 0.7152f * pJunction[1] + 0.0722f * pJunction[2]) / 255.0f;
        if (fBaselineLuminance <= 0.01f)
          continue;

        ++result.m_uiBaselineWaterPixels;
        if (fJunctionLuminance < 0.01f && fJunctionLuminance < fBaselineLuminance * 0.35f)
          ++result.m_uiLostWaterPixels;
      }
    }
    return result;
  }

  float ComputeCenterRedDominantFraction(const plImage& image)
  {
    const plUInt32 uiMinX = image.GetWidth() / 4;
    const plUInt32 uiMaxX = image.GetWidth() * 3 / 4;
    const plUInt32 uiMinY = image.GetHeight() / 4;
    const plUInt32 uiMaxY = image.GetHeight() * 3 / 4;
    plUInt64 uiRedPixels = 0;
    plUInt64 uiPixelCount = 0;
    for (plUInt32 y = uiMinY; y < uiMaxY; ++y)
    {
      for (plUInt32 x = uiMinX; x < uiMaxX; ++x)
      {
        const plUInt8* pPixel = image.GetPixelPointer<plUInt8>(0, 0, 0, x, y);
        if (pPixel[0] > 80 && pPixel[0] > pPixel[1] * 2 && pPixel[0] > pPixel[2] * 2)
          ++uiRedPixels;
        ++uiPixelCount;
      }
    }
    return uiPixelCount > 0 ? static_cast<float>(uiRedPixels) / static_cast<float>(uiPixelCount) : 0.0f;
  }
} // namespace

plTestAppRun plGameEngineTestWaterRendering::RunSurfaceDebugModesTest(bool bFiniteWater)
{
  static constexpr plViewRenderMode::Enum modes[] = {
    plViewRenderMode::PixelNormals,
    plViewRenderMode::Roughness,
    plViewRenderMode::StaticVsDynamic,
    plViewRenderMode::TexCoordsUV0,
    plViewRenderMode::WireframeMonochrome,
  };

  ++m_iFrame;
  m_pOwnApplication->Run();
  if (m_pOwnApplication->WasQuitRequested())
    return plTestAppRun::Quit;

  auto CaptureImage = [this](plImage& out_image) -> bool
  {
    if (plImageConversion::Convert(m_pOwnApplication->GetLastScreenshot(), out_image, plImageFormat::R8G8B8A8_UNORM).Failed())
    {
      PL_TEST_FAILURE("Water debug-mode screenshot could not be converted to RGBA8", "");
      return false;
    }
    return true;
  };

  if (m_iLifecyclePhase == 0 && m_iFrame >= 16)
  {
    if (bFiniteWater && !m_pOwnApplication->HasValidLakeJunction())
    {
      PL_TEST_FAILURE("Finite debug-mode fixture did not produce its lake-junction surface", "");
      return plTestAppRun::Quit;
    }
    if (!CaptureImage(m_BaselineImage) || !m_pOwnApplication->SetViewRenderMode(modes[0]))
    {
      PL_TEST_FAILURE("Water debug-mode fixture could not select its first view mode", "");
      return plTestAppRun::Quit;
    }
    m_iLifecyclePhase = 1;
    m_uiPhaseStartTick = m_iFrame;
    return plTestAppRun::Continue;
  }

  if (m_iLifecyclePhase > 0 && m_iLifecyclePhase <= PL_ARRAY_SIZE(modes) && m_iFrame >= static_cast<plInt32>(m_uiPhaseStartTick + 4))
  {
    const plUInt32 uiModeIndex = static_cast<plUInt32>(m_iLifecyclePhase - 1);
    plImage debugImage;
    if (!CaptureImage(debugImage))
      return plTestAppRun::Quit;

    const float fChangedFraction = ComputeCenterChangedFraction(m_BaselineImage, debugImage);
    PL_TEST_BOOL_MSG(fChangedFraction > 0.01f,
      "Water surface did not respond to view debug mode %u (changed center fraction %f).",
      static_cast<plUInt32>(modes[uiModeIndex]), fChangedFraction);
    if (modes[uiModeIndex] == plViewRenderMode::StaticVsDynamic)
    {
      const float fRedFraction = ComputeCenterRedDominantFraction(debugImage);
      PL_TEST_BOOL_MSG(fRedFraction > 0.01f,
        "Static-vs-dynamic mode must classify displaced water as dynamic/red (center red fraction %f).", fRedFraction);
    }

    if (uiModeIndex + 1 >= PL_ARRAY_SIZE(modes))
    {
      m_pOwnApplication->SetViewRenderMode(plViewRenderMode::None);
      return plTestAppRun::Quit;
    }

    if (!m_pOwnApplication->SetViewRenderMode(modes[uiModeIndex + 1]))
    {
      PL_TEST_FAILURE("Water debug-mode fixture could not advance its view mode", "");
      return plTestAppRun::Quit;
    }
    ++m_iLifecyclePhase;
    m_uiPhaseStartTick = m_iFrame;
  }

  if (m_iFrame > 180)
  {
    PL_TEST_FAILURE("Water debug-mode test timed out", "");
    return plTestAppRun::Quit;
  }
  return plTestAppRun::Continue;
}

plTestAppRun plGameEngineTestWaterRendering::RunLakeJunctionContinuityTest()
{
  ++m_iFrame;
  m_pOwnApplication->Run();
  if (m_pOwnApplication->WasQuitRequested())
    return plTestAppRun::Quit;

  auto CaptureImage = [this](plImage& out_image) -> bool
  {
    if (plImageConversion::Convert(m_pOwnApplication->GetLastScreenshot(), out_image, plImageFormat::R8G8B8A8_UNORM).Failed())
    {
      PL_TEST_FAILURE("Lake-junction screenshot could not be converted to RGBA8", "");
      return false;
    }
    return true;
  };

  if (m_iLifecyclePhase == 0 && m_iFrame >= 16)
  {
    if (!CaptureImage(m_BaselineImage))
      return plTestAppRun::Quit;
    if (!m_pOwnApplication->EnableLakeJunction())
    {
      PL_TEST_FAILURE("Lake-junction fixture could not create its junction", "");
      return plTestAppRun::Quit;
    }
    m_iLifecyclePhase = 1;
    return plTestAppRun::Continue;
  }

  if (m_iLifecyclePhase == 1 && m_iFrame >= 36)
  {
    if (!m_pOwnApplication->HasValidLakeJunction())
    {
      PL_TEST_FAILURE("Lake-junction fixture did not produce a valid transition mesh", "");
      return plTestAppRun::Quit;
    }

    plImage junctionImage;
    if (!CaptureImage(junctionImage))
      return plTestAppRun::Quit;
    const WaterCoverageLoss loss = ComputeWaterCoverageLoss(m_BaselineImage, junctionImage);
    const float fLostFraction = loss.m_uiBaselineWaterPixels > 0
                                  ? static_cast<float>(loss.m_uiLostWaterPixels) / static_cast<float>(loss.m_uiBaselineWaterPixels)
                                  : 1.0f;
    PL_TEST_BOOL_MSG(loss.m_uiBaselineWaterPixels > 500,
      "Lake-junction fixture must contain a material area of visible water (got %llu pixels).", loss.m_uiBaselineWaterPixels);
    PL_TEST_BOOL_MSG(fLostFraction < 0.01f,
      "Attaching a river must not remove lake coverage (lost %llu of %llu water pixels, fraction %f).",
      loss.m_uiLostWaterPixels, loss.m_uiBaselineWaterPixels, fLostFraction);
    return plTestAppRun::Quit;
  }

  if (m_iFrame > 180)
  {
    PL_TEST_FAILURE("Lake-junction continuity capture timed out", "");
    return plTestAppRun::Quit;
  }
  return plTestAppRun::Continue;
}

plTestAppRun plGameEngineTestWaterRendering::RunProjectedCausticsTest(bool bReflected, bool bExpectVisible)
{
  ++m_iFrame;
  m_pOwnApplication->Run();
  if (m_pOwnApplication->WasQuitRequested())
    return plTestAppRun::Quit;

  auto CaptureImage = [this](plImage& out_image) -> bool
  {
    if (plImageConversion::Convert(m_pOwnApplication->GetLastScreenshot(), out_image, plImageFormat::R8G8B8A8_UNORM).Failed())
    {
      PL_TEST_FAILURE("Water screenshot could not be converted to RGBA8", "");
      return false;
    }
    return true;
  };

  if (m_iLifecyclePhase == 0 && m_iFrame >= 16)
  {
    if (!CaptureImage(m_BaselineImage))
      return plTestAppRun::Quit;
    if (!m_pOwnApplication->SetProjectedCausticsEnabled(true))
    {
      PL_TEST_FAILURE("Projected-caustic test could not enable its water profile", "");
      return plTestAppRun::Quit;
    }
    m_iLifecyclePhase = 1;
    return plTestAppRun::Continue;
  }

  if (m_iLifecyclePhase == 1 && m_iFrame >= 28)
  {
    plImage enabledImage;
    if (!CaptureImage(enabledImage))
      return plTestAppRun::Quit;
    const ProjectedCausticDifference difference = ComputeProjectedCausticDifference(m_BaselineImage, enabledImage);
    const char* szLobe = bReflected ? "Reflected" : "Transmitted";
    PL_TEST_BOOL_MSG(difference.m_fBaselineMeanLuminance > 0.002f,
      "%s caustics require an already-lit receiver (baseline center luminance %f).",
      szLobe, difference.m_fBaselineMeanLuminance);
    if (bExpectVisible)
    {
      PL_TEST_BOOL_MSG(difference.m_fPositiveChangedFraction > 0.001f,
        "%s caustics must brighten a material area of receiver pixels when enabled "
        "(fraction %f, mean positive delta %f, peak %f).",
        szLobe, difference.m_fPositiveChangedFraction, difference.m_fMeanPositiveDelta, difference.m_fPeakPositiveDelta);
      PL_TEST_BOOL_MSG(difference.m_fMeanPositiveDelta > 0.00005f && difference.m_fPeakPositiveDelta > 0.01f,
        "%s caustics must add material projected lighting, not only sub-quantization noise "
        "(fraction %f, mean positive delta %f, peak %f).",
        szLobe, difference.m_fPositiveChangedFraction, difference.m_fMeanPositiveDelta, difference.m_fPeakPositiveDelta);
    }
    else
    {
      PL_TEST_BOOL_MSG(difference.m_fPositiveChangedFraction < 0.001f && difference.m_fPeakPositiveDelta < 0.01f,
        "Ocean reflected caustics must remain disabled even when its profile requests them "
        "(fraction %f, mean positive delta %f, peak %f).",
        difference.m_fPositiveChangedFraction, difference.m_fMeanPositiveDelta, difference.m_fPeakPositiveDelta);
    }
    return plTestAppRun::Quit;
  }

  if (m_iFrame > 180)
  {
    PL_TEST_FAILURE("Projected-caustic A/B capture timed out", "");
    return plTestAppRun::Quit;
  }
  return plTestAppRun::Continue;
}

plTestAppRun plGameEngineTestWaterRendering::RunInteractionDisplacementTest()
{
  ++m_iFrame;
  m_pOwnApplication->Run();
  if (m_pOwnApplication->WasQuitRequested())
    return plTestAppRun::Quit;

  auto CaptureImage = [this](plImage& out_image) -> bool
  {
    if (plImageConversion::Convert(m_pOwnApplication->GetLastScreenshot(), out_image, plImageFormat::R8G8B8A8_UNORM).Failed())
    {
      PL_TEST_FAILURE("Water screenshot could not be converted to RGBA8", "");
      return false;
    }
    return true;
  };

  // Full interaction lifecycle, timed in 60 Hz SIMULATION ticks (frames run much faster than sim time when
  // vsync is off): splash -> visible ring -> ring DECAYS -> a second splash produces a NEW ring. Each stage
  // has failed independently in the past.
  const plUInt64 uiTick = m_pOwnApplication->GetWaterSimulationTick();

  switch (m_iLifecyclePhase)
  {
    case 0: // settle, then capture the undisturbed flat surface
      if (m_iFrame >= 12)
      {
        if (!CaptureImage(m_BaselineImage))
          return plTestAppRun::Quit;
        const float fNearWhiteFraction = ComputeCenterNearWhiteFraction(m_BaselineImage);
        PL_TEST_BOOL_MSG(fNearWhiteFraction < 0.35f,
          "Lit water must not turn a broad part of the view white (near-white center fraction %f).", fNearWhiteFraction);
        // Use a strong authored splash so the physically small ripple survives the 8-bit test capture and its
        // conservative changed-pixel threshold. Production interactors allow a wide strength range.
        m_pOwnApplication->InjectWaterInteraction(plVec3(0.0f, 0.0f, 0.0f), true, 5.0f);
        m_uiPhaseStartTick = uiTick;
        m_iLifecyclePhase = 1;
      }
      break;

    case 1: // ~0.25 sim-seconds after the splash: the young ring must be visible
      if (uiTick >= m_uiPhaseStartTick + 15)
      {
        plImage currentImage;
        if (!CaptureImage(currentImage))
          return plTestAppRun::Quit;
        m_fFirstWakeChangedFraction = ComputeCenterChangedFraction(m_BaselineImage, currentImage);
        PL_TEST_BOOL_MSG(m_fFirstWakeChangedFraction > 0.0005f,
          "A water splash must visibly displace the surface (changed-pixel fraction %f). "
          "Zero means interaction events never reach the GPU wave simulation or the surface ignores its output.",
          m_fFirstWakeChangedFraction);
        if (m_fFirstWakeChangedFraction <= 0.0005f)
          return plTestAppRun::Quit;
        m_iLifecyclePhase = 2;
      }
      break;

    case 2: // ~6 sim-seconds with no further events: the ring must have largely dissipated. The window is
            // deliberately generous: a healthy ring EXPANDS while it decays, so measured too early its
            // changed-pixel AREA can exceed the young ring's even though its amplitude is falling.
      if (uiTick >= m_uiPhaseStartTick + 360)
      {
        if (!CaptureImage(m_DecayedImage))
          return plTestAppRun::Quit;
        const float fDecayedFraction = ComputeCenterChangedFraction(m_BaselineImage, m_DecayedImage);
        PL_TEST_BOOL_MSG(fDecayedFraction < m_fFirstWakeChangedFraction * 0.6f,
          "A water splash must dissipate over time (fraction %f after 6 sim-seconds vs %f at the peak). "
          "A non-decaying wake means old events are re-applied every frame or the integrator is frozen.",
          fDecayedFraction, m_fFirstWakeChangedFraction);
        m_pOwnApplication->InjectWaterInteraction(plVec3(3.0f, 1.5f, 0.0f), true, 5.0f);
        m_uiPhaseStartTick = uiTick;
        m_iLifecyclePhase = 3;
      }
      break;

    case 3: // ~0.25 sim-seconds after the SECOND splash: a new ring must appear relative to the decayed state
      if (uiTick >= m_uiPhaseStartTick + 15)
      {
        plImage currentImage;
        if (!CaptureImage(currentImage))
          return plTestAppRun::Quit;
        const float fNewWakeFraction = ComputeCenterChangedFraction(m_DecayedImage, currentImage);
        PL_TEST_BOOL_MSG(fNewWakeFraction > 0.0005f,
          "A second splash must produce a new visible wake (changed-pixel fraction %f). "
          "Zero means follow-up interaction events never reach the GPU (e.g. the event buffer only uploads once).",
          fNewWakeFraction);
        if (fNewWakeFraction <= 0.0005f)
          return plTestAppRun::Quit;
        m_uiPhaseStartTick = uiTick;
        m_uiLastStreamInjectTick = 0;
        m_iLifecyclePhase = 4;
      }
      break;

    case 4: // 3 sim-seconds of a strong 30 Hz continuous-event stream: energy injection must stay BOUNDED. An
            // un-normalized stream saturates the wave field into one huge plateau that expands until it covers
            // the view - the production failure mode. The stress strength survives the conservative 8-bit diff.
      if (m_uiLastStreamInjectTick == 0 || uiTick >= m_uiLastStreamInjectTick + 2)
      {
        m_uiLastStreamInjectTick = uiTick;
        m_pOwnApplication->InjectWaterInteraction(plVec3(-1.5f, -1.0f, 0.0f), false, 8.0f);
      }
      if (uiTick >= m_uiPhaseStartTick + 180)
      {
        plImage currentImage;
        if (!CaptureImage(currentImage))
          return plTestAppRun::Quit;
        m_fStreamChangedFraction = ComputeCenterChangedFraction(m_BaselineImage, currentImage);
        PL_TEST_BOOL_MSG(m_fStreamChangedFraction > 0.0003f,
          "A sustained wake stream must remain visible (changed-pixel fraction %f after 3 s of 30 Hz events).",
          m_fStreamChangedFraction);
        // Absolute bound: a healthy stationary contact ripples a multi-meter neighborhood (~0.1 of the center
        // crop with velocity-driven shading); runaway rate-scaled accumulation saturates several times that.
        PL_TEST_BOOL_MSG(m_fStreamChangedFraction < 0.25f,
          "A sustained wake stream must stay BOUNDED (changed-pixel fraction %f after 3 s vs %f for one splash). "
          "Unbounded growth means stream events inject rate-scaled energy and the field saturates into one huge wake.",
          m_fStreamChangedFraction, m_fFirstWakeChangedFraction);
        m_uiPhaseStartTick = uiTick;
        m_iLifecyclePhase = 5;
      }
      break;

    case 5: // stream stopped: the accumulated wake must dissipate within ~5 sim-seconds, not linger for minutes
      if (uiTick >= m_uiPhaseStartTick + 300)
      {
        plImage currentImage;
        if (!CaptureImage(currentImage))
          return plTestAppRun::Quit;
        const float fSettledFraction = ComputeCenterChangedFraction(m_BaselineImage, currentImage);
        PL_TEST_BOOL_MSG(fSettledFraction < plMath::Max(m_fStreamChangedFraction * 0.4f, 0.0005f),
          "A stopped wake stream must dissipate (changed-pixel fraction %f after 5 s vs %f while streaming). "
          "A lingering wake means the field stored far more energy than the render ceiling shows ('never expires').",
          fSettledFraction, m_fStreamChangedFraction);

        // Enter the component-emission phase: a REAL plWaterInteractorComponent (Splash type - the default)
        // dragged through the water for 2 sim-seconds.
        m_uiInteractionCursor = 0;
        plUInt32 uiTotal = 0, uiSplashes = 0;
        m_pOwnApplication->CountInteractionsSince(m_uiInteractionCursor, uiTotal, uiSplashes); // baseline the cursor
        m_vProberStart = plVec3(-3.0f, -4.0f, -0.1f);
        m_hProberObject = m_pOwnApplication->CreateWaterInteractor(m_vProberStart);
        m_uiPhaseStartTick = uiTick;
        m_iLifecyclePhase = 6;
      }
      break;

    case 6: // a moving SPLASH-type interactor must emit exactly ONE splash (its water entry) plus a wake
            // stream - a splash ring per 30 Hz movement update is the "one huge expanding disc" production bug,
            // and no gain cvar can fix it because it scales with the emission rate.
    {
      const plUInt64 uiElapsed = uiTick - m_uiPhaseStartTick;
      m_pOwnApplication->SetObjectPosition(m_hProberObject, m_vProberStart + plVec3(static_cast<float>(uiElapsed) * 0.03f, 0.0f, 0.0f));
      if (uiElapsed >= 120)
      {
        plUInt32 uiTotal = 0, uiSplashes = 0;
        m_pOwnApplication->CountInteractionsSince(m_uiInteractionCursor, uiTotal, uiSplashes);
        PL_TEST_BOOL_MSG(uiSplashes == 1,
          "A splash-type interactor moving through water must splash exactly ONCE - on entry (got %u splashes in 2 s). "
          "More means movement updates emit splash rings ~30x/s and the surface tiles into one huge disc of chop.",
          uiSplashes);
        PL_TEST_BOOL_MSG(uiTotal >= 10,
          "A moving interactor must emit a movement-wake stream (got %u events total in 2 s).", uiTotal);
        return plTestAppRun::Quit;
      }
      break;
    }
  }

  // Safety net so a stalled simulation cannot hang the test forever.
  if (m_iFrame > 8000)
  {
    PL_TEST_FAILURE("Water interaction lifecycle test timed out - the simulation tick is not advancing", "");
    return plTestAppRun::Quit;
  }

  return plTestAppRun::Continue;
}

plTestAppRun plGameEngineTestWaterRendering::RunSubTest(plInt32 iIdentifier, plUInt32 uiInvocationCount)
{
  PL_IGNORE_UNUSED(uiInvocationCount);

  if (iIdentifier == SubTests::InteractionDisplacement)
    return RunInteractionDisplacementTest();
  if (iIdentifier == SubTests::TransmittedCaustics)
    return RunProjectedCausticsTest(false);
  if (iIdentifier == SubTests::ReflectedCaustics)
    return RunProjectedCausticsTest(true);
  if (iIdentifier == SubTests::OceanReflectedCausticsSuppressed)
    return RunProjectedCausticsTest(true, false);
  if (iIdentifier == SubTests::LakeJunctionContinuity)
    return RunLakeJunctionContinuityTest();
  if (iIdentifier == SubTests::OceanDebugModes)
    return RunSurfaceDebugModesTest(false);
  if (iIdentifier == SubTests::FiniteDebugModes)
    return RunSurfaceDebugModesTest(true);

  ++m_iFrame;
  m_pOwnApplication->Run();
  if (m_pOwnApplication->WasQuitRequested())
    return plTestAppRun::Quit;

  // Allow component initialization, shader/resource loading and the fixed-step water clock to settle deterministically.
  if (m_iFrame < 12)
    return plTestAppRun::Continue;

  AssertNonBlackWaterImage();

  // Reference comparisons are opt-in until platform baselines are captured. Running the test framework in image-capture
  // mode with PL_WATER_TEST_COMPARE_IMAGES set writes/compares image 0 through the normal GameEngineTest hook.
  if (plEnvironmentVariableUtils::IsVariableSet("PL_WATER_TEST_COMPARE_IMAGES"))
  {
    PL_TEST_IMAGE(0, 350);
  }

  return plTestAppRun::Quit;
}
