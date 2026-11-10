#include <WaterTest/WaterTestPCH.h>

#include <Foundation/Containers/Map.h>
#include <Foundation/Containers/Set.h>
#include <Core/Graphics/Camera.h>
#include <RendererCore/Pipeline/SortingFunctions.h>
#include <WaterPlugin/Rendering/WaterRenderData.h>
#include <WaterPlugin/Utils/WaterNetworkMesher.h>

namespace
{
  plWaterNetworkBody MakeRiver(plUInt8 uiProfile, plArrayPtr<const plVec3> line, float fWidth = 4.0f, float fEdgeLength = 0.5f)
  {
    plWaterNetworkBody body;
    body.m_Type = plWaterBodyType::River;
    body.m_uiProfileIndex = uiProfile;
    body.m_Settings.m_fTargetEdgeLength = fEdgeLength;
    body.m_Settings.m_fShoreWaveFadeDistance = 0.75f;
    body.m_fDefaultDepth = 2.0f;
    for (const plVec3& position : line)
    {
      plWaterControlPoint& point = body.m_Points.ExpandAndGetRef();
      point.m_vPosition = position;
      point.m_fLeftWidth = fWidth * 0.5f;
      point.m_fRightWidth = fWidth * 0.5f;
      point.m_fDepth = 2.0f;
    }
    return body;
  }

  plWaterNetworkBody MakeLake(plUInt8 uiProfile, plArrayPtr<const plVec3> outline, float fEdgeLength = 0.5f)
  {
    plWaterNetworkBody body;
    body.m_Type = plWaterBodyType::Lake;
    body.m_uiProfileIndex = uiProfile;
    body.m_Settings.m_fTargetEdgeLength = fEdgeLength;
    body.m_Settings.m_fShoreWaveFadeDistance = 1.5f;
    body.m_fDefaultDepth = 6.0f;
    for (const plVec3& position : outline)
      body.m_Points.ExpandAndGetRef().m_vPosition = position;
    return body;
  }

  plWaterNetworkJoin MakeJoin(plUInt32 uiRiver, bool bRiverEnd, plUInt32 uiLake)
  {
    plWaterNetworkJoin join;
    join.m_uiRiver = uiRiver;
    join.m_bRiverEnd = bRiverEnd;
    join.m_uiLake = uiLake;
    return join;
  }

  /// Every triangle of a drawn part is wound counter-clockwise, has area, and names one profile pair at all three corners.
  bool PartIsWellFormed(const plWaterNetworkPart& part)
  {
    if (!part.m_bValid || part.m_Indices.IsEmpty() || part.m_Indices.GetCount() % 3 != 0)
      return false;
    for (plUInt32 i = 0; i < part.m_Indices.GetCount(); i += 3)
    {
      const plWaterGeometryVertex& a = part.m_Vertices[part.m_Indices[i]];
      const plWaterGeometryVertex& b = part.m_Vertices[part.m_Indices[i + 1]];
      const plWaterGeometryVertex& c = part.m_Vertices[part.m_Indices[i + 2]];
      const plVec2 ab = (b.m_vPosition - a.m_vPosition).GetAsVec2();
      const plVec2 ac = (c.m_vPosition - a.m_vPosition).GetAsVec2();
      if (ab.x * ac.y - ab.y * ac.x <= 0.0f)
        return false;
      if (a.m_uiProfileA != b.m_uiProfileA || a.m_uiProfileA != c.m_uiProfileA || a.m_uiProfileB != b.m_uiProfileB || a.m_uiProfileB != c.m_uiProfileB)
        return false;
    }
    return true;
  }

  float PartArea(const plWaterNetworkPart& part)
  {
    float fArea = 0.0f;
    for (plUInt32 i = 0; i + 2 < part.m_TopologyIndices.GetCount(); i += 3)
    {
      const plVec2 a = part.m_TopologyVertices[part.m_TopologyIndices[i]].m_vPosition.GetAsVec2();
      const plVec2 b = part.m_TopologyVertices[part.m_TopologyIndices[i + 1]].m_vPosition.GetAsVec2();
      const plVec2 c = part.m_TopologyVertices[part.m_TopologyIndices[i + 2]].m_vPosition.GetAsVec2();
      fArea += plMath::Abs((b - a).x * (c - a).y - (b - a).y * (c - a).x) * 0.5f;
    }
    return fArea;
  }

  /// The second profiles that a part's vertices blend towards with a weight above zero.
  void CollectBlendTargets(const plWaterNetworkPart& part, plUInt8 uiOwnProfile, plSet<plUInt8>& out_others)
  {
    for (const plWaterGeometryVertex& vertex : part.m_Vertices)
    {
      if (vertex.m_uiProfileA != uiOwnProfile)
        out_others.Insert(vertex.m_uiProfileA);
      if (vertex.m_uiProfileB != uiOwnProfile && vertex.m_fProfileBlend > 0.0f)
        out_others.Insert(vertex.m_uiProfileB);
    }
  }
} // namespace

PL_CREATE_SIMPLE_TEST(Water, Network)
{
  // A lake 32 x 36 m, its east shore at x = -28.
  const plVec3 lakeOutline[] = {plVec3(-60, -18, 3), plVec3(-28, -18, 3), plVec3(-28, 18, 3), plVec3(-60, 18, 3)};

  PL_TEST_BLOCK(plTestBlock::Enabled, "River into lake shares every mouth vertex at every LOD")
  {
    // The river flows west and ends on the east shore.
    const plVec3 riverLine[] = {plVec3(10, 4, 3), plVec3(-10, 2, 3), plVec3(-28, 0, 3)};
    plHybridArray<plWaterNetworkBody, 2> bodies;
    bodies.PushBack(MakeLake(0, lakeOutline));
    bodies.PushBack(MakeRiver(1, riverLine));
    plHybridArray<plWaterNetworkJoin, 1> joins;
    joins.PushBack(MakeJoin(1, true, 0));

    for (plUInt32 uiLod = 0; uiLod < 3; ++uiLod)
    {
      plWaterNetworkMesh mesh;
      plWaterNetworkMesher::Build(bodies, joins, uiLod, mesh);
      if (!PL_TEST_BOOL_MSG(mesh.m_Joins[0].m_bValid, "LOD %u: %s", uiLod, mesh.m_Joins[0].m_sProblem.GetData()))
        continue;

      PL_TEST_BOOL(PartIsWellFormed(mesh.m_Parts[0]));
      PL_TEST_BOOL(PartIsWellFormed(mesh.m_Parts[1]));
      PL_TEST_INT(mesh.m_Parts[1].m_uiFoldedTriangles, 0);
      PL_TEST_BOOL_MSG(mesh.m_Joins[0].m_uiSharedVertexCount >= 2, "LOD %u: %u shared vertices", uiLod, mesh.m_Joins[0].m_uiSharedVertexCount);
      PL_TEST_FLOAT(mesh.m_Joins[0].m_fMaximumSeamError, 0.0f, 0.0f);
      PL_TEST_INT(plWaterNetworkMesher::CountSeamDefects(mesh), 0);

      // The mouth is 4 m of shore around y = 0, and the lake keeps its full area: nothing was cut out of it.
      PL_TEST_FLOAT((mesh.m_Joins[0].m_vMouthLeft - mesh.m_Joins[0].m_vMouthRight).GetLength(), 4.0f, 0.01f);
      PL_TEST_FLOAT(mesh.m_Joins[0].m_vMouthLeft.x, -28.0f, 0.001f);
      PL_TEST_FLOAT(PartArea(mesh.m_Parts[0]), 32.0f * 36.0f, 0.05f);
    }
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Mouth is open water, the rest of the shore is pinned")
  {
    const plVec3 riverLine[] = {plVec3(10, 0, 3), plVec3(-28, 0, 3)};
    plHybridArray<plWaterNetworkBody, 2> bodies;
    bodies.PushBack(MakeLake(0, lakeOutline));
    bodies.PushBack(MakeRiver(1, riverLine));
    plHybridArray<plWaterNetworkJoin, 1> joins;
    joins.PushBack(MakeJoin(1, true, 0));

    plWaterNetworkMesh mesh;
    plWaterNetworkMesher::Build(bodies, joins, 0, mesh);
    PL_TEST_BOOL(mesh.m_Joins[0].m_bValid);

    float fMouthMiddle = -1.0f;
    float fShoreFarAway = -1.0f;
    for (const plWaterGeometryVertex& vertex : mesh.m_Parts[0].m_TopologyVertices)
    {
      if (plMath::Abs(vertex.m_vPosition.x + 28.0f) > 0.001f)
        continue;
      if (plMath::Abs(vertex.m_vPosition.y) < 0.3f)
        fMouthMiddle = plMath::Max(fMouthMiddle, vertex.m_fWaveAttenuation);
      if (plMath::Abs(vertex.m_vPosition.y - 10.0f) < 0.3f)
        fShoreFarAway = plMath::Max(fShoreFarAway, vertex.m_fWaveAttenuation);
    }
    // 2 m from either bank with a 1.5 m fade: full waves in the middle of the mouth.
    PL_TEST_FLOAT(fMouthMiddle, 1.0f, 0.001f);
    PL_TEST_FLOAT(fShoreFarAway, 0.0f, 0.001f);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "River joined at both ends: lake at its start, ocean at its end")
  {
    // Leaves the lake's east shore, bends, and reaches the sea 68 m on, falling from 3 m to 0.
    const plVec3 riverLine[] = {plVec3(-28, 0, 3), plVec3(-10, 0, 2.2f), plVec3(10, 10, 1.2f), plVec3(40, 10, 0)};
    plHybridArray<plWaterNetworkBody, 2> bodies;
    bodies.PushBack(MakeLake(0, lakeOutline));
    bodies.PushBack(MakeRiver(1, riverLine));
    plHybridArray<plWaterNetworkJoin, 2> joins;
    joins.PushBack(MakeJoin(1, false, 0));
    plWaterNetworkJoin sea = MakeJoin(1, true, plInvalidIndex);
    sea.m_uiOceanProfileIndex = 2;
    sea.m_fOceanHeight = 0.0f;
    sea.m_fMixLengthReceiver = 12.0f;
    joins.PushBack(sea);

    for (plUInt32 uiLod = 0; uiLod < 3; ++uiLod)
    {
      plWaterNetworkMesh mesh;
      plWaterNetworkMesher::Build(bodies, joins, uiLod, mesh);
      PL_TEST_BOOL_MSG(mesh.m_Joins[0].m_bValid, "%s", mesh.m_Joins[0].m_sProblem.GetData());
      PL_TEST_BOOL_MSG(mesh.m_Joins[1].m_bValid, "%s", mesh.m_Joins[1].m_sProblem.GetData());
      PL_TEST_BOOL(PartIsWellFormed(mesh.m_Parts[0]));
      PL_TEST_BOOL(PartIsWellFormed(mesh.m_Parts[1]));
      PL_TEST_INT(plWaterNetworkMesher::CountSeamDefects(mesh), 0);

      // One river mesh blends towards the lake at one end and towards the ocean at the other.
      plSet<plUInt8> others;
      CollectBlendTargets(mesh.m_Parts[1], 1, others);
      PL_TEST_BOOL(others.Contains(0));
      PL_TEST_BOOL(others.Contains(2));
      PL_TEST_INT(others.GetCount(), 2);

      // The apron reaches 12 m past the river's end and is pure ocean along its outer edge.
      float fFarthest = 0.0f;
      float fWeightAtFarEdge = 0.0f;
      for (const plWaterGeometryVertex& vertex : mesh.m_Parts[1].m_Vertices)
      {
        if (vertex.m_vPosition.x > fFarthest)
        {
          fFarthest = vertex.m_vPosition.x;
          fWeightAtFarEdge = vertex.m_fProfileBlend;
        }
        if (vertex.m_vPosition.x > 51.9f)
          PL_TEST_BOOL(vertex.m_uiProfileB == 2 && vertex.m_fProfileBlend > 0.999f);
      }
      PL_TEST_FLOAT(fFarthest, 52.0f, 0.01f);
      PL_TEST_FLOAT(fWeightAtFarEdge, 1.0f, 0.001f);

      // What the ocean may skip lies inside the apron and ends before its pure band.
      const plWaterNetworkJoinResult& result = mesh.m_Joins[1];
      PL_TEST_BOOL(result.m_fCutoutHalfLength > 4.0f);
      PL_TEST_BOOL(result.m_vCutoutCenter.x + result.m_fCutoutHalfLength < 52.0f - 1.0f);
      PL_TEST_BOOL(result.m_fCutoutEndHalfWidth >= result.m_fCutoutStartHalfWidth);
    }
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Three rivers into one lake: three mouths, three plumes")
  {
    const plVec3 east[] = {plVec3(0, 0, 3), plVec3(-28, 0, 3)};
    const plVec3 north[] = {plVec3(-44, 40, 3), plVec3(-44, 18, 3)};
    const plVec3 south[] = {plVec3(-50, -40, 3), plVec3(-50, -18, 3)};
    plHybridArray<plWaterNetworkBody, 4> bodies;
    bodies.PushBack(MakeLake(0, lakeOutline));
    bodies.PushBack(MakeRiver(1, east));
    bodies.PushBack(MakeRiver(2, north, 3.0f));
    bodies.PushBack(MakeRiver(3, south, 5.0f));
    plHybridArray<plWaterNetworkJoin, 3> joins;
    joins.PushBack(MakeJoin(1, true, 0));
    joins.PushBack(MakeJoin(2, true, 0));
    joins.PushBack(MakeJoin(3, true, 0));

    for (plUInt32 uiLod = 0; uiLod < 3; ++uiLod)
    {
      plWaterNetworkMesh mesh;
      plWaterNetworkMesher::Build(bodies, joins, uiLod, mesh);
      for (plUInt32 i = 0; i < 3; ++i)
      {
        PL_TEST_BOOL_MSG(mesh.m_Joins[i].m_bValid, "%s", mesh.m_Joins[i].m_sProblem.GetData());
        PL_TEST_FLOAT(mesh.m_Joins[i].m_fMaximumSeamError, 0.0f, 0.0f);
      }
      for (plUInt32 i = 0; i < 4; ++i)
        PL_TEST_BOOL(PartIsWellFormed(mesh.m_Parts[i]));
      PL_TEST_INT(plWaterNetworkMesher::CountSeamDefects(mesh), 0);

      // The lake's own mesh carries the look of all three rivers, each near its own mouth.
      plSet<plUInt8> plumes;
      CollectBlendTargets(mesh.m_Parts[0], 0, plumes);
      PL_TEST_INT(plumes.GetCount(), 3);
      PL_TEST_BOOL(plumes.Contains(1) && plumes.Contains(2) && plumes.Contains(3));
    }
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "A river that folds in a sharp corner is still built")
  {
    // A right angle in a river 6 m wide: the inside bank runs backwards there.
    const plVec3 riverLine[] = {plVec3(0, 0, 0), plVec3(20, 0, 0), plVec3(20, 20, 0)};
    plHybridArray<plWaterNetworkBody, 1> bodies;
    bodies.PushBack(MakeRiver(0, riverLine, 6.0f));

    // The old ribbon builder gives up on this line.
    plDynamicArray<plWaterGeometryVertex> oldVertices;
    plDynamicArray<plUInt32> oldIndices;
    plWaterFiniteMeshSettings oldSettings;
    oldSettings.m_fTargetEdgeLength = 0.5f;
    PL_TEST_BOOL(plWaterGeometry::BuildRiverRibbon(bodies[0].m_Points, oldVertices, oldIndices, oldSettings).Failed());

    plWaterNetworkMesh mesh;
    plWaterNetworkMesher::Build(bodies, plArrayPtr<const plWaterNetworkJoin>(), 0, mesh);
    PL_TEST_BOOL(PartIsWellFormed(mesh.m_Parts[0]));
    // 40 m of river, 6 m wide: the corner may lose a little, not the river.
    PL_TEST_BOOL_MSG(PartArea(mesh.m_Parts[0]) > 200.0f, "Area %.2f", PartArea(mesh.m_Parts[0]));
    PL_TEST_BOOL_MSG(mesh.m_Parts[0].m_uiFoldedTriangles < 40, "%u folded triangles", mesh.m_Parts[0].m_uiFoldedTriangles);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "A join that cannot be made is left out, and says why")
  {
    // The river ends 20 m short of the shore.
    const plVec3 riverLine[] = {plVec3(30, 0, 3), plVec3(-8, 0, 3)};
    plHybridArray<plWaterNetworkBody, 2> bodies;
    bodies.PushBack(MakeLake(0, lakeOutline));
    bodies.PushBack(MakeRiver(1, riverLine));
    plHybridArray<plWaterNetworkJoin, 1> joins;
    joins.PushBack(MakeJoin(1, true, 0));

    plWaterNetworkMesh mesh;
    plWaterNetworkMesher::Build(bodies, joins, 0, mesh);
    PL_TEST_BOOL(!mesh.m_Joins[0].m_bValid);
    PL_TEST_BOOL(!mesh.m_Joins[0].m_sProblem.IsEmpty());
    // Both bodies are still there, each on its own.
    PL_TEST_BOOL(PartIsWellFormed(mesh.m_Parts[0]));
    PL_TEST_BOOL(PartIsWellFormed(mesh.m_Parts[1]));
    PL_TEST_FLOAT(PartArea(mesh.m_Parts[1]), 38.0f * 4.0f, 0.05f);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "From eight sides of a river mouth, the ocean is drawn before the apron")
  {
    // Water does not write depth: whichever draw comes last is what shows. Sorted by distance alone, the ocean patch
    // under a river's apron comes last from some sides and first from others, and the mouth changes as the camera
    // walks round it. With layers it never does.
    const plVec3 vMouth(40.0f, 10.0f, 0.0f);
    plWaterRenderData river;
    river.m_vGlobalPosition = plVec3(15.0f, 8.0f, 1.0f); // the middle of the river's bounds, well inland
    river.m_uiSortLayer = 1;
    plWaterRenderData oceanPatches[3];
    oceanPatches[0].m_vGlobalPosition = plVec3(48.0f, 16.0f, 0.0f);    // the patch the apron lies on
    oceanPatches[1].m_vGlobalPosition = plVec3(-16.0f, 16.0f, 0.0f);   // one under the land, behind the river
    oceanPatches[2].m_vGlobalPosition = plVec3(400.0f, -300.0f, 0.0f); // one far out
    for (plWaterRenderData& patch : oceanPatches)
      patch.m_uiSortLayer = 0;

    for (plUInt32 uiSide = 0; uiSide < 8; ++uiSide)
    {
      const plAngle angle = plAngle::MakeFromDegree(45.0f * uiSide);
      const plVec3 vEye = vMouth + plVec3(plMath::Cos(angle) * 30.0f, plMath::Sin(angle) * 30.0f, 9.0f);
      plCamera camera;
      camera.SetCameraMode(plCameraMode::PerspectiveFixedFovY, 55.0f, 0.1f, 4000.0f);
      camera.LookAt(vEye, vMouth, plVec3(0.0f, 0.0f, 1.0f));

      const plUInt64 uiRiverKey = plWaterRenderData::GetSortingKey(&river, camera);
      bool bDistanceAloneWouldFlip = false;
      for (const plWaterRenderData& patch : oceanPatches)
      {
        PL_TEST_BOOL_MSG(plWaterRenderData::GetSortingKey(&patch, camera) < uiRiverKey, "From side %u an ocean patch is drawn after the river", uiSide);
        bDistanceAloneWouldFlip |= plRenderSortingFunctions::BackToFrontThenByRenderData(&patch, camera) > plRenderSortingFunctions::BackToFrontThenByRenderData(&river, camera);
      }
      // The test has teeth: from at least the sea side, distance alone puts an ocean patch after the river.
      if (uiSide == 0)
        PL_TEST_BOOL(bDistanceAloneWouldFlip);
    }
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Volume under a part is closed along its outline")
  {
    const plVec3 riverLine[] = {plVec3(10, 0, 3), plVec3(-28, 0, 3)};
    plHybridArray<plWaterNetworkBody, 2> bodies;
    bodies.PushBack(MakeLake(0, lakeOutline, 2.0f));
    bodies.PushBack(MakeRiver(1, riverLine, 4.0f, 2.0f));
    plHybridArray<plWaterNetworkJoin, 1> joins;
    joins.PushBack(MakeJoin(1, true, 0));

    plWaterNetworkMesh mesh;
    plWaterNetworkMesher::Build(bodies, joins, 0, mesh);
    for (plUInt32 uiPart = 0; uiPart < 2; ++uiPart)
    {
      const plWaterNetworkPart& part = mesh.m_Parts[uiPart];
      plDynamicArray<plWaterGeometryVertex> vertices;
      plDynamicArray<plUInt32> indices;
      PL_TEST_BOOL(plWaterNetworkMesher::BuildVolume(part.m_TopologyVertices, part.m_TopologyIndices, 5.0f, vertices, indices));
      PL_TEST_INT(vertices.GetCount(), part.m_TopologyVertices.GetCount() * 2);
      // Top and bottom, and at least one wall quad per outline edge.
      PL_TEST_BOOL(indices.GetCount() > part.m_TopologyIndices.GetCount() * 2);

      // Closed: every edge of the volume is used by exactly two triangles.
      plMap<plUInt64, plUInt32> edgeUse;
      for (plUInt32 i = 0; i + 2 < indices.GetCount(); i += 3)
      {
        for (plUInt32 uiEdge = 0; uiEdge < 3; ++uiEdge)
        {
          const plUInt32 a = indices[i + uiEdge];
          const plUInt32 b = indices[i + (uiEdge + 1) % 3];
          ++edgeUse[(static_cast<plUInt64>(plMath::Min(a, b)) << 32) | plMath::Max(a, b)];
        }
      }
      plUInt32 uiOpenEdges = 0;
      for (auto it = edgeUse.GetIterator(); it.IsValid(); ++it)
        uiOpenEdges += it.Value() != 2 ? 1 : 0;
      PL_TEST_INT(uiOpenEdges, 0);
    }
  }
}
