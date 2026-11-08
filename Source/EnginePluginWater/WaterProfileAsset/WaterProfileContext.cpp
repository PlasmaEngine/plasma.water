#include <EnginePluginWater/EnginePluginWaterPCH.h>

#include <EnginePluginWater/WaterProfileAsset/WaterProfileContext.h>
#include <EnginePluginWater/WaterProfileAsset/WaterProfileView.h>

#include <EditorEngineProcessFramework/EngineProcess/EngineProcessMessages.h>
#include <Foundation/Utilities/ConversionUtils.h>
#include <GameEngine/Interfaces/WaterWorldModule.h>
#include <RendererCore/Components/SkyBoxComponent.h>
#include <RendererCore/Components/SplineComponent.h>
#include <RendererCore/Lights/DirectionalLightComponent.h>
#include <RendererCore/Lights/SkyLightComponent.h>
#include <RendererCore/Meshes/MeshComponent.h>
#include <RendererCore/Meshes/MeshResource.h>
#include <WaterPlugin/Components/WaterBodyComponent.h>

PL_STATICLINK_FILE(EnginePluginWater, EnginePluginWater_WaterProfileAsset_WaterProfileContext);

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plWaterProfileContext, 1, plRTTIDefaultAllocator<plWaterProfileContext>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_CONSTANT_PROPERTY("DocumentType", (const char*) "WaterProfile"),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plWaterProfileContext::plWaterProfileContext()
  : plEngineProcessDocumentContext(plEngineProcessDocumentContextFlags::CreateWorld)
{
}

void plWaterProfileContext::HandleMessage(const plEditorEngineDocumentMsg* pMsg)
{
  if (const auto* pConfig = plDynamicCast<const plDocumentConfigMsgToEngine*>(pMsg))
  {
    if (pConfig->m_sWhatToDo == "SetWaterBodyType")
    {
      ConfigureBody(pConfig->m_iValue);
      return;
    }
    if (pConfig->m_sWhatToDo == "SetWaterQuality")
    {
      m_iPreviewQuality = plMath::Clamp(pConfig->m_iValue, 0, 3);
      for (plWaterProfileViewContext* pViewContext : m_ProfileViews)
        pViewContext->SetQuality(m_iPreviewQuality);
      return;
    }
    if (pConfig->m_sWhatToDo == "SetWaterFrozen")
    {
      if (m_pWorld != nullptr)
        m_pWorld->GetClock().SetPaused(pConfig->m_iValue != 0);
      return;
    }
    if (pConfig->m_sWhatToDo == "SetWaterScene")
    {
      ConfigureScene(pConfig->m_iValue);
      return;
    }
    if (pConfig->m_sWhatToDo == "SetWaterSunElevation")
    {
      SetSunElevation(pConfig->m_iValue);
      return;
    }
  }

  if (const auto* pSimpleConfig = plDynamicCast<const plSimpleDocumentConfigMsgToEngine*>(pMsg))
  {
    if (pSimpleConfig->m_sWhatToDo == "WaterInteract" && m_pWorld != nullptr && pSimpleConfig->m_PayloadValue.CanConvertTo<plVec3>())
    {
      TriggerInteraction(pSimpleConfig->m_PayloadValue.ConvertTo<plVec3>(), pSimpleConfig->m_sPayload == "Continuous");
      return;
    }
  }

  plEngineProcessDocumentContext::HandleMessage(pMsg);
}

void plWaterProfileContext::OnInitialize()
{
  PL_LOCK(m_pWorld->GetWriteMarker());

  // A textured opaque floor makes absorption, refraction and reflection behavior readable in the real water pass.
  {
    const char* szMeshName = "WaterProfilePreviewFloorMesh";
    plMeshResourceHandle hMesh = plResourceManager::GetExistingResource<plMeshResource>(szMeshName);
    if (!hMesh.IsValid())
    {
      plGeometry geometry;
      geometry.AddBox(plVec3(40.0f, 40.0f, 0.5f), true);
      geometry.ComputeTangents();

      plMeshBufferResourceDescriptor bufferDesc;
      bufferDesc.AddCommonStreams();
      bufferDesc.AllocateStreamsFromGeometry(geometry, plGALPrimitiveTopology::Triangles);
      const plMeshBufferResourceHandle hBuffer = plResourceManager::GetOrCreateResource<plMeshBufferResource>("WaterProfilePreviewFloorBuffer", std::move(bufferDesc));

      plResourceLock<plMeshBufferResource> pBuffer(hBuffer, plResourceAcquireMode::BlockTillLoaded);
      plMeshResourceDescriptor meshDesc;
      meshDesc.UseExistingMeshBuffer(hBuffer);
      meshDesc.AddSubMesh(pBuffer->GetPrimitiveCount(), 0, 0);
      meshDesc.SetMaterial(0, "Materials/Common/TestBricks.plMaterial");
      meshDesc.ComputeBounds();
      hMesh = plResourceManager::GetOrCreateResource<plMeshResource>(szMeshName, std::move(meshDesc));
    }

    plGameObjectDesc objectDesc;
    objectDesc.m_sName.Assign("WaterPreviewFloor");
    objectDesc.m_LocalPosition.Set(0, 0, -1.0f);
    objectDesc.m_bDynamic = true;
    plGameObject* pFloor = nullptr;
    m_hFloor = m_pWorld->CreateObject(objectDesc, pFloor);
    plMeshComponent* pMesh = nullptr;
    plMeshComponent::CreateComponent(pFloor, pMesh);
    pMesh->SetMesh(hMesh);
  }

  {
    plGameObjectDesc objectDesc;
    objectDesc.m_sName.Assign("WaterPreviewSkyLight");
    plGameObject* pObject = nullptr;
    m_pWorld->CreateObject(objectDesc, pObject);
    plSkyLightComponent* pSkyLight = nullptr;
    plSkyLightComponent::CreateComponent(pObject, pSkyLight);
    pSkyLight->SetCubeMapFile("{ 0b202e08-a64f-465d-b38e-15b81d161822 }");
    pSkyLight->SetReflectionProbeMode(plReflectionProbeMode::Static);
    // The preview lighting is unitless and tuned by eye: PI gives the sky the fill it was tuned with.
    pSkyLight->SetDiffuseIntensity(plMath::Pi<float>());
  }

  {
    plGameObjectDesc objectDesc;
    objectDesc.m_sName.Assign("WaterPreviewSun");
    objectDesc.m_LocalRotation = plQuat::MakeFromEulerAngles(plAngle::MakeFromDegree(-25.0f), plAngle::MakeFromDegree(35.0f), plAngle::MakeFromDegree(120.0f));
    objectDesc.m_bDynamic = true;
    plGameObject* pObject = nullptr;
    m_hSun = m_pWorld->CreateObject(objectDesc, pObject);
    plDirectionalLightComponent* pLight = nullptr;
    plDirectionalLightComponent::CreateComponent(pObject, pLight);
    pLight->SetIntensity(8.0f);
  }

  {
    plGameObjectDesc objectDesc;
    objectDesc.m_sName.Assign("WaterProfilePreviewBody");
    plGameObject* pBodyObject = nullptr;
    m_pWorld->CreateObject(objectDesc, pBodyObject);

    plSplineComponent::CreateComponent(pBodyObject, m_pSpline);
    plWaterBodyComponent::CreateComponent(pBodyObject, m_pWaterBody);

    plStringBuilder sProfileGuid;
    plConversionUtils::ToString(GetDocumentGuid(), sProfileGuid);
    m_pWaterBody->SetProfile(plResourceManager::LoadResource<plWaterProfileResource>(sProfileGuid));
    m_pWaterBody->SetOceanExtent(80.0f);
    m_pWaterBody->SetOceanPatchSize(16.0f);
    m_pWaterBody->SetOceanRingCount(4);
    m_pWaterBody->SetPatchResolution(48);
    m_pWaterBody->SetDefaultWidth(8.0f);
    m_pWaterBody->SetDefaultDepth(4.0f);
  }

  BuildPreviewSpline(plWaterBodyType::Ocean);
  m_pWaterBody->SetBodyType(plWaterBodyType::Ocean);
  m_iCurrentBodyType = plWaterBodyType::Ocean;
}

void plWaterProfileContext::ConfigureBody(plInt32 iBodyType)
{
  const plInt32 iClampedType = plMath::Clamp(iBodyType, static_cast<plInt32>(plWaterBodyType::Ocean), static_cast<plInt32>(plWaterBodyType::Lake));
  PL_LOCK(m_pWorld->GetWriteMarker());
  if (m_pWaterBody == nullptr || m_pSpline == nullptr)
    return;
  if (m_iCurrentBodyType == iClampedType)
    return;

  // Keep the preview fixtures comfortably inside the finite mesher's curvature-to-width limits. The old river
  // combined an eight metre ribbon with several tight reversals and was correctly rejected as a foldover.
  m_pWaterBody->SetDefaultWidth(iClampedType == plWaterBodyType::River ? 5.0f : 8.0f);
  BuildPreviewSpline(iClampedType);
  m_pWaterBody->SetBodyType(static_cast<plWaterBodyType::Enum>(iClampedType));
  m_iCurrentBodyType = iClampedType;
}

void plWaterProfileContext::BuildPreviewSpline(plInt32 iBodyType)
{
  plSpline spline;
  auto AddPoint = [&spline](const plVec3& position)
  {
    auto& point = spline.m_ControlPoints.ExpandAndGetRef();
    point.SetPosition(plSimdVec4f(position.x, position.y, position.z, 1.0f));
    point.SetTangentModeIn(plSplineTangentMode::Auto);
    point.SetTangentModeOut(plSplineTangentMode::Auto);
  };

  if (iBodyType == plWaterBodyType::River)
  {
    AddPoint(plVec3(-14, -2.5f, 0));
    AddPoint(plVec3(-5, -0.5f, 0));
    AddPoint(plVec3(4, 0.5f, 0));
    AddPoint(plVec3(14, 2.5f, 0));
  }
  else if (iBodyType == plWaterBodyType::Lake)
  {
    spline.m_bClosed = true;
    AddPoint(plVec3(-8, -5, 0));
    AddPoint(plVec3(6, -6, 0));
    AddPoint(plVec3(9, 2, 0));
    AddPoint(plVec3(3, 7, 0));
    AddPoint(plVec3(-7, 5, 0));
  }
  else
  {
    AddPoint(plVec3(-1, 0, 0));
    AddPoint(plVec3(1, 0, 0));
  }

  spline.CalculateUpDirAndAutoTangents();
  ++spline.m_uiChangeCounter;
  m_pSpline->SetSpline(std::move(spline));
}

void plWaterProfileContext::TriggerInteraction(const plVec3& vPosition, bool bContinuous)
{
  PL_LOCK(m_pWorld->GetWriteMarker());

  plWaterWorldModuleInterface* pModule = m_pWorld->GetOrCreateModule<plWaterWorldModuleInterface>();
  if (pModule == nullptr)
    return;

  plWaterInteraction interaction;
  interaction.m_vPosition = vPosition;
  interaction.m_vDirection = plVec3(0, 0, -1);
  interaction.m_Type = bContinuous ? plWaterInteractionType::Continuous : plWaterInteractionType::Splash;
  // Calibrated against plWaterInteractorComponent defaults (radius 0.5, strength 1.0): a click reads like an
  // object entering the water; a drag pumps at ~30 Hz, so its per-event strength must stay well below that.
  interaction.m_fRadius = 0.5f;
  interaction.m_fStrength = bContinuous ? 0.3f : 1.0f;
  interaction.m_uiSimulationTick = pModule->GetSimulationTick();

  // Snap to the actual surface and attribute the event to the previewed body, like plWaterInteractorComponent does.
  plWaterSurfaceQuery query;
  query.m_vPosition = vPosition;
  query.m_BodyTypes.Add(plWaterBodyTypeFlags::Ocean);
  query.m_BodyTypes.Add(plWaterBodyTypeFlags::River);
  query.m_BodyTypes.Add(plWaterBodyTypeFlags::Lake);
  query.m_Flags.Add(plWaterQueryFlags::IncludeWaves);
  plWaterSurfaceResult surface;
  if (pModule->TrySampleSurface(query, surface))
  {
    interaction.m_vPosition = surface.m_vSurfacePosition;
    interaction.m_BodyId = surface.m_BodyId;
  }

  plLog::Info("WaterInteract: clicked ({0}, {1}, {2}) -> event ({3}, {4}) {5}",
    plArgF(vPosition.x, 2), plArgF(vPosition.y, 2), plArgF(vPosition.z, 2),
    plArgF(interaction.m_vPosition.x, 2), plArgF(interaction.m_vPosition.y, 2),
    bContinuous ? "continuous" : "splash");

  pModule->AddInteraction(interaction);
}

void plWaterProfileContext::ConfigureScene(plInt32 iScene)
{
  const plInt32 iClampedScene = plMath::Clamp(iScene, 0, 3);
  PL_LOCK(m_pWorld->GetWriteMarker());

  plGameObject* pFloor = nullptr;
  if (!m_pWorld->TryGetObject(m_hFloor, pFloor))
    return;

  m_iCurrentScene = iClampedScene;

  switch (iClampedScene)
  {
    case 1: // Shoreline: a gently sloped floor that breaks the surface on one side, so shore fades and shore foam are visible.
      pFloor->SetLocalPosition(plVec3(0, 0, -1.0f));
      pFloor->SetLocalRotation(plQuat::MakeFromAxisAndAngle(plVec3::MakeAxisY(), plAngle::MakeFromDegree(-7.0f)));
      break;

    case 2: // River bend: shallow bed close under the ribbon.
      pFloor->SetLocalPosition(plVec3(0, 0, -1.5f));
      pFloor->SetLocalRotation(plQuat::MakeIdentity());
      break;

    case 3: // Lake bowl: a deeper basin so absorption has room to work.
      pFloor->SetLocalPosition(plVec3(0, 0, -3.0f));
      pFloor->SetLocalRotation(plQuat::MakeIdentity());
      break;

    default: // Open ocean: deep floor, optics dominated by the deep color.
      pFloor->SetLocalPosition(plVec3(0, 0, -8.0f));
      pFloor->SetLocalRotation(plQuat::MakeIdentity());
      break;
  }
}

void plWaterProfileContext::SetSunElevation(plInt32 iDegrees)
{
  m_iSunElevation = plMath::Clamp(iDegrees, 5, 85);
  PL_LOCK(m_pWorld->GetWriteMarker());

  plGameObject* pSun = nullptr;
  if (!m_pWorld->TryGetObject(m_hSun, pSun))
    return;

  pSun->SetLocalRotation(plQuat::MakeFromEulerAngles(
    plAngle::MakeFromDegree(0.0f), plAngle::MakeFromDegree(static_cast<float>(m_iSunElevation)), plAngle::MakeFromDegree(120.0f)));
}

plEngineProcessViewContext* plWaterProfileContext::CreateViewContext()
{
  plWaterProfileViewContext* pViewContext = PL_DEFAULT_NEW(plWaterProfileViewContext, this);
  pViewContext->SetQuality(m_iPreviewQuality);
  m_ProfileViews.PushBack(pViewContext);
  return pViewContext;
}

void plWaterProfileContext::DestroyViewContext(plEngineProcessViewContext* pContext)
{
  for (plUInt32 i = 0; i < m_ProfileViews.GetCount(); ++i)
  {
    if (m_ProfileViews[i] == pContext)
    {
      m_ProfileViews.RemoveAtAndCopy(i);
      break;
    }
  }
  PL_DEFAULT_DELETE(pContext);
}
