#include <WaterTest/WaterTestPCH.h>

#include <WaterPlugin/Utils/WaterGeometry.h>

PL_CREATE_SIMPLE_TEST_GROUP(Water);

PL_CREATE_SIMPLE_TEST(Water, Geometry)
{
  PL_TEST_BLOCK(plTestBlock::Enabled, "Concave lake ear clipping")
  {
    const plVec3 boundary[] = {
      plVec3(0, 0, 2),
      plVec3(4, 0, 2),
      plVec3(4, 4, 2),
      plVec3(2, 2, 2),
      plVec3(0, 4, 2),
    };

    plDynamicArray<plWaterGeometryVertex> vertices;
    plDynamicArray<plUInt32> indices;
    PL_TEST_BOOL(plWaterGeometry::TriangulateLake(boundary, vertices, indices).Succeeded());
    PL_TEST_BOOL(vertices.GetCount() > 5);
    PL_TEST_BOOL(indices.GetCount() > 9);

    float fTriangleArea = 0.0f;
    for (plUInt32 i = 0; i < indices.GetCount(); i += 3)
    {
      const plVec2 a = vertices[indices[i + 0]].m_vPosition.GetAsVec2();
      const plVec2 b = vertices[indices[i + 1]].m_vPosition.GetAsVec2();
      const plVec2 c = vertices[indices[i + 2]].m_vPosition.GetAsVec2();
      fTriangleArea += plMath::Abs((b - a).x * (c - a).y - (b - a).y * (c - a).x) * 0.5f;
    }
    PL_TEST_FLOAT(fTriangleArea, 12.0f, 0.001f);
    for (plUInt32 i = 0; i < PL_ARRAY_SIZE(boundary); ++i)
      PL_TEST_FLOAT(vertices[i].m_fWaveAttenuation, 0.0f, 0.001f);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Invalid lake outlines")
  {
    const plVec3 bowTie[] = {
      plVec3(0, 0, 0),
      plVec3(3, 3, 0),
      plVec3(0, 3, 0),
      plVec3(3, 0, 0),
    };

    plDynamicArray<plWaterGeometryVertex> vertices;
    plDynamicArray<plUInt32> indices;
    PL_TEST_BOOL(plWaterGeometry::TriangulateLake(bowTie, vertices, indices).Failed());
    PL_TEST_BOOL(vertices.IsEmpty());
    PL_TEST_BOOL(indices.IsEmpty());

    const plVec3 nonPlanar[] = {
      plVec3(0, 0, 0),
      plVec3(3, 0, 0),
      plVec3(3, 3, 0.1f),
      plVec3(0, 3, 0),
    };
    PL_TEST_BOOL(plWaterGeometry::TriangulateLake(nonPlanar, vertices, indices).Failed());
    PL_TEST_BOOL(vertices.IsEmpty());
    PL_TEST_BOOL(indices.IsEmpty());
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Lake topology regularization")
  {
    // An L-shaped shoreline forces ear clipping to begin with long, skinny diagonals. The refinement pass
    // must retain exact area while improving those diagonals into a reasonably uniform shared mesh.
    const plVec3 boundary[] = {
      plVec3(0, 0, 3),
      plVec3(24, 0, 3),
      plVec3(24, 3, 3),
      plVec3(8, 3, 3),
      plVec3(8, 14, 3),
      plVec3(0, 14, 3),
    };
    plWaterFiniteMeshSettings settings;
    settings.m_fTargetEdgeLength = 1.25f;

    plDynamicArray<plWaterGeometryVertex> vertices;
    plDynamicArray<plUInt32> indices;
    PL_TEST_BOOL(plWaterGeometry::TriangulateLake(boundary, vertices, indices, 0.001f, settings).Succeeded());

    float fArea = 0.0f;
    float fMinimumQuality = 1.0f;
    for (plUInt32 i = 0; i < indices.GetCount(); i += 3)
    {
      const plVec2 a = vertices[indices[i + 0]].m_vPosition.GetAsVec2();
      const plVec2 b = vertices[indices[i + 1]].m_vPosition.GetAsVec2();
      const plVec2 c = vertices[indices[i + 2]].m_vPosition.GetAsVec2();
      const float fTwiceArea = (b - a).x * (c - a).y - (b - a).y * (c - a).x;
      const float fEdgeEnergy = (b - a).GetLengthSquared() + (c - b).GetLengthSquared() + (a - c).GetLengthSquared();
      PL_TEST_BOOL(fTwiceArea > 0.000001f);
      fArea += fTwiceArea * 0.5f;
      fMinimumQuality = plMath::Min(fMinimumQuality,
        2.0f * plMath::Sqrt(3.0f) * fTwiceArea / plMath::Max(fEdgeEnergy, 0.000001f));
    }
    PL_TEST_FLOAT(fArea, 160.0f, 0.01f);
    PL_TEST_BOOL_MSG(fMinimumQuality > 0.12f,
      "Regularized lake triangles must not retain near-degenerate ear diagonals (minimum quality %f).", fMinimumQuality);
    for (plUInt32 i = 0; i < PL_ARRAY_SIZE(boundary); ++i)
    {
      PL_TEST_BOOL(vertices[i].m_vPosition.IsEqual(boundary[i], 0.0001f));
      PL_TEST_FLOAT(vertices[i].m_fWaveAttenuation, 0.0f, 0.001f);
    }
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "River ribbon indices and bounds")
  {
    plWaterControlPoint points[] = {
      {plVec3(0, 0, 1), 2.0f, 2.0f, 2.0f, 1.0f},
      {plVec3(5, 0, 1), 3.0f, 3.0f, 3.0f, 2.0f},
      {plVec3(10, 2, 1), 4.0f, 4.0f, 4.0f, 3.0f},
    };

    plDynamicArray<plWaterGeometryVertex> vertices;
    plDynamicArray<plUInt32> indices;
    PL_TEST_BOOL(plWaterGeometry::BuildRiverRibbon(points, vertices, indices).Succeeded());
    PL_TEST_BOOL(vertices.GetCount() > 6);
    PL_TEST_BOOL(indices.GetCount() > 12);
    for (plUInt32 index : indices)
      PL_TEST_BOOL(index < vertices.GetCount());
    PL_TEST_FLOAT(vertices[0].m_fSignedShoreDistance, 2.0f, 0.001f);
    PL_TEST_FLOAT(vertices[0].m_fWaveAttenuation, 0.0f, 0.001f);
    float fMaximumLongitudinalCoordinate = 0.0f;
    bool bHasInteriorDisplacement = false;
    for (const plWaterGeometryVertex& vertex : vertices)
    {
      fMaximumLongitudinalCoordinate = plMath::Max(fMaximumLongitudinalCoordinate, vertex.m_vTexCoord.x);
      bHasInteriorDisplacement |= vertex.m_fWaveAttenuation > 0.5f;
    }
    PL_TEST_BOOL(bHasInteriorDisplacement);
    PL_TEST_FLOAT(fMaximumLongitudinalCoordinate, (points[1].m_vPosition - points[0].m_vPosition).GetLength() + (points[2].m_vPosition - points[1].m_vPosition).GetLength(), 0.001f);

    for (plUInt32 i = 0; i < indices.GetCount(); i += 3)
    {
      const plVec2 a = vertices[indices[i]].m_vPosition.GetAsVec2();
      const plVec2 b = vertices[indices[i + 1]].m_vPosition.GetAsVec2();
      const plVec2 c = vertices[indices[i + 2]].m_vPosition.GetAsVec2();
      PL_TEST_BOOL((b - a).x * (c - a).y - (b - a).y * (c - a).x > 0.000001f);
    }

    plWaterRiverLocation location;
    PL_TEST_BOOL(plWaterGeometry::FindClosestPointOnRiver(points, plVec2(2.5f, 0.0f), location));
    PL_TEST_INT(location.m_uiSegment, 0);
    PL_TEST_FLOAT(location.m_fLeftWidth, 2.5f, 0.001f);
    PL_TEST_FLOAT(location.m_fRightWidth, 2.5f, 0.001f);
    PL_TEST_FLOAT(location.m_fDepth, 2.5f, 0.001f);
    PL_TEST_FLOAT(location.m_fFlowScale, 1.5f, 0.001f);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "River S-bend shared topology")
  {
    const plWaterControlPoint points[] = {
      {plVec3(0, 0, 0), 2, 2, 2, 1},
      {plVec3(8, 1, 0), 2, 3, 2, 1},
      {plVec3(16, -1, 0), 3, 2, 2, 1},
      {plVec3(24, 0, 0), 2, 2, 2, 1},
    };
    plWaterFiniteMeshSettings settings;
    settings.m_fTargetEdgeLength = 0.75f;
    plDynamicArray<plWaterGeometryVertex> vertices;
    plDynamicArray<plUInt32> indices;
    PL_TEST_BOOL(plWaterGeometry::BuildRiverRibbon(points, vertices, indices, settings).Succeeded());
    PL_TEST_BOOL(vertices.GetCount() > 40);
    PL_TEST_BOOL(indices.GetCount() > 100);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Ocean clipmap seams")
  {
    plDynamicArray<plWaterOceanClipmapPatch> patches;
    plWaterGeometry::BuildOceanClipmap(6, 64, 64.0f, patches);
    PL_TEST_INT(patches.GetCount(), 76);

    bool bFoundInnerSeam = false;
    bool bFoundOuterSeam = false;
    for (const plWaterOceanClipmapPatch& patch : patches)
    {
      PL_TEST_INT(patch.m_uiResolution, 64);
      bFoundInnerSeam |= patch.m_uiInnerSeamMask != 0;
      bFoundOuterSeam |= patch.m_uiOuterSeamMask != 0;
    }
    PL_TEST_BOOL(bFoundInnerSeam);
    PL_TEST_BOOL(bFoundOuterSeam);
  }
}
