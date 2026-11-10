#include <WaterPlugin/WaterPluginPCH.h>

#include <Core/Messages/TransformChangedMessage.h>
#include <Core/ResourceManager/ResourceManager.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>
#include <Foundation/SimdMath/SimdConversion.h>
#include <RendererCore/Components/SplineComponent.h>
#include <RendererCore/Pipeline/RenderDataManager.h>
#include <RendererCore/Pipeline/RenderPipeline.h>
#include <RendererCore/Pipeline/View.h>
#include <WaterPlugin/Components/WaterBodyComponent.h>
#include <WaterPlugin/Components/WaterSplinePointComponent.h>
#include <WaterPlugin/Rendering/WaterQuality.h>
#include <WaterPlugin/Rendering/WaterRenderData.h>
#include <WaterPlugin/Rendering/WaterSurfacePass.h>
#include <WaterPlugin/World/WaterWorldModule.h>

namespace
{

  struct MetadataPoint
  {
    const plWaterSplinePointComponent* m_pComponent = nullptr;
    plVec3 m_vGlobalPosition = plVec3::MakeZero();
  };

  void GatherMetadataPoints(const plGameObject* pObject, plDynamicArray<MetadataPoint>& out_points)
  {
    for (auto childIt = pObject->GetChildren(); childIt.IsValid(); ++childIt)
    {
      const plGameObject* pChild = childIt;
      const plWaterSplinePointComponent* pMetadata = nullptr;
      if (pChild->TryGetComponentOfBaseType(pMetadata))
      {
        MetadataPoint& point = out_points.ExpandAndGetRef();
        point.m_pComponent = pMetadata;
        point.m_vGlobalPosition = pChild->GetGlobalPosition();
      }
      GatherMetadataPoints(pChild, out_points);
    }
  }

  const plWaterSplinePointComponent* FindMetadataForPosition(plArrayPtr<const MetadataPoint> metadata, const plVec3& position)
  {
    const plWaterSplinePointComponent* pBest = nullptr;
    float fBestDistanceSquared = plMath::HighValue<float>();
    for (const MetadataPoint& candidate : metadata)
    {
      const float fDistanceSquared = (candidate.m_vGlobalPosition - position).GetLengthSquared();
      if (fDistanceSquared < fBestDistanceSquared)
      {
        fBestDistanceSquared = fDistanceSquared;
        pBest = candidate.m_pComponent;
      }
    }
    return pBest;
  }

  void FillCommonRenderData(plWaterRenderData& out_renderData, const plWaterBodyRenderInfo& info, const plVec3& center,
    plUInt32 uiCellsPerSide, plUInt32 uiGeometryType, float fSimulationTime)
  {
    const plWaterProfileResourceDescriptor& profile = info.m_Profile;
    out_renderData.m_Flags.Add(plRenderData::Flags::Dynamic);
    out_renderData.m_vGlobalPosition = center;
    out_renderData.m_vLastGlobalPosition = center;
    out_renderData.m_uiSortingKey = info.m_BodyId.m_uiIndex;
    // The ocean first, rivers and lakes over it: where an apron reaches into the sea, it is the apron that shows.
    out_renderData.m_uiSortLayer = info.m_Type == plWaterBodyType::Ocean ? 0 : 1;
    if (!profile.m_sNormalTexture.IsEmpty())
      out_renderData.m_hNormalTexture = plResourceManager::LoadResource<plTexture2DResource>(profile.m_sNormalTexture);
    if (!profile.m_sFoamTexture.IsEmpty())
      out_renderData.m_hFoamTexture = plResourceManager::LoadResource<plTexture2DResource>(profile.m_sFoamTexture);
    out_renderData.m_fSimulationTime = fSimulationTime;

    // What the surface pass needs before the profile table exists: which profiles want a slot in the wave
    // simulation, and whether any water in view casts caustics.
    out_renderData.m_vWindDirection = profile.m_vWindDirection;
    out_renderData.m_fWaveAmplitude = profile.m_fWaveAmplitude;
    out_renderData.m_fWaveLength = profile.m_fBaseWavelength;
    out_renderData.m_fWaveSpeed = profile.m_fWindSpeed;
    out_renderData.m_fChoppiness = profile.m_fWaveSteepness;
    out_renderData.m_uiWaveSeed = profile.m_uiWaveSeed;
    out_renderData.m_uiSpectrumMode = static_cast<plUInt8>(profile.m_SpectrumMode.GetValue());
    out_renderData.m_fSmallestGpuWavelength = profile.m_fSmallestGpuWavelength;
    const bool bSpectrumAllowed = info.m_Type == plWaterBodyType::Ocean || (info.m_Type == plWaterBodyType::Lake && profile.m_bEnableLakeSpectrum);
    out_renderData.m_uiGpuCascadeCount = bSpectrumAllowed ? profile.m_uiGpuCascadeCount : 0;
    out_renderData.m_fUnderwaterCaustics = profile.m_fUnderwaterCaustics;
    // Reflected caustics are for finite water only. An ocean would project them over the whole scene.
    out_renderData.m_fReflectedCaustics = info.m_Type == plWaterBodyType::Ocean ? 0.0f : profile.m_fReflectedCaustics;

    out_renderData.m_vAbsorption = profile.m_vAbsorption;
    out_renderData.m_vScattering = profile.m_vScattering;
    out_renderData.m_fUnderwaterFogDensity = profile.m_fUnderwaterFogDensity;
    out_renderData.m_fUnderwaterDistortion = profile.m_fUnderwaterDistortion;
    out_renderData.m_fUnderwaterLightShafts = profile.m_fUnderwaterLightShafts;

    out_renderData.m_uiBodyId = info.m_BodyId.m_uiIndex;
    out_renderData.m_uiProfileIndex = info.m_BodyId.m_uiIndex;
    out_renderData.m_uiGeometryType = uiGeometryType;
    out_renderData.m_uiCellsPerSide = uiCellsPerSide;
    out_renderData.m_uiPrimitiveCount = uiCellsPerSide * uiCellsPerSide * 2;
  }

  /// Water is drawn by plWaterSurfacePass, which the engine places in every pipeline that has a transparent pass.
  /// A pipeline without one (the mobile pipeline, or a custom pipeline) extracts water and never draws it. That used
  /// to be silent. Says so once per pipeline, for the views a person looks at.
  void WarnIfViewCannotDrawWater(const plView& view)
  {
    const plCameraUsageHint::Enum usage = view.GetCameraUsageHint();
    if (usage != plCameraUsageHint::MainView && usage != plCameraUsageHint::EditorView)
      return;

    const plRenderPipeline* pPipeline = view.GetRenderPipeline();
    if (pPipeline == nullptr)
      return;

    // Extraction runs for several views at once. The last few pipelines that were looked at are remembered, so the
    // pass list is walked once per pipeline and not once per water body and frame.
    static plMutex s_Mutex;
    static const plRenderPipeline* s_Checked[8] = {};
    static plUInt32 s_uiNext = 0;
    {
      PL_LOCK(s_Mutex);
      for (const plRenderPipeline* pChecked : s_Checked)
      {
        if (pChecked == pPipeline)
          return;
      }
      s_Checked[s_uiNext] = pPipeline;
      s_uiNext = (s_uiNext + 1) % PL_ARRAY_SIZE(s_Checked);
    }

    if (!pPipeline->HasActivePassOfType(plGetStaticRTTI<plWaterSurfacePass>()))
    {
      plLog::Warning("Water is in view '{}', but the view's render pipeline has no water surface pass, so no water is drawn there. Either the pipeline has no transparent pass for the water pass to go in front of (the mobile pipeline, for example), or pipeline injections are switched off (cvar Rendering.Pipeline.Injections, or 'plasma.water/Surface' in Rendering.Pipeline.Injections.Disabled).", view.GetName());
    }
  }

} // namespace

// clang-format off
PL_BEGIN_COMPONENT_TYPE(plWaterBodyComponent, 1, plComponentMode::Static)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ENUM_ACCESSOR_PROPERTY("WaterType", plWaterBodyType, GetBodyType, SetBodyType),
    PL_ACCESSOR_PROPERTY("Profile", GetProfileFile, SetProfileFile)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_WaterProfile")),
    PL_ACCESSOR_PROPERTY("Priority", GetPriority, SetPriority),
    PL_ACCESSOR_PROPERTY("DefaultWidth", GetDefaultWidth, SetDefaultWidth)->AddAttributes(new plDefaultValueAttribute(10.0f), new plClampValueAttribute(0.01f, 100000.0f)),
    PL_ACCESSOR_PROPERTY("DefaultDepth", GetDefaultDepth, SetDefaultDepth)->AddAttributes(new plDefaultValueAttribute(5.0f), new plClampValueAttribute(0.01f, 100000.0f)),
    PL_ACCESSOR_PROPERTY("FlowSpeed", GetFlowSpeed, SetFlowSpeed)->AddAttributes(new plDefaultValueAttribute(1.0f)),
    PL_ACCESSOR_PROPERTY("OceanExtent", GetOceanExtent, SetOceanExtent)->AddAttributes(new plDefaultValueAttribute(10000.0f), new plClampValueAttribute(1.0f, 10000000.0f)),
    PL_ACCESSOR_PROPERTY("OceanPatchSize", GetOceanPatchSize, SetOceanPatchSize)->AddAttributes(new plDefaultValueAttribute(64.0f), new plClampValueAttribute(1.0f, 100000.0f)),
    PL_ACCESSOR_PROPERTY("OceanRingCount", GetOceanRingCount, SetOceanRingCount)->AddAttributes(new plDefaultValueAttribute(6), new plClampValueAttribute(1, 12)),
    PL_ACCESSOR_PROPERTY("PatchResolution", GetPatchResolution, SetPatchResolution)->AddAttributes(new plDefaultValueAttribute(64), new plClampValueAttribute(4, 512)),
  }
  PL_END_PROPERTIES;
  PL_BEGIN_MESSAGEHANDLERS
  {
    PL_MESSAGE_HANDLER(plMsgWaterSplineChanged, OnSplineChanged),
    PL_MESSAGE_HANDLER(plMsgSplineChanged, OnStockSplineChanged),
    PL_MESSAGE_HANDLER(plMsgTransformChanged, OnTransformChanged),
    PL_MESSAGE_HANDLER(plMsgExtractRenderData, OnMsgExtractRenderData),
  }
  PL_END_MESSAGEHANDLERS;
  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("Rendering/Water"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_COMPONENT_TYPE;
// clang-format on

plWaterBodyComponent::plWaterBodyComponent() = default;

void plWaterBodyComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);
  auto& stream = inout_stream.GetStream();
  stream << m_BodyType;
  stream << m_hProfile;
  stream << m_iPriority;
  stream << m_fDefaultWidth;
  stream << m_fDefaultDepth;
  stream << m_fFlowSpeed;
  stream << m_fOceanExtent;
  stream << m_fOceanPatchSize;
  stream << m_uiOceanRingCount;
  stream << m_uiPatchResolution;
}

void plWaterBodyComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);
  inout_stream.GetComponentTypeVersion(GetStaticRTTI());
  auto& stream = inout_stream.GetStream();
  stream >> m_BodyType;
  stream >> m_hProfile;
  stream >> m_iPriority;
  stream >> m_fDefaultWidth;
  stream >> m_fDefaultDepth;
  stream >> m_fFlowSpeed;
  stream >> m_fOceanExtent;
  stream >> m_fOceanPatchSize;
  stream >> m_uiOceanRingCount;
  stream >> m_uiPatchResolution;
}

void plWaterBodyComponent::SetBodyType(plEnum<plWaterBodyType> type)
{
  m_BodyType = type;
  InvalidateWaterBody();
}

void plWaterBodyComponent::SetProfileFile(const char* szFile)
{
  SetProfile(plStringUtils::IsNullOrEmpty(szFile) ? plWaterProfileResourceHandle() : plResourceManager::LoadResource<plWaterProfileResource>(szFile));
}

const char* plWaterBodyComponent::GetProfileFile() const
{
  if (!m_hProfile.IsValid())
    return "";
  return m_hProfile.GetResourceID();
}

void plWaterBodyComponent::SetProfile(const plWaterProfileResourceHandle& hProfile)
{
  m_hProfile = hProfile;
  InvalidateWaterBody();
}

void plWaterBodyComponent::SetPriority(plInt16 iPriority)
{
  m_iPriority = iPriority;
  InvalidateWaterBody();
}

void plWaterBodyComponent::SetDefaultWidth(float fWidth)
{
  m_fDefaultWidth = plMath::Max(fWidth, 0.01f);
  InvalidateWaterBody();
}

void plWaterBodyComponent::SetDefaultDepth(float fDepth)
{
  m_fDefaultDepth = plMath::Max(fDepth, 0.01f);
  InvalidateWaterBody();
}

void plWaterBodyComponent::SetFlowSpeed(float fFlowSpeed)
{
  m_fFlowSpeed = fFlowSpeed;
  InvalidateWaterBody();
}

void plWaterBodyComponent::SetOceanExtent(float fExtent)
{
  m_fOceanExtent = plMath::Max(fExtent, 1.0f);
  InvalidateWaterBody();
}

void plWaterBodyComponent::SetOceanPatchSize(float fPatchSize)
{
  m_fOceanPatchSize = plMath::Max(fPatchSize, 1.0f);
  InvalidateWaterBody();
}

void plWaterBodyComponent::SetOceanRingCount(plUInt8 uiRingCount)
{
  m_uiOceanRingCount = plMath::Clamp<plUInt8>(uiRingCount, 1, 12);
  InvalidateWaterBody();
}

void plWaterBodyComponent::SetPatchResolution(plUInt16 uiResolution)
{
  m_uiPatchResolution = plMath::Clamp<plUInt16>(uiResolution, 4, 512);
  InvalidateWaterBody();
}

plUInt64 plWaterBodyComponent::GetNetworkId() const
{
  return (static_cast<plUInt64>(GetOwner()->GetStableRandomSeed()) << 32) | GetUniqueIdForRendering();
}

void plWaterBodyComponent::BuildWaterBodyDescriptor(plWaterBodyDescriptor& out_descriptor) const
{
  out_descriptor = plWaterBodyDescriptor();
  out_descriptor.m_Type = m_BodyType;
  out_descriptor.m_hOwnerObject = GetOwner()->GetHandle();
  out_descriptor.m_hProfile = m_hProfile;
  out_descriptor.m_uiNetworkId = GetNetworkId();
  out_descriptor.m_iPriority = m_iPriority;
  out_descriptor.m_fBaseHeight = GetOwner()->GetGlobalPosition().z;
  out_descriptor.m_fDefaultWidth = m_fDefaultWidth;
  out_descriptor.m_fDefaultDepth = m_fDefaultDepth;
  out_descriptor.m_fFlowSpeed = m_fFlowSpeed;
  out_descriptor.m_fOceanExtent = m_fOceanExtent;
  out_descriptor.m_fOceanPatchSize = m_fOceanPatchSize;
  out_descriptor.m_uiOceanRingCount = m_uiOceanRingCount;
  out_descriptor.m_uiPatchResolution = m_uiPatchResolution;

  if (m_BodyType == plWaterBodyType::Ocean)
    return;

  const plSplineComponent* pSpline = nullptr;
  if (!GetOwner()->TryGetComponentOfBaseType(pSpline))
    return;

  const plSpline& spline = pSpline->GetSpline();
  if (spline.GetNumControlPoints() < 2 || spline.GetNumSegments() == 0)
    return;
  if (m_BodyType == plWaterBodyType::Lake && !spline.m_bClosed)
    return;
  if (m_BodyType == plWaterBodyType::River && spline.m_bClosed)
    return;

  plDynamicArray<MetadataPoint> metadata;
  GatherMetadataPoints(GetOwner(), metadata);

  struct ControlMetadata
  {
    PL_DECLARE_POD_TYPE();

    float m_fLeftWidth;
    float m_fRightWidth;
    float m_fDepth;
    float m_fFlowScale;
  };
  plDynamicArray<ControlMetadata> controlMetadata;
  controlMetadata.SetCount(spline.GetNumControlPoints());
  for (plUInt32 i = 0; i < spline.GetNumControlPoints(); ++i)
  {
    const plVec3 position = pSpline->GetPositionAtKey(static_cast<float>(i), plSplineComponentSpace::Global);
    const plWaterSplinePointComponent* pPointMetadata = FindMetadataForPosition(metadata, position);
    ControlMetadata& values = controlMetadata[i];
    values.m_fLeftWidth = pPointMetadata != nullptr ? pPointMetadata->GetLeftWidth() : m_fDefaultWidth * 0.5f;
    values.m_fRightWidth = pPointMetadata != nullptr ? pPointMetadata->GetRightWidth() : m_fDefaultWidth * 0.5f;
    values.m_fDepth = pPointMetadata != nullptr ? pPointMetadata->GetDepth() : m_fDefaultDepth;
    values.m_fFlowScale = pPointMetadata != nullptr ? pPointMetadata->GetFlowScale() : 1.0f;
  }

  constexpr plUInt32 uiSamplesPerSegment = 8;
  out_descriptor.m_ControlPoints.Reserve(spline.GetNumSegments() * uiSamplesPerSegment + 1);
  for (plUInt32 segment = 0; segment < spline.GetNumSegments(); ++segment)
  {
    const plUInt32 nextControlPoint = (segment + 1) % spline.GetNumControlPoints();
    for (plUInt32 sample = 0; sample < uiSamplesPerSegment; ++sample)
    {
      const float t = static_cast<float>(sample) / static_cast<float>(uiSamplesPerSegment);
      const float key = static_cast<float>(segment) + t;
      plWaterControlPoint& point = out_descriptor.m_ControlPoints.ExpandAndGetRef();
      point.m_vPosition = pSpline->GetPositionAtKey(key, plSplineComponentSpace::Global);
      point.m_fLeftWidth = plMath::Lerp(controlMetadata[segment].m_fLeftWidth, controlMetadata[nextControlPoint].m_fLeftWidth, t);
      point.m_fRightWidth = plMath::Lerp(controlMetadata[segment].m_fRightWidth, controlMetadata[nextControlPoint].m_fRightWidth, t);
      point.m_fDepth = plMath::Lerp(controlMetadata[segment].m_fDepth, controlMetadata[nextControlPoint].m_fDepth, t);
      point.m_fFlowScale = plMath::Lerp(controlMetadata[segment].m_fFlowScale, controlMetadata[nextControlPoint].m_fFlowScale, t);
    }
  }

  if (!spline.m_bClosed)
  {
    plWaterControlPoint& last = out_descriptor.m_ControlPoints.ExpandAndGetRef();
    last.m_vPosition = pSpline->GetPositionAtKey(static_cast<float>(spline.GetNumControlPoints() - 1), plSplineComponentSpace::Global);
    const ControlMetadata& values = controlMetadata.PeekBack();
    last.m_fLeftWidth = values.m_fLeftWidth;
    last.m_fRightWidth = values.m_fRightWidth;
    last.m_fDepth = values.m_fDepth;
    last.m_fFlowScale = values.m_fFlowScale;
  }
}

void plWaterBodyComponent::InvalidateWaterBody()
{
  if (!IsActiveAndInitialized())
    return;

  plWaterBodyDescriptor descriptor;
  BuildWaterBodyDescriptor(descriptor);
  auto* pModule = GetWorld()->GetOrCreateModule<plWaterWorldModule>();
  if (!m_BodyId.IsValid())
    m_BodyId = pModule->RegisterBody(descriptor);
  else if (!pModule->UpdateBody(m_BodyId, descriptor))
    m_BodyId = pModule->RegisterBody(descriptor);

  TriggerLocalBoundsUpdate();
  InvalidateCachedRenderData();
}

void plWaterBodyComponent::OnActivated()
{
  SUPER::OnActivated();
  GetOwner()->EnableStaticTransformChangesNotifications();
  InvalidateWaterBody();
}

void plWaterBodyComponent::OnDeactivated()
{
  if (auto* pModule = GetWorld()->GetModule<plWaterWorldModule>())
    pModule->UnregisterBody(m_BodyId);
  SUPER::OnDeactivated();
}

void plWaterBodyComponent::OnSplineChanged(plMsgWaterSplineChanged& msg)
{
  PL_IGNORE_UNUSED(msg);
  InvalidateWaterBody();
}

void plWaterBodyComponent::OnStockSplineChanged(plMsgSplineChanged& msg)
{
  PL_IGNORE_UNUSED(msg);
  InvalidateWaterBody();
}

void plWaterBodyComponent::OnTransformChanged(plMsgTransformChanged& msg)
{
  PL_IGNORE_UNUSED(msg);
  InvalidateWaterBody();
}

void plWaterBodyComponent::OnMsgExtractRenderData(plMsgExtractRenderData& msg) const
{
  if (msg.m_pView == nullptr || msg.m_pRenderDataManager == nullptr || msg.m_pView->GetCullingCamera() == nullptr ||
      msg.m_pView->GetCameraUsageHint() == plCameraUsageHint::Shadow)
    return;

  const plWaterWorldModule* pModule = GetWorld()->GetModuleReadOnly<plWaterWorldModule>();
  plWaterBodyRenderInfo info;
  if (pModule == nullptr || !pModule->TryGetBodyRenderInfo(m_BodyId, info) || !info.m_bGeometryValid)
    return;

  // The water passes build their targets and run the wave simulation only while this is reported.
  msg.AddContentFlags(PL_WATER_CONTENT_FLAG_SURFACES);
  WarnIfViewCannotDrawWater(*msg.m_pView);

  const plVec3 cameraPosition = msg.m_pView->GetCullingCamera()->GetPosition();
  const float fSimulationTime = static_cast<float>(pModule->GetFixedSimulationStep().GetSeconds() * pModule->GetSimulationTick());

  if (info.m_Type == plWaterBodyType::Ocean)
  {
    // Where a river's apron reaches into this ocean, the apron draws the water and the ocean leaves its inside out.
    plHybridArray<plWaterOceanCutout, 8> cutouts;
    pModule->CopyOceanCutouts(m_BodyId, cutouts);

    const float fSnapSize = info.m_fOceanPatchSize;
    const plVec2 snappedCamera(plMath::Floor(cameraPosition.x / fSnapSize) * fSnapSize,
      plMath::Floor(cameraPosition.y / fSnapSize) * fSnapSize);
    for (const plWaterOceanClipmapPatch& patch : info.m_OceanPatches)
    {
      const plVec2 center2 = snappedCamera + patch.m_vCenterOffset;
      const plVec3 center(center2.x, center2.y, info.m_fBaseHeight);
      plWaterRenderData* pRenderData = msg.m_pRenderDataManager->CreateRenderDataForThisFrame<plWaterRenderData>(GetOwner());
      const plUInt32 uiResolution = plMath::Clamp<plUInt32>(static_cast<plUInt32>(patch.m_uiResolution) >> plMath::Min<plUInt8>(patch.m_uiLod, 6), 2u, 64u);
      FillCommonRenderData(*pRenderData, info, center, uiResolution, 0, fSimulationTime);
      pRenderData->m_uiLod = patch.m_uiLod;
      pRenderData->m_uiOceanResolution = patch.m_uiResolution;
      pRenderData->m_uiInnerSeamMask = patch.m_uiInnerSeamMask;
      pRenderData->m_uiOuterSeamMask = patch.m_uiOuterSeamMask;
      pRenderData->m_vPatchOrigin = plVec3(center2.x - patch.m_fPatchSize * 0.5f, center2.y - patch.m_fPatchSize * 0.5f, 0.0f);
      pRenderData->m_vPatchAxisU = plVec3::MakeAxisX();
      pRenderData->m_vPatchAxisV = plVec3::MakeAxisY();
      pRenderData->m_fExtentU = patch.m_fPatchSize;
      pRenderData->m_fExtentV = patch.m_fPatchSize;
      pRenderData->m_fBaseHeight = info.m_fBaseHeight;

      // A draw carries two cutouts: the two that reach into this patch. More than two river mouths in one patch
      // is rare, and what it costs is some ocean drawn under an apron, which the apron then covers.
      plUInt32 uiSlot = 0;
      const plVec2 patchMin(center2.x - patch.m_fPatchSize * 0.5f, center2.y - patch.m_fPatchSize * 0.5f);
      const plVec2 patchMax = patchMin + plVec2(patch.m_fPatchSize);
      for (const plWaterOceanCutout& cutout : cutouts)
      {
        const float fReach = cutout.m_fHalfLength + plMath::Max(cutout.m_fStartHalfWidth, cutout.m_fEndHalfWidth);
        if (cutout.m_vCenter.x + fReach < patchMin.x || cutout.m_vCenter.y + fReach < patchMin.y || cutout.m_vCenter.x - fReach > patchMax.x || cutout.m_vCenter.y - fReach > patchMax.y)
          continue;

        plWaterRenderData::Cutout& slot = pRenderData->m_Cutouts[uiSlot];
        slot.m_vCenter = cutout.m_vCenter;
        slot.m_vDirection = cutout.m_vDirection;
        slot.m_fHalfLength = cutout.m_fHalfLength;
        slot.m_fStartHalfWidth = cutout.m_fStartHalfWidth;
        slot.m_fEndHalfWidth = cutout.m_fEndHalfWidth;
        slot.m_fEnabled = 1.0f;
        if (++uiSlot == PL_ARRAY_SIZE(pRenderData->m_Cutouts))
          break;
      }
      msg.AddRenderData(pRenderData, plWaterRenderData::WaterSurface, plRenderData::Caching::Never);
    }
    return;
  }

  if (info.m_FiniteMeshLods.IsEmpty())
    return;

  // A river or a lake draws its part of the network's mesh. Where it meets another body, the two parts share the
  // vertices of the mouth line, and the vertices themselves say which profiles they blend. Nothing here knows about junctions.
  const plVec3 center = info.m_WorldBounds.IsValid() ? info.m_WorldBounds.GetCenter() : GetOwner()->GetGlobalPosition();
  const plVec3 halfExtents = info.m_WorldBounds.IsValid() ? info.m_WorldBounds.GetHalfExtents() : plVec3(10.0f);
  const float fBodyScale = plMath::Max(plVec2(halfExtents.x, halfExtents.y).GetLength(), 10.0f);
  const float fDistanceScale = (cameraPosition - center).GetLength() / fBodyScale;
  plUInt32 uiLod = 0;
  while (uiLod + 1 < info.m_FiniteMeshLods.GetCount() && fDistanceScale > info.m_FiniteMeshLods[uiLod].m_fMaximumDistanceScale)
    ++uiLod;
  const plWaterFiniteMeshLod& lod = info.m_FiniteMeshLods[uiLod];

  auto submit = [&](const plMeshBufferResourceHandle& hMesh, plUInt32 uiPrimitiveCount, plUInt32 uiGeometryType)
  {
    if (!hMesh.IsValid() || uiPrimitiveCount == 0)
      return;
    plWaterRenderData* pRenderData = msg.m_pRenderDataManager->CreateRenderDataForThisFrame<plWaterRenderData>(GetOwner());
    FillCommonRenderData(*pRenderData, info, center, 1, uiGeometryType, fSimulationTime);
    pRenderData->m_hFiniteMesh = hMesh;
    pRenderData->m_uiPrimitiveCount = uiPrimitiveCount;
    pRenderData->m_uiGeometryRevision = info.m_uiGeometryRevision;
    pRenderData->m_uiLod = static_cast<plUInt8>(uiLod);
    msg.AddRenderData(pRenderData, plWaterRenderData::WaterSurface, plRenderData::Caching::Never);
  };
  submit(lod.m_hSurfaceMesh, lod.m_uiSurfacePrimitiveCount, plWaterRenderData::GeometrySurface);
  submit(lod.m_hVolumeMesh, lod.m_uiVolumePrimitiveCount, plWaterRenderData::GeometryVolume);
}

plResult plWaterBodyComponent::GetLocalBounds(plBoundingBoxSphere& out_bounds, bool& out_bAlwaysVisible, plMsgUpdateLocalBounds& ref_msg)
{
  PL_IGNORE_UNUSED(ref_msg);
  out_bAlwaysVisible = m_BodyType == plWaterBodyType::Ocean;

  if (m_BodyType == plWaterBodyType::Ocean)
  {
    out_bounds = plBoundingBoxSphere::MakeFromBox(plBoundingBox::MakeFromCenterAndHalfExtents(
      plVec3(0.0f, 0.0f, -m_fDefaultDepth * 0.5f), plVec3(m_fOceanExtent, m_fOceanExtent, m_fDefaultDepth * 0.5f + 8.0f)));
    return PL_SUCCESS;
  }

  const plTransform inverse = GetOwner()->GetGlobalTransform().GetInverse();
  plBoundingBox bounds = plBoundingBox::MakeInvalid();

  // What the body draws: its part of the network mesh, which for a river reaches past its spline, onto the shore of
  // the lake it joins or out to sea as an apron.
  plWaterBodyRenderInfo info;
  const plWaterWorldModule* pModule = GetWorld()->GetModuleReadOnly<plWaterWorldModule>();
  if (pModule != nullptr && pModule->TryGetBodyRenderInfo(m_BodyId, info) && info.m_bGeometryValid && info.m_WorldBounds.IsValid())
  {
    plVec3 corners[8];
    info.m_WorldBounds.GetCorners(corners);
    for (const plVec3& corner : corners)
      bounds.ExpandToInclude(inverse.TransformPosition(corner));
  }

  plWaterBodyDescriptor descriptor;
  BuildWaterBodyDescriptor(descriptor);
  float fMaxWidth = m_fDefaultWidth;
  float fMaxDepth = m_fDefaultDepth;
  for (const plWaterControlPoint& point : descriptor.m_ControlPoints)
  {
    bounds.ExpandToInclude(inverse.TransformPosition(point.m_vPosition));
    fMaxWidth = plMath::Max(fMaxWidth, point.m_fLeftWidth + point.m_fRightWidth);
    fMaxDepth = plMath::Max(fMaxDepth, point.m_fDepth);
  }
  if (!bounds.IsValid())
    return PL_FAILURE;

  bounds.Grow(plVec3(fMaxWidth, fMaxWidth, fMaxDepth + 8.0f));
  out_bounds = plBoundingBoxSphere::MakeFromBox(bounds);
  return PL_SUCCESS;
}
