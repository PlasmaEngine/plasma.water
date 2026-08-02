#include <WaterPlugin/WaterPluginPCH.h>

#include <Core/Messages/TransformChangedMessage.h>
#include <Core/ResourceManager/ResourceManager.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>
#include <Foundation/SimdMath/SimdConversion.h>
#include <RendererCore/Components/SplineComponent.h>
#include <RendererCore/Pipeline/RenderDataManager.h>
#include <RendererCore/Pipeline/View.h>
#include <WaterPlugin/Components/WaterBodyComponent.h>
#include <WaterPlugin/Components/WaterSplinePointComponent.h>
#include <WaterPlugin/Rendering/WaterRenderData.h>
#include <WaterPlugin/World/WaterWorldModule.h>

namespace
{
  constexpr float s_fJunctionMaterialMixStrength = 0.6f;

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

  void FillCommonRenderData(plWaterRenderData& out_renderData, const plWaterBodySnapshot& snapshot, const plVec3& center,
    plUInt32 uiCellsPerSide, plUInt32 uiGeometryType, float fWaveAmplitude, float fSimulationTime)
  {
    out_renderData.m_Flags.Add(plRenderData::Flags::Dynamic);
    out_renderData.m_vGlobalPosition = center;
    out_renderData.m_vLastGlobalPosition = center;
    out_renderData.m_uiSortingKey = snapshot.m_BodyId.m_uiIndex;
    out_renderData.m_vAbsorption = snapshot.m_Profile.m_vAbsorption;
    out_renderData.m_vScattering = snapshot.m_Profile.m_vScattering;
    out_renderData.m_ShallowColor = snapshot.m_Profile.m_ShallowColor;
    out_renderData.m_DeepColor = snapshot.m_Profile.m_DeepColor;
    out_renderData.m_vWindDirection = snapshot.m_Profile.m_vWindDirection;
    if (!snapshot.m_Profile.m_sNormalTexture.IsEmpty())
      out_renderData.m_hNormalTexture = plResourceManager::LoadResource<plTexture2DResource>(snapshot.m_Profile.m_sNormalTexture);
    if (!snapshot.m_Profile.m_sFoamTexture.IsEmpty())
      out_renderData.m_hFoamTexture = plResourceManager::LoadResource<plTexture2DResource>(snapshot.m_Profile.m_sFoamTexture);
    out_renderData.m_fRoughness = snapshot.m_Profile.m_fRoughness;
    out_renderData.m_fIndexOfRefraction = snapshot.m_Profile.m_fIndexOfRefraction;
    out_renderData.m_fReflectionStrength = snapshot.m_Profile.m_fReflectionStrength;
    out_renderData.m_fEnvironmentReflectionWeight = snapshot.m_Profile.m_fEnvironmentReflectionWeight;
    out_renderData.m_fMicroNormalScale = snapshot.m_Profile.m_fMicroNormalScale;
    out_renderData.m_fMicroNormalStrength = snapshot.m_Profile.m_fMicroNormalStrength;
    out_renderData.m_fSunGlitterStrength = snapshot.m_Profile.m_fSunGlitterStrength;
    out_renderData.m_fRefractionEdgeRejection = snapshot.m_Profile.m_fRefractionEdgeRejection;
    out_renderData.m_fRefractionStrength = snapshot.m_Profile.m_fRefractionStrength;
    out_renderData.m_fRefractionDepth = snapshot.m_Profile.m_fRefractionDepth;
    out_renderData.m_fShoreFadeDepth = snapshot.m_Profile.m_fShoreFadeDepth;
    out_renderData.m_fFoamStrength = snapshot.m_Profile.m_fFoamStrength;
    out_renderData.m_fFoamCrestThreshold = snapshot.m_Profile.m_fFoamCrestThreshold;
    out_renderData.m_fFoamShoreDepth = snapshot.m_Profile.m_fFoamShoreDepth;
    out_renderData.m_FoamColor = snapshot.m_Profile.m_FoamColor;
    out_renderData.m_fFoamRoughness = snapshot.m_Profile.m_fFoamRoughness;
    out_renderData.m_fFoamBreakupScale = snapshot.m_Profile.m_fFoamBreakupScale;
    out_renderData.m_fRippleMaxAmplitude = snapshot.m_Profile.m_fRippleMaxAmplitude;
    out_renderData.m_fSimulationTime = fSimulationTime;
    out_renderData.m_fUnderwaterFogDensity = snapshot.m_Profile.m_fUnderwaterFogDensity;
    out_renderData.m_fUnderwaterDistortion = snapshot.m_Profile.m_fUnderwaterDistortion;
    out_renderData.m_fUnderwaterCaustics = snapshot.m_Profile.m_fUnderwaterCaustics;
    // Reflected caustics are intentionally finite-water only. An ocean's unbounded fallback/source field would
    // otherwise project the effect across the entire scene and amplify every receiver above sea level.
    out_renderData.m_fReflectedCaustics = snapshot.m_Descriptor.m_Type == plWaterBodyType::Ocean
                                           ? 0.0f
                                           : snapshot.m_Profile.m_fReflectedCaustics;
    out_renderData.m_fUnderwaterLightShafts = snapshot.m_Profile.m_fUnderwaterLightShafts;
    out_renderData.m_fWaveAmplitude = fWaveAmplitude;
    out_renderData.m_fWaveLength = snapshot.m_Profile.m_fBaseWavelength;
    out_renderData.m_fWaveSpeed = snapshot.m_Profile.m_fWindSpeed;
    out_renderData.m_fChoppiness = snapshot.m_Profile.m_fWaveSteepness;
    out_renderData.m_uiBodyId = snapshot.m_BodyId.m_uiIndex;
    out_renderData.m_uiProfileIndex = snapshot.m_BodyId.m_uiIndex;
    out_renderData.m_uiWaveSeed = snapshot.m_Profile.m_uiWaveSeed;
    out_renderData.m_uiSpectrumMode = static_cast<plUInt8>(snapshot.m_Profile.m_SpectrumMode.GetValue());
    out_renderData.m_fSmallestGpuWavelength = snapshot.m_Profile.m_fSmallestGpuWavelength;
    const bool bSpectrumAllowed = snapshot.m_Descriptor.m_Type == plWaterBodyType::Ocean ||
                                  (snapshot.m_Descriptor.m_Type == plWaterBodyType::Lake && snapshot.m_Profile.m_bEnableLakeSpectrum);
    out_renderData.m_uiGpuCascadeCount = bSpectrumAllowed ? snapshot.m_Profile.m_uiGpuCascadeCount : 0;
    out_renderData.m_uiGeometryType = uiGeometryType;
    out_renderData.m_uiCellsPerSide = uiCellsPerSide;
    out_renderData.m_uiPrimitiveCount = uiCellsPerSide * uiCellsPerSide * 2;
  }

  void FillSecondaryProfile(plWaterRenderData& out_renderData, const plWaterProfileResourceDescriptor& profile,
    plEnum<plWaterBodyType> bodyType, const plVec2& flow)
  {
    out_renderData.m_vSecondaryAbsorption = profile.m_vAbsorption;
    out_renderData.m_vSecondaryScattering = profile.m_vScattering;
    out_renderData.m_vSecondaryWindDirection = profile.m_vWindDirection;
    out_renderData.m_vSecondaryFlow = flow;
    out_renderData.m_SecondaryShallowColor = profile.m_ShallowColor;
    out_renderData.m_SecondaryDeepColor = profile.m_DeepColor;
    out_renderData.m_fSecondaryWaveAmplitude = profile.m_fWaveAmplitude;
    out_renderData.m_fSecondaryWaveLength = profile.m_fBaseWavelength;
    out_renderData.m_fSecondaryWaveSpeed = profile.m_fWindSpeed;
    out_renderData.m_fSecondaryChoppiness = profile.m_fWaveSteepness;
    out_renderData.m_fSecondaryRoughness = profile.m_fRoughness;
    out_renderData.m_fSecondaryIndexOfRefraction = profile.m_fIndexOfRefraction;
    out_renderData.m_fSecondaryReflectionStrength = profile.m_fReflectionStrength;
    out_renderData.m_fSecondaryEnvironmentReflectionWeight = profile.m_fEnvironmentReflectionWeight;
    out_renderData.m_fSecondaryMicroNormalScale = profile.m_fMicroNormalScale;
    out_renderData.m_fSecondaryMicroNormalStrength = profile.m_fMicroNormalStrength;
    out_renderData.m_fSecondarySunGlitterStrength = profile.m_fSunGlitterStrength;
    out_renderData.m_fSecondaryRefractionEdgeRejection = profile.m_fRefractionEdgeRejection;
    out_renderData.m_fSecondaryRefractionStrength = profile.m_fRefractionStrength;
    out_renderData.m_fSecondaryRefractionDepth = profile.m_fRefractionDepth;
    out_renderData.m_fSecondaryShoreFadeDepth = profile.m_fShoreFadeDepth;
    out_renderData.m_fSecondaryFoamStrength = profile.m_fFoamStrength;
    out_renderData.m_fSecondaryFoamCrestThreshold = profile.m_fFoamCrestThreshold;
    out_renderData.m_fSecondaryFoamShoreDepth = profile.m_fFoamShoreDepth;
    out_renderData.m_SecondaryFoamColor = profile.m_FoamColor;
    out_renderData.m_fSecondaryFoamRoughness = profile.m_fFoamRoughness;
    out_renderData.m_fSecondaryFoamBreakupScale = profile.m_fFoamBreakupScale;
    out_renderData.m_fSecondaryUnderwaterCaustics = profile.m_fUnderwaterCaustics;
    out_renderData.m_fSecondaryReflectedCaustics = bodyType == plWaterBodyType::Ocean ? 0.0f : profile.m_fReflectedCaustics;
    if (!profile.m_sNormalTexture.IsEmpty())
      out_renderData.m_hSecondaryNormalTexture = plResourceManager::LoadResource<plTexture2DResource>(profile.m_sNormalTexture);
    if (!profile.m_sFoamTexture.IsEmpty())
      out_renderData.m_hSecondaryFoamTexture = plResourceManager::LoadResource<plTexture2DResource>(profile.m_sFoamTexture);
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
  plWaterBodySnapshot snapshot;
  if (pModule == nullptr || !pModule->TryGetBodySnapshot(m_BodyId, snapshot) || !snapshot.m_bGeometryValid)
    return;

  const plVec3 cameraPosition = msg.m_pView->GetCullingCamera()->GetPosition();
  const float fSimulationTime = static_cast<float>(pModule->GetFixedSimulationStep().GetSeconds() * pModule->GetSimulationTick());
  plDynamicArray<plWaterJunctionRenderSnapshot> junctions;
  pModule->CopyJunctionRenderSnapshots(junctions);
  auto FillJunctionFlow = [](plWaterRenderData& renderData, const plWaterJunctionRenderSnapshot& junction, plUInt32 uiSlot)
  {
    if (uiSlot >= PL_ARRAY_SIZE(renderData.m_JunctionFlows) || junction.m_SecondaryType != plWaterBodyType::Lake ||
        junction.m_fFlowFieldLength <= 0.0f || junction.m_fFlowFieldHalfWidth <= 0.0f || junction.m_fFlowFieldSpeed <= 0.0f)
      return;

    plWaterRenderData::JunctionFlow& flow = renderData.m_JunctionFlows[uiSlot];
    flow.m_vOrigin = junction.m_vFlowFieldOrigin;
    flow.m_vDirection = junction.m_vFlowFieldDirection;
    flow.m_fLength = junction.m_fFlowFieldLength;
    flow.m_fHalfWidth = junction.m_fFlowFieldHalfWidth;
    flow.m_fSpeed = junction.m_fFlowFieldSpeed;
    flow.m_fDirectionSign = junction.m_fFlowFieldDirectionSign;
  };
  auto ApplyJunctionCutout = [&](plWaterRenderData& renderData) {
    plUInt32 uiSlot = 0;
    for (const plWaterJunctionRenderSnapshot& junction : junctions)
    {
      // Never destructively cut the receiving lake/ocean. The transition patch can safely overdraw that
      // continuous surface, while a missing/mismatched patch must never expose terrain or the scene clear color.
      if (junction.m_PrimaryBody != snapshot.m_BodyId)
        continue;

      // A merged junction remesh already cut the river geometrically at the weld row; there is no replaced
      // terminal segment left to discard and an analytic cutout would punch a hole into the merged surface.
      if (junction.m_bMergedIntoPrimary)
        continue;

      // The river terminal segment is replaced by the exact-welded portion of the patch. Inset its analytic
      // cutout so the two geometries overlap slightly at the boundary instead of exposing interpolation cracks.
      const float fMargin = junction.m_fCutoutMargin;
      plWaterRenderData::Cutout& cutout = renderData.m_JunctionCutouts[uiSlot];
      cutout.m_vDirection = junction.m_vCutoutDirection;
      cutout.m_fHalfLength = plMath::Max(junction.m_fCutoutHalfLength - fMargin * 0.5f, 0.0f);
      cutout.m_fStartHalfWidth = junction.m_fStartHalfWidth;
      cutout.m_fEndHalfWidth = junction.m_fEndHalfWidth;
      cutout.m_vCenter = junction.m_vCutoutCenter + junction.m_vCutoutDirection * (fMargin * 0.5f);
      cutout.m_fEnabled = 1.0f;

      if (++uiSlot == PL_ARRAY_SIZE(renderData.m_JunctionCutouts))
        break;
    }

    // The receiving lake remains topologically intact. Its own tessellated surface evaluates this compact
    // analytic field, so a visible current can reach into the basin without replacing a long wedge of lake mesh.
    uiSlot = 0;
    for (const plWaterJunctionRenderSnapshot& junction : junctions)
    {
      if (junction.m_SecondaryBody != snapshot.m_BodyId || junction.m_SecondaryType != plWaterBodyType::Lake)
        continue;
      FillJunctionFlow(renderData, junction, uiSlot);

      // Carry the first inflowing river profile onto the lake draw. The shader evaluates its material plume
      // from this exact flow slot; geometry remains the lake's own surface, so material mixing cannot reopen
      // the welded shoreline. Additional junctions still contribute current but do not alias another profile
      // through the same secondary constant set.
      if (renderData.m_fJunctionMaterialMixStrength <= 0.0f && junction.m_fFlowFieldDirectionSign > 0.0f)
      {
        plWaterBodySnapshot primarySnapshot;
        if (pModule->TryGetBodySnapshot(junction.m_PrimaryBody, primarySnapshot) &&
            primarySnapshot.m_Descriptor.m_Type == plWaterBodyType::River)
        {
          FillSecondaryProfile(renderData, primarySnapshot.m_Profile, plWaterBodyType::River,
            plVec2(primarySnapshot.m_Descriptor.m_fFlowSpeed, primarySnapshot.m_Profile.m_fFlowScale));
          renderData.m_uiSecondaryGpuCascadeCount = 0;
          renderData.m_fSecondaryRippleMaxAmplitude = primarySnapshot.m_Profile.m_fRippleMaxAmplitude;
          renderData.m_fJunctionMaterialMixStrength = s_fJunctionMaterialMixStrength;
          renderData.m_uiJunctionMaterialFlowSlot = uiSlot;
        }
      }

      if (++uiSlot == PL_ARRAY_SIZE(renderData.m_JunctionFlows))
        break;
    }
  };
  auto SubmitJunctionMeshes = [&]()
  {
    // The receiving lake/ocean owns extraction of the replacement patch. Its bounds contain the whole patch,
    // so culling cannot remove the transition while the receiving surface is visible. Ocean ownership also
    // keeps the patch alive independently of clipmap patch placement.
    for (const plWaterJunctionRenderSnapshot& junction : junctions)
    {
      if (junction.m_SecondaryBody != snapshot.m_BodyId)
        continue;

      // Merged junctions render as one continuous mesh submitted by the primary river; there is no separate
      // patch left for the receiving body to own.
      if (junction.m_bMergedIntoPrimary)
        continue;

      plWaterBodySnapshot primarySnapshot;
      if (!pModule->TryGetBodySnapshot(junction.m_PrimaryBody, primarySnapshot) ||
          primarySnapshot.m_Descriptor.m_Type != plWaterBodyType::River)
        continue;

      auto submitJunctionMesh = [&](const plMeshBufferResourceHandle& hMesh, plUInt32 uiPrimitiveCount, plUInt32 uiGeometryType)
      {
        if (!hMesh.IsValid() || uiPrimitiveCount == 0)
          return;
        plWaterRenderData* pRenderData = msg.m_pRenderDataManager->CreateRenderDataForThisFrame<plWaterRenderData>(GetOwner());
        FillCommonRenderData(*pRenderData, primarySnapshot, junction.m_Descriptor.m_vCenter, 1, uiGeometryType,
          primarySnapshot.m_Profile.m_fWaveAmplitude, fSimulationTime);
        FillSecondaryProfile(*pRenderData, junction.m_SecondaryProfile, junction.m_SecondaryType,
          plVec2(snapshot.m_Descriptor.m_fFlowSpeed, snapshot.m_Profile.m_fFlowScale));
        pRenderData->m_hFiniteMesh = hMesh;
        pRenderData->m_uiPrimitiveCount = uiPrimitiveCount;
        pRenderData->m_uiGeometryRevision = junction.m_uiRevision;
        // The river itself has no GPU spectrum, but the patch must reproduce the SECONDARY body's cascade
        // displacement toward its seam or the two surfaces separate by the spectrum wave height.
        pRenderData->m_uiGpuCascadeCount = 0;
        pRenderData->m_uiSecondaryBodyId = junction.m_SecondaryBody.m_uiIndex;
        pRenderData->m_uiSecondaryProfileIndex = junction.m_SecondaryBody.m_uiIndex;
        pRenderData->m_uiSecondaryGpuCascadeCount = junction.m_uiSecondaryGpuCascadeCount;
        pRenderData->m_fSecondaryRippleMaxAmplitude = junction.m_SecondaryProfile.m_fRippleMaxAmplitude;
        pRenderData->m_fMouthFoamStrength = junction.m_fMouthFoamStrength;
        pRenderData->m_fJunctionMaterialMixStrength = junction.m_fFlowFieldDirectionSign > 0.0f ? s_fJunctionMaterialMixStrength : 0.0f;
        pRenderData->m_uiJunctionMaterialFlowSlot = 0;
        FillJunctionFlow(*pRenderData, junction, 0);
        // Both cutout slots stay disabled (default): the replacement patch must never discard itself.
        pRenderData->m_vFlow = plVec2(primarySnapshot.m_Descriptor.m_fFlowSpeed, primarySnapshot.m_Profile.m_fFlowScale);
        msg.AddRenderData(pRenderData, plWaterRenderData::WaterSurface, plRenderData::Caching::Never);
      };
      submitJunctionMesh(junction.m_hSurfaceMesh, junction.m_uiSurfacePrimitiveCount, 5u);
      submitJunctionMesh(junction.m_hVolumeMesh, junction.m_uiVolumePrimitiveCount, 6u);
    }
  };
  if (snapshot.m_Descriptor.m_Type == plWaterBodyType::Ocean)
  {
    const float fSnapSize = snapshot.m_Descriptor.m_fOceanPatchSize;
    const plVec2 snappedCamera(plMath::Floor(cameraPosition.x / fSnapSize) * fSnapSize,
      plMath::Floor(cameraPosition.y / fSnapSize) * fSnapSize);
    for (const plWaterOceanClipmapPatch& patch : snapshot.m_OceanPatches)
    {
      const plVec2 center2 = snappedCamera + patch.m_vCenterOffset;
      const plVec3 center(center2.x, center2.y, snapshot.m_Descriptor.m_fBaseHeight);
      plWaterRenderData* pRenderData = msg.m_pRenderDataManager->CreateRenderDataForThisFrame<plWaterRenderData>(GetOwner());
      const plUInt32 uiResolution = plMath::Clamp<plUInt32>(static_cast<plUInt32>(patch.m_uiResolution) >> plMath::Min<plUInt8>(patch.m_uiLod, 6), 2u, 64u);
      FillCommonRenderData(*pRenderData, snapshot, center, uiResolution, 0, snapshot.m_Profile.m_fWaveAmplitude, fSimulationTime);
      pRenderData->m_uiLod = patch.m_uiLod;
      pRenderData->m_vPatchOrigin = plVec3(center2.x - patch.m_fPatchSize * 0.5f, center2.y - patch.m_fPatchSize * 0.5f, 0.0f);
      pRenderData->m_vPatchAxisU = plVec3::MakeAxisX();
      pRenderData->m_vPatchAxisV = plVec3::MakeAxisY();
      pRenderData->m_fExtentU = patch.m_fPatchSize;
      pRenderData->m_fExtentV = patch.m_fPatchSize;
      pRenderData->m_fBaseHeight = snapshot.m_Descriptor.m_fBaseHeight;
      ApplyJunctionCutout(*pRenderData);
      msg.AddRenderData(pRenderData, plWaterRenderData::WaterSurface, plRenderData::Caching::Never);
    }
    SubmitJunctionMeshes();
    return;
  }

  const plVec3 center = snapshot.m_WorldBounds.IsValid() ? snapshot.m_WorldBounds.GetCenter() : GetOwner()->GetGlobalPosition();
  auto submitFiniteMesh = [&](const plMeshBufferResourceHandle& hMesh, plUInt32 uiPrimitiveCount, plUInt32 uiGeometryType) {
    if (!hMesh.IsValid() || uiPrimitiveCount == 0)
      return;
    plWaterRenderData* pRenderData = msg.m_pRenderDataManager->CreateRenderDataForThisFrame<plWaterRenderData>(GetOwner());
    FillCommonRenderData(*pRenderData, snapshot, center, 1, uiGeometryType, snapshot.m_Profile.m_fWaveAmplitude, fSimulationTime);
    pRenderData->m_hFiniteMesh = hMesh;
    pRenderData->m_uiPrimitiveCount = uiPrimitiveCount;
    pRenderData->m_uiGeometryRevision = snapshot.m_uiGeometryRevision;
    pRenderData->m_vFlow = plVec2(snapshot.m_Descriptor.m_fFlowSpeed, snapshot.m_Profile.m_fFlowScale);
    ApplyJunctionCutout(*pRenderData);
    msg.AddRenderData(pRenderData, plWaterRenderData::WaterSurface, plRenderData::Caching::Never);
  };

  // A merged junction remesh replaces this river's own finite meshes entirely: the merged mesh already
  // contains the kept river ribbon (cut at the weld row), the transition patch and the duplicated lake
  // shoreline as ONE topology. Rendering it as a junction draw keeps the upstream river visually identical
  // (profileBlend is 0 there) while the transition shades and displaces in the same evaluation as the mouth.
  const plWaterJunctionRenderSnapshot* pMergedJunction = nullptr;
  for (const plWaterJunctionRenderSnapshot& junction : junctions)
  {
    if (junction.m_bMergedIntoPrimary && junction.m_PrimaryBody == snapshot.m_BodyId)
    {
      pMergedJunction = &junction;
      break;
    }
  }

  const bool bRiver = snapshot.m_Descriptor.m_Type == plWaterBodyType::River;
  if (pMergedJunction != nullptr)
  {
    const plWaterJunctionRenderSnapshot& junction = *pMergedJunction;
    plVec2 secondaryFlow = plVec2::MakeZero();
    plWaterBodySnapshot secondarySnapshot;
    if (pModule->TryGetBodySnapshot(junction.m_SecondaryBody, secondarySnapshot))
      secondaryFlow = plVec2(secondarySnapshot.m_Descriptor.m_fFlowSpeed, secondarySnapshot.m_Profile.m_fFlowScale);
    auto submitMergedMesh = [&](const plMeshBufferResourceHandle& hMesh, plUInt32 uiPrimitiveCount, plUInt32 uiGeometryType)
    {
      if (!hMesh.IsValid() || uiPrimitiveCount == 0)
        return;
      plWaterRenderData* pRenderData = msg.m_pRenderDataManager->CreateRenderDataForThisFrame<plWaterRenderData>(GetOwner());
      FillCommonRenderData(*pRenderData, snapshot, center, 1, uiGeometryType, snapshot.m_Profile.m_fWaveAmplitude, fSimulationTime);
      FillSecondaryProfile(*pRenderData, junction.m_SecondaryProfile, junction.m_SecondaryType, secondaryFlow);
      pRenderData->m_hFiniteMesh = hMesh;
      pRenderData->m_uiPrimitiveCount = uiPrimitiveCount;
      pRenderData->m_uiGeometryRevision = junction.m_uiRevision;
      pRenderData->m_uiSecondaryBodyId = junction.m_SecondaryBody.m_uiIndex;
      pRenderData->m_uiSecondaryProfileIndex = junction.m_SecondaryBody.m_uiIndex;
      pRenderData->m_uiSecondaryGpuCascadeCount = junction.m_uiSecondaryGpuCascadeCount;
      pRenderData->m_fSecondaryRippleMaxAmplitude = junction.m_SecondaryProfile.m_fRippleMaxAmplitude;
      pRenderData->m_fMouthFoamStrength = junction.m_fMouthFoamStrength;
      pRenderData->m_fJunctionMaterialMixStrength = junction.m_fFlowFieldDirectionSign > 0.0f ? s_fJunctionMaterialMixStrength : 0.0f;
      pRenderData->m_uiJunctionMaterialFlowSlot = 0;
      FillJunctionFlow(*pRenderData, junction, 0);
      // No cutouts: the merged mesh was cut geometrically, so nothing overlaps and nothing may be discarded.
      pRenderData->m_vFlow = plVec2(snapshot.m_Descriptor.m_fFlowSpeed, snapshot.m_Profile.m_fFlowScale);
      msg.AddRenderData(pRenderData, plWaterRenderData::WaterSurface, plRenderData::Caching::Never);
    };
    submitMergedMesh(junction.m_hMergedSurfaceMesh, junction.m_uiMergedSurfacePrimitiveCount, 5u);
    submitMergedMesh(junction.m_hMergedVolumeMesh, junction.m_uiMergedVolumePrimitiveCount, 6u);
  }
  else if (!snapshot.m_FiniteMeshLods.IsEmpty())
  {
    const plVec3 halfExtents = snapshot.m_WorldBounds.GetHalfExtents();
    const float fBodyScale = plMath::Max(plVec2(halfExtents.x, halfExtents.y).GetLength(), 10.0f);
    const float fDistanceScale = (cameraPosition - center).GetLength() / fBodyScale;
    plUInt32 uiLod = 0;
    while (uiLod + 1 < snapshot.m_FiniteMeshLods.GetCount() && fDistanceScale > snapshot.m_FiniteMeshLods[uiLod].m_fMaximumDistanceScale)
      ++uiLod;
    const plWaterFiniteMeshLod& lod = snapshot.m_FiniteMeshLods[uiLod];
    submitFiniteMesh(lod.m_hSurfaceMesh, lod.m_uiSurfacePrimitiveCount, bRiver ? 1u : 2u);
    submitFiniteMesh(lod.m_hVolumeMesh, lod.m_uiVolumePrimitiveCount, bRiver ? 3u : 4u);
  }

  SubmitJunctionMeshes();
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

  plWaterBodyDescriptor descriptor;
  BuildWaterBodyDescriptor(descriptor);
  if (descriptor.m_ControlPoints.IsEmpty())
    return PL_FAILURE;

  const plTransform inverse = GetOwner()->GetGlobalTransform().GetInverse();
  plBoundingBox bounds = plBoundingBox::MakeInvalid();
  float fMaxWidth = m_fDefaultWidth;
  float fMaxDepth = m_fDefaultDepth;
  for (const plWaterControlPoint& point : descriptor.m_ControlPoints)
  {
    bounds.ExpandToInclude(inverse.TransformPosition(point.m_vPosition));
    fMaxWidth = plMath::Max(fMaxWidth, point.m_fLeftWidth + point.m_fRightWidth);
    fMaxDepth = plMath::Max(fMaxDepth, point.m_fDepth);
  }

  if (m_BodyType != plWaterBodyType::Ocean)
  {
    // A finite receiving body submits its junction meshes. Include their complete footprints in its culling
    // bounds so the patch and the cutout are always extracted as one indivisible render unit.
    if (const plWaterWorldModule* pModule = GetWorld()->GetModuleReadOnly<plWaterWorldModule>())
    {
      plDynamicArray<plWaterJunctionRenderSnapshot> junctions;
      pModule->CopyJunctionRenderSnapshots(junctions);
      for (const plWaterJunctionRenderSnapshot& junction : junctions)
      {
        // A merged junction remesh is submitted by the PRIMARY river and extends past the river spline to
        // the receiving shoreline, so the river's culling bounds must cover the patch footprint as well.
        const bool bReceivesJunction = junction.m_SecondaryBody == m_BodyId;
        const bool bMergedPrimary = junction.m_bMergedIntoPrimary && junction.m_PrimaryBody == m_BodyId;
        if (!bReceivesJunction && !bMergedPrimary)
          continue;

        const plVec2 direction = junction.m_vCutoutDirection;
        const plVec2 perpendicular(-direction.y, direction.x);
        const plVec2 start = junction.m_vCutoutCenter - direction * junction.m_fCutoutHalfLength;
        const plVec2 end = junction.m_vCutoutCenter + direction * junction.m_fCutoutHalfLength;
        const float z = junction.m_Descriptor.m_vCenter.z;
        const plVec2 startLeft = start + perpendicular * junction.m_fStartHalfWidth;
        const plVec2 startRight = start - perpendicular * junction.m_fStartHalfWidth;
        const plVec2 endLeft = end + perpendicular * junction.m_fEndHalfWidth;
        const plVec2 endRight = end - perpendicular * junction.m_fEndHalfWidth;
        bounds.ExpandToInclude(inverse.TransformPosition(plVec3(startLeft.x, startLeft.y, z)));
        bounds.ExpandToInclude(inverse.TransformPosition(plVec3(startRight.x, startRight.y, z)));
        bounds.ExpandToInclude(inverse.TransformPosition(plVec3(endLeft.x, endLeft.y, z)));
        bounds.ExpandToInclude(inverse.TransformPosition(plVec3(endRight.x, endRight.y, z)));
      }
    }
  }
  bounds.Grow(plVec3(fMaxWidth, fMaxWidth, fMaxDepth + 8.0f));
  out_bounds = plBoundingBoxSphere::MakeFromBox(bounds);
  return PL_SUCCESS;
}

PL_STATICLINK_FILE(WaterPlugin, WaterPlugin_Components_Implementation_WaterBodyComponent);
