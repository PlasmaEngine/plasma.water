#include <WaterTest/WaterTestPCH.h>

#include <Core/Graphics/Geometry.h>
#include <Core/ResourceManager/ResourceManager.h>
#include <Foundation/Configuration/CVar.h>
#include <Foundation/IO/FileSystem/FileSystem.h>
#include <Foundation/IO/FileSystem/FileWriter.h>
#include <Foundation/Types/TagRegistry.h>
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
#include <RendererFoundation/Profiling/Profiling.h>
#include <Texture/Image/ImageConversion.h>
#include <WaterPlugin/Components/WaterBodyComponent.h>
#include <WaterPlugin/Components/WaterJunctionComponent.h>
#include <WaterPlugin/Rendering/WaterQuality.h>
#include <WaterPlugin/Rendering/WaterSurfacePass.h>
#include <WaterPlugin/Resources/WaterProfileResource.h>
#include <WaterPlugin/World/WaterWorldModule.h>
#include <WaterTest/WaterGalleryTest.h>

static plGameEngineTestWaterGallery s_GameEngineTestWaterGallery;

namespace
{
  const plVec3 s_vUp(0.0f, 0.0f, 1.0f);

  // The three presets of the package (Assets/Water/DefaultProfiles), written out because profile assets are not
  // transformed for this test project. Values that are not set here are the descriptor's defaults, as in the assets.
  plWaterProfileResourceDescriptor MakeOceanProfile(float fAmplitude, float fWindSpeed, float fWavelength)
  {
    plWaterProfileResourceDescriptor profile;
    profile.m_SpectrumMode = plWaterWaveSpectrumMode::JONSWAP;
    profile.m_uiGpuCascadeCount = 3;
    profile.m_fWaveAmplitude = fAmplitude;
    profile.m_fWindSpeed = fWindSpeed;
    profile.m_fBaseWavelength = fWavelength;
    profile.m_fWaveSteepness = 0.24f;
    profile.m_vWindDirection = plVec2(0.8f, 0.6f);
    profile.m_fFiniteMeshCellSize = 1.0f;
    profile.m_ShallowColor = plColor(0.10f, 0.55f, 0.52f, 0.55f);
    profile.m_DeepColor = plColor(0.01f, 0.16f, 0.18f, 0.90f);
    profile.m_vAbsorption = plVec3(0.12f, 0.04f, 0.02f);
    profile.m_vScattering = plVec3(0.02f, 0.08f, 0.09f);
    profile.m_fRefractionDepth = 4.0f;
    profile.m_fFoamStrength = 0.4f;
    profile.m_fSunGlitterStrength = 0.5f;
    profile.m_fReflectedCaustics = 0.0f;
    return profile;
  }

  /// The Ocean preset as shipped: amplitude 0.14 m, wind 0.7, wavelength 12 m.
  plWaterProfileResourceDescriptor MakeOceanProfile()
  {
    return MakeOceanProfile(0.14f, 0.7f, 12.0f);
  }

  plWaterProfileResourceDescriptor MakeLakeProfile()
  {
    plWaterProfileResourceDescriptor profile;
    profile.m_SpectrumMode = plWaterWaveSpectrumMode::Analytic;
    profile.m_uiGpuCascadeCount = 2;
    profile.m_fWaveAmplitude = 0.11f;
    profile.m_fBaseWavelength = 8.0f;
    profile.m_fWindSpeed = 1.2f;
    profile.m_fWaveSteepness = 0.16f;
    profile.m_fShoreWaveFadeDistance = 1.5f;
    profile.m_ShallowColor = plColor(0.055f, 0.39f, 0.48f, 0.58f);
    profile.m_DeepColor = plColor(0.008f, 0.065f, 0.12f, 0.96f);
    profile.m_vAbsorption = plVec3(0.24f, 0.055f, 0.018f);
    profile.m_vScattering = plVec3(0.015f, 0.045f, 0.065f);
    profile.m_fRoughness = 0.065f;
    profile.m_fEnvironmentReflectionWeight = 1.15f;
    profile.m_fRefractionDepth = 6.0f;
    profile.m_fFoamStrength = 0.18f;
    profile.m_fSunGlitterStrength = 0.48f;
    return profile;
  }

  plWaterProfileResourceDescriptor MakeRiverProfile()
  {
    plWaterProfileResourceDescriptor profile;
    profile.m_SpectrumMode = plWaterWaveSpectrumMode::Analytic;
    profile.m_uiGpuCascadeCount = 1;
    profile.m_fWaveAmplitude = 0.07f;
    profile.m_fBaseWavelength = 4.5f;
    profile.m_fWindSpeed = 2.0f;
    profile.m_fWaveSteepness = 0.18f;
    profile.m_fFlowScale = 1.35f;
    profile.m_fShoreWaveFadeDistance = 0.75f;
    profile.m_ShallowColor = plColor(0.07f, 0.43f, 0.39f, 0.48f);
    profile.m_DeepColor = plColor(0.012f, 0.12f, 0.13f, 0.88f);
    profile.m_vAbsorption = plVec3(0.18f, 0.045f, 0.025f);
    profile.m_vScattering = plVec3(0.015f, 0.045f, 0.065f);
    profile.m_fEnvironmentReflectionWeight = 0.8f;
    profile.m_fRefractionDepth = 2.5f;
    profile.m_fFoamStrength = 0.28f;
    profile.m_fSunGlitterStrength = 0.12f;
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

  /// Distance from a point to a polyline in the XY plane, and how far along the line (0..1 by length) the closest point is.
  float DistanceToPolyline(plArrayPtr<const plVec3> line, const plVec2& vPoint, float& out_fAlong)
  {
    float fTotal = 0.0f;
    for (plUInt32 i = 0; i + 1 < line.GetCount(); ++i)
      fTotal += (line[i + 1].GetAsVec2() - line[i].GetAsVec2()).GetLength();

    float fBest = plMath::HighValue<float>();
    float fWalked = 0.0f;
    out_fAlong = 0.0f;
    for (plUInt32 i = 0; i + 1 < line.GetCount(); ++i)
    {
      const plVec2 a = line[i].GetAsVec2();
      const plVec2 ab = line[i + 1].GetAsVec2() - a;
      const float fLength = ab.GetLength();
      const float t = plMath::Saturate((vPoint - a).Dot(ab) / plMath::Max(fLength * fLength, 0.0001f));
      const float fDistance = (vPoint - (a + ab * t)).GetLength();
      if (fDistance < fBest)
      {
        fBest = fDistance;
        out_fAlong = (fWalked + fLength * t) / plMath::Max(fTotal, 0.0001f);
      }
      fWalked += fLength;
    }
    return fBest;
  }

  /// The z of a polyline at a position given as a fraction of its XY length.
  float HeightAlongPolyline(plArrayPtr<const plVec3> line, float fAlong)
  {
    float fTotal = 0.0f;
    for (plUInt32 i = 0; i + 1 < line.GetCount(); ++i)
      fTotal += (line[i + 1].GetAsVec2() - line[i].GetAsVec2()).GetLength();

    float fTarget = fAlong * fTotal;
    for (plUInt32 i = 0; i + 1 < line.GetCount(); ++i)
    {
      const float fLength = (line[i + 1].GetAsVec2() - line[i].GetAsVec2()).GetLength();
      if (fTarget <= fLength || i + 2 == line.GetCount())
        return plMath::Lerp(line[i].z, line[i + 1].z, plMath::Saturate(fTarget / plMath::Max(fLength, 0.0001f)));
      fTarget -= fLength;
    }
    return line[line.GetCount() - 1].z;
  }

  /// Rounds the corners of an open polyline by cutting each one three times (Chaikin). The end points stay.
  void RoundCorners(plArrayPtr<const plVec3> line, plDynamicArray<plVec3>& out_line)
  {
    out_line = line;
    for (plUInt32 uiPass = 0; uiPass < 3; ++uiPass)
    {
      plDynamicArray<plVec3> next;
      next.PushBack(out_line[0]);
      for (plUInt32 i = 0; i + 1 < out_line.GetCount(); ++i)
      {
        if (i > 0)
          next.PushBack(plMath::Lerp(out_line[i], out_line[i + 1], 0.25f));
        if (i + 2 < out_line.GetCount())
          next.PushBack(plMath::Lerp(out_line[i], out_line[i + 1], 0.75f));
      }
      next.PushBack(out_line.PeekBack());
      out_line = next;
    }
  }

  /// Signed distance to an axis aligned rectangle in the XY plane: negative inside.
  float DistanceToRect(const plVec2& vMin, const plVec2& vMax, const plVec2& vPoint)
  {
    const plVec2 vCenter = (vMin + vMax) * 0.5f;
    const plVec2 vHalf = (vMax - vMin) * 0.5f;
    const plVec2 d(plMath::Abs(vPoint.x - vCenter.x) - vHalf.x, plMath::Abs(vPoint.y - vCenter.y) - vHalf.y);
    const float fOutside = plVec2(plMath::Max(d.x, 0.0f), plMath::Max(d.y, 0.0f)).GetLength();
    return fOutside + plMath::Min(plMath::Max(d.x, d.y), 0.0f);
  }

  void SetCVarBool(const char* szName, bool bValue)
  {
    if (auto* pCVar = static_cast<plCVarBool*>(plCVar::FindCVarByName(szName)))
    {
      *pCVar = bValue;
    }
    else
    {
      PL_TEST_FAILURE("CVar not found", "%s", szName);
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
  m_TerrainMeshes.Clear();
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
  desc.m_Tags.Set(plTagRegistry::GetGlobalRegistry().RegisterTag("CastShadow"));
  plGameObject* pObject = nullptr;
  m_pWorld->CreateObject(desc, pObject);

  plMeshComponent* pMesh = nullptr;
  m_pWorld->GetOrCreateComponentManager<plMeshComponentManager>()->CreateComponent(pObject, pMesh);
  pMesh->SetMesh(m_hBoxMesh);
  pMesh->SetColor(color);
  return pObject;
}

void plWaterGalleryApplication::AddTerrain(const char* szName, const plVec2& vMin, const plVec2& vMax, float fCellSize, const plDelegate<float(const plVec2&)>& height)
{
  // Two meshes, so that what is near or under the waterline is sand and the rest is grass, without a material of its own.
  const plUInt32 uiCellsX = plMath::Max<plUInt32>(1, static_cast<plUInt32>(plMath::Ceil((vMax.x - vMin.x) / fCellSize)));
  const plUInt32 uiCellsY = plMath::Max<plUInt32>(1, static_cast<plUInt32>(plMath::Ceil((vMax.y - vMin.y) / fCellSize)));

  plDynamicArray<float> heights;
  heights.SetCountUninitialized((uiCellsX + 1) * (uiCellsY + 1));
  for (plUInt32 y = 0; y <= uiCellsY; ++y)
  {
    for (plUInt32 x = 0; x <= uiCellsX; ++x)
    {
      heights[y * (uiCellsX + 1) + x] = height(plVec2(vMin.x + x * fCellSize, vMin.y + y * fCellSize));
    }
  }

  auto heightAt = [&](plInt32 x, plInt32 y)
  {
    x = plMath::Clamp<plInt32>(x, 0, uiCellsX);
    y = plMath::Clamp<plInt32>(y, 0, uiCellsY);
    return heights[y * (uiCellsX + 1) + x];
  };

  plGeometry geometry[2];
  for (plUInt32 y = 0; y < uiCellsY; ++y)
  {
    for (plUInt32 x = 0; x < uiCellsX; ++x)
    {
      const float fMean = 0.25f * (heightAt(x, y) + heightAt(x + 1, y) + heightAt(x, y + 1) + heightAt(x + 1, y + 1));
      plGeometry& geo = geometry[fMean > 0.6f ? 1 : 0];

      plUInt32 quad[4];
      const plInt32 corners[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
      for (plUInt32 c = 0; c < 4; ++c)
      {
        const plInt32 cx = x + corners[c][0];
        const plInt32 cy = y + corners[c][1];
        const plVec3 vNormal = plVec3(heightAt(cx - 1, cy) - heightAt(cx + 1, cy), heightAt(cx, cy - 1) - heightAt(cx, cy + 1), 2.0f * fCellSize).GetNormalized();
        const plVec3 vPosition(vMin.x + cx * fCellSize, vMin.y + cy * fCellSize, heightAt(cx, cy));
        quad[c] = geo.AddVertex(vPosition, vNormal, plVec2(vPosition.x, vPosition.y) * 0.25f);
      }
      geo.AddPolygon(quad, false);
    }
  }

  const plColor colors[2] = {plColor(0.62f, 0.54f, 0.40f), plColor(0.16f, 0.28f, 0.10f)};
  for (plUInt32 i = 0; i < 2; ++i)
  {
    if (geometry[i].GetPolygons().IsEmpty())
      continue;

    geometry[i].TriangulatePolygons();
    geometry[i].ComputeTangents();

    plMeshResourceDescriptor descriptor;
    plMeshBufferResourceDescriptor& meshBuffer = descriptor.MeshBufferDesc();
    meshBuffer.AddCommonStreams();
    meshBuffer.AllocateStreamsFromGeometry(geometry[i], plGALPrimitiveTopology::Triangles);
    descriptor.AddSubMesh(meshBuffer.GetPrimitiveCount(), 0, 0);
    descriptor.SetMaterial(0, "{ 7c71fc2d-2a1a-4938-9305-f3a0f26d73cc }"); // LitStandard
    descriptor.ComputeBounds();

    plStringBuilder sResource;
    sResource.SetFormat("GameEngineTest/WaterGallery/Terrain{}", m_uiTerrainCounter++);
    plMeshResourceHandle hMesh = plResourceManager::CreateResource<plMeshResource>(sResource, std::move(descriptor), "Water gallery terrain");
    m_TerrainMeshes.PushBack(hMesh);

    plGameObjectDesc desc;
    desc.m_sName.Assign(szName);
    desc.m_Tags.Set(plTagRegistry::GetGlobalRegistry().RegisterTag("CastShadow"));
    plGameObject* pObject = nullptr;
    m_pWorld->CreateObject(desc, pObject);
    plMeshComponent* pMesh = nullptr;
    m_pWorld->GetOrCreateComponentManager<plMeshComponentManager>()->CreateComponent(pObject, pMesh);
    pMesh->SetMesh(hMesh);
    pMesh->SetColor(colors[i]);
  }
}

plGameObject* plWaterGalleryApplication::AddWaterBody(const char* szName, plWaterBodyType::Enum type, const plWaterProfileResourceHandle& hProfile, plArrayPtr<const plVec3> splinePoints, bool bClosed, float fHeight)
{
  // The spline points are local to the object. A lake takes its water level from the object, so it is the object that is raised.
  plGameObjectDesc desc;
  desc.m_sName.Assign(szName);
  desc.m_LocalPosition = plVec3(0.0f, 0.0f, fHeight);
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
  // Waves are a function of the water's own clock. Starting it again makes a scene look the same whatever ran before it.
  m_pWorld->GetOrCreateModule<plWaterWorldModule>()->ResetSimulationClock();

  const plColor rock(0.22f, 0.21f, 0.20f);
  const plColor sand(0.62f, 0.54f, 0.40f);
  const plColor grass(0.16f, 0.28f, 0.10f);

  switch (scene)
  {
    case Scene::OpenOcean:
    {
      AddSunAndSky(plVec3(0.55f, 0.35f, 0.42f), 10.0f);
      // Calm is the Ocean preset as shipped. Breeze and Storm raise its waves; nothing else changes.
      const float fAmplitude[] = {0.14f, 0.45f, 1.4f};
      const float fWind[] = {0.7f, 6.0f, 14.0f};
      const float fWavelength[] = {12.0f, 22.0f, 48.0f};
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
      AddWaterBody("Ocean", plWaterBodyType::Ocean, makeProfile(MakeOceanProfile()), {}, false);
      // The waterline is x = 0 and the bed falls towards +x: 6% for y < 0 (the gentle beach), 25% for y > 0 (the steep one).
      AddTerrain("Beach", plVec2(-40.0f, -80.0f), plVec2(160.0f, 80.0f), 1.0f, [](const plVec2& p)
        {
          const float fSlope = plMath::Lerp(0.06f, 0.25f, plMath::SmoothStep(p.y, -4.0f, 4.0f));
          return plMath::Max(-p.x * fSlope, -30.0f); });
      // Posts every 5 m across each waterline, to read distances in the picture. The red one stands on the waterline.
      for (plInt32 i = 0; i < 6; ++i)
      {
        const plColor color = i == 0 ? plColor(0.8f, 0.1f, 0.1f) : plColor(0.9f, 0.9f, 0.9f);
        AddBox("Post", plVec3(i * 5.0f, -30.0f, 0.0f), plVec3(0.25f, 0.25f, 3.0f), color);
        AddBox("Post", plVec3(i * 5.0f, 30.0f, 0.0f), plVec3(0.25f, 0.25f, 3.0f), color);
      }
      break;
    }

    case Scene::ShadedCove:
    {
      // Low sun behind a cliff. The cliff's shadow covers the water between it and the camera.
      AddSunAndSky(plVec3(0.9f, 0.0f, 0.38f), 10.0f);
      AddWaterBody("Ocean", plWaterBodyType::Ocean, makeProfile(MakeOceanProfile()), {}, false);
      AddBox("Seabed", plVec3(0.0f, 0.0f, -6.0f), plVec3(600.0f, 600.0f, 2.0f), sand);
      AddBox("Cliff", plVec3(30.0f, -30.0f, 9.0f), plVec3(8.0f, 60.0f, 30.0f), rock);
      // A floating crate half in shadow, to show where the shadow edge is on an opaque surface.
      AddBox("CrateInShadow", plVec3(12.0f, -20.0f, 0.4f), plVec3(2.0f, 2.0f, 2.0f), plColor(0.7f, 0.5f, 0.2f));
      AddBox("CrateInSun", plVec3(12.0f, 14.0f, 0.4f), plVec3(2.0f, 2.0f, 2.0f), plColor(0.7f, 0.5f, 0.2f));
      break;
    }

    case Scene::RiverNetwork:
    {
      // A lake 3 m above the sea, a river that leaves it, bends once and falls to the coast at x = 40.
      AddSunAndSky(plVec3(0.35f, 0.45f, 0.82f), 10.0f);

      // The river builder rejects a ribbon that folds over itself, and a 4 m river folds in a sharp corner. So the
      // corners are rounded here, and the terrain is carved along the same rounded line.
      const plVec3 riverCorners[] = {plVec3(-28.0f, 0.0f, 3.0f), plVec3(-10.0f, 0.0f, 2.2f), plVec3(10.0f, 10.0f, 1.2f), plVec3(40.0f, 10.0f, 0.0f)};
      plDynamicArray<plVec3> riverLine;
      RoundCorners(riverCorners, riverLine);
      const plVec2 lakeMin(-60.0f, -18.0f);
      const plVec2 lakeMax(-28.0f, 18.0f);

      AddTerrain("Land", plVec2(-90.0f, -60.0f), plVec2(120.0f, 70.0f), 0.5f, [&](const plVec2& p)
        {
          // The land falls from 4.4 m behind the lake to 1 m at the coast, then runs out under the sea.
          float fHeight = plMath::Lerp(4.4f, 1.0f, plMath::Saturate((p.x + 28.0f) / 68.0f));
          fHeight = plMath::Min(fHeight, plMath::Max((40.0f - p.x) * 0.15f, -12.0f));

          // The lake basin: the bed crosses the water level 1 m inside the outline and falls to 6 m.
          const float fLake = DistanceToRect(lakeMin, lakeMax, p);
          fHeight = plMath::Min(fHeight, 3.0f + plMath::Max((fLake + 1.0f) * 0.8f, -6.0f));

          // The river channel: 4 m of water between banks, 2.5 m deep, following the river's own fall.
          float fAlong = 0.0f;
          const float fRiver = DistanceToPolyline(riverLine, p, fAlong);
          const float fSurface = HeightAlongPolyline(riverLine, fAlong);
          fHeight = plMath::Min(fHeight, fSurface + plMath::Max((fRiver - 1.5f) * 1.2f, -2.5f));
          return fHeight; });

      const plVec3 lakeOutline[] = {plVec3(lakeMin.x, lakeMin.y, 0.0f), plVec3(lakeMax.x, lakeMin.y, 0.0f), plVec3(lakeMax.x, lakeMax.y, 0.0f), plVec3(lakeMin.x, lakeMax.y, 0.0f)};
      plGameObject* pLake = AddWaterBody("Lake", plWaterBodyType::Lake, makeProfile(MakeLakeProfile()), lakeOutline, true, 3.0f);
      plGameObject* pRiver = AddWaterBody("River", plWaterBodyType::River, makeProfile(MakeRiverProfile()), riverLine, false);
      plGameObject* pOcean = AddWaterBody("Ocean", plWaterBodyType::Ocean, makeProfile(MakeOceanProfile()), {}, false);

      AddJunction(pRiver, pLake, riverLine[0], false);
      AddJunction(pRiver, pOcean, riverLine.PeekBack(), true);
      break;
    }

    case Scene::RiverBend:
    {
      AddSunAndSky(plVec3(0.35f, 0.45f, 0.82f), 10.0f);
      const plVec3 riverCorners[] = {plVec3(-40.0f, -12.0f, 0.0f), plVec3(-14.0f, -12.0f, 0.0f), plVec3(0.0f, 0.0f, 0.0f), plVec3(14.0f, 12.0f, 0.0f), plVec3(40.0f, 12.0f, 0.0f)};
      plDynamicArray<plVec3> riverLine;
      RoundCorners(riverCorners, riverLine);
      AddTerrain("Land", plVec2(-60.0f, -50.0f), plVec2(60.0f, 50.0f), 0.5f, [&](const plVec2& p)
        {
          float fAlong = 0.0f;
          const float fRiver = DistanceToPolyline(riverLine, p, fAlong);
          return plMath::Min(1.0f, plMath::Max((fRiver - 1.5f) * 1.2f, -2.5f)); });
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
    // The same waterline at three camera pitches (gentle beach), then the steep beach.
    {"GentleGrazing", plVec3(-6.0f, -28.0f, 1.2f), plVec3(20.0f, -30.0f, 0.0f)},
    {"GentleStanding", plVec3(-4.0f, -28.0f, 3.0f), plVec3(10.0f, -30.0f, 0.0f)},
    {"GentleFromAbove", plVec3(6.0f, -29.0f, 22.0f), plVec3(8.0f, -30.0f, 0.0f)},
    {"SteepStanding", plVec3(-4.0f, 32.0f, 3.0f), plVec3(10.0f, 30.0f, 0.0f)},
    {"BothFromAbove", plVec3(6.0f, 0.0f, 70.0f), plVec3(10.0f, 0.5f, 0.0f)},
  };
  static const Shot s_Cove[] = {
    {"FromShade", plVec3(-10.0f, -22.0f, 3.0f), plVec3(20.0f, -18.0f, 0.0f)},
    {"Across", plVec3(-14.0f, -2.0f, 10.0f), plVec3(16.0f, -6.0f, 0.0f)},
    // Water.Debug.JunctionPatches 16: the sun's shadow map looked up at the water surface. White is lit.
    {"Across_SunShadowAtSurface", plVec3(-14.0f, -2.0f, 10.0f), plVec3(16.0f, -6.0f, 0.0f), 16},
    // 17: green where the shadow map covers the surface point, red where it has no page for it.
    {"Across_ShadowMapCoverage", plVec3(-14.0f, -2.0f, 10.0f), plVec3(16.0f, -6.0f, 0.0f), 17},
  };
  static const Shot s_Network[] = {
    {"Overview", plVec3(-10.0f, -75.0f, 60.0f), plVec3(-4.0f, 4.0f, 0.0f)},
    {"LakeMouth", plVec3(-18.0f, -10.0f, 10.0f), plVec3(-28.0f, 0.0f, 3.0f)},
    {"LakeMouthFromLake", plVec3(-42.0f, 6.0f, 7.0f), plVec3(-26.0f, 0.0f, 3.0f)},
    {"LakeFar", plVec3(60.0f, -60.0f, 70.0f), plVec3(-44.0f, 0.0f, 3.0f)},
    {"SeaMouth", plVec3(28.0f, -4.0f, 9.0f), plVec3(42.0f, 10.0f, 0.0f)},
    {"SeaMouthFromSea", plVec3(64.0f, 26.0f, 9.0f), plVec3(40.0f, 10.0f, 0.0f)},
    {"SeaMouthFromAbove", plVec3(40.0f, 9.0f, 40.0f), plVec3(41.0f, 10.0f, 0.0f)},
    // Water.Debug.JunctionPatches 1, the stitch view: a body's own water is dark blue, and where two waters mix the
    // first of the pair is magenta and the second green.
    {"LakeMouth_Stitch", plVec3(-18.0f, -10.0f, 10.0f), plVec3(-28.0f, 0.0f, 3.0f), 1},
    {"SeaMouthFromAbove_Stitch", plVec3(40.0f, 9.0f, 40.0f), plVec3(41.0f, 10.0f, 0.0f), 1},
    {"Overview_Stitch", plVec3(-10.0f, -75.0f, 60.0f), plVec3(-4.0f, 4.0f, 0.0f), 1},
  };
  static const Shot s_Bend[] = {
    {"Overview", plVec3(0.0f, -34.0f, 26.0f), plVec3(0.0f, 0.0f, 0.0f)},
    {"InBend", plVec3(-16.0f, -16.0f, 3.0f), plVec3(0.0f, 0.0f, 0.0f)},
    {"FromAbove", plVec3(0.0f, -1.0f, 30.0f), plVec3(0.0f, 0.0f, 0.0f)},
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
  if (auto* pCVar = static_cast<plCVarInt*>(plCVar::FindCVarByName("Water.Debug.JunctionPatches")))
  {
    *pCVar = shot.m_iDebugMode;
  }

  if (plWaterRenderingTestGameState* pGameState = plDynamicCast<plWaterRenderingTestGameState*>(GetActiveGameState()))
  {
    pGameState->GetMainCamera()->LookAt(shot.m_vPosition, shot.m_vTarget, s_vUp);
  }
}

bool plWaterGalleryApplication::CheckSceneIsValid()
{
  PL_LOCK(m_pWorld->GetReadMarker());
  const plWaterWorldModule* pModule = m_pWorld->GetModuleReadOnly<plWaterWorldModule>();
  if (pModule == nullptr)
    return false;

  bool bValid = true;
  plDynamicArray<plWaterBodySnapshot> bodies;
  pModule->CopyBodySnapshots(bodies);
  for (const plWaterBodySnapshot& body : bodies)
  {
    if (!body.m_bGeometryValid)
    {
      plLog::Error("Water gallery: body {} (type {}) has no valid geometry ({} control points)", body.m_BodyId.m_uiIndex, body.m_Descriptor.m_Type.GetValue(), body.m_Descriptor.m_ControlPoints.GetCount());
      bValid = false;
    }
  }

  plDynamicArray<plWaterJunctionRenderSnapshot> junctions;
  pModule->CopyJunctionRenderSnapshots(junctions);
  for (const plWaterJunctionRenderSnapshot& junction : junctions)
  {
    if (!junction.m_bValid)
    {
      plLog::Error("Water gallery: the junction at ({}, {}, {}) was left out of the mesh: {}", junction.m_Descriptor.m_vCenter.x, junction.m_Descriptor.m_vCenter.y, junction.m_Descriptor.m_vCenter.z, junction.m_sProblem);
      bValid = false;
    }
    else if (junction.m_fMaximumSeamError > 0.0f)
    {
      plLog::Error("Water gallery: the junction at ({}, {}, {}) has a seam: the two meshes differ by {} on the mouth line", junction.m_Descriptor.m_vCenter.x, junction.m_Descriptor.m_vCenter.y, junction.m_Descriptor.m_vCenter.z, junction.m_fMaximumSeamError);
      bValid = false;
    }
  }
  return bValid && !bodies.IsEmpty();
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
  AddSubTest("Tier Timing", SubTests::TierTiming);
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
    case SubTests::TierTiming:
      m_Scene = Scene::RiverNetwork;
      m_Timing.Clear();
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
    case SubTests::TierTiming:
      return RunTierTiming();
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

  if (m_uiShot == 0)
  {
    // A scene that silently lost a body or a junction would still produce pictures.
    PL_TEST_BOOL(m_pOwnApplication->CheckSceneIsValid());
  }

  const auto shots = m_pOwnApplication->GetShots(m_Scene);
  plStringBuilder sName(sPrefix, "_", shots[m_uiShot].m_szName);
  PL_TEST_BOOL(m_pOwnApplication->SaveShot(sName).Succeeded());

  if (++m_uiShot >= shots.GetCount())
  {
    if (auto* pCVar = static_cast<plCVarInt*>(plCVar::FindCVarByName("Water.Debug.JunctionPatches")))
    {
      *pCVar = 0;
    }
    return plTestAppRun::Quit;
  }

  m_pOwnApplication->SetShot(shots[m_uiShot]);
  m_iShotFrame = 0;
  return plTestAppRun::Continue;
}

plTestAppRun plGameEngineTestWaterGallery::RunTierTiming()
{
  // GPU time of a whole frame for two views per quality tier, written to Gallery/TierTiming.csv, at this project's
  // 960 x 540. The numbers compare tiers with each other on this machine; they are not a budget for another one.
  // The wall clock column is there for reference only: it moves by several milliseconds from run to run.
  // The first row is the same frame without the water passes (injections off). The rows under High take one feature
  // out each, which is how the cost of that feature is read off.
  struct Row
  {
    const char* m_szName;
    bool m_bWaterPasses;
    plInt32 m_iTier;
    bool m_bNoTessellation;
    bool m_bNoCaustics;
    bool m_bNoSpectrum;
  };
  static const Row s_Rows[] = {
    {"NoWaterPasses", false, plWaterQualityTier::High, false, false, false},
    {"Low", true, plWaterQualityTier::Low, false, false, false},
    {"Balanced", true, plWaterQualityTier::Balanced, false, false, false},
    {"High", true, plWaterQualityTier::High, false, false, false},
    {"High without tessellation", true, plWaterQualityTier::High, true, false, false},
    {"High without caustics", true, plWaterQualityTier::High, false, true, false},
    {"High without spectrum and ripples", true, plWaterQualityTier::High, false, false, true},
    {"High without all three", true, plWaterQualityTier::High, true, true, true},
    {"Ultra", true, plWaterQualityTier::Ultra, false, false, false},
  };
  static const plWaterGalleryApplication::Shot s_Views[] = {
    {"RiverNetworkOverview", plVec3(-10.0f, -75.0f, 60.0f), plVec3(-4.0f, 4.0f, 0.0f)},
    {"SeaAtEyeLevel", plVec3(70.0f, -20.0f, 1.7f), plVec3(140.0f, 10.0f, 0.5f)},
  };
  constexpr plInt32 iWarmUpFrames = 60;
  constexpr plInt32 iMeasuredFrames = 180;

  const plUInt32 uiStep = static_cast<plUInt32>(m_iPhase); // row * views + view
  const plUInt32 uiTier = uiStep / PL_ARRAY_SIZE(s_Views);
  const plUInt32 uiView = uiStep % PL_ARRAY_SIZE(s_Views);

  auto setRow = [](const Row& row)
  {
    SetCVarBool("Rendering.Pipeline.Injections", row.m_bWaterPasses);
    SetCVarBool("Water.Debug.NoTessellation", row.m_bNoTessellation);
    SetCVarBool("Water.Debug.NoCaustics", row.m_bNoCaustics);
    SetCVarBool("Water.Debug.NoSpectrum", row.m_bNoSpectrum);
    if (auto* pCVar = static_cast<plCVarInt*>(plCVar::FindCVarByName("Water.Quality")))
    {
      *pCVar = row.m_iTier;
    }
  };

  if (m_iShotFrame == 0)
  {
    setRow(s_Rows[uiTier]);
    m_pOwnApplication->SetShot(s_Views[uiView]);
  }

  const plTime start = plTime::Now();
  {
    // Without its pass, water in view makes the component warn once. That row is here on purpose.
    PL_LOG_BLOCK_MUTE();
    m_pOwnApplication->Run();
  }
  const plTime frameTime = plTime::Now() - start;
  if (m_pOwnApplication->WasQuitRequested())
    return plTestAppRun::Quit;

  ++m_iShotFrame;
  if (m_iShotFrame > iWarmUpFrames)
  {
    m_TimingSamples.PushBack(static_cast<float>(frameTime.GetMilliseconds()));
    // The GPU's own time for a whole frame, read back a few frames late. Zero where the backend has no timestamps.
    m_GpuTimingSamples.PushBack(static_cast<float>(plProfilingScopeAndMarker::GetLastFrameGpuTime().GetMilliseconds()));
  }
  if (m_iShotFrame < iWarmUpFrames + iMeasuredFrames)
    return plTestAppRun::Continue;

  // The median, so that a frame that compiled a shader or loaded a texture does not count.
  m_TimingSamples.Sort();
  m_GpuTimingSamples.Sort();
  const float fMedian = m_TimingSamples[m_TimingSamples.GetCount() / 2];
  const float fGpuMedian = m_GpuTimingSamples[m_GpuTimingSamples.GetCount() / 2];
  const float fGpuP90 = m_GpuTimingSamples[(m_GpuTimingSamples.GetCount() * 9) / 10];
  m_Timing.AppendFormat("{},{},{},{},{}\n", s_Rows[uiTier].m_szName, s_Views[uiView].m_szName, plArgF(fGpuMedian, 2), plArgF(fGpuP90, 2), plArgF(fMedian, 2));
  PL_TEST_BOOL(fMedian > 0.0f);

  m_TimingSamples.Clear();
  m_GpuTimingSamples.Clear();
  m_iShotFrame = 0;
  ++m_iPhase;
  if (static_cast<plUInt32>(m_iPhase) < PL_ARRAY_SIZE(s_Rows) * PL_ARRAY_SIZE(s_Views))
    return plTestAppRun::Continue;

  setRow(s_Rows[3]);

  plFileWriter file;
  if (PL_TEST_BOOL(file.Open(":imgout/Gallery/TierTiming.csv").Succeeded()))
  {
    plStringBuilder sText("Tier,View,GpuFrameMedianMs,GpuFrameP90Ms,WallClockFrameMedianMs\n");
    sText.Append(m_Timing.GetView());
    file.WriteBytes(sText.GetData(), sText.GetElementCount()).IgnoreResult();
  }
  return plTestAppRun::Quit;
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
      PL_TEST_BOOL_MSG(fChanged > 0.2f, "Water must be visible through the main pipeline: only %.1f%% of the pixels differ from the frame without injections (mean difference %.4f)", fChanged * 100.0f, fMean);
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
    PL_TEST_BOOL_MSG(fChanged < 0.05f, "With a failing surface pass the frame must be the frame without water: %.1f%% of the pixels differ (mean difference %.4f)", fChanged * 100.0f, fMean);
  }
  if (PL_TEST_BOOL(CompareImages(m_pOwnApplication->GetLastScreenshot(), m_WithWater, fMean, fChanged)))
  {
    PL_TEST_BOOL_MSG(fChanged > 0.2f, "The failing surface pass still drew water (%.1f%% of the pixels differ from the frame with water)", fChanged * 100.0f);
  }

  restore();
  return plTestAppRun::Quit;
}
