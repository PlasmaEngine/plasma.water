#include <WaterTest/WaterTestPCH.h>

#include <Core/Graphics/Geometry.h>
#include <Core/ResourceManager/ResourceManager.h>
#include <Foundation/Configuration/CVar.h>
#include <Foundation/IO/FileSystem/FileSystem.h>
#include <RendererCore/Components/SplineComponent.h>
#include <RendererCore/Lights/AmbientLightComponent.h>
#include <RendererCore/Lights/DirectionalLightComponent.h>
#include <RendererCore/Lights/SkyAtmosphereComponent.h>
#include <RendererCore/Lights/SkyLightComponent.h>
#include <RendererCore/Meshes/MeshComponent.h>
#include <RendererCore/Meshes/MeshResource.h>
#include <RendererCore/Pipeline/RenderPipeline.h>
#include <RendererCore/Pipeline/RenderPipelineInjection.h>
#include <RendererCore/Pipeline/View.h>
#include <RendererCore/RenderWorld/RenderWorld.h>
#include <Texture/Image/ImageConversion.h>
#include <WaterPlugin/Components/WaterBodyComponent.h>
#include <WaterPlugin/Components/WaterJunctionComponent.h>
#include <WaterPlugin/Rendering/WaterSurfacePass.h>
#include <WaterPlugin/Resources/WaterProfileResource.h>
#include <WaterTest/WaterGalleryTest.h>

static plGameEngineTestWaterGallery s_GameEngineTestWaterGallery;

namespace
{
  const plVec3 s_vUp(0.0f, 0.0f, 1.0f);

  /// The package's Ocean preset, as far as the look goes, written out because profile assets are not transformed here.
  plWaterProfileResourceDescriptor MakeOceanProfile(float fAmplitude, float fWindSpeed, float fWavelength)
  {
    plWaterProfileResourceDescriptor profile;
    profile.m_SpectrumMode = plWaterWaveSpectrumMode::JONSWAP;
    profile.m_uiGpuCascadeCount = 3;
    profile.m_fWaveAmplitude = fAmplitude;
    profile.m_fWindSpeed = fWindSpeed;
    profile.m_fBaseWavelength = fWavelength;
    profile.m_fWaveSteepness = 0.35f;
    profile.m_vWindDirection = plVec2(0.8f, 0.6f);
    profile.m_fSunGlitterStrength = 0.5f;
    return profile;
  }

  plWaterProfileResourceDescriptor MakeLakeProfile()
  {
    plWaterProfileResourceDescriptor profile;
    profile.m_fWaveAmplitude = 0.06f;
    profile.m_fBaseWavelength = 6.0f;
    profile.m_fWindSpeed = 1.5f;
    profile.m_ShallowColor = plColor(0.06f, 0.30f, 0.30f, 0.65f);
    profile.m_DeepColor = plColor(0.006f, 0.05f, 0.06f, 0.95f);
    return profile;
  }

  plWaterProfileResourceDescriptor MakeRiverProfile()
  {
    plWaterProfileResourceDescriptor profile;
    profile.m_fWaveAmplitude = 0.04f;
    profile.m_fBaseWavelength = 3.0f;
    profile.m_fWindSpeed = 1.0f;
    profile.m_fFlowScale = 1.0f;
    profile.m_ShallowColor = plColor(0.22f, 0.24f, 0.12f, 0.75f);
    profile.m_DeepColor = plColor(0.07f, 0.07f, 0.03f, 0.96f);
    profile.m_vAbsorption = plVec3(0.30f, 0.22f, 0.40f);
    profile.m_vScattering = plVec3(0.10f, 0.09f, 0.05f);
    return profile;
  }

  /// Mean absolute difference of two screenshots, 0..1, and the fraction of pixels that differ by more than 4/255.
  bool CompareImages(const plImage& a, const plImage& b, float& out_fMean, float& out_fChangedFraction)
  {
    plImage ia, ib;
    if (plImageConversion::Convert(a, ia, plImageFormat::R8G8B8A8_UNORM).Failed() || plImageConversion::Convert(b, ib, plImageFormat::R8G8B8A8_UNORM).Failed())
      return false;
    if (ia.GetWidth() != ib.GetWidth() || ia.GetHeight() != ib.GetHeight() || ia.GetWidth() == 0)
      return false;

    plUInt64 uiSum = 0;
    plUInt64 uiChanged = 0;
    const plUInt64 uiPixels = static_cast<plUInt64>(ia.GetWidth()) * ia.GetHeight();
    for (plUInt32 y = 0; y < ia.GetHeight(); ++y)
    {
      const plUInt8* pa = ia.GetPixelPointer<plUInt8>(0, 0, 0, 0, y);
      const plUInt8* pb = ib.GetPixelPointer<plUInt8>(0, 0, 0, 0, y);
      for (plUInt32 x = 0; x < ia.GetWidth(); ++x)
      {
        plUInt32 uiPixel = 0;
        for (plUInt32 c = 0; c < 3; ++c)
        {
          uiPixel += static_cast<plUInt32>(plMath::Abs(static_cast<plInt32>(pa[x * 4 + c]) - static_cast<plInt32>(pb[x * 4 + c])));
        }
        uiSum += uiPixel;
        uiChanged += uiPixel > 12 ? 1 : 0;
      }
    }

    out_fMean = static_cast<float>(static_cast<double>(uiSum) / (static_cast<double>(uiPixels) * 3.0 * 255.0));
    out_fChangedFraction = static_cast<float>(static_cast<double>(uiChanged) / static_cast<double>(uiPixels));
    return true;
  }

  void SetCVarBool(const char* szName, bool bValue)
  {
    if (auto* pCVar = static_cast<plCVarBool*>(plCVar::FindCVarByName(szName)))
    {
      *pCVar = bValue;
    }
    else
    {
      PL_TEST_FAILURE("CVar not found", "{}", szName);
    }
  }
} // namespace

//////////////////////////////////////////////////////////////////////////

plWaterGalleryApplication::plWaterGalleryApplication()
  : plWaterTestApplication("WaterGallery")
{
}

plUniquePtr<plGameStateBase> plWaterGalleryApplication::CreateGameState()
{
  // The plain test game state plus access to the camera and the view mode. It keeps the project's main pipeline.
  return PL_DEFAULT_NEW(plWaterRenderingTestGameState);
}

void plWaterGalleryApplication::BeforeHighLevelSystemsShutdown()
{
  m_hBoxMesh.Invalidate();
  plWaterTestApplication::BeforeHighLevelSystemsShutdown();
}

plGameObject* plWaterGalleryApplication::AddBox(const char* szName, const plVec3& vCenter, const plVec3& vSize, const plColor& color, const plQuat& qRotation)
{
  if (!m_hBoxMesh.IsValid())
  {
    plGeometry geometry;
    geometry.AddBox(plVec3(1.0f), true);
    geometry.ComputeTangents();

    plMeshResourceDescriptor descriptor;
    plMeshBufferResourceDescriptor& meshBuffer = descriptor.MeshBufferDesc();
    meshBuffer.AddCommonStreams();
    meshBuffer.AllocateStreamsFromGeometry(geometry, plGALPrimitiveTopology::Triangles);
    descriptor.AddSubMesh(meshBuffer.GetPrimitiveCount(), 0, 0);
    descriptor.SetMaterial(0, "{ 7c71fc2d-2a1a-4938-9305-f3a0f26d73cc }"); // LitStandard
    descriptor.ComputeBounds();
    m_hBoxMesh = plResourceManager::GetOrCreateResource<plMeshResource>("GameEngineTest/WaterGallery/Box", std::move(descriptor), "Water gallery box");
  }

  plGameObjectDesc desc;
  desc.m_sName.Assign(szName);
  desc.m_LocalPosition = vCenter;
  desc.m_LocalScaling = vSize;
  desc.m_LocalRotation = qRotation;
  plGameObject* pObject = nullptr;
  m_pWorld->CreateObject(desc, pObject);

  plMeshComponent* pMesh = nullptr;
  m_pWorld->GetOrCreateComponentManager<plMeshComponentManager>()->CreateComponent(pObject, pMesh);
  pMesh->SetMesh(m_hBoxMesh);
  pMesh->SetColor(color);
  return pObject;
}

plGameObject* plWaterGalleryApplication::AddWaterBody(const char* szName, plWaterBodyType::Enum type, const plWaterProfileResourceHandle& hProfile, plArrayPtr<const plVec3> splinePoints, bool bClosed)
{
  plGameObjectDesc desc;
  desc.m_sName.Assign(szName);
  plGameObject* pObject = nullptr;
  m_pWorld->CreateObject(desc, pObject);

  if (!splinePoints.IsEmpty())
  {
    plSpline spline;
    spline.m_bClosed = bClosed;
    for (const plVec3& position : splinePoints)
    {
      auto& point = spline.m_ControlPoints.ExpandAndGetRef();
      point.SetPosition(plSimdVec4f(position.x, position.y, position.z, 1.0f));
      point.SetTangentModeIn(plSplineTangentMode::Linear);
      point.SetTangentModeOut(plSplineTangentMode::Linear);
    }
    spline.CalculateUpDirAndAutoTangents();
    ++spline.m_uiChangeCounter;
    plSplineComponent* pSpline = nullptr;
    plSplineComponent::CreateComponent(pObject, pSpline);
    pSpline->SetSpline(std::move(spline));
  }

  plWaterBodyComponent* pWater = nullptr;
  m_pWorld->GetOrCreateComponentManager<plWaterBodyComponentManager>()->CreateComponent(pObject, pWater);
  pWater->SetBodyType(type);
  pWater->SetProfile(hProfile);
  if (type == plWaterBodyType::Ocean)
  {
    pWater->SetDefaultDepth(40.0f);
    pWater->SetOceanExtent(2048.0f);
    pWater->SetOceanPatchSize(32.0f);
    pWater->SetOceanRingCount(6);
    pWater->SetPatchResolution(64);
  }
  else if (type == plWaterBodyType::River)
  {
    pWater->SetDefaultWidth(4.0f);
    pWater->SetDefaultDepth(2.5f);
    pWater->SetFlowSpeed(1.5f);
  }
  else
  {
    pWater->SetDefaultDepth(6.0f);
  }
  return pObject;
}

void plWaterGalleryApplication::AddJunction(plGameObject* pRiver, plGameObject* pOther, const plVec3& vPosition, bool bRiverEnd)
{
  plGameObjectDesc desc;
  desc.m_sName.Assign("Junction");
  desc.m_LocalPosition = vPosition;
  plGameObject* pObject = nullptr;
  m_pWorld->CreateObject(desc, pObject);

  plWaterJunctionComponent* pJunction = nullptr;
  m_pWorld->GetOrCreateComponentManager<plWaterJunctionComponentManager>()->CreateComponent(pObject, pJunction);
  pJunction->SetBodyA(pRiver->GetHandle());
  pJunction->SetBodyB(pOther->GetHandle());
  pJunction->SetAnchorA(bRiverEnd ? plWaterAttachmentAnchor::RiverEnd : plWaterAttachmentAnchor::RiverStart);
}

void plWaterGalleryApplication::AddSunAndSky(const plVec3& vToSun, float fLux)
{
  plGameObjectDesc sunDesc;
  sunDesc.m_sName.Assign("Sun");
  sunDesc.m_LocalRotation = plQuat::MakeShortestRotation(plVec3(-1.0f, 0.0f, 0.0f), vToSun.GetNormalized());
  plGameObject* pSunObject = nullptr;
  m_pWorld->CreateObject(sunDesc, pSunObject);
  plDirectionalLightComponent* pSun = nullptr;
  plDirectionalLightComponent::CreateComponent(pSunObject, pSun);
  pSun->SetIntensity(fLux);
  pSun->SetCastShadows(true);

  plGameObjectDesc skyDesc;
  skyDesc.m_sName.Assign("Sky");
  plGameObject* pSkyObject = nullptr;
  m_pWorld->CreateObject(skyDesc, pSkyObject);
  plSkyAtmosphereComponent* pAtmosphere = nullptr;
  plSkyAtmosphereComponent::CreateComponent(pSkyObject, pAtmosphere);
  plSkyLightComponent* pSkyLight = nullptr;
  plSkyLightComponent::CreateComponent(pSkyObject, pSkyLight);
}

plResult plWaterGalleryApplication::SetupScene(Scene scene, plUInt32 uiVariant)
{
  plWaterRenderingTestGameState* pGameState = plDynamicCast<plWaterRenderingTestGameState*>(GetActiveGameState());
  if (pGameState == nullptr)
    return PL_FAILURE;

  auto makeProfile = [this](plWaterProfileResourceDescriptor&& profile)
  {
    profile.Sanitize();
    plStringBuilder sName;
    sName.SetFormat("GameEngineTest/WaterGallery/Profile{}", m_uiProfileCounter++);
    return plResourceManager::CreateResource<plWaterProfileResource>(sName, std::move(profile), "Water gallery profile");
  };

  PL_LOCK(m_pWorld->GetWriteMarker());
  m_pWorld->Clear();
  m_pWorld->GetRandomNumberGenerator().Initialize(0x57415445u);
  m_pWorld->GetClock().Reset(false);

  const plColor rock(0.22f, 0.21f, 0.20f);
  const plColor sand(0.62f, 0.54f, 0.40f);
  const plColor grass(0.16f, 0.28f, 0.10f);

  switch (scene)
  {
    case Scene::OpenOcean:
    {
      AddSunAndSky(plVec3(0.55f, 0.35f, 0.42f), 10.0f);
      const float fAmplitude[] = {0.12f, 0.45f, 1.4f};
      const float fWind[] = {2.0f, 6.0f, 14.0f};
      const float fWavelength[] = {10.0f, 22.0f, 48.0f};
      const plUInt32 v = plMath::Min<plUInt32>(uiVariant, 2);
      AddWaterBody("Ocean", plWaterBodyType::Ocean, makeProfile(MakeOceanProfile(fAmplitude[v], fWind[v], fWavelength[v])), {}, false);
      AddBox("Seabed", plVec3(0.0f, 0.0f, -41.0f), plVec3(4000.0f, 4000.0f, 2.0f), sand);
      AddBox("RockA", plVec3(34.0f, 6.0f, 0.5f), plVec3(7.0f, 5.0f, 9.0f), rock, plQuat::MakeFromAxisAndAngle(s_vUp, plAngle::MakeFromDegree(25.0f)));
      AddBox("RockB", plVec3(60.0f, -22.0f, 0.0f), plVec3(5.0f, 8.0f, 5.0f), rock, plQuat::MakeFromAxisAndAngle(s_vUp, plAngle::MakeFromDegree(-12.0f)));
      break;
    }

    case Scene::Beach:
    {
      AddSunAndSky(plVec3(0.35f, 0.45f, 0.82f), 10.0f);
      AddWaterBody("Ocean", plWaterBodyType::Ocean, makeProfile(MakeOceanProfile(0.10f, 2.0f, 9.0f)), {}, false);
      // A slope of 12% that leaves the water at x = 0 and falls towards +x.
      const float fSlope = plMath::ATan(0.12f).GetRadian();
      AddBox("Sand", plVec3(40.0f, 0.0f, -40.0f * 0.12f - 0.5f), plVec3(200.0f, 400.0f, 1.0f), sand, plQuat::MakeFromAxisAndAngle(plVec3(0.0f, 1.0f, 0.0f), plAngle::MakeFromRadian(fSlope)));
      // Markers every 5 m across the waterline, to read distances in the picture.
      for (plInt32 i = 0; i < 6; ++i)
      {
        AddBox("Post", plVec3(i * 5.0f, -6.0f, 0.0f), plVec3(0.25f, 0.25f, 3.0f), i == 0 ? plColor(0.8f, 0.1f, 0.1f) : plColor(0.9f, 0.9f, 0.9f));
      }
      break;
    }

    case Scene::ShadedCove:
    {
      // Low sun behind a cliff. The cliff's shadow covers the water between it and the camera.
      AddSunAndSky(plVec3(0.9f, 0.0f, 0.38f), 10.0f);
      AddWaterBody("Ocean", plWaterBodyType::Ocean, makeProfile(MakeOceanProfile(0.10f, 2.0f, 9.0f)), {}, false);
      AddBox("Seabed", plVec3(0.0f, 0.0f, -6.0f), plVec3(600.0f, 600.0f, 2.0f), sand);
      AddBox("Cliff", plVec3(30.0f, -30.0f, 9.0f), plVec3(8.0f, 60.0f, 30.0f), rock);
      // A floating crate half in shadow, to show where the shadow edge is on an opaque surface.
      AddBox("CrateInShadow", plVec3(12.0f, -20.0f, 0.4f), plVec3(2.0f, 2.0f, 2.0f), plColor(0.7f, 0.5f, 0.2f));
      AddBox("CrateInSun", plVec3(12.0f, 14.0f, 0.4f), plVec3(2.0f, 2.0f, 2.0f), plColor(0.7f, 0.5f, 0.2f));
      break;
    }

    case Scene::RiverNetwork:
    {
      AddSunAndSky(plVec3(0.35f, 0.45f, 0.82f), 10.0f);
      AddBox("Ground", plVec3(-30.0f, 0.0f, -4.0f), plVec3(140.0f, 200.0f, 2.0f), grass);
      AddBox("Seabed", plVec3(140.0f, 0.0f, -12.0f), plVec3(200.0f, 400.0f, 2.0f), sand);

      const plVec3 lakeOutline[] = {plVec3(-60.0f, -18.0f, 0.0f), plVec3(-28.0f, -18.0f, 0.0f), plVec3(-28.0f, 18.0f, 0.0f), plVec3(-60.0f, 18.0f, 0.0f)};
      plGameObject* pLake = AddWaterBody("Lake", plWaterBodyType::Lake, makeProfile(MakeLakeProfile()), lakeOutline, true);

      // Leaves the lake's east shore, bends once and reaches the sea at x = 40.
      const plVec3 riverLine[] = {plVec3(-28.0f, 0.0f, 0.0f), plVec3(-10.0f, 0.0f, 0.0f), plVec3(10.0f, 10.0f, 0.0f), plVec3(40.0f, 10.0f, 0.0f)};
      plGameObject* pRiver = AddWaterBody("River", plWaterBodyType::River, makeProfile(MakeRiverProfile()), riverLine, false);

      plGameObject* pOcean = AddWaterBody("Ocean", plWaterBodyType::Ocean, makeProfile(MakeOceanProfile(0.25f, 4.0f, 14.0f)), {}, false);
      // The sea is everywhere at z = 0; the land in front of x = 40 hides it.
      AddBox("Land", plVec3(-30.0f, 0.0f, -1.6f), plVec3(140.0f, 200.0f, 3.0f), grass);

      AddJunction(pRiver, pLake, plVec3(-28.0f, 0.0f, 0.0f), false);
      AddJunction(pRiver, pOcean, plVec3(40.0f, 10.0f, 0.0f), true);
      break;
    }

    case Scene::RiverBend:
    {
      AddSunAndSky(plVec3(0.35f, 0.45f, 0.82f), 10.0f);
      AddBox("Ground", plVec3(0.0f, 0.0f, -4.0f), plVec3(200.0f, 200.0f, 2.0f), grass);
      const plVec3 riverLine[] = {plVec3(-40.0f, -12.0f, 0.0f), plVec3(-14.0f, -12.0f, 0.0f), plVec3(0.0f, 0.0f, 0.0f), plVec3(14.0f, 12.0f, 0.0f), plVec3(40.0f, 12.0f, 0.0f)};
      AddWaterBody("River", plWaterBodyType::River, makeProfile(MakeRiverProfile()), riverLine, false);
      break;
    }
  }

  pGameState->SetViewRenderMode(plViewRenderMode::None);
  pGameState->GetMainCamera()->SetCameraMode(plCameraMode::PerspectiveFixedFovY, 55.0f, 0.1f, 4000.0f);
  SetShot(GetShots(scene)[0]);
  return PL_SUCCESS;
}

plArrayPtr<const plWaterGalleryApplication::Shot> plWaterGalleryApplication::GetShots(Scene scene) const
{
  static const Shot s_Ocean[] = {
    {"Low", plVec3(0.0f, 0.0f, 1.7f), plVec3(40.0f, 4.0f, 0.5f)},
    {"Deck", plVec3(-6.0f, -4.0f, 12.0f), plVec3(40.0f, 4.0f, 0.0f)},
    {"High", plVec3(-40.0f, -30.0f, 120.0f), plVec3(40.0f, 4.0f, 0.0f)},
  };
  static const Shot s_Beach[] = {
    {"Grazing", plVec3(-6.0f, 0.0f, 1.2f), plVec3(20.0f, 0.0f, 0.0f)},
    {"Standing", plVec3(-4.0f, 0.0f, 3.0f), plVec3(10.0f, 0.0f, 0.0f)},
    {"Steep", plVec3(4.0f, 0.0f, 16.0f), plVec3(8.0f, 0.0f, 0.0f)},
  };
  static const Shot s_Cove[] = {
    {"FromShade", plVec3(-10.0f, -22.0f, 3.0f), plVec3(20.0f, -18.0f, 0.0f)},
    {"Across", plVec3(-14.0f, -2.0f, 10.0f), plVec3(16.0f, -6.0f, 0.0f)},
  };
  static const Shot s_Network[] = {
    {"Overview", plVec3(-20.0f, -70.0f, 60.0f), plVec3(0.0f, 4.0f, 0.0f)},
    {"LakeMouth", plVec3(-18.0f, -12.0f, 9.0f), plVec3(-28.0f, 0.0f, 0.0f)},
    {"SeaMouth", plVec3(30.0f, -6.0f, 9.0f), plVec3(42.0f, 10.0f, 0.0f)},
    {"SeaMouthFromSea", plVec3(64.0f, 26.0f, 9.0f), plVec3(40.0f, 10.0f, 0.0f)},
  };
  static const Shot s_Bend[] = {
    {"Overview", plVec3(0.0f, -34.0f, 26.0f), plVec3(0.0f, 0.0f, 0.0f)},
    {"InBend", plVec3(-12.0f, -16.0f, 4.0f), plVec3(2.0f, 2.0f, 0.0f)},
  };

  switch (scene)
  {
    case Scene::OpenOcean:
      return s_Ocean;
    case Scene::Beach:
      return s_Beach;
    case Scene::ShadedCove:
      return s_Cove;
    case Scene::RiverNetwork:
      return s_Network;
    case Scene::RiverBend:
      return s_Bend;
  }
  return {};
}

void plWaterGalleryApplication::SetShot(const Shot& shot)
{
  if (plWaterRenderingTestGameState* pGameState = plDynamicCast<plWaterRenderingTestGameState*>(GetActiveGameState()))
  {
    pGameState->GetMainCamera()->LookAt(shot.m_vPosition, shot.m_vTarget, s_vUp);
  }
}

plResult plWaterGalleryApplication::SaveShot(plStringView sName)
{
  plStringBuilder sPath;
  sPath.SetFormat(":imgout/Gallery/{}.png", sName);
  return GetLastScreenshot().SaveTo(sPath);
}

//////////////////////////////////////////////////////////////////////////

const char* plGameEngineTestWaterGallery::GetTestName() const
{
  return "Water Gallery";
}

plGameEngineTestApplication* plGameEngineTestWaterGallery::CreateApplication()
{
  m_pOwnApplication = PL_DEFAULT_NEW(plWaterGalleryApplication);
  return m_pOwnApplication;
}

void plGameEngineTestWaterGallery::SetupSubTests()
{
  AddSubTest("Main Pipeline Draws Water", SubTests::MainPipelineDrawsWater);
  AddSubTest("Open Ocean Calm", SubTests::OpenOceanCalm);
  AddSubTest("Open Ocean Breeze", SubTests::OpenOceanBreeze);
  AddSubTest("Open Ocean Storm", SubTests::OpenOceanStorm);
  AddSubTest("Beach", SubTests::Beach);
  AddSubTest("Shaded Cove", SubTests::ShadedCove);
  AddSubTest("River Network", SubTests::RiverNetwork);
  AddSubTest("River Bend", SubTests::RiverBend);
}

plResult plGameEngineTestWaterGallery::InitializeSubTest(plInt32 iIdentifier)
{
  PL_SUCCEED_OR_RETURN(SUPER::InitializeSubTest(iIdentifier));

  m_iFrame = -1;
  m_uiShot = 0;
  m_iShotFrame = 0;
  m_iPhase = 0;

  using Scene = plWaterGalleryApplication::Scene;
  plUInt32 uiVariant = 0;
  switch (iIdentifier)
  {
    case SubTests::MainPipelineDrawsWater:
      m_Scene = Scene::OpenOcean;
      uiVariant = 1;
      break;
    case SubTests::OpenOceanCalm:
      m_Scene = Scene::OpenOcean;
      uiVariant = 0;
      break;
    case SubTests::OpenOceanBreeze:
      m_Scene = Scene::OpenOcean;
      uiVariant = 1;
      break;
    case SubTests::OpenOceanStorm:
      m_Scene = Scene::OpenOcean;
      uiVariant = 2;
      break;
    case SubTests::Beach:
      m_Scene = Scene::Beach;
      break;
    case SubTests::ShadedCove:
      m_Scene = Scene::ShadedCove;
      break;
    case SubTests::RiverNetwork:
      m_Scene = Scene::RiverNetwork;
      break;
    case SubTests::RiverBend:
      m_Scene = Scene::RiverBend;
      break;
    default:
      return PL_FAILURE;
  }
  return m_pOwnApplication->SetupScene(m_Scene, uiVariant);
}

plTestAppRun plGameEngineTestWaterGallery::RunSubTest(plInt32 iIdentifier, plUInt32 uiInvocationCount)
{
  ++m_iFrame;

  switch (iIdentifier)
  {
    case SubTests::MainPipelineDrawsWater:
      return RunMainPipelineDrawsWater();
    case SubTests::OpenOceanCalm:
      return RunShots("OceanCalm");
    case SubTests::OpenOceanBreeze:
      return RunShots("OceanBreeze");
    case SubTests::OpenOceanStorm:
      return RunShots("OceanStorm");
    case SubTests::Beach:
      return RunShots("Beach");
    case SubTests::ShadedCove:
      return RunShots("Cove");
    case SubTests::RiverNetwork:
      return RunShots("Network");
    case SubTests::RiverBend:
      return RunShots("Bend");
  }
  return plTestAppRun::Quit;
}

plTestAppRun plGameEngineTestWaterGallery::RunShots(plStringView sPrefix)
{
  m_pOwnApplication->Run();
  if (m_pOwnApplication->WasQuitRequested())
    return plTestAppRun::Quit;

  // The first shot waits for shaders, the sky probe and auto exposure. Later shots only for exposure and TAA.
  const plInt32 iSettleFrames = m_uiShot == 0 ? 90 : 40;
  if (++m_iShotFrame < iSettleFrames)
    return plTestAppRun::Continue;

  const auto shots = m_pOwnApplication->GetShots(m_Scene);
  plStringBuilder sName(sPrefix, "_", shots[m_uiShot].m_szName);
  PL_TEST_BOOL(m_pOwnApplication->SaveShot(sName).Succeeded());

  if (++m_uiShot >= shots.GetCount())
    return plTestAppRun::Quit;

  m_pOwnApplication->SetShot(shots[m_uiShot]);
  m_iShotFrame = 0;
  return plTestAppRun::Continue;
}

plTestAppRun plGameEngineTestWaterGallery::RunMainPipelineDrawsWater()
{
  // Nothing in this test touches a render pipeline. Water has to be in the frame because the plugin registered its
  // passes; it has to leave the frame when injections are switched off; and when the surface pass fails, the frame
  // has to be the frame without water, not a black one.
  const plInt32 iSettleFrames = m_iPhase == 0 ? 90 : 45;

  if (m_iPhase == 2)
  {
    // The engine logs the bypass of the failing pass. That is what this phase is for.
    PL_LOG_BLOCK_MUTE();
    m_pOwnApplication->Run();
  }
  else
  {
    m_pOwnApplication->Run();
  }
  if (m_pOwnApplication->WasQuitRequested())
    return plTestAppRun::Quit;

  if (++m_iShotFrame < iSettleFrames)
    return plTestAppRun::Continue;
  m_iShotFrame = 0;

  auto restore = []()
  {
    SetCVarBool("Rendering.Pipeline.Injections", true);
    SetCVarBool("Water.Debug.FailSurfacePass", false);
  };

  if (m_iPhase == 0)
  {
    // The view's pipeline holds a water surface pass that the engine placed.
    {
      plView* pView = nullptr;
      const plViewHandle hView = plRenderWorld::GetMainViews().IsEmpty() ? plViewHandle() : plRenderWorld::GetMainViews()[0];
      bool bInjected = false;
      if (plRenderWorld::TryGetView(hView, pView) && pView->GetRenderPipeline() != nullptr)
      {
        plHybridArray<plRenderPipelinePass*, 64> passes;
        pView->GetRenderPipeline()->GetPasses(passes);
        for (plRenderPipelinePass* pPass : passes)
        {
          bInjected |= pPass->IsInstanceOf<plWaterSurfacePass>() && pView->GetRenderPipeline()->IsPassInjected(pPass);
        }
      }
      PL_TEST_BOOL_MSG(bInjected, "The main view's pipeline has no injected water surface pass");
    }

    m_WithWater.ResetAndCopy(m_pOwnApplication->GetLastScreenshot());
    PL_TEST_BOOL(m_pOwnApplication->SaveShot("MainPipeline_Water").Succeeded());
    SetCVarBool("Rendering.Pipeline.Injections", false);
    m_iPhase = 1;
    return plTestAppRun::Continue;
  }

  if (m_iPhase == 1)
  {
    m_WithoutInjections.ResetAndCopy(m_pOwnApplication->GetLastScreenshot());
    PL_TEST_BOOL(m_pOwnApplication->SaveShot("MainPipeline_InjectionsOff").Succeeded());

    float fMean = 0.0f, fChanged = 0.0f;
    if (PL_TEST_BOOL(CompareImages(m_WithWater, m_WithoutInjections, fMean, fChanged)))
    {
      PL_TEST_BOOL_MSG(fChanged > 0.2f, "Water must be visible through the main pipeline: only {}% of the pixels differ from the frame without injections (mean difference {})", fChanged * 100.0f, fMean);
    }

    SetCVarBool("Rendering.Pipeline.Injections", true);
    SetCVarBool("Water.Debug.FailSurfacePass", true);
    m_iPhase = 2;
    return plTestAppRun::Continue;
  }

  PL_TEST_BOOL(m_pOwnApplication->SaveShot("MainPipeline_SurfacePassFails").Succeeded());

  float fMean = 0.0f, fChanged = 0.0f;
  if (PL_TEST_BOOL(CompareImages(m_pOwnApplication->GetLastScreenshot(), m_WithoutInjections, fMean, fChanged)))
  {
    // Clouds and exposure drift a little between the two captures; a lost frame would differ everywhere.
    PL_TEST_BOOL_MSG(fChanged < 0.05f, "With a failing surface pass the frame must be the frame without water: {}% of the pixels differ (mean difference {})", fChanged * 100.0f, fMean);
  }
  if (PL_TEST_BOOL(CompareImages(m_pOwnApplication->GetLastScreenshot(), m_WithWater, fMean, fChanged)))
  {
    PL_TEST_BOOL_MSG(fChanged > 0.2f, "The failing surface pass still drew water ({}% of the pixels differ from the frame with water)", fChanged * 100.0f);
  }

  restore();
  return plTestAppRun::Quit;
}
