#include <WaterPlugin/WaterPluginPCH.h>

#include <Core/ResourceManager/ResourceManager.h>
#include <Core/ResourceManager/Implementation/ResourceLock.h>
#include <Core/World/World.h>
#include <Foundation/Configuration/CVar.h>
#include <Foundation/Containers/HashTable.h>
#include <RendererCore/Debug/DebugRenderer.h>
#include <RendererCore/Meshes/MeshBufferResource.h>
#include <WaterPlugin/Components/OceanComponent.h>
#include <WaterPlugin/World/WaterWorldModule.h>

#include <Shaders/Water/WaterProfileData.h>
#include <WaterPlugin/Utils/WaterNetworkMesher.h>

plCVarBool cvar_WaterDebugShowBodies("Water.Debug.ShowBodies", false, plCVarFlags::Default, "Shows authored water surface topology.");
plCVarBool cvar_WaterDebugShowLod("Water.Debug.ShowLod", false, plCVarFlags::Default, "Shows water patch and clipmap LOD information.");
plCVarBool cvar_WaterDebugShowFlow("Water.Debug.ShowFlow", false, plCVarFlags::Default, "Shows deterministic river flow vectors.");
plCVarBool cvar_WaterDebugShowJunctions("Water.Debug.ShowJunctions", false, plCVarFlags::Default, "Shows water attachment anchors and blend radii.");
plCVarBool cvar_WaterDebugShowInteractions("Water.Debug.ShowInteractions", false, plCVarFlags::Default, "Shows recent splash and wake events, faded by age.");
plCVarBool cvar_WaterDebugStitch("Water.Debug.Stitch", false, plCVarFlags::Default,
  "Shows how rivers, lakes and the ocean are stitched: mouth lines with their shared vertex counts, junctions that were left out, and the profile weights on the water itself.");
plCVarBool cvar_WaterDebugShowUnderwaterVolumes("Water.Debug.ShowUnderwaterVolumes", false, plCVarFlags::Default, "Shows finite underwater proxy bounds.");

// clang-format off
PL_IMPLEMENT_WORLD_MODULE(plWaterWorldModule);

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plWaterWorldModule, 1, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

namespace
{
  constexpr plUInt32 s_uiMaximumFiniteWaterVertices = 262144;
  constexpr plUInt32 s_uiMaximumFiniteWaterIndices = 3145728;

  bool ValidateFiniteWaterTopology(plArrayPtr<const plWaterGeometryVertex> vertices, plArrayPtr<const plUInt32> indices)
  {
    if (vertices.IsEmpty() || indices.IsEmpty() || vertices.GetCount() > s_uiMaximumFiniteWaterVertices ||
        indices.GetCount() > s_uiMaximumFiniteWaterIndices || indices.GetCount() % 3 != 0)
      return false;

    for (const plWaterGeometryVertex& vertex : vertices)
    {
      if (!vertex.m_vPosition.IsValid() || !vertex.m_vNormal.IsValid() || !vertex.m_vFlow.IsValid() || !vertex.m_vTexCoord.IsValid() ||
          !plMath::IsFinite(vertex.m_fDepth) || !plMath::IsFinite(vertex.m_fWaveAttenuation) || !plMath::IsFinite(vertex.m_fProfileBlend))
        return false;
    }

    for (plUInt32 index : indices)
    {
      if (index >= vertices.GetCount())
        return false;
    }
    return true;
  }

  plUInt32 HashWaterValue(plUInt32 value)
  {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
  }

  float HashToUnitFloat(plUInt32 value)
  {
    return static_cast<float>(HashWaterValue(value) & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
  }

  plVec2 NormalizeOr(const plVec2& value, const plVec2& fallback)
  {
    const float fLengthSquared = value.GetLengthSquared();
    return fLengthSquared > plMath::SmallEpsilon<float>() ? value / plMath::Sqrt(fLengthSquared) : fallback;
  }

  float EvaluateJunctionFlowInfluence(const plWaterJunctionRenderSnapshot& junction, const plVec2& position)
  {
    if (!junction.m_bValid || junction.m_fFlowFieldLength <= 0.0f || junction.m_fFlowFieldHalfWidth <= 0.0f ||
        junction.m_fFlowFieldSpeed <= 0.0f)
      return 0.0f;

    const plVec2 inward = NormalizeOr(junction.m_vFlowFieldDirection, plVec2(1.0f, 0.0f));
    const plVec2 lateralAxis(-inward.y, inward.x);
    const plVec2 delta = position - junction.m_vFlowFieldOrigin;
    const float fNormalizedDistance = plMath::Saturate(delta.Dot(inward) / junction.m_fFlowFieldLength);
    const float fLeadingFade = plMath::SmoothStep(fNormalizedDistance, 0.0f, 0.12f);
    const float fTrailingFade = 1.0f - plMath::SmoothStep(fNormalizedDistance, 0.58f, 1.0f);
    const float fLocalHalfWidth = junction.m_fFlowFieldHalfWidth * plMath::Lerp(0.72f, 1.45f, fNormalizedDistance);
    const float fLateralDistance = plMath::Abs(delta.Dot(lateralAxis));
    const float fLateralFade = 1.0f - plMath::SmoothStep(fLateralDistance, fLocalHalfWidth * 0.55f, fLocalHalfWidth);
    return fLeadingFade * fTrailingFade * fLateralFade;
  }

  bool IsTypeEnabled(const plWaterSurfaceQuery& query, plWaterBodyType::Enum type)
  {
    switch (type)
    {
      case plWaterBodyType::Ocean:
        return query.m_BodyTypes.IsSet(plWaterBodyTypeFlags::Ocean);
      case plWaterBodyType::River:
        return query.m_BodyTypes.IsSet(plWaterBodyTypeFlags::River);
      case plWaterBodyType::Lake:
        return query.m_BodyTypes.IsSet(plWaterBodyTypeFlags::Lake);
    }

    return false;
  }

  plMeshBufferResourceHandle CreateFiniteWaterMesh(plArrayPtr<const plWaterGeometryVertex> vertices, plArrayPtr<const plUInt32> indices,
    plUInt64 uiNetworkId, plUInt32 uiRevision, const char* szSuffix)
  {
    if (!ValidateFiniteWaterTopology(vertices, indices))
      return {};

    plMeshBufferResourceDescriptor mesh;
    mesh.AddCommonStreams(true);
    mesh.AddStream(plMeshVertexStreamType::TexCoord1, true);
    mesh.AddStream(plMeshVertexStreamType::Color0);
    mesh.AllocateStreams(vertices.GetCount(), plGALPrimitiveTopology::Triangles, indices.GetCount() / 3);
    for (plUInt32 i = 0; i < vertices.GetCount(); ++i)
    {
      const plWaterGeometryVertex& source = vertices[i];
      plVec3 tangent = source.m_vFlow;
      tangent.NormalizeIfNotZero(plVec3::MakeAxisX()).IgnoreResult();
      mesh.SetPosition(i, source.m_vPosition);
      mesh.SetNormal(i, source.m_vNormal);
      mesh.SetTangent(i, tangent.GetAsVec4(1.0f));
      mesh.SetTexCoord0(i, source.m_vTexCoord);
      mesh.SetTexCoord1(i, plVec2(source.m_fWaveAttenuation, source.m_fDepth));
      // (weight of the second profile, first profile, second profile): see DecodeWaterProfilePair in WaterProfile.h.
      mesh.SetColor0(i, plColorLinearUB(plMath::ColorFloatToByte(plMath::Saturate(source.m_fProfileBlend)), source.m_uiProfileA, source.m_uiProfileB,
                          plMath::ColorFloatToByte(plMath::Saturate(source.m_fFrontBreakup))));
    }
    for (plUInt32 i = 0; i < indices.GetCount(); i += 3)
      mesh.SetTriangleIndices(i / 3, indices[i], indices[i + 1], indices[i + 2]);

    plStringBuilder resourceName;
    resourceName.SetFormat("WaterFiniteMesh_{}_{}_{}", uiNetworkId, uiRevision, szSuffix);
    return plResourceManager::CreateResource<plMeshBufferResource>(resourceName, std::move(mesh), resourceName);
  }

} // namespace

plWaterWorldModule::plWaterWorldModule(plWorld* pWorld)
  : plWaterWorldModuleInterface(pWorld)
{
}

plWaterWorldModule::~plWaterWorldModule() = default;

void plWaterWorldModule::Initialize()
{
  m_LastWallClockTime = plTime::Now();
  auto desc = PL_CREATE_MODULE_UPDATE_FUNCTION_DESC(plWaterWorldModule::Update, this);
  desc.m_Phase = plWorldUpdatePhase::PostTransform;
  desc.m_bOnlyUpdateWhenSimulating = false;
  desc.m_fPriority = 1000.0f;
  RegisterUpdateFunction(desc);
}

void plWaterWorldModule::Update(const plWorldModule::UpdateContext& context)
{
  PL_IGNORE_UNUSED(context);
  const plTime wallClockNow = plTime::Now();
  const double fDeltaSeconds = GetWorldSimulationEnabled()
                                 ? GetWorld()->GetClock().GetTimeDiff().GetSeconds()
                                 : (wallClockNow - m_LastWallClockTime).GetSeconds();
  m_LastWallClockTime = wallClockNow;
  {
    PL_LOCK(m_Mutex);
    m_fSimulationAccumulator += plMath::Clamp(fDeltaSeconds, 0.0, 0.25);
    plUInt32 uiCatchUpSteps = 0;
    while (m_fSimulationAccumulator >= s_fFixedStepSeconds && uiCatchUpSteps < 8)
    {
      m_fSimulationAccumulator -= s_fFixedStepSeconds;
      ++m_uiSimulationTick;
      ++uiCatchUpSteps;
    }
    if (m_fSimulationAccumulator >= s_fFixedStepSeconds)
      m_fSimulationAccumulator = s_fFixedStepSeconds;

    // Profile resources can be hot-reloaded without touching the component. Refresh immutable body snapshots when
    // their resource changes so editor previews and running worlds see profile edits immediately.
    plHybridArray<plUInt32, 8> reloaded;
    for (plUInt32 uiBody = 0; uiBody < m_Bodies.GetCount(); ++uiBody)
    {
      BodySlot& slot = m_Bodies[uiBody];
      if (!slot.m_bInUse || !slot.m_Data.m_Descriptor.m_hProfile.IsValid())
        continue;

      plResourceLock<plWaterProfileResource> profile(slot.m_Data.m_Descriptor.m_hProfile, plResourceAcquireMode::AllowLoadingFallback_NeverFail);
      if (!profile || profile->GetCurrentResourceChangeCounter() == slot.m_Data.m_uiProfileChangeCounter)
        continue;

      const plWaterBodyDescriptor descriptor = slot.m_Data.m_Descriptor;
      const plWaterBodyId bodyId = slot.m_Data.m_BodyId;
      PrepareBody(slot, descriptor);
      slot.m_Data.m_BodyId = bodyId;
      // PrepareBody blocks until the profile is loaded, so the counter it stored is the one just compared against.
      slot.m_Data.m_uiProfileChangeCounter = profile->GetCurrentResourceChangeCounter();
      reloaded.PushBack(uiBody);
    }
    // The mesh settings, mix lengths and mouth weights all come from profiles.
    for (plUInt32 uiBody : reloaded)
      RebuildNetworksAround(uiBody);
  }

  {
    // Interaction records are a short-lived feed for consumers (render extractors, debug overlay), not
    // permanent history: without an age bound the ring retained the session's entire event list forever,
    // the debug overlay drew every event ever created, and stale events could resurface through consumer
    // cursor resyncs. Ten seconds comfortably outlives every legitimate consumer.
    PL_LOCK(m_Mutex);
    while (!m_Interactions.IsEmpty() && m_Interactions[0].m_uiSimulationTick + 600 < m_uiSimulationTick)
      m_Interactions.RemoveAtAndCopy(0);
  }

  FlushPendingInteractions();
  DrawDebug();
}

void plWaterWorldModule::DrawDebug() const
{
  if (!cvar_WaterDebugShowBodies && !cvar_WaterDebugShowLod && !cvar_WaterDebugShowFlow && !cvar_WaterDebugShowJunctions &&
      !cvar_WaterDebugShowInteractions && !cvar_WaterDebugShowUnderwaterVolumes)
    return;

  PL_LOCK(m_Mutex);
  for (const BodySlot& slot : m_Bodies)
  {
    if (!slot.m_bInUse)
      continue;

    const plWaterBodySnapshot& body = slot.m_Data;
    const plColor bodyColor = body.m_Descriptor.m_Type == plWaterBodyType::Ocean ? plColor::DeepSkyBlue
                              : body.m_Descriptor.m_Type == plWaterBodyType::River ? plColor::Cyan
                                                                                  : plColor::CornflowerBlue;
    if (cvar_WaterDebugShowBodies && body.m_Descriptor.m_Type != plWaterBodyType::Ocean)
    {
      plHybridArray<plDebugRenderer::Line, 128> lines;
      for (plUInt32 i = 0; i + 2 < body.m_Indices.GetCount(); i += 3)
      {
        const plVec3& a = body.m_Vertices[body.m_Indices[i]].m_vPosition;
        const plVec3& b = body.m_Vertices[body.m_Indices[i + 1]].m_vPosition;
        const plVec3& c = body.m_Vertices[body.m_Indices[i + 2]].m_vPosition;
        lines.PushBack(plDebugRenderer::Line(a, b, bodyColor));
        lines.PushBack(plDebugRenderer::Line(b, c, bodyColor));
        lines.PushBack(plDebugRenderer::Line(c, a, bodyColor));
      }
      plDebugRenderer::DrawLines(GetWorld(), lines, bodyColor);
    }

    if (cvar_WaterDebugShowFlow && body.m_Descriptor.m_Type == plWaterBodyType::River)
    {
      plHybridArray<plDebugRenderer::Line, 64> lines;
      const auto& points = body.m_Descriptor.m_ControlPoints;
      for (plUInt32 i = 0; i + 1 < points.GetCount(); ++i)
      {
        plVec3 direction = points[i + 1].m_vPosition - points[i].m_vPosition;
        direction.NormalizeIfNotZero(plVec3::MakeAxisX()).IgnoreResult();
        const float fLength = plMath::Max(plMath::Abs(body.m_Descriptor.m_fFlowSpeed * points[i].m_fFlowScale), 0.25f);
        lines.PushBack(plDebugRenderer::Line(points[i].m_vPosition, points[i].m_vPosition + direction * fLength, plColor::LimeGreen));
      }
      plDebugRenderer::DrawLines(GetWorld(), lines, plColor::LimeGreen);
    }

    if (cvar_WaterDebugShowUnderwaterVolumes && body.m_Descriptor.m_Type != plWaterBodyType::Ocean && body.m_WorldBounds.IsValid())
      plDebugRenderer::DrawLineBox(GetWorld(), body.m_WorldBounds, plColor::BlueViolet);

    if (cvar_WaterDebugShowLod && body.m_WorldBounds.IsValid())
    {
      if (body.m_Descriptor.m_Type == plWaterBodyType::Ocean)
      {
        plDebugRenderer::Draw3DText(GetWorld(),
          plFmt("Water {}: ocean patch grid {}", body.m_BodyId.m_uiIndex, body.m_Descriptor.m_uiPatchResolution),
          body.m_WorldBounds.GetCenter(), bodyColor);
      }
      else
      {
        plDebugRenderer::Draw3DText(GetWorld(),
          plFmt("Water {}: {} vertices, {} triangles, {} LODs, {} m target edge", body.m_BodyId.m_uiIndex,
            body.m_Vertices.GetCount(), body.m_Indices.GetCount() / 3, body.m_FiniteMeshLods.GetCount(), body.m_Profile.m_fFiniteMeshCellSize),
          body.m_WorldBounds.GetCenter(), bodyColor);
      }
    }
  }

  if (cvar_WaterDebugShowJunctions || cvar_WaterDebugShowFlow || cvar_WaterDebugStitch)
  {
    for (const JunctionSlot& slot : m_Junctions)
    {
      if (!slot.m_bInUse)
        continue;
      if (cvar_WaterDebugShowJunctions)
      {
        const plDebugRenderer::Line line(slot.m_Data.m_vAnchorA, slot.m_Data.m_vAnchorB, plColor::Yellow);
        plDebugRenderer::DrawLines(GetWorld(), plMakeArrayPtr(&line, 1), plColor::Yellow);
        plDebugRenderer::DrawLineSphere(GetWorld(),
          plBoundingSphere::MakeFromCenterAndRadius(slot.m_Data.m_vCenter, slot.m_Data.m_fBlendDistance), plColor::Yellow);
      }

      if ((cvar_WaterDebugShowJunctions || cvar_WaterDebugStitch) && (slot.m_RenderData.m_bValid || !slot.m_RenderData.m_sProblem.IsEmpty()))
      {
        // The mouth line: green where the river's mesh and the receiver's share every vertex of it, red where they
        // do not or where the junction was left out. With the vertex counts of the three levels of detail.
        const plWaterJunctionRenderSnapshot& render = slot.m_RenderData;
        const bool bSound = render.m_bValid && render.m_fMaximumSeamError <= 0.0f;
        const plColor color = bSound ? plColor::LimeGreen : plColor::Red;
        const plVec3 vLift(0.0f, 0.0f, 0.08f);
        if (render.m_bValid)
        {
          const plDebugRenderer::Line seam(render.m_vMouthLeft + vLift, render.m_vMouthRight + vLift, color);
          plDebugRenderer::DrawLines(GetWorld(), plMakeArrayPtr(&seam, 1), color);
          plDebugRenderer::DrawCross(GetWorld(), render.m_vMouthLeft + vLift, 0.3f, color);
          plDebugRenderer::DrawCross(GetWorld(), render.m_vMouthRight + vLift, 0.3f, color);
        }
        if (cvar_WaterDebugStitch)
        {
          plStringBuilder sText;
          if (!render.m_bValid)
            sText.SetFormat("Junction left out: {}", render.m_sProblem);
          else if (render.m_SecondaryType == plWaterBodyType::Ocean)
            sText.SetFormat("River {} into ocean {}: apron, mix {} m", render.m_PrimaryBody.m_uiIndex, render.m_SecondaryBody.m_uiIndex, plArgF(render.m_fMixLengthReceiver, 1));
          else
            sText.SetFormat("River {} and lake {}: {} / {} / {} shared vertices, seam error {}", render.m_PrimaryBody.m_uiIndex, render.m_SecondaryBody.m_uiIndex,
              render.m_uiSharedVertexCount[0], render.m_uiSharedVertexCount[1], render.m_uiSharedVertexCount[2], plArgF(render.m_fMaximumSeamError, 5));
          plDebugRenderer::Draw3DText(GetWorld(), sText.GetData(), slot.m_Data.m_vCenter + plVec3(0.0f, 0.0f, 0.6f), color);
        }
      }

      if (cvar_WaterDebugShowFlow && slot.m_RenderData.m_bValid && slot.m_RenderData.m_fFlowFieldLength > 0.0f)
      {
        const plVec2 inward = NormalizeOr(slot.m_RenderData.m_vFlowFieldDirection, plVec2(1.0f, 0.0f));
        const plVec2 velocityDirection = inward * slot.m_RenderData.m_fFlowFieldDirectionSign;
        plHybridArray<plDebugRenderer::Line, 8> flowLines;
        for (plUInt32 i = 1; i <= 5; ++i)
        {
          const float t = static_cast<float>(i) / 6.0f;
          const plVec2 position = slot.m_RenderData.m_vFlowFieldOrigin + inward * (slot.m_RenderData.m_fFlowFieldLength * t);
          const float fInfluence = EvaluateJunctionFlowInfluence(slot.m_RenderData, position);
          const plVec3 start(position.x, position.y, slot.m_Data.m_vCenter.z + 0.1f);
          const plVec2 arrow = velocityDirection * plMath::Max(slot.m_RenderData.m_fFlowFieldSpeed * fInfluence, 0.25f);
          flowLines.PushBack(plDebugRenderer::Line(start, start + plVec3(arrow.x, arrow.y, 0.0f), plColor::LimeGreen));
        }
        plDebugRenderer::DrawLines(GetWorld(), flowLines, plColor::LimeGreen);
      }
    }
  }

  if (cvar_WaterDebugShowInteractions)
  {
    for (const plWaterInteraction& interaction : m_Interactions)
    {
      // Fade by age so the overlay reflects actual liveness: an event integrates into the wave field once and
      // its ripple decays within seconds - drawing old records at full brightness makes consumed events look
      // immortal and misleads debugging.
      const plUInt64 uiAgeTicks = m_uiSimulationTick > interaction.m_uiSimulationTick ? m_uiSimulationTick - interaction.m_uiSimulationTick : 0;
      if (uiAgeTicks > 300)
        continue;
      const float fFade = plMath::Max(0.15f, 1.0f - static_cast<float>(uiAgeTicks) / 300.0f);
      plColor eventColor = plColor::OrangeRed;
      eventColor.ScaleRGB(fFade);
      plDebugRenderer::DrawLineSphere(GetWorld(),
        plBoundingSphere::MakeFromCenterAndRadius(interaction.m_vPosition, interaction.m_fRadius), eventColor);
      plDebugRenderer::Line direction(interaction.m_vPosition,
        interaction.m_vPosition + interaction.m_vDirection * plMath::Max(interaction.m_fStrength * 0.05f, 0.25f), eventColor);
      plDebugRenderer::DrawLines(GetWorld(), plMakeArrayPtr(&direction, 1), eventColor);
    }

    // The currently valid 100 m interaction window (published by the primary view's extractor). Ripples only
    // exist inside this region; events outside it are dropped at GPU upload.
    plVec2 vWindowCenter;
    bool bWindowValid = false;
    {
      PL_LOCK(m_Mutex);
      vWindowCenter = m_vDebugInteractionWindow;
      bWindowValid = m_bDebugInteractionWindowValid;
    }
    if (bWindowValid)
    {
      float fHeight = 0.0f;
      plWaterSurfaceQuery query;
      query.m_vPosition = plVec3(vWindowCenter.x, vWindowCenter.y, 0.0f);
      query.m_BodyTypes.Add(plWaterBodyTypeFlags::Ocean);
      query.m_BodyTypes.Add(plWaterBodyTypeFlags::River);
      query.m_BodyTypes.Add(plWaterBodyTypeFlags::Lake);
      plWaterSurfaceResult surface;
      if (TrySampleSurface(query, surface))
        fHeight = surface.m_vSurfacePosition.z;

      const float fHalfExtent = 50.0f;
      const plVec3 corners[4] = {
        plVec3(vWindowCenter.x - fHalfExtent, vWindowCenter.y - fHalfExtent, fHeight),
        plVec3(vWindowCenter.x + fHalfExtent, vWindowCenter.y - fHalfExtent, fHeight),
        plVec3(vWindowCenter.x + fHalfExtent, vWindowCenter.y + fHalfExtent, fHeight),
        plVec3(vWindowCenter.x - fHalfExtent, vWindowCenter.y + fHalfExtent, fHeight),
      };
      const plVec3 vCenter(vWindowCenter.x, vWindowCenter.y, fHeight);
      const plDebugRenderer::Line windowLines[6] = {
        {corners[0], corners[1], plColor::Cyan},
        {corners[1], corners[2], plColor::Cyan},
        {corners[2], corners[3], plColor::Cyan},
        {corners[3], corners[0], plColor::Cyan},
        {vCenter - plVec3(1, 0, 0), vCenter + plVec3(1, 0, 0), plColor::Cyan},
        {vCenter - plVec3(0, 1, 0), vCenter + plVec3(0, 1, 0), plColor::Cyan},
      };
      plDebugRenderer::DrawLines(GetWorld(), plMakeArrayPtr(windowLines, 6), plColor::Cyan);
    }
  }
}

void plWaterWorldModule::ResetSimulationClock(plTime startTime)
{
  PL_LOCK(m_Mutex);
  m_uiSimulationTick = static_cast<plUInt64>(plMath::Max(startTime.GetSeconds(), 0.0) / s_fFixedStepSeconds);
  m_fSimulationAccumulator = 0.0;
  m_LastWallClockTime = plTime::Now();
  m_PendingInteractions.Clear();
  m_Interactions.Clear();
}

void plWaterWorldModule::SetDebugInteractionWindow(const plVec2& vCenter) const
{
  PL_LOCK(m_Mutex);
  m_vDebugInteractionWindow = vCenter;
  m_bDebugInteractionWindowValid = true;
}

plWaterBodyId plWaterWorldModule::RegisterBody(const plWaterBodyDescriptor& descriptor)
{
  PL_LOCK(m_Mutex);

  plUInt32 uiIndex = 0;
  for (; uiIndex < m_Bodies.GetCount(); ++uiIndex)
  {
    if (!m_Bodies[uiIndex].m_bInUse)
      break;
  }
  if (uiIndex == m_Bodies.GetCount())
    m_Bodies.ExpandAndGetRef();

  BodySlot& slot = m_Bodies[uiIndex];
  slot.m_bInUse = true;
  slot.m_uiGeneration = plMath::Max(slot.m_uiGeneration, 1u);
  slot.m_Data = plWaterBodySnapshot();
  PrepareBody(slot, descriptor);
  slot.m_Data.m_BodyId.m_uiIndex = uiIndex;
  slot.m_Data.m_BodyId.m_uiGeneration = slot.m_uiGeneration;
  RebuildNetworksAround(uiIndex);
  return slot.m_Data.m_BodyId;
}

bool plWaterWorldModule::UpdateBody(plWaterBodyId bodyId, const plWaterBodyDescriptor& descriptor)
{
  PL_LOCK(m_Mutex);
  if (!IsBodyIdValid(bodyId))
    return false;

  BodySlot& slot = m_Bodies[bodyId.m_uiIndex];
  PrepareBody(slot, descriptor);
  slot.m_Data.m_BodyId = bodyId;
  RebuildNetworksAround(bodyId.m_uiIndex);
  return true;
}

void plWaterWorldModule::UnregisterBody(plWaterBodyId& inout_bodyId)
{
  PL_LOCK(m_Mutex);
  if (IsBodyIdValid(inout_bodyId))
  {
    // Whatever was joined to this body has to be meshed again without it.
    plHybridArray<plUInt32, 8> neighbours;
    CollectJunctionNeighbours(inout_bodyId.m_uiIndex, neighbours);

    BodySlot& slot = m_Bodies[inout_bodyId.m_uiIndex];
    slot.m_bInUse = false;
    slot.m_Data = plWaterBodySnapshot();
    ++slot.m_uiGeneration;
    if (slot.m_uiGeneration == 0)
      slot.m_uiGeneration = 1;

    for (JunctionSlot& junction : m_Junctions)
    {
      if (junction.m_bInUse && (junction.m_Data.m_BodyA.m_uiIndex == inout_bodyId.m_uiIndex || junction.m_Data.m_BodyB.m_uiIndex == inout_bodyId.m_uiIndex))
        ResetJunctionRenderData(junction);
    }
    RebuildNetworks(neighbours);
  }
  inout_bodyId.Invalidate();
}

void plWaterWorldModule::CollectJunctionNeighbours(plUInt32 uiBody, plDynamicArray<plUInt32>& out_bodies) const
{
  for (const JunctionSlot& junction : m_Junctions)
  {
    if (!junction.m_bInUse)
      continue;
    if (junction.m_Data.m_BodyA.m_uiIndex == uiBody && IsBodyIdValid(junction.m_Data.m_BodyB))
      out_bodies.PushBack(junction.m_Data.m_BodyB.m_uiIndex);
    if (junction.m_Data.m_BodyB.m_uiIndex == uiBody && IsBodyIdValid(junction.m_Data.m_BodyA))
      out_bodies.PushBack(junction.m_Data.m_BodyA.m_uiIndex);
  }
}

void plWaterWorldModule::RebuildNetworksAround(plUInt32 uiBody)
{
  // The body's own network, and the networks of everything a junction ties it to: an ocean is in no network, but
  // the rivers that reach it carry its level and its profile in their aprons.
  plHybridArray<plUInt32, 8> seeds;
  seeds.PushBack(uiBody);
  CollectJunctionNeighbours(uiBody, seeds);
  RebuildNetworks(seeds);
}

void plWaterWorldModule::ResetJunctionRenderData(JunctionSlot& junction)
{
  const plUInt32 uiRevision = junction.m_RenderData.m_uiRevision + 1;
  junction.m_RenderData = plWaterJunctionRenderSnapshot();
  junction.m_RenderData.m_Descriptor = junction.m_Data;
  junction.m_RenderData.m_uiRevision = uiRevision;
}

plWaterBodyId plWaterWorldModule::RegisterLegacyOcean(plOceanComponent* pComponent)
{
  if (pComponent == nullptr)
    return plWaterBodyId();

  plWaterBodyDescriptor descriptor;
  descriptor.m_Type = plWaterBodyType::Ocean;
  descriptor.m_hOwnerObject = pComponent->GetOwner()->GetHandle();
  descriptor.m_uiNetworkId = (static_cast<plUInt64>(pComponent->GetOwner()->GetStableRandomSeed()) << 32) | pComponent->GetUniqueIdForRendering();
  descriptor.m_iPriority = plMath::MinValue<plInt16>();
  descriptor.m_fBaseHeight = pComponent->GetWaterLevel();
  descriptor.m_fDefaultDepth = 10000.0f;
  descriptor.m_fOceanPatchSize = pComponent->GetTileSize();
  descriptor.m_fOceanExtent = plMath::Max(pComponent->GetTileSize() * pComponent->GetTileCountXZ() * 0.5f, pComponent->GetLodDistance3());
  descriptor.m_uiPatchResolution = static_cast<plUInt16>(plMath::Clamp(pComponent->GetTileTesselation().x, 4u, 512u));
  descriptor.m_uiOceanRingCount = 6;
  descriptor.m_bOverrideWaveAmplitude = true;
  descriptor.m_fWaveAmplitudeOverride = pComponent->GetWaveAmplitude();
  return RegisterBody(descriptor);
}

bool plWaterWorldModule::UpdateLegacyOcean(plWaterBodyId bodyId, const plOceanComponent* pComponent)
{
  if (pComponent == nullptr)
    return false;

  plWaterBodyDescriptor descriptor;
  descriptor.m_Type = plWaterBodyType::Ocean;
  descriptor.m_hOwnerObject = pComponent->GetOwner()->GetHandle();
  descriptor.m_uiNetworkId = (static_cast<plUInt64>(pComponent->GetOwner()->GetStableRandomSeed()) << 32) | pComponent->GetUniqueIdForRendering();
  descriptor.m_iPriority = plMath::MinValue<plInt16>();
  descriptor.m_fBaseHeight = pComponent->GetWaterLevel();
  descriptor.m_fDefaultDepth = 10000.0f;
  descriptor.m_fOceanPatchSize = pComponent->GetTileSize();
  descriptor.m_fOceanExtent = plMath::Max(pComponent->GetTileSize() * pComponent->GetTileCountXZ() * 0.5f, pComponent->GetLodDistance3());
  descriptor.m_uiPatchResolution = static_cast<plUInt16>(plMath::Clamp(pComponent->GetTileTesselation().x, 4u, 512u));
  descriptor.m_uiOceanRingCount = 6;
  descriptor.m_bOverrideWaveAmplitude = true;
  descriptor.m_fWaveAmplitudeOverride = pComponent->GetWaveAmplitude();
  return UpdateBody(bodyId, descriptor);
}

plUInt32 plWaterWorldModule::RegisterJunction(const plWaterJunctionDescriptor& descriptor)
{
  PL_LOCK(m_Mutex);
  plUInt32 uiIndex = 0;
  for (; uiIndex < m_Junctions.GetCount(); ++uiIndex)
  {
    if (!m_Junctions[uiIndex].m_bInUse)
      break;
  }
  if (uiIndex == m_Junctions.GetCount())
    m_Junctions.ExpandAndGetRef();
  m_Junctions[uiIndex].m_Data = descriptor;
  m_Junctions[uiIndex].m_bInUse = true;
  ResetJunctionRenderData(m_Junctions[uiIndex]);
  const plUInt32 seeds[] = {descriptor.m_BodyA.m_uiIndex, descriptor.m_BodyB.m_uiIndex};
  RebuildNetworks(seeds);
  return uiIndex;
}

bool plWaterWorldModule::UpdateJunction(plUInt32 uiIndex, const plWaterJunctionDescriptor& descriptor)
{
  PL_LOCK(m_Mutex);
  if (uiIndex >= m_Junctions.GetCount() || !m_Junctions[uiIndex].m_bInUse)
    return false;
  // The bodies the junction joined before may not be the ones it joins now.
  const plUInt32 seeds[] = {m_Junctions[uiIndex].m_Data.m_BodyA.m_uiIndex, m_Junctions[uiIndex].m_Data.m_BodyB.m_uiIndex, descriptor.m_BodyA.m_uiIndex, descriptor.m_BodyB.m_uiIndex};
  m_Junctions[uiIndex].m_Data = descriptor;
  ResetJunctionRenderData(m_Junctions[uiIndex]);
  RebuildNetworks(seeds);
  return true;
}

void plWaterWorldModule::UnregisterJunction(plUInt32& inout_uiIndex)
{
  PL_LOCK(m_Mutex);
  if (inout_uiIndex < m_Junctions.GetCount())
  {
    const plUInt32 seeds[] = {m_Junctions[inout_uiIndex].m_Data.m_BodyA.m_uiIndex, m_Junctions[inout_uiIndex].m_Data.m_BodyB.m_uiIndex};
    m_Junctions[inout_uiIndex].m_bInUse = false;
    m_Junctions[inout_uiIndex].m_RenderData = plWaterJunctionRenderSnapshot();
    RebuildNetworks(seeds);
  }
  inout_uiIndex = plInvalidIndex;
}

plUInt32 plWaterWorldModule::RegisterExclusion(const plWaterExclusionDescriptor& descriptor)
{
  PL_LOCK(m_Mutex);
  plUInt32 uiIndex = 0;
  for (; uiIndex < m_Exclusions.GetCount(); ++uiIndex)
  {
    if (!m_Exclusions[uiIndex].m_bInUse)
      break;
  }
  if (uiIndex == m_Exclusions.GetCount())
    m_Exclusions.ExpandAndGetRef();
  m_Exclusions[uiIndex].m_Data = descriptor;
  m_Exclusions[uiIndex].m_bInUse = true;
  return uiIndex;
}

bool plWaterWorldModule::UpdateExclusion(plUInt32 uiIndex, const plWaterExclusionDescriptor& descriptor)
{
  PL_LOCK(m_Mutex);
  if (uiIndex >= m_Exclusions.GetCount() || !m_Exclusions[uiIndex].m_bInUse)
    return false;
  m_Exclusions[uiIndex].m_Data = descriptor;
  return true;
}

void plWaterWorldModule::UnregisterExclusion(plUInt32& inout_uiIndex)
{
  PL_LOCK(m_Mutex);
  if (inout_uiIndex < m_Exclusions.GetCount())
    m_Exclusions[inout_uiIndex].m_bInUse = false;
  inout_uiIndex = plInvalidIndex;
}

bool plWaterWorldModule::TryGetBodyId(plGameObjectHandle hObject, plWaterBodyId& out_bodyId) const
{
  PL_LOCK(m_Mutex);
  for (plUInt32 i = 0; i < m_Bodies.GetCount(); ++i)
  {
    if (m_Bodies[i].m_bInUse && m_Bodies[i].m_Data.m_Descriptor.m_hOwnerObject == hObject)
    {
      out_bodyId = m_Bodies[i].m_Data.m_BodyId;
      return true;
    }
  }
  out_bodyId.Invalidate();
  return false;
}

bool plWaterWorldModule::TryGetBodySnapshot(plWaterBodyId bodyId, plWaterBodySnapshot& out_snapshot) const
{
  PL_LOCK(m_Mutex);
  if (!IsBodyIdValid(bodyId))
    return false;
  out_snapshot = m_Bodies[bodyId.m_uiIndex].m_Data;
  return true;
}

void plWaterWorldModule::CopyBodySnapshots(plDynamicArray<plWaterBodySnapshot>& out_snapshots) const
{
  PL_LOCK(m_Mutex);
  out_snapshots.Clear();
  out_snapshots.Reserve(m_Bodies.GetCount());
  for (const BodySlot& slot : m_Bodies)
  {
    if (slot.m_bInUse)
      out_snapshots.PushBack(slot.m_Data);
  }
}

bool plWaterWorldModule::TryGetBodyRenderInfo(plWaterBodyId bodyId, plWaterBodyRenderInfo& out_info) const
{
  PL_LOCK(m_Mutex);
  if (!IsBodyIdValid(bodyId))
    return false;

  const plWaterBodySnapshot& body = m_Bodies[bodyId.m_uiIndex].m_Data;
  out_info.m_BodyId = body.m_BodyId;
  out_info.m_Type = body.m_Descriptor.m_Type;
  out_info.m_fBaseHeight = body.m_Descriptor.m_fBaseHeight;
  out_info.m_fOceanPatchSize = body.m_Descriptor.m_fOceanPatchSize;
  out_info.m_fFlowSpeed = body.m_Descriptor.m_fFlowSpeed;
  out_info.m_Profile = body.m_Profile;
  out_info.m_WorldBounds = body.m_WorldBounds;
  out_info.m_FiniteMeshLods = body.m_FiniteMeshLods;
  out_info.m_OceanPatches = body.m_OceanPatches;
  out_info.m_uiGeometryRevision = body.m_uiGeometryRevision;
  out_info.m_bGeometryValid = body.m_bGeometryValid;
  return true;
}

void plWaterWorldModule::CopyOceanCutouts(plWaterBodyId oceanId, plDynamicArray<plWaterOceanCutout>& out_cutouts) const
{
  PL_LOCK(m_Mutex);
  out_cutouts.Clear();
  for (const JunctionSlot& slot : m_Junctions)
  {
    const plWaterJunctionRenderSnapshot& render = slot.m_RenderData;
    if (!slot.m_bInUse || !render.m_bValid || render.m_SecondaryBody != oceanId || render.m_fCutoutHalfLength <= 0.0f)
      continue;

    plWaterOceanCutout& cutout = out_cutouts.ExpandAndGetRef();
    cutout.m_vCenter = render.m_vCutoutCenter;
    cutout.m_vDirection = render.m_vCutoutDirection;
    cutout.m_fHalfLength = render.m_fCutoutHalfLength;
    cutout.m_fStartHalfWidth = render.m_fStartHalfWidth;
    cutout.m_fEndHalfWidth = render.m_fEndHalfWidth;
  }
}

void plWaterFillGpuProfile(plWaterGpuProfile& out_entry, const plWaterProfileResourceDescriptor& profile, plWaterBodyType::Enum type, float fFlowSpeed)
{
  const bool bRiver = type == plWaterBodyType::River;
  // The ocean always may use the spectrum, a lake only when its profile says so, a river never.
  const bool bSpectrumAllowed = type == plWaterBodyType::Ocean || (type == plWaterBodyType::Lake && profile.m_bEnableLakeSpectrum);
  // Reflected caustics are for finite water only. An ocean would project them over the whole scene.
  const float fReflectedCaustics = type == plWaterBodyType::Ocean ? 0.0f : profile.m_fReflectedCaustics;
  const plVec2 vWind = profile.m_vWindDirection.IsZero() ? plVec2(1.0f, 0.0f) : profile.m_vWindDirection;

  out_entry.WaveParams = plVec4(profile.m_fWaveAmplitude, profile.m_fBaseWavelength, profile.m_fWindSpeed, profile.m_fWaveSteepness);
  // An ocean has no current: its detail and foam drift with the wind only.
  const bool bOcean = type == plWaterBodyType::Ocean;
  out_entry.WindAndFlow = plVec4(vWind.x, vWind.y, bOcean ? 0.0f : fFlowSpeed, bOcean ? 0.0f : profile.m_fFlowScale);
  out_entry.SpectrumParams = plVec4(0.0f, bSpectrumAllowed ? static_cast<float>(profile.m_uiGpuCascadeCount) : 0.0f, profile.m_fRippleMaxAmplitude, bRiver ? 1.0f : 0.0f);
  // Extinction per metre, fitted to the profile's look: white sand under Refraction Depth metres of water has the
  // Shallow Color. Deep water has the Deep Color under white light. This is the one optical model the surface
  // shader has; the profile's Absorption and Scattering are what the underwater view uses.
  const float fReferenceDepth = plMath::Max(profile.m_fRefractionDepth, 0.1f);
  const plVec3 vExtinction(
    -plMath::Ln(plMath::Clamp(profile.m_ShallowColor.r, 0.02f, 0.98f)) / fReferenceDepth,
    -plMath::Ln(plMath::Clamp(profile.m_ShallowColor.g, 0.02f, 0.98f)) / fReferenceDepth,
    -plMath::Ln(plMath::Clamp(profile.m_ShallowColor.b, 0.02f, 0.98f)) / fReferenceDepth);
  out_entry.AbsorptionAndRoughness = plVec4(vExtinction.x, vExtinction.y, vExtinction.z, profile.m_fRoughness);
  out_entry.ScatteringAndIor = plVec4(profile.m_vScattering.x, profile.m_vScattering.y, profile.m_vScattering.z, profile.m_fIndexOfRefraction);
  out_entry.ShallowColor = profile.m_ShallowColor.GetAsVec4();
  out_entry.DeepColor = profile.m_DeepColor.GetAsVec4();
  out_entry.OpticalParams = plVec4(profile.m_fReflectionStrength, profile.m_fEnvironmentReflectionWeight, profile.m_fRefractionDepth, profile.m_fShoreFadeDepth);
  out_entry.FoamParams = plVec4(profile.m_fFoamStrength, profile.m_fFoamCrestThreshold, profile.m_fFoamShoreDepth, profile.m_fRefractionStrength);
  out_entry.SurfaceDetailParams = plVec4(profile.m_fMicroNormalScale, profile.m_fMicroNormalStrength, profile.m_fSunGlitterStrength, profile.m_fRefractionEdgeRejection);
  out_entry.FoamMaterialParams = plVec4(profile.m_fFoamRoughness, profile.m_fFoamBreakupScale, profile.m_sFoamTexture.IsEmpty() ? 0.0f : 1.0f, profile.m_sNormalTexture.IsEmpty() ? 0.0f : 1.0f);
  out_entry.FoamColor = profile.m_FoamColor.GetAsVec4();
  out_entry.CausticParams = plVec4(profile.m_fUnderwaterCaustics, fReflectedCaustics, 0.0f, 0.0f);
}

plUInt32 plWaterWorldModule::FillGpuProfileTable(plArrayPtr<plWaterGpuProfile> out_entries) const
{
  PL_LOCK(m_Mutex);

  // A body that is not there leaves a hole in the table. Holes hold the default profile, so an index that outlived
  // its body still reads something sane.
  const plWaterProfileResourceDescriptor defaultProfile;
  plUInt32 uiCount = 0;
  for (plUInt32 i = 0; i < m_Bodies.GetCount() && i < out_entries.GetCount(); ++i)
  {
    const BodySlot& slot = m_Bodies[i];
    if (slot.m_bInUse)
    {
      plWaterFillGpuProfile(out_entries[i], slot.m_Data.m_Profile, slot.m_Data.m_Descriptor.m_Type, slot.m_Data.m_Descriptor.m_fFlowSpeed);
      uiCount = i + 1;
    }
    else
    {
      plWaterFillGpuProfile(out_entries[i], defaultProfile, plWaterBodyType::Lake, 0.0f);
    }
  }
  return uiCount;
}

void plWaterWorldModule::CopyJunctionRenderSnapshots(plDynamicArray<plWaterJunctionRenderSnapshot>& out_snapshots) const
{
  PL_LOCK(m_Mutex);
  out_snapshots.Clear();
  for (const JunctionSlot& slot : m_Junctions)
  {
    if (slot.m_bInUse)
      out_snapshots.PushBack(slot.m_RenderData);
  }
}

void plWaterWorldModule::PrepareBody(BodySlot& slot, const plWaterBodyDescriptor& descriptor)
{
  // The descriptor, the resolved profile and, for an ocean, its clipmap. The meshes of rivers and lakes are built per
  // network (RebuildNetworks), because where a river meets a lake both meshes depend on each other.
  const plWaterBodyId oldId = slot.m_Data.m_BodyId;
  const plWaterBodySnapshot previous = std::move(slot.m_Data);
  slot.m_Data = plWaterBodySnapshot();
  slot.m_Data.m_BodyId = oldId;
  slot.m_Data.m_uiGeometryRevision = previous.m_uiGeometryRevision;
  slot.m_Data.m_Descriptor = descriptor;
  slot.m_Data.m_Descriptor.m_fDefaultWidth = plMath::Max(descriptor.m_fDefaultWidth, 0.01f);
  slot.m_Data.m_Descriptor.m_fDefaultDepth = plMath::Max(descriptor.m_fDefaultDepth, 0.01f);
  slot.m_Data.m_Descriptor.m_fOceanExtent = plMath::Max(descriptor.m_fOceanExtent, 1.0f);
  slot.m_Data.m_Descriptor.m_fOceanPatchSize = plMath::Max(descriptor.m_fOceanPatchSize, 1.0f);
  slot.m_Data.m_Descriptor.m_uiOceanRingCount = plMath::Clamp<plUInt8>(descriptor.m_uiOceanRingCount, 1, 12);
  slot.m_Data.m_Descriptor.m_uiPatchResolution = plMath::Clamp<plUInt16>(descriptor.m_uiPatchResolution, 4, 512);

  const plWaterProfileResourceDescriptor defaultProfile;
  slot.m_Data.m_Profile = defaultProfile;
  if (descriptor.m_hProfile.IsValid())
  {
    plResourceLock<plWaterProfileResource> profile(descriptor.m_hProfile, plResourceAcquireMode::BlockTillLoaded_NeverFail);
    if (profile)
    {
      slot.m_Data.m_Profile = profile->GetDescriptor();
      slot.m_Data.m_uiProfileChangeCounter = profile->GetCurrentResourceChangeCounter();
    }
  }
  if (descriptor.m_bOverrideWaveAmplitude)
    slot.m_Data.m_Profile.m_fWaveAmplitude = plMath::Max(descriptor.m_fWaveAmplitudeOverride, 0.0f);

  if (descriptor.m_Type == plWaterBodyType::Ocean)
  {
    ++slot.m_Data.m_uiGeometryRevision;
    plWaterGeometry::BuildOceanClipmap(slot.m_Data.m_Descriptor.m_uiOceanRingCount, slot.m_Data.m_Descriptor.m_uiPatchResolution,
      slot.m_Data.m_Descriptor.m_fOceanPatchSize, slot.m_Data.m_OceanPatches);
    const float fExtent = slot.m_Data.m_Descriptor.m_fOceanExtent;
    const float fHeightMargin = slot.m_Data.m_Profile.m_fWaveAmplitude * 2.0f + 1.0f;
    slot.m_Data.m_WorldBounds = plBoundingBox::MakeFromMinMax(
      plVec3(-fExtent, -fExtent, descriptor.m_fBaseHeight - slot.m_Data.m_Descriptor.m_fDefaultDepth),
      plVec3(fExtent, fExtent, descriptor.m_fBaseHeight + fHeightMargin));
    slot.m_Data.m_bGeometryValid = !slot.m_Data.m_OceanPatches.IsEmpty();
    return;
  }

  if (previous.m_Descriptor.m_Type != descriptor.m_Type)
    return;

  // A river or lake keeps the meshes it has until its network is rebuilt. If the new outline cannot be meshed, they
  // stay: a body that was visible does not vanish while its spline is being repaired.
  slot.m_Data.m_Vertices = previous.m_Vertices;
  slot.m_Data.m_Indices = previous.m_Indices;
  slot.m_Data.m_hSurfaceMesh = previous.m_hSurfaceMesh;
  slot.m_Data.m_hVolumeMesh = previous.m_hVolumeMesh;
  slot.m_Data.m_FiniteMeshLods = previous.m_FiniteMeshLods;
  slot.m_Data.m_WorldBounds = previous.m_WorldBounds;
  slot.m_Data.m_bGeometryValid = previous.m_bGeometryValid;
}

bool plWaterWorldModule::ResolveJoin(const JunctionSlot& junction, plUInt32& out_uiRiver, plUInt32& out_uiReceiver, bool& out_bRiverEnd) const
{
  if (!junction.m_bInUse || !IsBodyIdValid(junction.m_Data.m_BodyA) || !IsBodyIdValid(junction.m_Data.m_BodyB))
    return false;

  const plWaterBodySnapshot& bodyA = m_Bodies[junction.m_Data.m_BodyA.m_uiIndex].m_Data;
  const plWaterBodySnapshot& bodyB = m_Bodies[junction.m_Data.m_BodyB.m_uiIndex].m_Data;
  plVec3 vRiverAnchor;
  if (bodyA.m_Descriptor.m_Type == plWaterBodyType::River && bodyB.m_Descriptor.m_Type != plWaterBodyType::River)
  {
    out_uiRiver = junction.m_Data.m_BodyA.m_uiIndex;
    out_uiReceiver = junction.m_Data.m_BodyB.m_uiIndex;
    vRiverAnchor = junction.m_Data.m_vAnchorA;
  }
  else if (bodyB.m_Descriptor.m_Type == plWaterBodyType::River && bodyA.m_Descriptor.m_Type != plWaterBodyType::River)
  {
    out_uiRiver = junction.m_Data.m_BodyB.m_uiIndex;
    out_uiReceiver = junction.m_Data.m_BodyA.m_uiIndex;
    vRiverAnchor = junction.m_Data.m_vAnchorB;
  }
  else
  {
    // Lake to lake, lake to ocean and river to river have no mesh of their own. Queries still blend across them.
    return false;
  }

  const auto& points = m_Bodies[out_uiRiver].m_Data.m_Descriptor.m_ControlPoints;
  if (points.GetCount() < 2)
    return false;
  out_bRiverEnd = (points.PeekBack().m_vPosition - vRiverAnchor).GetLengthSquared() < (points[0].m_vPosition - vRiverAnchor).GetLengthSquared();
  return true;
}

void plWaterWorldModule::CollectNetwork(plUInt32 uiSeedBody, plDynamicArray<plUInt32>& out_bodies, plDynamicArray<plUInt32>& out_junctions) const
{
  out_bodies.Clear();
  out_junctions.Clear();
  if (uiSeedBody >= m_Bodies.GetCount() || !m_Bodies[uiSeedBody].m_bInUse || m_Bodies[uiSeedBody].m_Data.m_Descriptor.m_Type == plWaterBodyType::Ocean)
    return;

  out_bodies.PushBack(uiSeedBody);
  for (plUInt32 uiNext = 0; uiNext < out_bodies.GetCount(); ++uiNext)
  {
    const plUInt32 uiBody = out_bodies[uiNext];
    for (plUInt32 uiJunction = 0; uiJunction < m_Junctions.GetCount(); ++uiJunction)
    {
      plUInt32 uiRiver = 0;
      plUInt32 uiReceiver = 0;
      bool bRiverEnd = false;
      if (!ResolveJoin(m_Junctions[uiJunction], uiRiver, uiReceiver, bRiverEnd) || (uiRiver != uiBody && uiReceiver != uiBody))
        continue;

      if (!out_junctions.Contains(uiJunction))
        out_junctions.PushBack(uiJunction);
      // The ocean is not a member of a network: a river only borrows its level and its profile for the apron.
      const plUInt32 uiOther = uiRiver == uiBody ? uiReceiver : uiRiver;
      if (m_Bodies[uiOther].m_Data.m_Descriptor.m_Type != plWaterBodyType::Ocean && !out_bodies.Contains(uiOther))
        out_bodies.PushBack(uiOther);
    }
  }
}

void plWaterWorldModule::RebuildNetworks(plArrayPtr<const plUInt32> seedBodies)
{
  plHybridArray<plUInt32, 16> done;
  plDynamicArray<plUInt32> networkBodies;
  plDynamicArray<plUInt32> networkJunctions;
  for (plUInt32 uiSeed : seedBodies)
  {
    if (done.Contains(uiSeed))
      continue;
    CollectNetwork(uiSeed, networkBodies, networkJunctions);
    if (networkBodies.IsEmpty())
      continue;
    for (plUInt32 uiBody : networkBodies)
      done.PushBack(uiBody);
    RebuildNetwork(networkBodies, networkJunctions);
  }
}

void plWaterWorldModule::RebuildNetwork(plArrayPtr<const plUInt32> networkBodies, plArrayPtr<const plUInt32> networkJunctions)
{
  // What the mesher needs of each body.
  plDynamicArray<plWaterNetworkBody> bodies;
  bodies.SetCount(networkBodies.GetCount());
  for (plUInt32 i = 0; i < networkBodies.GetCount(); ++i)
  {
    const plWaterBodySnapshot& source = m_Bodies[networkBodies[i]].m_Data;
    plWaterNetworkBody& body = bodies[i];
    body.m_Type = source.m_Descriptor.m_Type;
    body.m_uiProfileIndex = static_cast<plUInt8>(plMath::Min<plUInt32>(networkBodies[i], WATER_MAX_PROFILES - 1));
    body.m_Points = source.m_Descriptor.m_ControlPoints;
    body.m_fDefaultDepth = source.m_Descriptor.m_fDefaultDepth;
    body.m_Settings.m_fTargetEdgeLength = plMath::Clamp(source.m_Profile.m_fFiniteMeshCellSize, 0.1f, 20.0f);
    body.m_Settings.m_uiMaxRiverCrossSubdivisions = plMath::Clamp<plUInt16>(source.m_Profile.m_uiMaxRiverCrossSubdivisions, 2, 128);
    body.m_Settings.m_fShoreWaveFadeDistance = plMath::Clamp(source.m_Profile.m_fShoreWaveFadeDistance, 0.01f, 100.0f);
    body.m_Settings.m_fRiverCurvatureRefinement = plMath::Saturate(source.m_Profile.m_fRiverCurvatureRefinement);
  }

  // And of each junction that joins a river of the network to a lake of it, or to an ocean.
  struct JoinSource
  {
    PL_DECLARE_POD_TYPE();

    plUInt32 m_uiJunction;
    plUInt32 m_uiRiverBody;
    plUInt32 m_uiReceiverBody;
    float m_fRiverPush;
    float m_fDirectionSign;
  };
  plDynamicArray<plWaterNetworkJoin> joins;
  plDynamicArray<JoinSource> joinSources;
  for (plUInt32 uiJunction : networkJunctions)
  {
    JunctionSlot& junction = m_Junctions[uiJunction];
    plUInt32 uiRiver = 0;
    plUInt32 uiReceiver = 0;
    bool bRiverEnd = false;
    if (!ResolveJoin(junction, uiRiver, uiReceiver, bRiverEnd))
      continue;

    const plWaterBodySnapshot& river = m_Bodies[uiRiver].m_Data;
    const plWaterBodySnapshot& receiver = m_Bodies[uiReceiver].m_Data;
    const bool bOcean = receiver.m_Descriptor.m_Type == plWaterBodyType::Ocean;
    const plWaterControlPoint& endPoint = bRiverEnd ? river.m_Descriptor.m_ControlPoints.PeekBack() : river.m_Descriptor.m_ControlPoints[0];

    // The river pushes out with its current, the receiver pushes back with its waves. Whichever is stronger owns more
    // of the mouth line, and how hard both push decides how much the mouth churns.
    const float fFlowScalar = river.m_Descriptor.m_fFlowSpeed * river.m_Profile.m_fFlowScale;
    const float fRiverPush = plMath::Abs(fFlowScalar) * plMath::Max(endPoint.m_fFlowScale, 0.0f);
    const float fWavePush = plMath::Max(receiver.m_Profile.m_fWaveAmplitude, 0.0f) * plMath::Max(receiver.m_Profile.m_fWindSpeed, 0.1f);
    const float fPushback = fWavePush / plMath::Max(fWavePush + fRiverPush, 0.0001f);
    // Water enters the receiver at a river's end and leaves it at a river's start, unless the flow speed is negative.
    const bool bInflow = bRiverEnd == (fFlowScalar >= 0.0f);
    const float fBlend = plMath::Max(junction.m_Data.m_fBlendDistance, 0.5f);
    const float fRiverWidth = endPoint.m_fLeftWidth + endPoint.m_fRightWidth;

    plWaterNetworkJoin& join = joins.ExpandAndGetRef();
    join.m_uiRiver = networkBodies.IndexOf(uiRiver);
    join.m_bRiverEnd = bRiverEnd;
    join.m_uiLake = bOcean ? plInvalidIndex : networkBodies.IndexOf(uiReceiver);
    join.m_uiOceanProfileIndex = static_cast<plUInt8>(plMath::Min<plUInt32>(uiReceiver, WATER_MAX_PROFILES - 1));
    join.m_fOceanHeight = receiver.m_Descriptor.m_fBaseHeight;
    if (bInflow)
    {
      join.m_fMixLengthRiver = fBlend * 0.4f;
      join.m_fMixLengthReceiver = bOcean ? plMath::Max(fBlend * 1.5f, fRiverWidth * 2.5f) : fBlend;
      join.m_fMouthWeight = plMath::Lerp(0.35f, 0.65f, fPushback);
    }
    else
    {
      // The receiver's water runs into the river: its look reaches up the river, and the river's hardly into it.
      join.m_fMixLengthRiver = fBlend;
      join.m_fMixLengthReceiver = bOcean ? plMath::Max(fBlend, fRiverWidth * 1.5f) : fBlend * 0.3f;
      join.m_fMouthWeight = 0.75f;
    }
    // The junction component has already checked its own snap radius. This one only has to cover the same ground.
    join.m_fSnapRadius = (junction.m_Data.m_vAnchorA - junction.m_Data.m_vAnchorB).GetLength() + fRiverWidth + 1.0f;
    join.m_fHeightTolerance = plMath::Max(junction.m_Data.m_fHeightTolerance, 0.01f);
    join.m_fFrontBreakup = plMath::Saturate(junction.m_Data.m_fFrontBreakup);

    JoinSource& source = joinSources.ExpandAndGetRef();
    source.m_uiJunction = uiJunction;
    source.m_uiRiverBody = uiRiver;
    source.m_uiReceiverBody = uiReceiver;
    source.m_fRiverPush = fRiverPush;
    source.m_fDirectionSign = bInflow ? 1.0f : -1.0f;
  }

  // Three levels of detail, each a conforming network of its own.
  constexpr plUInt32 uiLodCount = 3;
  plWaterNetworkMesh meshes[uiLodCount];
  for (plUInt32 uiLod = 0; uiLod < uiLodCount; ++uiLod)
    plWaterNetworkMesher::Build(bodies, joins, uiLod, meshes[uiLod]);

  for (plUInt32 i = 0; i < networkBodies.GetCount(); ++i)
  {
    plWaterBodySnapshot& body = m_Bodies[networkBodies[i]].m_Data;
    const plWaterNetworkPart& part = meshes[0].m_Parts[i];
    if (!part.m_bValid)
    {
      if (body.m_bGeometryValid)
        plLog::Warning("Water body {} cannot be meshed as it is now. It keeps its last mesh (revision {}) until its spline or profile is repaired.", networkBodies[i], body.m_uiGeometryRevision);
      else
        plLog::Warning("Water body {} cannot be meshed and is not drawn: {}.", networkBodies[i],
          body.m_Descriptor.m_Type == plWaterBodyType::Lake ? "its outline has to be a closed, flat loop that does not cross itself" : "its spline needs at least two points");
      continue;
    }
    if (part.m_uiFoldedTriangles > 0)
      plLog::Warning("Water body {}: the river bends more tightly than it is wide, and {} triangles were left out where it folds over itself. Widen the bend or narrow the river there.", networkBodies[i], part.m_uiFoldedTriangles);

    const plUInt32 uiRevision = body.m_uiGeometryRevision + 1;
    plHybridArray<plWaterFiniteMeshLod, 3> lods;
    bool bFailed = false;
    for (plUInt32 uiLod = 0; uiLod < uiLodCount; ++uiLod)
    {
      const plWaterNetworkPart& lodPart = meshes[uiLod].m_Parts[i];
      if (!lodPart.m_bValid)
        break;

      plDynamicArray<plWaterGeometryVertex> volumeVertices;
      plDynamicArray<plUInt32> volumeIndices;
      if (!plWaterNetworkMesher::BuildVolume(lodPart.m_TopologyVertices, lodPart.m_TopologyIndices, body.m_Descriptor.m_fDefaultDepth, volumeVertices, volumeIndices))
      {
        bFailed = uiLod == 0;
        break;
      }

      plStringBuilder sSurface;
      plStringBuilder sVolume;
      sSurface.SetFormat("SurfaceLod{}", uiLod);
      sVolume.SetFormat("VolumeLod{}", uiLod);
      plWaterFiniteMeshLod lod;
      lod.m_hSurfaceMesh = CreateFiniteWaterMesh(lodPart.m_Vertices, lodPart.m_Indices, body.m_Descriptor.m_uiNetworkId, uiRevision, sSurface);
      lod.m_hVolumeMesh = CreateFiniteWaterMesh(volumeVertices, volumeIndices, body.m_Descriptor.m_uiNetworkId, uiRevision, sVolume);
      if (!lod.m_hSurfaceMesh.IsValid() || !lod.m_hVolumeMesh.IsValid())
      {
        bFailed = uiLod == 0;
        break;
      }
      lod.m_uiSurfacePrimitiveCount = lodPart.m_Indices.GetCount() / 3;
      lod.m_uiVolumePrimitiveCount = volumeIndices.GetCount() / 3;
      lod.m_fMaximumDistanceScale = uiLod == 0 ? 2.0f : (uiLod == 1 ? 6.0f : plMath::HighValue<float>());
      lods.PushBack(lod);
    }
    if (bFailed || lods.IsEmpty())
    {
      plLog::Warning("Water body {} produced a mesh that is invalid or too large. It keeps what it had.", networkBodies[i]);
      continue;
    }
    // The last level is used at any distance, whichever it is.
    lods.PeekBack().m_fMaximumDistanceScale = plMath::HighValue<float>();

    body.m_uiGeometryRevision = uiRevision;
    body.m_Vertices = part.m_TopologyVertices;
    body.m_Indices = part.m_TopologyIndices;
    body.m_FiniteMeshLods = lods;
    body.m_hSurfaceMesh = lods[0].m_hSurfaceMesh;
    body.m_hVolumeMesh = lods[0].m_hVolumeMesh;
    body.m_WorldBounds = plBoundingBox::MakeInvalid();
    for (const plWaterGeometryVertex& vertex : body.m_Vertices)
      body.m_WorldBounds.ExpandToInclude(vertex.m_vPosition);
    body.m_WorldBounds.Grow(plVec3(0.1f, 0.1f, body.m_Descriptor.m_fDefaultDepth + body.m_Profile.m_fWaveAmplitude * 2.0f));
    body.m_bGeometryValid = true;
  }

  // What each junction came to.
  for (plUInt32 uiJunction : networkJunctions)
  {
    JunctionSlot& junction = m_Junctions[uiJunction];
    const plUInt32 uiRevision = junction.m_RenderData.m_uiRevision + 1;
    junction.m_RenderData = plWaterJunctionRenderSnapshot();
    junction.m_RenderData.m_Descriptor = junction.m_Data;
    junction.m_RenderData.m_uiRevision = uiRevision;
  }
  for (plUInt32 uiJoin = 0; uiJoin < joins.GetCount(); ++uiJoin)
  {
    const JoinSource& source = joinSources[uiJoin];
    const plWaterNetworkJoin& join = joins[uiJoin];
    const plWaterNetworkJoinResult& result = meshes[0].m_Joins[uiJoin];
    plWaterJunctionRenderSnapshot& render = m_Junctions[source.m_uiJunction].m_RenderData;
    const plWaterBodySnapshot& receiver = m_Bodies[source.m_uiReceiverBody].m_Data;

    render.m_PrimaryBody = m_Bodies[source.m_uiRiverBody].m_Data.m_BodyId;
    render.m_SecondaryBody = receiver.m_BodyId;
    render.m_SecondaryType = receiver.m_Descriptor.m_Type;
    render.m_bRiverEnd = join.m_bRiverEnd;
    render.m_bValid = result.m_bValid && m_Bodies[source.m_uiRiverBody].m_Data.m_bGeometryValid && receiver.m_bGeometryValid;
    render.m_sProblem = result.m_sProblem;
    if (!result.m_bValid)
    {
      plLog::Warning("Water junction between bodies {} and {} was left out: {}", source.m_uiRiverBody, source.m_uiReceiverBody, result.m_sProblem);
      continue;
    }

    render.m_vMouthLeft = result.m_vMouthLeft;
    render.m_vMouthRight = result.m_vMouthRight;
    render.m_vInward = result.m_vInward;
    render.m_fMixLengthRiver = join.m_fMixLengthRiver;
    render.m_fMixLengthReceiver = join.m_fMixLengthReceiver;
    render.m_fMouthWeight = join.m_fMouthWeight;
    for (plUInt32 uiLod = 0; uiLod < uiLodCount; ++uiLod)
    {
      const plWaterNetworkJoinResult& lodResult = meshes[uiLod].m_Joins[uiJoin];
      render.m_uiSharedVertexCount[uiLod] = lodResult.m_bValid ? lodResult.m_uiSharedVertexCount : 0;
      if (lodResult.m_bValid)
        render.m_fMaximumSeamError = plMath::Max(render.m_fMaximumSeamError, lodResult.m_fMaximumSeamError);
    }
    render.m_vCutoutCenter = result.m_vCutoutCenter;
    render.m_vCutoutDirection = result.m_vCutoutDirection;
    render.m_fCutoutHalfLength = result.m_fCutoutHalfLength;
    render.m_fStartHalfWidth = result.m_fCutoutStartHalfWidth;
    render.m_fEndHalfWidth = result.m_fCutoutEndHalfWidth;

    // The current that queries see in the receiver: a plume from the middle of the mouth line.
    const float fHalfWidth = (result.m_vMouthLeft - result.m_vMouthRight).GetLength() * 0.5f;
    render.m_vFlowFieldOrigin = ((result.m_vMouthLeft + result.m_vMouthRight) * 0.5f).GetAsVec2();
    render.m_vFlowFieldDirection = result.m_vInward;
    render.m_fFlowFieldLength = plMath::Clamp(plMath::Max(join.m_fMixLengthReceiver * 1.5f, fHalfWidth * 4.0f), 3.0f, 24.0f);
    render.m_fFlowFieldHalfWidth = plMath::Clamp(fHalfWidth * 1.35f, 0.75f, 12.0f);
    render.m_fFlowFieldSpeed = source.m_fRiverPush;
    render.m_fFlowFieldDirectionSign = source.m_fDirectionSign;
  }
}


bool plWaterWorldModule::TrySampleSurface(const plWaterSurfaceQuery& query, plWaterSurfaceResult& out_result) const
{
  return TrySampleSurfaceAtTick(query, GetSimulationTick(), out_result);
}

bool plWaterWorldModule::TrySampleSurfaceAtTime(const plWaterSurfaceQuery& query, plTime simulationTime, plWaterSurfaceResult& out_result) const
{
  const double fSeconds = plMath::Max(simulationTime.GetSeconds(), 0.0);
  const plUInt64 uiTick = static_cast<plUInt64>(plMath::Floor(fSeconds / s_fFixedStepSeconds));
  return TrySampleSurfaceAtTick(query, uiTick, out_result);
}

bool plWaterWorldModule::TrySampleSurfaceAtTick(const plWaterSurfaceQuery& query, plUInt64 uiSimulationTick, plWaterSurfaceResult& out_result) const
{
  PL_LOCK(m_Mutex);

  bool bFound = false;
  plInt16 iBestPriority = plMath::MinValue<plInt16>();
  plUInt64 uiBestNetworkId = plMath::MaxValue<plUInt64>();
  plWaterSurfaceResult best;

  for (const BodySlot& slot : m_Bodies)
  {
    if (!slot.m_bInUse || !slot.m_Data.m_bGeometryValid || !IsTypeEnabled(query, slot.m_Data.m_Descriptor.m_Type))
      continue;

    plWaterSurfaceResult candidate;
    if (!SampleBody(slot, query, uiSimulationTick, candidate))
      continue;

    const plInt16 iPriority = slot.m_Data.m_Descriptor.m_iPriority;
    const float fHeightDifference = candidate.m_vSurfacePosition.z - best.m_vSurfacePosition.z;
    const bool bBetterPriority = !bFound || iPriority > iBestPriority;
    const bool bSamePriorityHigherSurface = bFound && iPriority == iBestPriority && fHeightDifference > 0.02f;
    const bool bStableTieBreak = bFound && iPriority == iBestPriority && plMath::Abs(fHeightDifference) <= 0.02f && candidate.m_uiNetworkId < uiBestNetworkId;
    if (bBetterPriority || bSamePriorityHigherSurface || bStableTieBreak)
    {
      best = candidate;
      iBestPriority = iPriority;
      uiBestNetworkId = candidate.m_uiNetworkId;
      bFound = true;
    }
  }

  if (!bFound)
    return false;

  best.m_uiNetworkId = ResolveConnectedNetworkId(best.m_BodyId);

  if (query.m_Flags.IsSet(plWaterQueryFlags::IncludeJunctions))
    ApplyJunctions(query, uiSimulationTick, best);

  out_result = best;
  return true;
}

bool plWaterWorldModule::SampleBody(const BodySlot& slot, const plWaterSurfaceQuery& query, plUInt64 uiSimulationTick,
  plWaterSurfaceResult& out_result) const
{
  const plWaterBodyDescriptor& body = slot.m_Data.m_Descriptor;
  if (query.m_Flags.IsSet(plWaterQueryFlags::IncludeExclusionVolumes) && IsExcluded(slot, query))
    return false;

  float fSurfaceHeight = body.m_fBaseHeight;
  float fBodyDepth = body.m_fDefaultDepth;
  float fWaveAttenuation = 1.0f;
  plVec3 flow = plVec3::MakeZero();

  if (body.m_Type == plWaterBodyType::River)
  {
    plWaterRiverLocation location;
    if (!plWaterGeometry::FindClosestPointOnRiver(body.m_ControlPoints, query.m_vPosition.GetAsVec2(), location))
      return false;
    const float fBankWidth = location.m_fSignedLateralDistance >= 0.0f ? location.m_fLeftWidth : location.m_fRightWidth;
    if (location.m_fDistanceSquared > plMath::Square(plMath::Max(fBankWidth, 0.01f)))
      return false;

    fSurfaceHeight = location.m_vCenter.z;
    fBodyDepth = plMath::Max(location.m_fDepth, 0.01f);
    flow = location.m_vTangent * (body.m_fFlowSpeed * location.m_fFlowScale);
    const float fDistanceToShore = location.m_fSignedLateralDistance >= 0.0f
                                     ? location.m_fLeftWidth - location.m_fSignedLateralDistance
                                     : location.m_fRightWidth + location.m_fSignedLateralDistance;
    fWaveAttenuation = plMath::SmoothStep(plMath::Max(fDistanceToShore, 0.0f), 0.0f,
      plMath::Max(slot.m_Data.m_Profile.m_fShoreWaveFadeDistance, 0.01f));
  }
  else if (body.m_Type == plWaterBodyType::Lake)
  {
    if (body.m_ControlPoints.GetCount() < 3)
      return false;

    plHybridArray<plVec3, 32> polygon;
    polygon.Reserve(body.m_ControlPoints.GetCount());
    fSurfaceHeight = 0.0f;
    for (const plWaterControlPoint& point : body.m_ControlPoints)
    {
      polygon.PushBack(point.m_vPosition);
      fSurfaceHeight += point.m_vPosition.z;
    }
    fSurfaceHeight /= static_cast<float>(body.m_ControlPoints.GetCount());
    if (!plWaterGeometry::IsPointInPolygonXY(polygon, query.m_vPosition.GetAsVec2()))
      return false;

    float fDistanceToShoreSquared = plMath::HighValue<float>();
    for (plUInt32 i = 0; i < polygon.GetCount(); ++i)
    {
      const plVec2 a = polygon[i].GetAsVec2();
      const plVec2 b = polygon[(i + 1) % polygon.GetCount()].GetAsVec2();
      const plVec2 edge = b - a;
      const float fEdgeLengthSquared = edge.GetLengthSquared();
      const float t = fEdgeLengthSquared > plMath::SmallEpsilon<float>()
                        ? plMath::Clamp((query.m_vPosition.GetAsVec2() - a).Dot(edge) / fEdgeLengthSquared, 0.0f, 1.0f)
                        : 0.0f;
      fDistanceToShoreSquared = plMath::Min(fDistanceToShoreSquared,
        (query.m_vPosition.GetAsVec2() - (a + edge * t)).GetLengthSquared());
    }
    fWaveAttenuation = plMath::SmoothStep(plMath::Sqrt(fDistanceToShoreSquared), 0.0f,
      plMath::Max(slot.m_Data.m_Profile.m_fShoreWaveFadeDistance, 0.01f));
  }

  float fWaveHeight = 0.0f;
  plVec3 waveNormal = plVec3::MakeAxisZ();
  plVec3 waveVelocity = plVec3::MakeZero();
  if (query.m_Flags.IsSet(plWaterQueryFlags::IncludeWaves))
  {
    const plVec2 primaryDirection = body.m_Type == plWaterBodyType::River ? NormalizeOr(flow.GetAsVec2(), slot.m_Data.m_Profile.m_vWindDirection)
                                                                          : slot.m_Data.m_Profile.m_vWindDirection;
    EvaluateWaves(slot.m_Data.m_Profile, query.m_vPosition.GetAsVec2(), primaryDirection, fWaveAttenuation,
      static_cast<double>(uiSimulationTick) * s_fFixedStepSeconds,
      fWaveHeight, waveNormal, waveVelocity);
  }

  fSurfaceHeight += fWaveHeight;
  const float fDepth = fSurfaceHeight - query.m_vPosition.z;
  if (plMath::Abs(fDepth) > query.m_fMaxVerticalDistance)
    return false;

  out_result = plWaterSurfaceResult();
  out_result.m_BodyId = slot.m_Data.m_BodyId;
  out_result.m_hBodyObject = body.m_hOwnerObject;
  out_result.m_Type = body.m_Type;
  out_result.m_uiNetworkId = body.m_uiNetworkId;
  out_result.m_vSurfacePosition = plVec3(query.m_vPosition.x, query.m_vPosition.y, fSurfaceHeight);
  out_result.m_vSurfaceNormal = waveNormal;
  out_result.m_vFlowVelocity = flow * slot.m_Data.m_Profile.m_fFlowScale + waveVelocity;
  out_result.m_fDepth = fDepth;
  out_result.m_bContained = fDepth >= 0.0f && (body.m_Type == plWaterBodyType::Ocean || fDepth <= fBodyDepth);
  out_result.m_fSubmersion = plMath::Saturate(fDepth / (body.m_Type == plWaterBodyType::Ocean ? 100.0f : fBodyDepth));
  out_result.m_fDensity = slot.m_Data.m_Profile.m_fDensity;
  out_result.m_fLinearDrag = slot.m_Data.m_Profile.m_fLinearDrag;
  out_result.m_fAngularDrag = slot.m_Data.m_Profile.m_fAngularDrag;
  out_result.m_uiSimulationTick = uiSimulationTick;
  return true;
}

bool plWaterWorldModule::IsExcluded(const BodySlot& slot, const plWaterSurfaceQuery& query) const
{
  for (const ExclusionSlot& exclusion : m_Exclusions)
  {
    if (!exclusion.m_bInUse)
      continue;
    if (exclusion.m_Data.m_TargetBody.IsValid() && exclusion.m_Data.m_TargetBody != slot.m_Data.m_BodyId)
      continue;

    const plVec3 localPosition = exclusion.m_Data.m_GlobalTransform.GetInverse().TransformPosition(query.m_vPosition);
    const plVec3 halfExtents = exclusion.m_Data.m_vExtents * 0.5f;
    if (plMath::Abs(localPosition.x) <= halfExtents.x && plMath::Abs(localPosition.y) <= halfExtents.y && plMath::Abs(localPosition.z) <= halfExtents.z)
      return true;
  }
  return false;
}

void plWaterWorldModule::ApplyJunctions(const plWaterSurfaceQuery& query, plUInt64 uiSimulationTick, plWaterSurfaceResult& inout_result) const
{
  for (const JunctionSlot& junction : m_Junctions)
  {
    if (!junction.m_bInUse || !junction.m_Data.m_BodyA.IsValid() || !junction.m_Data.m_BodyB.IsValid())
      continue;
    if (inout_result.m_BodyId != junction.m_Data.m_BodyA && inout_result.m_BodyId != junction.m_Data.m_BodyB)
      continue;

    const float fFlowInfluence = EvaluateJunctionFlowInfluence(junction.m_RenderData, query.m_vPosition.GetAsVec2());
    auto ApplyFlowField = [&]()
    {
      if (fFlowInfluence <= 0.0f)
        return;
      const plVec2 inward = NormalizeOr(junction.m_RenderData.m_vFlowFieldDirection, plVec2(1.0f, 0.0f));
      const plVec2 fieldVelocity = inward * (junction.m_RenderData.m_fFlowFieldSpeed * junction.m_RenderData.m_fFlowFieldDirectionSign);
      const plVec2 blendedVelocity = inout_result.m_vFlowVelocity.GetAsVec2() * (1.0f - fFlowInfluence) + fieldVelocity * fFlowInfluence;
      inout_result.m_vFlowVelocity.x = blendedVelocity.x;
      inout_result.m_vFlowVelocity.y = blendedVelocity.y;
    };

    const plVec2 delta = (query.m_vPosition - junction.m_Data.m_vCenter).GetAsVec2();
    const float fBlendDistance = plMath::Max(junction.m_Data.m_fBlendDistance, 0.01f);
    if (delta.GetLengthSquared() > plMath::Square(fBlendDistance))
    {
      // Material/profile blending stays local to the watertight topology bridge, while gameplay queries see
      // the same longer current plume that is displaced on the receiving lake mesh.
      if (inout_result.m_BodyId == junction.m_RenderData.m_SecondaryBody && fFlowInfluence > 0.0f)
      {
        ApplyFlowField();
        return;
      }
      continue;
    }
    if (!IsBodyIdValid(junction.m_Data.m_BodyA) || !IsBodyIdValid(junction.m_Data.m_BodyB))
      continue;

    plWaterSurfaceResult a;
    plWaterSurfaceResult b;
    bool bHasA = SampleBody(m_Bodies[junction.m_Data.m_BodyA.m_uiIndex], query, uiSimulationTick, a);
    bool bHasB = SampleBody(m_Bodies[junction.m_Data.m_BodyB.m_uiIndex], query, uiSimulationTick, b);

    if (!bHasA)
    {
      plWaterSurfaceQuery attachmentQuery = query;
      attachmentQuery.m_vPosition.x = junction.m_Data.m_vAnchorA.x;
      attachmentQuery.m_vPosition.y = junction.m_Data.m_vAnchorA.y;
      bHasA = SampleBody(m_Bodies[junction.m_Data.m_BodyA.m_uiIndex], attachmentQuery, uiSimulationTick, a);
      a.m_vSurfacePosition.x = query.m_vPosition.x;
      a.m_vSurfacePosition.y = query.m_vPosition.y;
    }
    if (!bHasB)
    {
      plWaterSurfaceQuery attachmentQuery = query;
      attachmentQuery.m_vPosition.x = junction.m_Data.m_vAnchorB.x;
      attachmentQuery.m_vPosition.y = junction.m_Data.m_vAnchorB.y;
      bHasB = SampleBody(m_Bodies[junction.m_Data.m_BodyB.m_uiIndex], attachmentQuery, uiSimulationTick, b);
      b.m_vSurfacePosition.x = query.m_vPosition.x;
      b.m_vSurfacePosition.y = query.m_vPosition.y;
    }
    if (!bHasA || !bHasB)
      continue;
    if (plMath::Abs(a.m_vSurfacePosition.z - b.m_vSurfacePosition.z) > junction.m_Data.m_fHeightTolerance)
      continue;

    const plVec2 direction = NormalizeOr(junction.m_Data.m_vBlendDirection.GetAsVec2(), plVec2(1.0f, 0.0f));
    float t = plMath::Saturate(0.5f + delta.Dot(direction) / (2.0f * fBlendDistance));
    t = t * t * (3.0f - 2.0f * t);

    inout_result = a;
    inout_result.m_SecondaryBodyId = b.m_BodyId;
    const plUInt64 uiConnectedNetworkId = ResolveConnectedNetworkId(a.m_BodyId);
    inout_result.m_uiNetworkId = uiConnectedNetworkId;
    inout_result.m_uiSecondaryNetworkId = uiConnectedNetworkId;
    inout_result.m_fProfileBlend = t;
    inout_result.m_vSurfacePosition = plMath::Lerp(a.m_vSurfacePosition, b.m_vSurfacePosition, t);
    inout_result.m_vSurfaceNormal = plMath::Lerp(a.m_vSurfaceNormal, b.m_vSurfaceNormal, t);
    inout_result.m_vSurfaceNormal.NormalizeIfNotZero(plVec3::MakeAxisZ()).IgnoreResult();
    inout_result.m_vFlowVelocity = plMath::Lerp(a.m_vFlowVelocity, b.m_vFlowVelocity, t);
    ApplyFlowField();
    inout_result.m_fDepth = inout_result.m_vSurfacePosition.z - query.m_vPosition.z;
    inout_result.m_fSubmersion = plMath::Lerp(a.m_fSubmersion, b.m_fSubmersion, t);
    inout_result.m_fDensity = plMath::Lerp(a.m_fDensity, b.m_fDensity, t);
    inout_result.m_fLinearDrag = plMath::Lerp(a.m_fLinearDrag, b.m_fLinearDrag, t);
    inout_result.m_fAngularDrag = plMath::Lerp(a.m_fAngularDrag, b.m_fAngularDrag, t);
    inout_result.m_bContained = a.m_bContained || b.m_bContained;
    return;
  }
}

plUInt64 plWaterWorldModule::ResolveConnectedNetworkId(plWaterBodyId bodyId) const
{
  if (!IsBodyIdValid(bodyId))
    return 0;

  plDynamicArray<plUInt8> visited;
  visited.SetCount(m_Bodies.GetCount(), 0);
  plHybridArray<plWaterBodyId, 16> stack;
  stack.PushBack(bodyId);
  plUInt64 uiNetworkId = plMath::MaxValue<plUInt64>();

  while (!stack.IsEmpty())
  {
    const plWaterBodyId current = stack.PeekBack();
    stack.PopBack();
    if (!IsBodyIdValid(current) || visited[current.m_uiIndex] != 0)
      continue;

    visited[current.m_uiIndex] = 1;
    uiNetworkId = plMath::Min(uiNetworkId, m_Bodies[current.m_uiIndex].m_Data.m_Descriptor.m_uiNetworkId);
    for (const JunctionSlot& junction : m_Junctions)
    {
      if (!junction.m_bInUse)
        continue;
      if (junction.m_Data.m_BodyA == current && IsBodyIdValid(junction.m_Data.m_BodyB))
        stack.PushBack(junction.m_Data.m_BodyB);
      else if (junction.m_Data.m_BodyB == current && IsBodyIdValid(junction.m_Data.m_BodyA))
        stack.PushBack(junction.m_Data.m_BodyA);
    }
  }

  return uiNetworkId == plMath::MaxValue<plUInt64>() ? 0 : uiNetworkId;
}

void plWaterWorldModule::EvaluateWaves(const plWaterProfileResourceDescriptor& profile, const plVec2& position,
  const plVec2& primaryDirection, float fAmplitudeScale, double fTime,
  float& out_fHeight, plVec3& out_normal, plVec3& out_velocity)
{
  out_fHeight = 0.0f;
  out_normal = plVec3::MakeAxisZ();
  out_velocity = plVec3::MakeZero();

  const plUInt32 uiWaveCount = plMath::Clamp<plUInt32>(profile.m_uiCpuWaveCount, 1, 16);
  const plVec2 wind = NormalizeOr(primaryDirection, NormalizeOr(profile.m_vWindDirection, plVec2(1.0f, 0.0f)));
  const plAngle baseAngle = plMath::ATan2(wind.y, wind.x);
  float fWeightSum = 0.0f;
  plVec2 derivative = plVec2::MakeZero();
  float fVerticalVelocity = 0.0f;

  for (plUInt32 i = 0; i < uiWaveCount; ++i)
  {
    const float fWeight = plMath::Pow(0.65f, static_cast<float>(i));
    fWeightSum += fWeight;
    const float fDirectionJitter = (HashToUnitFloat(profile.m_uiWaveSeed + i * 17u) - 0.5f) * 1.4f;
    const float fAngle = baseAngle.GetRadian() + fDirectionJitter;
    const plVec2 direction(plMath::Cos(plAngle::MakeFromRadian(fAngle)), plMath::Sin(plAngle::MakeFromRadian(fAngle)));
    const float fWaveLengthScale = plMath::Lerp(0.45f, 1.8f, HashToUnitFloat(profile.m_uiWaveSeed + i * 31u + 7u));
    const float fWavelength = plMath::Max(profile.m_fBaseWavelength * fWaveLengthScale, 0.1f);
    const float k = 2.0f * plMath::Pi<float>() / fWavelength;
    const float omega = plMath::Sqrt(9.81f * k);
    const float fPhaseOffset = HashToUnitFloat(profile.m_uiWaveSeed + i * 53u + 13u) * 2.0f * plMath::Pi<float>();
    const float phase = k * direction.Dot(position) - omega * static_cast<float>(fTime) + fPhaseOffset;
    const float fAmplitude = profile.m_fWaveAmplitude * plMath::Saturate(fAmplitudeScale) * fWeight;
    const float fSin = plMath::Sin(plAngle::MakeFromRadian(phase));
    const float fCos = plMath::Cos(plAngle::MakeFromRadian(phase));

    out_fHeight += fAmplitude * fSin;
    derivative += direction * (fAmplitude * k * fCos * profile.m_fWaveSteepness);
    fVerticalVelocity += -fAmplitude * omega * fCos;
  }

  if (fWeightSum > 0.0f)
  {
    out_fHeight /= fWeightSum;
    derivative /= fWeightSum;
    fVerticalVelocity /= fWeightSum;
  }

  out_normal = plVec3(-derivative.x, -derivative.y, 1.0f);
  out_normal.NormalizeIfNotZero(plVec3::MakeAxisZ()).IgnoreResult();
  out_velocity.z = fVerticalVelocity;
}

plUInt64 plWaterWorldModule::GetSimulationTick() const
{
  PL_LOCK(m_Mutex);
  return m_uiSimulationTick;
}

plTime plWaterWorldModule::GetFixedSimulationStep() const
{
  return plTime::MakeFromSeconds(s_fFixedStepSeconds);
}

void plWaterWorldModule::AddInteraction(plWaterInteraction interaction)
{
  PL_LOCK(m_Mutex);
  if (interaction.m_uiSimulationTick == 0)
    interaction.m_uiSimulationTick = m_uiSimulationTick;
  interaction.m_uiSequence = 0;
  m_PendingInteractions.PushBack(interaction);
}

void plWaterWorldModule::FlushPendingInteractions()
{
  plDynamicArray<plWaterInteraction> finalized;
  {
    PL_LOCK(m_Mutex);
    if (m_PendingInteractions.IsEmpty())
      return;

    m_PendingInteractions.Sort([](const plWaterInteraction& a, const plWaterInteraction& b)
      {
        if (a.m_uiSimulationTick != b.m_uiSimulationTick) return a.m_uiSimulationTick < b.m_uiSimulationTick;
        if (a.m_uiSourceStableId != b.m_uiSourceStableId) return a.m_uiSourceStableId < b.m_uiSourceStableId;
        return a.m_uiSourceSequence < b.m_uiSourceSequence;
      });

    finalized = std::move(m_PendingInteractions);
    m_PendingInteractions.Clear();
    for (plWaterInteraction& interaction : finalized)
    {
      interaction.m_uiSequence = m_uiNextInteractionSequence++;
      m_Interactions.PushBack(interaction);
    }
    while (m_Interactions.GetCount() > 1024)
      m_Interactions.RemoveAtAndCopy(0);
  }

  for (const plWaterInteraction& interaction : finalized)
    m_InteractionEvents.Broadcast(interaction);
}

plUInt64 plWaterWorldModule::CopyInteractionsSince(plUInt64 uiAfterSequence, plDynamicArray<plWaterInteraction>& out_interactions) const
{
  PL_LOCK(m_Mutex);
  out_interactions.Clear();
  for (const plWaterInteraction& interaction : m_Interactions)
  {
    if (interaction.m_uiSequence > uiAfterSequence)
      out_interactions.PushBack(interaction);
  }
  return m_Interactions.IsEmpty() ? uiAfterSequence : m_Interactions.PeekBack().m_uiSequence;
}

bool plWaterWorldModule::IsBodyIdValid(plWaterBodyId bodyId) const
{
  return bodyId.IsValid() && bodyId.m_uiIndex < m_Bodies.GetCount() && m_Bodies[bodyId.m_uiIndex].m_bInUse &&
         m_Bodies[bodyId.m_uiIndex].m_uiGeneration == bodyId.m_uiGeneration;
}

PL_STATICLINK_FILE(WaterPlugin, WaterPlugin_World_Implementation_WaterWorldModule);
