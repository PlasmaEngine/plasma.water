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

plCVarBool cvar_WaterDebugShowBodies("Water.Debug.ShowBodies", false, plCVarFlags::Default, "Shows authored water surface topology.");
plCVarBool cvar_WaterDebugShowLod("Water.Debug.ShowLod", false, plCVarFlags::Default, "Shows water patch and clipmap LOD information.");
plCVarBool cvar_WaterDebugShowFlow("Water.Debug.ShowFlow", false, plCVarFlags::Default, "Shows deterministic river flow vectors.");
plCVarBool cvar_WaterDebugShowJunctions("Water.Debug.ShowJunctions", false, plCVarFlags::Default, "Shows water attachment anchors and blend radii.");
plCVarBool cvar_WaterDebugShowInteractions("Water.Debug.ShowInteractions", false, plCVarFlags::Default, "Shows recent splash and wake events, faded by age.");
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

  struct BoundaryEdge
  {
    plUInt32 m_uiA = plInvalidIndex;
    plUInt32 m_uiB = plInvalidIndex;
    plUInt8 m_uiUseCount = 0;
  };

  plUInt64 MakeUndirectedEdgeKey(plUInt32 a, plUInt32 b)
  {
    const plUInt32 lo = plMath::Min(a, b);
    const plUInt32 hi = plMath::Max(a, b);
    return (static_cast<plUInt64>(lo) << 32) | hi;
  }

  /// Extracts the single manifold perimeter of a triangulated finite-water surface. The returned vertex indices
  /// reference the source mesh directly, allowing a junction strip to duplicate the exact shoreline attributes.
  bool BuildBoundaryLoop(plArrayPtr<const plWaterGeometryVertex> vertices, plArrayPtr<const plUInt32> indices,
    plDynamicArray<plUInt32>& out_loop)
  {
    out_loop.Clear();
    if (vertices.GetCount() < 3 || indices.GetCount() < 3 || indices.GetCount() % 3 != 0)
      return false;

    plHashTable<plUInt64, BoundaryEdge> edges;
    edges.Reserve(indices.GetCount());
    for (plUInt32 i = 0; i < indices.GetCount(); i += 3)
    {
      const plUInt32 triangle[3] = {indices[i], indices[i + 1], indices[i + 2]};
      for (plUInt32 edgeIndex = 0; edgeIndex < 3; ++edgeIndex)
      {
        const plUInt32 a = triangle[edgeIndex];
        const plUInt32 b = triangle[(edgeIndex + 1) % 3];
        if (a >= vertices.GetCount() || b >= vertices.GetCount() || a == b)
          return false;

        const plUInt64 key = MakeUndirectedEdgeKey(a, b);
        BoundaryEdge edge;
        if (edges.TryGetValue(key, edge))
        {
          ++edge.m_uiUseCount;
          edges[key] = edge;
        }
        else
        {
          edge.m_uiA = a;
          edge.m_uiB = b;
          edge.m_uiUseCount = 1;
          edges.Insert(key, edge);
        }
      }
    }

    plDynamicArray<plUInt32> neighborA;
    plDynamicArray<plUInt32> neighborB;
    neighborA.SetCount(vertices.GetCount());
    neighborB.SetCount(vertices.GetCount());
    for (plUInt32 i = 0; i < vertices.GetCount(); ++i)
    {
      neighborA[i] = plInvalidIndex;
      neighborB[i] = plInvalidIndex;
    }

    auto AddNeighbor = [&](plUInt32 vertex, plUInt32 neighbor) -> bool
    {
      if (neighborA[vertex] == plInvalidIndex)
        neighborA[vertex] = neighbor;
      else if (neighborB[vertex] == plInvalidIndex && neighborA[vertex] != neighbor)
        neighborB[vertex] = neighbor;
      else
        return false;
      return true;
    };

    plUInt32 uiStart = plInvalidIndex;
    plUInt32 uiBoundaryEdgeCount = 0;
    for (auto it = edges.GetIterator(); it.IsValid(); ++it)
    {
      const BoundaryEdge& edge = it.Value();
      if (edge.m_uiUseCount > 2)
        return false;
      if (edge.m_uiUseCount != 1)
        continue;
      if (!AddNeighbor(edge.m_uiA, edge.m_uiB) || !AddNeighbor(edge.m_uiB, edge.m_uiA))
        return false;
      uiStart = edge.m_uiA;
      ++uiBoundaryEdgeCount;
    }
    if (uiStart == plInvalidIndex)
      return false;

    plUInt32 uiPrevious = plInvalidIndex;
    plUInt32 uiCurrent = uiStart;
    for (plUInt32 guard = 0; guard <= vertices.GetCount(); ++guard)
    {
      out_loop.PushBack(uiCurrent);
      const plUInt32 uiNext = neighborA[uiCurrent] != uiPrevious ? neighborA[uiCurrent] : neighborB[uiCurrent];
      if (uiNext == plInvalidIndex)
        return false;
      uiPrevious = uiCurrent;
      uiCurrent = uiNext;
      if (uiCurrent == uiStart)
        return out_loop.GetCount() >= 3 && out_loop.GetCount() == uiBoundaryEdgeCount;
    }

    return false;
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
      mesh.SetColor0(i, plColorLinearUB(plMath::ColorFloatToByte(plMath::Saturate(source.m_fProfileBlend)), source.m_uiProfileA, source.m_uiProfileB, 255));
    }
    for (plUInt32 i = 0; i < indices.GetCount(); i += 3)
      mesh.SetTriangleIndices(i / 3, indices[i], indices[i + 1], indices[i + 2]);

    plStringBuilder resourceName;
    resourceName.SetFormat("WaterFiniteMesh_{}_{}_{}", uiNetworkId, uiRevision, szSuffix);
    return plResourceManager::CreateResource<plMeshBufferResource>(resourceName, std::move(mesh), resourceName);
  }

  bool BuildFiniteWaterVolume(plArrayPtr<const plWaterGeometryVertex> surfaceVertices, plArrayPtr<const plUInt32> surfaceIndices,
    float fDefaultDepth, plDynamicArray<plWaterGeometryVertex>& out_vertices, plDynamicArray<plUInt32>& out_indices,
    bool bStructuredGrid = false, plArrayPtr<const plVec3> authoredBoundary = {})
  {
    out_vertices.Clear();
    out_indices.Clear();
    if (!ValidateFiniteWaterTopology(surfaceVertices, surfaceIndices))
      return false;

    const plUInt32 uiSurfaceVertexCount = surfaceVertices.GetCount();
    const plUInt64 uiVolumeVertexCapacity = static_cast<plUInt64>(uiSurfaceVertexCount) * 2 + authoredBoundary.GetCount() * 2ull;
    const plUInt64 uiBaseVolumeIndexCapacity = static_cast<plUInt64>(surfaceIndices.GetCount()) * 2;
    if (uiVolumeVertexCapacity > s_uiMaximumFiniteWaterVertices * 2ull || uiBaseVolumeIndexCapacity > s_uiMaximumFiniteWaterIndices * 2ull)
      return false;

    out_vertices.Reserve(static_cast<plUInt32>(uiVolumeVertexCapacity));
    out_vertices.PushBackRange(surfaceVertices);
    for (const plWaterGeometryVertex& source : surfaceVertices)
    {
      plWaterGeometryVertex bottom = source;
      bottom.m_vPosition.z -= source.m_fDepth > 0.0f ? source.m_fDepth : plMath::Max(fDefaultDepth, 0.01f);
      bottom.m_vFlow = plVec3::MakeZero();
      bottom.m_fWaveAttenuation = 0.0f;
      out_vertices.PushBack(bottom);
    }

    // Reserve only the known top and bottom. Side storage is proportional to the explicit perimeter below.
    out_indices.Reserve(static_cast<plUInt32>(uiBaseVolumeIndexCapacity));
    for (plUInt32 i = 0; i + 2 < surfaceIndices.GetCount(); i += 3)
    {
      const plUInt32 a = surfaceIndices[i], b = surfaceIndices[i + 1], c = surfaceIndices[i + 2];
      out_indices.PushBack(a);
      out_indices.PushBack(b);
      out_indices.PushBack(c);
      out_indices.PushBack(a + uiSurfaceVertexCount);
      out_indices.PushBack(c + uiSurfaceVertexCount);
      out_indices.PushBack(b + uiSurfaceVertexCount);

    }

    auto EmitIndexedSide = [&](plUInt32 a, plUInt32 b)
    {
      out_indices.PushBack(a);
      out_indices.PushBack(a + uiSurfaceVertexCount);
      out_indices.PushBack(b);
      out_indices.PushBack(b);
      out_indices.PushBack(a + uiSurfaceVertexCount);
      out_indices.PushBack(b + uiSurfaceVertexCount);
    };

    if (bStructuredGrid)
    {
      plUInt32 uiColumns = 1;
      const float fFirstLongitudinal = surfaceVertices[0].m_vTexCoord.x;
      while (uiColumns < uiSurfaceVertexCount &&
             plMath::Abs(surfaceVertices[uiColumns].m_vTexCoord.x - fFirstLongitudinal) < 0.0001f)
        ++uiColumns;
      if (uiColumns < 2 || uiSurfaceVertexCount % uiColumns != 0)
        return false;

      const plUInt32 uiRows = uiSurfaceVertexCount / uiColumns;
      for (plUInt32 column = 0; column + 1 < uiColumns; ++column)
      {
        EmitIndexedSide(column + 1, column);
        const plUInt32 lastRow = (uiRows - 1) * uiColumns;
        EmitIndexedSide(lastRow + column, lastRow + column + 1);
      }
      for (plUInt32 row = 0; row + 1 < uiRows; ++row)
      {
        EmitIndexedSide(row * uiColumns, (row + 1) * uiColumns);
        EmitIndexedSide((row + 1) * uiColumns + uiColumns - 1, row * uiColumns + uiColumns - 1);
      }
    }
    else if (authoredBoundary.GetCount() >= 3)
    {
      const plUInt32 uiSideVertexOffset = out_vertices.GetCount();
      for (const plVec3& position : authoredBoundary)
      {
        plWaterGeometryVertex top;
        top.m_vPosition = position;
        top.m_fWaveAttenuation = 0.0f;
        top.m_fDepth = plMath::Max(fDefaultDepth, 0.01f);
        out_vertices.PushBack(top);
        plWaterGeometryVertex bottom = top;
        bottom.m_vPosition.z -= top.m_fDepth;
        out_vertices.PushBack(bottom);
      }
      for (plUInt32 i = 0; i < authoredBoundary.GetCount(); ++i)
      {
        const plUInt32 next = (i + 1) % authoredBoundary.GetCount();
        const plUInt32 aTop = uiSideVertexOffset + i * 2;
        const plUInt32 aBottom = aTop + 1;
        const plUInt32 bTop = uiSideVertexOffset + next * 2;
        const plUInt32 bBottom = bTop + 1;
        out_indices.PushBack(aTop); out_indices.PushBack(aBottom); out_indices.PushBack(bTop);
        out_indices.PushBack(bTop); out_indices.PushBack(aBottom); out_indices.PushBack(bBottom);
      }
    }
    return true;
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
    bool bRebuildJunctions = false;
    for (BodySlot& slot : m_Bodies)
    {
      if (!slot.m_bInUse || !slot.m_Data.m_Descriptor.m_hProfile.IsValid())
        continue;

      plResourceLock<plWaterProfileResource> profile(slot.m_Data.m_Descriptor.m_hProfile, plResourceAcquireMode::AllowLoadingFallback_NeverFail);
      if (!profile || profile->GetCurrentResourceChangeCounter() == slot.m_Data.m_uiProfileChangeCounter)
        continue;

      const plWaterBodyDescriptor descriptor = slot.m_Data.m_Descriptor;
      BodySlot candidate;
      candidate.m_bInUse = true;
      candidate.m_uiGeneration = slot.m_uiGeneration;
      candidate.m_Data.m_BodyId = slot.m_Data.m_BodyId;
      candidate.m_Data.m_uiGeometryRevision = slot.m_Data.m_uiGeometryRevision;
      PrepareBody(candidate, descriptor);
      if (descriptor.m_Type == plWaterBodyType::Ocean || candidate.m_Data.m_bGeometryValid)
      {
        slot.m_Data = std::move(candidate.m_Data);
        bRebuildJunctions = true;
      }
      else
        slot.m_Data.m_uiProfileChangeCounter = profile->GetCurrentResourceChangeCounter();
    }
    if (bRebuildJunctions)
    {
      for (JunctionSlot& junction : m_Junctions)
      {
        if (junction.m_bInUse)
          PrepareJunction(junction);
      }
    }
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

  if (cvar_WaterDebugShowJunctions || cvar_WaterDebugShowFlow)
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
        if (slot.m_RenderData.m_uiStitchedLakeVertexCount >= 2)
        {
          const float z = slot.m_Data.m_vCenter.z + 0.08f;
          const plDebugRenderer::Line seam(
            plVec3(slot.m_RenderData.m_vStitchedLakeLeft.x, slot.m_RenderData.m_vStitchedLakeLeft.y, z),
            plVec3(slot.m_RenderData.m_vStitchedLakeRight.x, slot.m_RenderData.m_vStitchedLakeRight.y, z), plColor::LimeGreen);
          plDebugRenderer::DrawLines(GetWorld(), plMakeArrayPtr(&seam, 1), plColor::LimeGreen);
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
  PrepareBody(slot, descriptor);
  slot.m_Data.m_BodyId.m_uiIndex = uiIndex;
  slot.m_Data.m_BodyId.m_uiGeneration = slot.m_uiGeneration;
  return slot.m_Data.m_BodyId;
}

bool plWaterWorldModule::UpdateBody(plWaterBodyId bodyId, const plWaterBodyDescriptor& descriptor)
{
  PL_LOCK(m_Mutex);
  if (!IsBodyIdValid(bodyId))
    return false;

  BodySlot& slot = m_Bodies[bodyId.m_uiIndex];
  BodySlot candidate;
  candidate.m_bInUse = true;
  candidate.m_uiGeneration = slot.m_uiGeneration;
  candidate.m_Data.m_BodyId = bodyId;
  candidate.m_Data.m_uiGeometryRevision = slot.m_Data.m_uiGeometryRevision;
  PrepareBody(candidate, descriptor);
  candidate.m_Data.m_BodyId = bodyId;
  if (descriptor.m_Type != plWaterBodyType::Ocean && !candidate.m_Data.m_bGeometryValid)
  {
    plLog::Warning("Water body '{}' produced invalid finite geometry. Keeping revision {} until the spline or profile is repaired.",
      descriptor.m_uiNetworkId, slot.m_Data.m_uiGeometryRevision);
    return true;
  }
  slot.m_Data = std::move(candidate.m_Data);
  for (JunctionSlot& junction : m_Junctions)
  {
    if (junction.m_bInUse && (junction.m_Data.m_BodyA == bodyId || junction.m_Data.m_BodyB == bodyId))
      PrepareJunction(junction);
  }
  return true;
}

void plWaterWorldModule::UnregisterBody(plWaterBodyId& inout_bodyId)
{
  PL_LOCK(m_Mutex);
  if (IsBodyIdValid(inout_bodyId))
  {
    BodySlot& slot = m_Bodies[inout_bodyId.m_uiIndex];
    slot.m_bInUse = false;
    slot.m_Data = plWaterBodySnapshot();
    ++slot.m_uiGeneration;
    if (slot.m_uiGeneration == 0)
      slot.m_uiGeneration = 1;
  }
  inout_bodyId.Invalidate();
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
  PrepareJunction(m_Junctions[uiIndex]);
  PrepareOtherJunctions(&m_Junctions[uiIndex]);
  return uiIndex;
}

bool plWaterWorldModule::UpdateJunction(plUInt32 uiIndex, const plWaterJunctionDescriptor& descriptor)
{
  PL_LOCK(m_Mutex);
  if (uiIndex >= m_Junctions.GetCount() || !m_Junctions[uiIndex].m_bInUse)
    return false;
  m_Junctions[uiIndex].m_Data = descriptor;
  PrepareJunction(m_Junctions[uiIndex]);
  PrepareOtherJunctions(&m_Junctions[uiIndex]);
  return true;
}

void plWaterWorldModule::UnregisterJunction(plUInt32& inout_uiIndex)
{
  PL_LOCK(m_Mutex);
  if (inout_uiIndex < m_Junctions.GetCount())
  {
    m_Junctions[inout_uiIndex].m_bInUse = false;
    m_Junctions[inout_uiIndex].m_RenderData = plWaterJunctionRenderSnapshot();
    PrepareOtherJunctions(&m_Junctions[inout_uiIndex]);
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
  out_entry.AbsorptionAndRoughness = plVec4(profile.m_vAbsorption.x, profile.m_vAbsorption.y, profile.m_vAbsorption.z, profile.m_fRoughness);
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
    if (slot.m_bInUse && slot.m_RenderData.m_bValid)
      out_snapshots.PushBack(slot.m_RenderData);
  }
}

void plWaterWorldModule::PrepareJunction(JunctionSlot& slot)
{
  const plUInt32 uiRevision = slot.m_RenderData.m_uiRevision + 1;
  slot.m_RenderData = plWaterJunctionRenderSnapshot();
  slot.m_RenderData.m_Descriptor = slot.m_Data;
  slot.m_RenderData.m_uiRevision = uiRevision;

  if (!IsBodyIdValid(slot.m_Data.m_BodyA) || !IsBodyIdValid(slot.m_Data.m_BodyB))
    return;

  const plWaterBodySnapshot& bodyA = m_Bodies[slot.m_Data.m_BodyA.m_uiIndex].m_Data;
  const plWaterBodySnapshot& bodyB = m_Bodies[slot.m_Data.m_BodyB.m_uiIndex].m_Data;
  const plWaterBodySnapshot* pRiver = nullptr;
  const plWaterBodySnapshot* pSecondary = nullptr;
  plVec3 riverAnchor = plVec3::MakeZero();
  plVec3 secondaryAnchor = plVec3::MakeZero();
  if (bodyA.m_Descriptor.m_Type == plWaterBodyType::River && bodyB.m_Descriptor.m_Type != plWaterBodyType::River)
  {
    pRiver = &bodyA;
    pSecondary = &bodyB;
    riverAnchor = slot.m_Data.m_vAnchorA;
    secondaryAnchor = slot.m_Data.m_vAnchorB;
  }
  else if (bodyB.m_Descriptor.m_Type == plWaterBodyType::River && bodyA.m_Descriptor.m_Type != plWaterBodyType::River)
  {
    pRiver = &bodyB;
    pSecondary = &bodyA;
    riverAnchor = slot.m_Data.m_vAnchorB;
    secondaryAnchor = slot.m_Data.m_vAnchorA;
  }
  else
  {
    return; // Other typed junction pairs continue to use deterministic query blending only.
  }

  if (!pRiver->m_bGeometryValid || !pSecondary->m_bGeometryValid || pRiver->m_Vertices.GetCount() < 4)
    return;

  plUInt32 uiColumns = 1;
  const float fFirstCoordinate = pRiver->m_Vertices[0].m_vTexCoord.x;
  while (uiColumns < pRiver->m_Vertices.GetCount() &&
         plMath::Abs(pRiver->m_Vertices[uiColumns].m_vTexCoord.x - fFirstCoordinate) < 0.0001f)
  {
    ++uiColumns;
  }
  if (uiColumns < 3 || pRiver->m_Vertices.GetCount() % uiColumns != 0)
    return;

  const plUInt32 uiRows = pRiver->m_Vertices.GetCount() / uiColumns;
  auto RowCenter = [&](plUInt32 uiRow)
  {
    return (pRiver->m_Vertices[uiRow * uiColumns].m_vPosition + pRiver->m_Vertices[uiRow * uiColumns + uiColumns - 1].m_vPosition) * 0.5f;
  };

  const bool bAttachAtEnd = (RowCenter(uiRows - 1) - riverAnchor).GetLengthSquared() < (RowCenter(0) - riverAnchor).GetLengthSquared();
  plVec3 outward = bAttachAtEnd ? RowCenter(uiRows - 1) - RowCenter(uiRows - 2) : RowCenter(0) - RowCenter(1);
  outward.z = 0.0f;
  outward.NormalizeIfNotZero(plVec3::MakeAxisX()).IgnoreResult();
  const float fBlendDistance = plMath::Max(slot.m_Data.m_fBlendDistance, 0.5f);
  const plVec3 desiredStart = riverAnchor - outward * fBlendDistance;

  plUInt32 uiStartRow = 0;
  float fBestDistanceSquared = plMath::HighValue<float>();
  for (plUInt32 uiRow = 0; uiRow < uiRows; ++uiRow)
  {
    const float fDistanceSquared = (RowCenter(uiRow) - desiredStart).GetLengthSquared();
    if (fDistanceSquared < fBestDistanceSquared)
    {
      fBestDistanceSquared = fDistanceSquared;
      uiStartRow = uiRow;
    }
  }

  const plVec3 rowFirst = pRiver->m_Vertices[uiStartRow * uiColumns].m_vPosition;
  const plVec3 rowLast = pRiver->m_Vertices[uiStartRow * uiColumns + uiColumns - 1].m_vPosition;
  const plVec3 startCenter = (rowFirst + rowLast) * 0.5f;
  plVec2 patchDirection = (riverAnchor - startCenter).GetAsVec2();
  patchDirection.NormalizeIfNotZero(outward.GetAsVec2()).IgnoreResult();
  const plVec2 patchPerpendicular(-patchDirection.y, patchDirection.x);

  // Patch column 0 is the +perpendicular ("left") side. The river row may run either way; remember the
  // orientation so row 0 can copy the river vertices verbatim - the weld must be exact, not re-derived.
  const bool bRowForward = (rowFirst - startCenter).GetAsVec2().Dot(patchPerpendicular) >= 0.0f;
  auto RiverRowVertex = [&](plUInt32 uiColumn) -> const plWaterGeometryVertex&
  {
    const plUInt32 uiRiverColumn = bRowForward ? uiColumn : uiColumns - 1 - uiColumn;
    return pRiver->m_Vertices[uiStartRow * uiColumns + uiRiverColumn];
  };
  const plVec3 startLeft = RiverRowVertex(0).m_vPosition;
  const plVec3 startRight = RiverRowVertex(uiColumns - 1).m_vPosition;
  const plVec2 localRiverFlow = RiverRowVertex(uiColumns / 2).m_vFlow.GetAsVec2();
  const float fLocalRiverFlowScale = plMath::Max(localRiverFlow.GetLength(), 0.0f);
  const plVec2 riverFlowDirection = NormalizeOr(localRiverFlow, patchDirection);
  const float fRiverFlowScalar = pRiver->m_Descriptor.m_fFlowSpeed * pRiver->m_Profile.m_fFlowScale;
  const float fRiverFlowScalarSign = fRiverFlowScalar < 0.0f ? -1.0f : 1.0f;
  const plVec2 effectiveRiverFlowDirection = riverFlowDirection * fRiverFlowScalarSign;
  // River ribbons are authored start -> end. At an end attachment that direction enters the receiving body;
  // at a start attachment it points back into the river and therefore represents lake outflow. A negative
  // body FlowSpeed reverses that result without requiring the spline or junction anchors to be rebuilt.
  const float fFlowDirectionSign = effectiveRiverFlowDirection.Dot(patchDirection) >= 0.0f ? 1.0f : -1.0f;

  const float fStartHalfWidth = (startLeft - startRight).GetLength() * 0.5f;
  const bool bLakeReceiver = pSecondary->m_Descriptor.m_Type == plWaterBodyType::Lake;
  const float fDesiredEndHalfWidth = bLakeReceiver
                                       ? plMath::Max(fStartHalfWidth * 1.1f, fStartHalfWidth + 0.25f)
                                       : plMath::Max(fStartHalfWidth * 1.5f, fStartHalfWidth + 1.0f);
  const float fCellSize = plMath::Clamp(
    plMath::Min(pRiver->m_Profile.m_fFiniteMeshCellSize, pSecondary->m_Profile.m_fFiniteMeshCellSize), 0.25f, 4.0f);

  // The ocean surface sits at the owner height, but a lake surface sits at its authored control-point height
  // (see SampleBody). Using the owner height here can sink the patch below the opaque floor, where the
  // occlusion clip removes it and leaves the cutout as a hole.
  float fSecondarySurfaceZ = pSecondary->m_Descriptor.m_fBaseHeight;
  if (pSecondary->m_Descriptor.m_Type == plWaterBodyType::Lake && !pSecondary->m_Descriptor.m_ControlPoints.IsEmpty())
  {
    fSecondarySurfaceZ = 0.0f;
    for (const plWaterControlPoint& point : pSecondary->m_Descriptor.m_ControlPoints)
      fSecondarySurfaceZ += point.m_vPosition.z;
    fSecondarySurfaceZ /= static_cast<float>(pSecondary->m_Descriptor.m_ControlPoints.GetCount());
  }

  plVec2 receiverAdvance = patchDirection;
  plVec2 receiverLateral = patchPerpendicular;
  float fReceiverPenetration = fBlendDistance;
  plVec3 shorelinePoint = secondaryAnchor;
  plDynamicArray<plUInt32> lakeBoundaryLoop;
  plDynamicArray<plUInt32> lakeShoreChain;
  float fMaximumLakeSeamError = 0.0f;
  if (bLakeReceiver)
  {
    // Use the already-triangulated lake perimeter as the receiving edge. The transition can then duplicate
    // those exact vertices rather than ending on a separately sampled row inside the lake.
    if (!BuildBoundaryLoop(pSecondary->m_Vertices, pSecondary->m_Indices, lakeBoundaryLoop))
      return;

    float fSignedArea = 0.0f;
    float fBestDistanceSquared = plMath::HighValue<float>();
    float fBestEdgeFraction = 0.0f;
    plUInt32 uiBestEdge = plInvalidIndex;
    for (plUInt32 i = 0; i < lakeBoundaryLoop.GetCount(); ++i)
    {
      const plVec2 a = pSecondary->m_Vertices[lakeBoundaryLoop[i]].m_vPosition.GetAsVec2();
      const plVec2 b = pSecondary->m_Vertices[lakeBoundaryLoop[(i + 1) % lakeBoundaryLoop.GetCount()]].m_vPosition.GetAsVec2();
      fSignedArea += a.x * b.y - b.x * a.y;
      const plVec2 edge = b - a;
      const float fLengthSquared = edge.GetLengthSquared();
      if (fLengthSquared <= plMath::SmallEpsilon<float>())
        continue;
      const float t = plMath::Clamp((secondaryAnchor.GetAsVec2() - a).Dot(edge) / fLengthSquared, 0.0f, 1.0f);
      const plVec2 closest = a + edge * t;
      const float fDistanceSquared = (secondaryAnchor.GetAsVec2() - closest).GetLengthSquared();
      if (fDistanceSquared < fBestDistanceSquared)
      {
        fBestDistanceSquared = fDistanceSquared;
        fBestEdgeFraction = t;
        uiBestEdge = i;
        shorelinePoint = plVec3(closest.x, closest.y,
          plMath::Lerp(pSecondary->m_Vertices[lakeBoundaryLoop[i]].m_vPosition.z,
            pSecondary->m_Vertices[lakeBoundaryLoop[(i + 1) % lakeBoundaryLoop.GetCount()]].m_vPosition.z, t));
      }
    }
    if (uiBestEdge == plInvalidIndex)
      return;

    const plUInt32 uiNextEdgeVertex = (uiBestEdge + 1) % lakeBoundaryLoop.GetCount();
    const plVec2 shorelineTangent = NormalizeOr(
      pSecondary->m_Vertices[lakeBoundaryLoop[uiNextEdgeVertex]].m_vPosition.GetAsVec2() -
        pSecondary->m_Vertices[lakeBoundaryLoop[uiBestEdge]].m_vPosition.GetAsVec2(),
      patchPerpendicular);
    receiverAdvance = fSignedArea >= 0.0f
                        ? plVec2(-shorelineTangent.y, shorelineTangent.x)
                        : plVec2(shorelineTangent.y, -shorelineTangent.x);
    receiverLateral = shorelineTangent;
    if (receiverLateral.Dot(patchPerpendicular) < 0.0f)
      receiverLateral = -receiverLateral;

    const plUInt32 uiCenter = fBestEdgeFraction < 0.5f ? uiBestEdge : uiNextEdgeVertex;
    const plUInt32 uiPrevious = (uiCenter + lakeBoundaryLoop.GetCount() - 1) % lakeBoundaryLoop.GetCount();
    const plUInt32 uiNext = (uiCenter + 1) % lakeBoundaryLoop.GetCount();
    const plVec2 centerPosition = pSecondary->m_Vertices[lakeBoundaryLoop[uiCenter]].m_vPosition.GetAsVec2();
    const float fPreviousLateral = (pSecondary->m_Vertices[lakeBoundaryLoop[uiPrevious]].m_vPosition.GetAsVec2() - centerPosition).Dot(receiverLateral);
    const float fNextLateral = (pSecondary->m_Vertices[lakeBoundaryLoop[uiNext]].m_vPosition.GetAsVec2() - centerPosition).Dot(receiverLateral);
    const bool bPositiveLateralIsNext = fNextLateral >= fPreviousLateral;
    auto AdvanceBoundary = [&](plUInt32 index, bool bPositiveDirection)
    {
      const bool bAdvanceNext = bPositiveDirection == bPositiveLateralIsNext;
      return bAdvanceNext ? (index + 1) % lakeBoundaryLoop.GetCount()
                          : (index + lakeBoundaryLoop.GetCount() - 1) % lakeBoundaryLoop.GetCount();
    };
    auto ExpandToWidth = [&](bool bPositiveDirection)
    {
      plUInt32 index = uiCenter;
      float fDistance = 0.0f;
      for (plUInt32 guard = 0; guard + 2 < lakeBoundaryLoop.GetCount() && fDistance < fDesiredEndHalfWidth; ++guard)
      {
        const plUInt32 next = AdvanceBoundary(index, bPositiveDirection);
        fDistance += (pSecondary->m_Vertices[lakeBoundaryLoop[next]].m_vPosition -
                      pSecondary->m_Vertices[lakeBoundaryLoop[index]].m_vPosition).GetLength();
        index = next;
      }
      return index;
    };

    const plUInt32 uiLeft = ExpandToWidth(true);
    const plUInt32 uiRight = ExpandToWidth(false);
    plUInt32 index = uiLeft;
    for (plUInt32 guard = 0; guard < lakeBoundaryLoop.GetCount(); ++guard)
    {
      lakeShoreChain.PushBack(lakeBoundaryLoop[index]);
      if (index == uiRight)
        break;
      index = AdvanceBoundary(index, false);
    }
    if (lakeShoreChain.GetCount() < 2 || lakeShoreChain.PeekBack() != lakeBoundaryLoop[uiRight])
      return;

    // The current plume begins on the actual shoreline. There is no lake-side penetration or overdraw.
    fReceiverPenetration = 0.0f;
  }

  plVec3 endCenter(secondaryAnchor.x, secondaryAnchor.y, fSecondarySurfaceZ);
  plVec3 endLeft;
  plVec3 endRight;
  float fEndHalfWidth = fDesiredEndHalfWidth;
  if (bLakeReceiver)
  {
    endLeft = pSecondary->m_Vertices[lakeShoreChain[0]].m_vPosition;
    endRight = pSecondary->m_Vertices[lakeShoreChain.PeekBack()].m_vPosition;
    endCenter = (endLeft + endRight) * 0.5f;
    fEndHalfWidth = (endLeft - endRight).GetLength() * 0.5f;
  }
  else
  {
    endCenter += plVec3(receiverAdvance.x, receiverAdvance.y, 0.0f) * fReceiverPenetration;
    endLeft = endCenter + plVec3(receiverLateral.x, receiverLateral.y, 0.0f) * fEndHalfWidth;
    endRight = endCenter - plVec3(receiverLateral.x, receiverLateral.y, 0.0f) * fEndHalfWidth;
  }
  const float fPatchLength = (endCenter - startCenter).GetLength();
  plVec2 cutoutDirection = (endCenter - startCenter).GetAsVec2();
  cutoutDirection.NormalizeIfNotZero(patchDirection).IgnoreResult();
  // A lake junction needs at least three longitudinal segments: one to leave the river material, one to
  // converge all material/displacement inputs, and one full pure-lake strip before the stitched shoreline.
  const plUInt32 uiMinimumPatchSegments = bLakeReceiver ? 3u : 1u;
  const plUInt32 uiPatchSegments = plMath::Clamp<plUInt32>(
    static_cast<plUInt32>(plMath::Ceil(fPatchLength / fCellSize)), uiMinimumPatchSegments, 128);
  const plUInt32 uiPatchRows = bLakeReceiver ? uiPatchSegments : uiPatchSegments + 1;
  // Column count must match the river cross-section exactly: row 0 copies those vertices one to one, and any
  // resampling would leave T-junction cracks along the weld once waves displace the shared edge.
  const plUInt32 uiPatchColumns = uiColumns;

  // The lake current extends beyond the narrow topology bridge and is evaluated on the lake's own tessellated
  // surface. Keeping it separate from the cutout prevents a long replacement trapezoid from damaging lake
  // topology while still making water visibly enter or leave the basin.
  const float fRiverPush = plMath::Abs(fRiverFlowScalar) * fLocalRiverFlowScale;
  const float fFlowFieldLength = bLakeReceiver
                                   ? plMath::Clamp(plMath::Max(fBlendDistance * 1.5f, fStartHalfWidth * 4.0f), 3.0f, 24.0f)
                                   : 0.0f;
  const float fFlowFieldHalfWidth = bLakeReceiver
                                      ? plMath::Clamp(plMath::Max(fStartHalfWidth * 1.35f, fCellSize * 1.5f), 0.75f, 12.0f)
                                      : 0.0f;

  // River inflow contests waves travelling back into the mouth. Lake outflow through a river-start junction
  // moves with the receiving water instead, so the lake profile reaches farther upstream and does not create
  // artificial collision foam.
  const float fWavePush = plMath::Max(pSecondary->m_Profile.m_fWaveAmplitude, 0.0f) * plMath::Max(pSecondary->m_Profile.m_fWindSpeed, 0.1f);
  const float fPushback = fWavePush / plMath::Max(fWavePush + fRiverPush, 0.0001f);
  const float fBlendMidpoint = fFlowDirectionSign > 0.0f ? plMath::Lerp(0.65f, 0.35f, fPushback) : 0.3f;
  const float fBlendHalfWidth = plMath::Max(plMath::Min(fBlendMidpoint, 1.0f - fBlendMidpoint), 0.05f);
  const float fContest = fFlowDirectionSign > 0.0f ? 4.0f * fPushback * (1.0f - fPushback) : 0.0f;
  slot.m_RenderData.m_fMouthFoamStrength = fContest * plMath::Min((fRiverPush + fWavePush) * 0.25f, 1.0f);

  // The secondary's wave attenuation field at a position: oceans are unattenuated, lakes fade waves toward
  // their authored boundary. The patch must reproduce this exactly or its edges displace differently than
  // the surface they abut.
  auto SecondaryAttenuationAt = [&](const plVec2& vPosition) -> float
  {
    if (pSecondary->m_Descriptor.m_Type != plWaterBodyType::Lake || pSecondary->m_Descriptor.m_ControlPoints.GetCount() < 3)
      return 1.0f;
    float fBestDistanceSquared = plMath::HighValue<float>();
    const auto& boundary = pSecondary->m_Descriptor.m_ControlPoints;
    for (plUInt32 i = 0; i < boundary.GetCount(); ++i)
    {
      const plVec2 a = boundary[i].m_vPosition.GetAsVec2();
      const plVec2 b = boundary[(i + 1) % boundary.GetCount()].m_vPosition.GetAsVec2();
      const plVec2 edge = b - a;
      const float fLengthSquared = edge.GetLengthSquared();
      const float fEdgeT = fLengthSquared > plMath::SmallEpsilon<float>() ? plMath::Clamp((vPosition - a).Dot(edge) / fLengthSquared, 0.0f, 1.0f) : 0.0f;
      fBestDistanceSquared = plMath::Min(fBestDistanceSquared, (vPosition - (a + edge * fEdgeT)).GetLengthSquared());
    }
    return plMath::SmoothStep(plMath::Sqrt(fBestDistanceSquared), 0.0f, plMath::Max(pSecondary->m_Profile.m_fShoreWaveFadeDistance, 0.01f));
  };

  const float fLateralFeather = plMath::Clamp(fBlendDistance * 0.5f, 1.0f, 6.0f);
  plDynamicArray<plWaterGeometryVertex> vertices;
  plDynamicArray<plUInt32> indices;
  const plUInt32 uiGridVertexCount = uiPatchRows * uiPatchColumns;
  const plUInt32 uiLakeBoundaryVertexCount = bLakeReceiver ? lakeShoreChain.GetCount() : 0;
  vertices.SetCount(uiGridVertexCount + uiLakeBoundaryVertexCount);
  indices.Reserve(uiPatchRows * (uiPatchColumns + uiLakeBoundaryVertexCount) * 6);
  for (plUInt32 row = 0; row < uiPatchRows; ++row)
  {
    const float t = static_cast<float>(row) / static_cast<float>(uiPatchSegments);
    const float shape = t * t * (3.0f - 2.0f * t);
    const float fBlendAlong = plMath::SmoothStep(t, fBlendMidpoint - fBlendHalfWidth, fBlendMidpoint + fBlendHalfWidth);
    const float fLakeBlendEnd = static_cast<float>(uiPatchSegments - 1) / static_cast<float>(uiPatchSegments);
    const float fLakeBlendWidth = plMath::Max(2.0f / static_cast<float>(uiPatchSegments), 0.3f);
    const float fLakeMaterialConvergence = bLakeReceiver
                                             ? plMath::SmoothStep(t, plMath::Max(fLakeBlendEnd - fLakeBlendWidth, 0.1f), fLakeBlendEnd)
                                             : 0.0f;
    const plVec3 left = plMath::Lerp(startLeft, endLeft, shape);
    const plVec3 right = plMath::Lerp(startRight, endRight, shape);
    const float fWidth = (left - right).GetLength();
    // The lateral feather turns the patch pure-secondary at its side edges, which cut through open water
    // once the trapezoid has widened past the river mouth. It must stay off at the weld row, where the
    // side columns are the river banks and remain river water.
    const float fSideRamp = plMath::SmoothStep(t, 0.0f, 0.35f);
    for (plUInt32 column = 0; column < uiPatchColumns; ++column)
    {
      const float lateral = static_cast<float>(column) / static_cast<float>(uiPatchColumns - 1);
      const plWaterGeometryVertex& riverVertex = RiverRowVertex(column);
      plWaterGeometryVertex& vertex = vertices[row * uiPatchColumns + column];
      if (row == 0)
      {
        // Exact weld: identical position and identical displacement inputs mean the shared edge transforms
        // bit-for-bit like the river's own row, so no crack can open regardless of wave state.
        vertex = riverVertex;
        vertex.m_vTexCoord = plVec2(0.0f, lateral);
        vertex.m_fProfileBlend = 0.0f;
        continue;
      }

      vertex.m_vPosition = plMath::Lerp(left, right, lateral);
      const float fDistanceToSide = plMath::Min(lateral, 1.0f - lateral) * fWidth;
      const float fLateralOcean = (1.0f - plMath::SmoothStep(fDistanceToSide, 0.0f, fLateralFeather)) * fSideRamp;
      const float fBaseBlend2D = plMath::Saturate(1.0f - (1.0f - fBlendAlong) * (1.0f - fLateralOcean));
      // Blend every vertex-driven input to pure lake before the final strip. That strip and the lake draw then
      // evaluate identical displacement, normal, foam and optical endpoints across the shared shoreline.
      const float fBlend2D = plMath::Saturate(1.0f - (1.0f - fBaseBlend2D) * (1.0f - fLakeMaterialConvergence));

      plVec2 flowDirection = riverVertex.m_vFlow.GetAsVec2();
      flowDirection.NormalizeIfNotZero(patchDirection).IgnoreResult();
      // Tangents are multiplied by the signed flow scalar in the shader. Divide the desired physical field
      // direction by that sign here so negative FlowSpeed does not reverse the lake plume twice.
      const plVec2 receivingTangentDirection = receiverAdvance * (fFlowDirectionSign * fRiverFlowScalarSign);
      plVec2 spillDirection = plMath::Lerp(flowDirection, receivingTangentDirection, fBlendAlong);
      spillDirection.NormalizeIfNotZero(receivingTangentDirection).IgnoreResult();
      // Tangents encode direction only. The localized speed/falloff lives in JunctionFlow constants; scaling
      // this vector was ineffective because mesh upload correctly normalizes tangents.
      vertex.m_vFlow = plVec3(spillDirection.x, spillDirection.y, 0.0f);
      vertex.m_vTexCoord = plVec2(t * fPatchLength, lateral);
      vertex.m_fSignedShoreDistance = plMath::Lerp(fWidth * 0.5f, -fWidth * 0.5f, lateral);
      vertex.m_fDepth = plMath::Lerp(riverVertex.m_fDepth, pSecondary->m_Descriptor.m_fDefaultDepth, fBlend2D);
      // Blend from the river cross-section's own attenuation toward the secondary body's field so both the
      // weld row and the outer edges displace exactly like the surfaces they touch.
      vertex.m_fWaveAttenuation = plMath::Lerp(riverVertex.m_fWaveAttenuation, SecondaryAttenuationAt(vertex.m_vPosition.GetAsVec2()), fBlend2D);
      vertex.m_fProfileBlend = fBlend2D;
    }
  }

  if (bLakeReceiver)
  {
    for (plUInt32 i = 0; i < lakeShoreChain.GetCount(); ++i)
    {
      // Duplicate the complete lake boundary vertex. ProfileBlend is the only changed attribute because this
      // mesh is shaded as river-primary/lake-secondary; all geometric displacement inputs remain identical.
      plWaterGeometryVertex& vertex = vertices[uiGridVertexCount + i];
      vertex = pSecondary->m_Vertices[lakeShoreChain[i]];
      vertex.m_fProfileBlend = 1.0f;
      fMaximumLakeSeamError = plMath::Max(fMaximumLakeSeamError,
        (vertex.m_vPosition - pSecondary->m_Vertices[lakeShoreChain[i]].m_vPosition).GetLength());
    }
  }

  for (plUInt32 row = 0; row + 1 < uiPatchRows; ++row)
  {
    for (plUInt32 column = 0; column + 1 < uiPatchColumns; ++column)
    {
      const plUInt32 a = row * uiPatchColumns + column;
      const plUInt32 b = a + 1;
      const plUInt32 c = a + uiPatchColumns;
      const plUInt32 d = c + 1;
      indices.PushBack(a); indices.PushBack(b); indices.PushBack(c);
      indices.PushBack(b); indices.PushBack(d); indices.PushBack(c);
    }
  }

  if (bLakeReceiver)
  {
    // Join the regular river-width row to the lake's differently sampled shoreline with an arc-length zipper.
    // Both polylines keep every source vertex, so no T-junction is introduced on either side of the seam.
    const plUInt32 uiLastGridRow = (uiPatchRows - 1) * uiPatchColumns;
    plDynamicArray<float> gridDistance;
    plDynamicArray<float> shoreDistance;
    gridDistance.SetCount(uiPatchColumns);
    shoreDistance.SetCount(uiLakeBoundaryVertexCount);
    gridDistance[0] = 0.0f;
    shoreDistance[0] = 0.0f;
    for (plUInt32 i = 1; i < uiPatchColumns; ++i)
      gridDistance[i] = gridDistance[i - 1] + (vertices[uiLastGridRow + i].m_vPosition - vertices[uiLastGridRow + i - 1].m_vPosition).GetLength();
    for (plUInt32 i = 1; i < uiLakeBoundaryVertexCount; ++i)
      shoreDistance[i] = shoreDistance[i - 1] + (vertices[uiGridVertexCount + i].m_vPosition - vertices[uiGridVertexCount + i - 1].m_vPosition).GetLength();

    const float fGridWidth = gridDistance.PeekBack();
    const float fShoreWidth = shoreDistance.PeekBack();
    if (fGridWidth <= plMath::SmallEpsilon<float>() || fShoreWidth <= plMath::SmallEpsilon<float>())
      return;

    plUInt32 grid = 0;
    plUInt32 shore = 0;
    while (grid + 1 < uiPatchColumns || shore + 1 < uiLakeBoundaryVertexCount)
    {
      const float fNextGrid = grid + 1 < uiPatchColumns ? gridDistance[grid + 1] / fGridWidth : plMath::HighValue<float>();
      const float fNextShore = shore + 1 < uiLakeBoundaryVertexCount ? shoreDistance[shore + 1] / fShoreWidth : plMath::HighValue<float>();
      if (fNextGrid <= fNextShore)
      {
        indices.PushBack(uiLastGridRow + grid);
        indices.PushBack(uiLastGridRow + grid + 1);
        indices.PushBack(uiGridVertexCount + shore);
        ++grid;
      }
      else
      {
        indices.PushBack(uiLastGridRow + grid);
        indices.PushBack(uiGridVertexCount + shore + 1);
        indices.PushBack(uiGridVertexCount + shore);
        ++shore;
      }
    }
  }

  // ---------------------------------------------------------------------------------------------------------
  // Merged junction remesh. Overlaying a separate patch draw on the river (analytic cutout + overlap margin)
  // leaves a visible split: two transparent surfaces double-blend inside the overlap band and the discard
  // boundary is a hard straight edge. Instead, cut the river's terminal segment for real and emit ONE
  // continuous mesh containing the kept river ribbon, the transition patch and the duplicated lake shoreline.
  // The weld row is shared by INDEX rather than duplicated, so a single draw carries one set of profile
  // constants, one displacement evaluation and one tessellation domain across the whole transition.
  // Requires the river to participate in no other junction, because the junction constants can only express
  // a single secondary profile per draw; multi-junction rivers keep the legacy overlap path.
  bool bMerged = false;
  {
    bool bRiverHasOtherJunction = false;
    for (const JunctionSlot& other : m_Junctions)
    {
      if (&other == &slot || !other.m_bInUse)
        continue;
      if (other.m_Data.m_BodyA == pRiver->m_BodyId || other.m_Data.m_BodyB == pRiver->m_BodyId)
      {
        bRiverHasOtherJunction = true;
        break;
      }
    }

    const plUInt32 uiKeptRows = bAttachAtEnd ? uiStartRow + 1 : uiRows - uiStartRow;
    if (!bRiverHasOtherJunction && uiKeptRows >= 2)
    {
      const plUInt32 uiKeptVertexCount = uiKeptRows * uiColumns;
      const plUInt32 uiRiverCopyOffset = bAttachAtEnd ? 0 : uiStartRow * uiColumns;
      const plUInt32 uiWeldRowBase = bAttachAtEnd ? (uiKeptRows - 1) * uiColumns : 0;

      plDynamicArray<plWaterGeometryVertex> mergedVertices;
      mergedVertices.Reserve(uiKeptVertexCount + vertices.GetCount() - uiPatchColumns);
      for (plUInt32 i = 0; i < uiKeptVertexCount; ++i)
        mergedVertices.PushBack(pRiver->m_Vertices[uiRiverCopyOffset + i]);
      // Patch rows 1..N and the duplicated lake shoreline keep their original order. Row 0 is NOT copied:
      // those are the weld vertices, which the merged mesh indexes directly inside the kept river ribbon.
      for (plUInt32 i = uiPatchColumns; i < vertices.GetCount(); ++i)
        mergedVertices.PushBack(vertices[i]);

      // The patch's row 0 was copied from the river in patch-column order (possibly mirrored); route those
      // indices back onto the shared river weld row so the strip continues the river topologically.
      auto RemapPatchIndex = [&](plUInt32 uiIndex) -> plUInt32
      {
        if (uiIndex < uiPatchColumns)
          return uiWeldRowBase + (bRowForward ? uiIndex : uiColumns - 1 - uiIndex);
        return uiKeptVertexCount + (uiIndex - uiPatchColumns);
      };

      plDynamicArray<plUInt32> mergedIndices;
      mergedIndices.Reserve((uiKeptRows - 1) * (uiColumns - 1) * 6 + indices.GetCount());
      for (plUInt32 row = 0; row + 1 < uiKeptRows; ++row)
      {
        for (plUInt32 column = 0; column + 1 < uiColumns; ++column)
        {
          const plUInt32 i0 = row * uiColumns + column;
          const plUInt32 i1 = i0 + 1;
          const plUInt32 i2 = i0 + uiColumns;
          const plUInt32 i3 = i2 + 1;
          mergedIndices.PushBack(i0); mergedIndices.PushBack(i1); mergedIndices.PushBack(i2);
          mergedIndices.PushBack(i1); mergedIndices.PushBack(i3); mergedIndices.PushBack(i2);
        }
      }
      for (plUInt32 i = 0; i < indices.GetCount(); ++i)
        mergedIndices.PushBack(RemapPatchIndex(indices[i]));

      // The merged volume derives its side skirt from the actual stitched perimeter (river far end, banks,
      // patch sides, lake shoreline). Orient it counter-clockwise to match the authored-boundary convention.
      plDynamicArray<plUInt32> mergedBoundaryLoop;
      if (BuildBoundaryLoop(mergedVertices, mergedIndices, mergedBoundaryLoop))
      {
        float fMergedSignedArea = 0.0f;
        for (plUInt32 i = 0; i < mergedBoundaryLoop.GetCount(); ++i)
        {
          const plVec2 a = mergedVertices[mergedBoundaryLoop[i]].m_vPosition.GetAsVec2();
          const plVec2 b = mergedVertices[mergedBoundaryLoop[(i + 1) % mergedBoundaryLoop.GetCount()]].m_vPosition.GetAsVec2();
          fMergedSignedArea += a.x * b.y - b.x * a.y;
        }
        plDynamicArray<plVec3> mergedBoundary;
        mergedBoundary.Reserve(mergedBoundaryLoop.GetCount());
        for (plUInt32 i = 0; i < mergedBoundaryLoop.GetCount(); ++i)
        {
          const plUInt32 uiLoopIndex = fMergedSignedArea >= 0.0f ? i : mergedBoundaryLoop.GetCount() - 1 - i;
          mergedBoundary.PushBack(mergedVertices[mergedBoundaryLoop[uiLoopIndex]].m_vPosition);
        }

        plDynamicArray<plWaterGeometryVertex> mergedVolumeVertices;
        plDynamicArray<plUInt32> mergedVolumeIndices;
        if (BuildFiniteWaterVolume(mergedVertices, mergedIndices, pRiver->m_Descriptor.m_fDefaultDepth,
              mergedVolumeVertices, mergedVolumeIndices, false, mergedBoundary))
        {
          const plUInt64 uiMergedResourceKey = (static_cast<plUInt64>(pRiver->m_BodyId.m_uiIndex) << 32) | pSecondary->m_BodyId.m_uiIndex;
          plStringBuilder mergedSurfaceSuffix, mergedVolumeSuffix;
          mergedSurfaceSuffix.SetFormat("JunctionMergedSurface_{}_{}_{}", slot.m_Data.m_vCenter.x, slot.m_Data.m_vCenter.y, uiRevision);
          mergedVolumeSuffix.SetFormat("JunctionMergedVolume_{}_{}_{}", slot.m_Data.m_vCenter.x, slot.m_Data.m_vCenter.y, uiRevision);
          slot.m_RenderData.m_hMergedSurfaceMesh = CreateFiniteWaterMesh(mergedVertices, mergedIndices, uiMergedResourceKey, uiRevision, mergedSurfaceSuffix);
          slot.m_RenderData.m_hMergedVolumeMesh = CreateFiniteWaterMesh(mergedVolumeVertices, mergedVolumeIndices, uiMergedResourceKey, uiRevision, mergedVolumeSuffix);
          slot.m_RenderData.m_uiMergedSurfacePrimitiveCount = mergedIndices.GetCount() / 3;
          slot.m_RenderData.m_uiMergedVolumePrimitiveCount = mergedVolumeIndices.GetCount() / 3;
          bMerged = slot.m_RenderData.m_hMergedSurfaceMesh.IsValid() && slot.m_RenderData.m_hMergedVolumeMesh.IsValid();
          if (!bMerged)
          {
            slot.m_RenderData.m_hMergedSurfaceMesh.Invalidate();
            slot.m_RenderData.m_hMergedVolumeMesh.Invalidate();
            slot.m_RenderData.m_uiMergedSurfacePrimitiveCount = 0;
            slot.m_RenderData.m_uiMergedVolumePrimitiveCount = 0;
          }
        }
      }
    }
  }
  slot.m_RenderData.m_bMergedIntoPrimary = bMerged;

  if (!bMerged)
  {
    plDynamicArray<plWaterGeometryVertex> volumeVertices;
    plDynamicArray<plUInt32> volumeIndices;
    plDynamicArray<plVec3> patchVolumeBoundary;
    if (bLakeReceiver)
    {
      patchVolumeBoundary.Reserve(uiPatchColumns + uiLakeBoundaryVertexCount + uiPatchRows * 2);
      for (plUInt32 column = 0; column < uiPatchColumns; ++column)
        patchVolumeBoundary.PushBack(vertices[column].m_vPosition);
      for (plUInt32 row = 1; row < uiPatchRows; ++row)
        patchVolumeBoundary.PushBack(vertices[row * uiPatchColumns + uiPatchColumns - 1].m_vPosition);
      for (plUInt32 i = uiLakeBoundaryVertexCount; i-- > 0;)
        patchVolumeBoundary.PushBack(vertices[uiGridVertexCount + i].m_vPosition);
      for (plUInt32 row = uiPatchRows; row-- > 1;)
        patchVolumeBoundary.PushBack(vertices[row * uiPatchColumns].m_vPosition);
    }
    if (!BuildFiniteWaterVolume(vertices, indices, pRiver->m_Descriptor.m_fDefaultDepth, volumeVertices, volumeIndices,
          !bLakeReceiver, patchVolumeBoundary))
      return;
    const plUInt64 uiResourceKey = (static_cast<plUInt64>(pRiver->m_BodyId.m_uiIndex) << 32) | pSecondary->m_BodyId.m_uiIndex;
    plStringBuilder surfaceSuffix, volumeSuffix;
    surfaceSuffix.SetFormat("JunctionSurface_{}_{}_{}", slot.m_Data.m_vCenter.x, slot.m_Data.m_vCenter.y, uiRevision);
    volumeSuffix.SetFormat("JunctionVolume_{}_{}_{}", slot.m_Data.m_vCenter.x, slot.m_Data.m_vCenter.y, uiRevision);

    slot.m_RenderData.m_hSurfaceMesh = CreateFiniteWaterMesh(vertices, indices, uiResourceKey, uiRevision, surfaceSuffix);
    slot.m_RenderData.m_hVolumeMesh = CreateFiniteWaterMesh(volumeVertices, volumeIndices, uiResourceKey, uiRevision, volumeSuffix);
    slot.m_RenderData.m_uiSurfacePrimitiveCount = indices.GetCount() / 3;
    slot.m_RenderData.m_uiVolumePrimitiveCount = volumeIndices.GetCount() / 3;
  }
  slot.m_RenderData.m_PrimaryBody = pRiver->m_BodyId;
  slot.m_RenderData.m_SecondaryBody = pSecondary->m_BodyId;
  slot.m_RenderData.m_PrimaryProfile = pRiver->m_Profile;
  slot.m_RenderData.m_SecondaryProfile = pSecondary->m_Profile;
  slot.m_RenderData.m_SecondaryType = pSecondary->m_Descriptor.m_Type;
  slot.m_RenderData.m_vCutoutCenter = ((startCenter + endCenter) * 0.5f).GetAsVec2();
  slot.m_RenderData.m_vCutoutDirection = cutoutDirection;
  slot.m_RenderData.m_fCutoutHalfLength = fPatchLength * 0.5f;
  slot.m_RenderData.m_fStartHalfWidth = fStartHalfWidth;
  slot.m_RenderData.m_fEndHalfWidth = fEndHalfWidth;
  slot.m_RenderData.m_vFlowFieldOrigin = shorelinePoint.GetAsVec2();
  slot.m_RenderData.m_vFlowFieldDirection = receiverAdvance;
  slot.m_RenderData.m_fFlowFieldLength = fFlowFieldLength;
  slot.m_RenderData.m_fFlowFieldHalfWidth = fFlowFieldHalfWidth;
  slot.m_RenderData.m_fFlowFieldSpeed = fRiverPush;
  slot.m_RenderData.m_fFlowFieldDirectionSign = fFlowDirectionSign;
  if (bLakeReceiver)
  {
    slot.m_RenderData.m_vStitchedLakeLeft = endLeft.GetAsVec2();
    slot.m_RenderData.m_vStitchedLakeRight = endRight.GetAsVec2();
    slot.m_RenderData.m_uiStitchedRiverVertexCount = uiPatchColumns;
    slot.m_RenderData.m_uiStitchedLakeVertexCount = uiLakeBoundaryVertexCount;
    slot.m_RenderData.m_fMaximumLakeSeamError = fMaximumLakeSeamError;
    slot.m_RenderData.m_fLakeMaterialConvergenceDistance = fPatchLength / static_cast<float>(uiPatchSegments);
  }
  // Keep a full ring of patch geometry under the river at the copied weld row. The receiving lake is not cut:
  // its exact shoreline vertices form the other edge of the transition.
  slot.m_RenderData.m_fCutoutMargin = plMath::Max(fCellSize * 1.5f, 0.25f);
  const bool bSecondarySpectrum = pSecondary->m_Descriptor.m_Type == plWaterBodyType::Ocean ||
                                  (pSecondary->m_Descriptor.m_Type == plWaterBodyType::Lake && pSecondary->m_Profile.m_bEnableLakeSpectrum);
  slot.m_RenderData.m_uiSecondaryGpuCascadeCount = bSecondarySpectrum ? pSecondary->m_Profile.m_uiGpuCascadeCount : 0;
  slot.m_RenderData.m_bValid = slot.m_RenderData.m_bMergedIntoPrimary ||
                               (slot.m_RenderData.m_hSurfaceMesh.IsValid() && slot.m_RenderData.m_hVolumeMesh.IsValid());
}

void plWaterWorldModule::PrepareOtherJunctions(const JunctionSlot* pExcept)
{
  // Merge eligibility depends on how many junctions reference a river, so any junction lifecycle change can
  // flip a sibling between the merged remesh and the legacy overlap patch. Junction counts are tiny and
  // changes are edit-time events; re-preparing all other junctions keeps every slot's state consistent.
  for (JunctionSlot& junction : m_Junctions)
  {
    if (junction.m_bInUse && &junction != pExcept)
      PrepareJunction(junction);
  }
}

void plWaterWorldModule::PrepareBody(BodySlot& slot, const plWaterBodyDescriptor& descriptor)
{
  const plWaterBodyId oldId = slot.m_Data.m_BodyId;
  const plUInt32 uiGeometryRevision = slot.m_Data.m_uiGeometryRevision + 1;
  slot.m_Data = plWaterBodySnapshot();
  slot.m_Data.m_BodyId = oldId;
  slot.m_Data.m_uiGeometryRevision = uiGeometryRevision;
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

  plWaterFiniteMeshSettings meshSettings;
  meshSettings.m_fTargetEdgeLength = plMath::Clamp(slot.m_Data.m_Profile.m_fFiniteMeshCellSize, 0.1f, 20.0f);
  meshSettings.m_uiMaxRiverCrossSubdivisions = plMath::Clamp<plUInt16>(slot.m_Data.m_Profile.m_uiMaxRiverCrossSubdivisions, 2, 128);
  meshSettings.m_fShoreWaveFadeDistance = plMath::Clamp(slot.m_Data.m_Profile.m_fShoreWaveFadeDistance, 0.01f, 100.0f);
  meshSettings.m_fRiverCurvatureRefinement = plMath::Saturate(slot.m_Data.m_Profile.m_fRiverCurvatureRefinement);

  switch (descriptor.m_Type.GetValue())
  {
    case plWaterBodyType::Ocean:
    {
      plWaterGeometry::BuildOceanClipmap(slot.m_Data.m_Descriptor.m_uiOceanRingCount, slot.m_Data.m_Descriptor.m_uiPatchResolution,
        slot.m_Data.m_Descriptor.m_fOceanPatchSize, slot.m_Data.m_OceanPatches);
      const float fExtent = slot.m_Data.m_Descriptor.m_fOceanExtent;
      const float fHeightMargin = slot.m_Data.m_Profile.m_fWaveAmplitude * 2.0f + 1.0f;
      slot.m_Data.m_WorldBounds = plBoundingBox::MakeFromMinMax(
        plVec3(-fExtent, -fExtent, descriptor.m_fBaseHeight - slot.m_Data.m_Descriptor.m_fDefaultDepth),
        plVec3(fExtent, fExtent, descriptor.m_fBaseHeight + fHeightMargin));
      slot.m_Data.m_bGeometryValid = !slot.m_Data.m_OceanPatches.IsEmpty();
      break;
    }

    case plWaterBodyType::River:
    {
      slot.m_Data.m_bGeometryValid = plWaterGeometry::BuildRiverRibbon(descriptor.m_ControlPoints, slot.m_Data.m_Vertices, slot.m_Data.m_Indices, meshSettings).Succeeded();
      break;
    }

    case plWaterBodyType::Lake:
    {
      plDynamicArray<plVec3> boundary;
      boundary.Reserve(descriptor.m_ControlPoints.GetCount());
      for (const plWaterControlPoint& point : descriptor.m_ControlPoints)
        boundary.PushBack(point.m_vPosition);
      slot.m_Data.m_bGeometryValid = plWaterGeometry::TriangulateLake(boundary, slot.m_Data.m_Vertices, slot.m_Data.m_Indices, 0.001f, meshSettings).Succeeded();
      break;
    }
  }

  if (descriptor.m_Type != plWaterBodyType::Ocean && slot.m_Data.m_bGeometryValid)
  {
    plDynamicArray<plVec3> volumeBoundary;
    if (descriptor.m_Type == plWaterBodyType::Lake)
    {
      volumeBoundary.Reserve(descriptor.m_ControlPoints.GetCount());
      for (const plWaterControlPoint& point : descriptor.m_ControlPoints)
        volumeBoundary.PushBack(point.m_vPosition);
      if (volumeBoundary.GetCount() > 2 && volumeBoundary[0].IsEqual(volumeBoundary.PeekBack(), 0.001f))
        volumeBoundary.PopBack();
    }
    if (!BuildFiniteWaterVolume(slot.m_Data.m_Vertices, slot.m_Data.m_Indices, slot.m_Data.m_Descriptor.m_fDefaultDepth,
          slot.m_Data.m_VolumeVertices, slot.m_Data.m_VolumeIndices, descriptor.m_Type == plWaterBodyType::River, volumeBoundary))
    {
      slot.m_Data.m_bGeometryValid = false;
      plLog::Warning("Water body '{}' produced invalid or oversized finite topology; volume generation was skipped.", descriptor.m_uiNetworkId);
      return;
    }
    slot.m_Data.m_hSurfaceMesh = CreateFiniteWaterMesh(slot.m_Data.m_Vertices, slot.m_Data.m_Indices,
      descriptor.m_uiNetworkId, uiGeometryRevision, "Surface");
    slot.m_Data.m_hVolumeMesh = CreateFiniteWaterMesh(slot.m_Data.m_VolumeVertices, slot.m_Data.m_VolumeIndices,
      descriptor.m_uiNetworkId, uiGeometryRevision, "Volume");
    const plUInt32 uiVolumePrimitiveCount = slot.m_Data.m_VolumeIndices.GetCount() / 3;
    slot.m_Data.m_bGeometryValid = slot.m_Data.m_bGeometryValid && slot.m_Data.m_hSurfaceMesh.IsValid() && slot.m_Data.m_hVolumeMesh.IsValid();

    if (slot.m_Data.m_bGeometryValid)
    {
      plWaterFiniteMeshLod& lod0 = slot.m_Data.m_FiniteMeshLods.ExpandAndGetRef();
      lod0.m_hSurfaceMesh = slot.m_Data.m_hSurfaceMesh;
      lod0.m_hVolumeMesh = slot.m_Data.m_hVolumeMesh;
      lod0.m_uiSurfacePrimitiveCount = slot.m_Data.m_Indices.GetCount() / 3;
      lod0.m_uiVolumePrimitiveCount = uiVolumePrimitiveCount;
      lod0.m_fMaximumDistanceScale = 2.0f;

      for (plUInt32 uiLod = 1; uiLod < 3; ++uiLod)
      {
        plWaterFiniteMeshSettings lodSettings = meshSettings;
        lodSettings.m_fTargetEdgeLength *= static_cast<float>(1u << uiLod);
        lodSettings.m_uiMaxRiverCrossSubdivisions = plMath::Max<plUInt16>(2, meshSettings.m_uiMaxRiverCrossSubdivisions >> uiLod);
        plDynamicArray<plWaterGeometryVertex> lodVertices;
        plDynamicArray<plUInt32> lodIndices;
        plResult buildResult = PL_FAILURE;
        if (descriptor.m_Type == plWaterBodyType::River)
        {
          buildResult = plWaterGeometry::BuildRiverRibbon(descriptor.m_ControlPoints, lodVertices, lodIndices, lodSettings);
        }
        else
        {
          plDynamicArray<plVec3> boundary;
          boundary.Reserve(descriptor.m_ControlPoints.GetCount());
          for (const plWaterControlPoint& point : descriptor.m_ControlPoints)
            boundary.PushBack(point.m_vPosition);
          buildResult = plWaterGeometry::TriangulateLake(boundary, lodVertices, lodIndices, 0.001f, lodSettings);
        }
        if (buildResult.Failed())
          break;

        plDynamicArray<plWaterGeometryVertex> lodVolumeVertices;
        plDynamicArray<plUInt32> lodVolumeIndices;
        if (!BuildFiniteWaterVolume(lodVertices, lodIndices, slot.m_Data.m_Descriptor.m_fDefaultDepth, lodVolumeVertices, lodVolumeIndices,
              descriptor.m_Type == plWaterBodyType::River, volumeBoundary))
          break;
        plStringBuilder surfaceSuffix, volumeSuffix;
        surfaceSuffix.SetFormat("SurfaceLod{}", uiLod);
        volumeSuffix.SetFormat("VolumeLod{}", uiLod);
        plWaterFiniteMeshLod lod;
        lod.m_hSurfaceMesh = CreateFiniteWaterMesh(lodVertices, lodIndices, descriptor.m_uiNetworkId, uiGeometryRevision, surfaceSuffix);
        lod.m_hVolumeMesh = CreateFiniteWaterMesh(lodVolumeVertices, lodVolumeIndices, descriptor.m_uiNetworkId, uiGeometryRevision, volumeSuffix);
        if (!lod.m_hSurfaceMesh.IsValid() || !lod.m_hVolumeMesh.IsValid())
          break;
        lod.m_uiSurfacePrimitiveCount = lodIndices.GetCount() / 3;
        lod.m_uiVolumePrimitiveCount = lodVolumeIndices.GetCount() / 3;
        lod.m_fMaximumDistanceScale = uiLod == 1 ? 6.0f : plMath::HighValue<float>();
        slot.m_Data.m_FiniteMeshLods.PushBack(lod);
      }
    }
    // Mesh resources own the uploaded proxy. Retaining another CPU copy made every render snapshot
    // duplicate the full volume topology and amplified memory use during editor spline rebuilds.
    slot.m_Data.m_VolumeVertices.Clear();
    slot.m_Data.m_VolumeVertices.Compact();
    slot.m_Data.m_VolumeIndices.Clear();
    slot.m_Data.m_VolumeIndices.Compact();
    slot.m_Data.m_WorldBounds = plBoundingBox::MakeInvalid();
    for (const plWaterGeometryVertex& vertex : slot.m_Data.m_Vertices)
      slot.m_Data.m_WorldBounds.ExpandToInclude(vertex.m_vPosition);
    slot.m_Data.m_WorldBounds.Grow(plVec3(0.1f, 0.1f, slot.m_Data.m_Descriptor.m_fDefaultDepth + slot.m_Data.m_Profile.m_fWaveAmplitude * 2.0f));
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
