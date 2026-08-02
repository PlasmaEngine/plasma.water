#include <WaterPlugin/WaterPluginPCH.h>

#include <WaterPlugin/Utils/WaterGeometry.h>

#include <Foundation/Containers/HashTable.h>

namespace
{
  constexpr plUInt32 s_uiMaximumFiniteWaterVertices = 262144;

  float Cross2D(const plVec2& a, const plVec2& b, const plVec2& c)
  {
    const plVec2 ab = b - a;
    const plVec2 ac = c - a;
    return ab.x * ac.y - ab.y * ac.x;
  }

  bool IsPointOnSegment(const plVec2& a, const plVec2& b, const plVec2& p, float fEpsilon)
  {
    if (plMath::Abs(Cross2D(a, b, p)) > fEpsilon)
      return false;

    const float fDot = (p - a).Dot(b - a);
    const float fLengthSquared = (b - a).GetLengthSquared();
    return fDot >= -fEpsilon && fDot <= fLengthSquared + fEpsilon;
  }

  bool SegmentsIntersect(const plVec2& a0, const plVec2& a1, const plVec2& b0, const plVec2& b1, float fEpsilon)
  {
    const float c0 = Cross2D(a0, a1, b0);
    const float c1 = Cross2D(a0, a1, b1);
    const float c2 = Cross2D(b0, b1, a0);
    const float c3 = Cross2D(b0, b1, a1);

    if (((c0 > fEpsilon && c1 < -fEpsilon) || (c0 < -fEpsilon && c1 > fEpsilon)) &&
        ((c2 > fEpsilon && c3 < -fEpsilon) || (c2 < -fEpsilon && c3 > fEpsilon)))
      return true;

    return (plMath::Abs(c0) <= fEpsilon && IsPointOnSegment(a0, a1, b0, fEpsilon)) ||
           (plMath::Abs(c1) <= fEpsilon && IsPointOnSegment(a0, a1, b1, fEpsilon)) ||
           (plMath::Abs(c2) <= fEpsilon && IsPointOnSegment(b0, b1, a0, fEpsilon)) ||
           (plMath::Abs(c3) <= fEpsilon && IsPointOnSegment(b0, b1, a1, fEpsilon));
  }

  bool PointInTriangle(const plVec2& p, const plVec2& a, const plVec2& b, const plVec2& c, float fEpsilon)
  {
    const float c0 = Cross2D(a, b, p);
    const float c1 = Cross2D(b, c, p);
    const float c2 = Cross2D(c, a, p);
    return c0 >= -fEpsilon && c1 >= -fEpsilon && c2 >= -fEpsilon;
  }

  plVec2 SafeNormalized(const plVec2& value, const plVec2& fallback)
  {
    const float fLengthSquared = value.GetLengthSquared();
    if (fLengthSquared <= plMath::SmallEpsilon<float>())
      return fallback;

    return value / plMath::Sqrt(fLengthSquared);
  }

  plUInt64 MakeEdgeKey(plUInt32 a, plUInt32 b)
  {
    if (a > b)
      plMath::Swap(a, b);
    return (static_cast<plUInt64>(a) << 32) | b;
  }

  float TriangleQuality(const plWaterGeometryVertex& a, const plWaterGeometryVertex& b, const plWaterGeometryVertex& c)
  {
    const plVec2 pa = a.m_vPosition.GetAsVec2();
    const plVec2 pb = b.m_vPosition.GetAsVec2();
    const plVec2 pc = c.m_vPosition.GetAsVec2();
    const float fEdgeEnergy = (pb - pa).GetLengthSquared() + (pc - pb).GetLengthSquared() + (pa - pc).GetLengthSquared();
    if (fEdgeEnergy <= plMath::SmallEpsilon<float>())
      return 0.0f;

    // 1 for an equilateral triangle and 0 as a triangle collapses.
    return 2.0f * plMath::Sqrt(3.0f) * plMath::Abs(Cross2D(pa, pb, pc)) / fEdgeEnergy;
  }

  void SetCounterClockwiseTriangle(plDynamicArray<plUInt32>& ref_indices, plUInt32 uiTriangle,
    plUInt32 a, plUInt32 b, plUInt32 c, plArrayPtr<const plWaterGeometryVertex> vertices)
  {
    if (Cross2D(vertices[a].m_vPosition.GetAsVec2(), vertices[b].m_vPosition.GetAsVec2(), vertices[c].m_vPosition.GetAsVec2()) < 0.0f)
      plMath::Swap(b, c);

    ref_indices[uiTriangle * 3 + 0] = a;
    ref_indices[uiTriangle * 3 + 1] = b;
    ref_indices[uiTriangle * 3 + 2] = c;
  }

  void RegularizeLakeTriangles(plArrayPtr<const plWaterGeometryVertex> vertices, plDynamicArray<plUInt32>& ref_indices)
  {
    struct EdgeOwner
    {
      plUInt32 m_uiTriangle = plInvalidIndex;
      plUInt32 m_uiOpposite = plInvalidIndex;
      plUInt32 m_uiEdgeA = plInvalidIndex;
      plUInt32 m_uiEdgeB = plInvalidIndex;
    };
    struct FlipCandidate
    {
      EdgeOwner m_First;
      EdgeOwner m_Second;
    };

    const plUInt32 uiTriangleCount = ref_indices.GetCount() / 3;
    // Lawson-style quality flips retain the polygon boundary and all shared vertices, but remove the long,
    // skinny diagonals inherited from the initial ear-clipping order. Multiple independent flips are accepted
    // per pass; touched triangles are skipped until adjacency is rebuilt on the next pass.
    for (plUInt32 uiPass = 0; uiPass < 12; ++uiPass)
    {
      plHashTable<plUInt64, EdgeOwner> owners;
      owners.Reserve(ref_indices.GetCount());
      plDynamicArray<FlipCandidate> candidates;
      candidates.Reserve(ref_indices.GetCount() / 2);

      for (plUInt32 uiTriangle = 0; uiTriangle < uiTriangleCount; ++uiTriangle)
      {
        const plUInt32 triangle[3] = {
          ref_indices[uiTriangle * 3 + 0], ref_indices[uiTriangle * 3 + 1], ref_indices[uiTriangle * 3 + 2]};
        for (plUInt32 uiEdge = 0; uiEdge < 3; ++uiEdge)
        {
          EdgeOwner owner;
          owner.m_uiTriangle = uiTriangle;
          owner.m_uiEdgeA = triangle[uiEdge];
          owner.m_uiEdgeB = triangle[(uiEdge + 1) % 3];
          owner.m_uiOpposite = triangle[(uiEdge + 2) % 3];
          const plUInt64 uiKey = MakeEdgeKey(owner.m_uiEdgeA, owner.m_uiEdgeB);
          EdgeOwner first;
          if (owners.TryGetValue(uiKey, first))
          {
            FlipCandidate& candidate = candidates.ExpandAndGetRef();
            candidate.m_First = first;
            candidate.m_Second = owner;
          }
          else
          {
            owners.Insert(uiKey, owner);
          }
        }
      }

      plDynamicArray<plUInt8> touched;
      touched.SetCount(uiTriangleCount);
      for (plUInt8& value : touched)
        value = 0;
      bool bFlippedAny = false;
      for (const FlipCandidate& candidate : candidates)
      {
        const EdgeOwner& first = candidate.m_First;
        const EdgeOwner& second = candidate.m_Second;
        if (touched[first.m_uiTriangle] != 0 || touched[second.m_uiTriangle] != 0)
          continue;

        const plUInt32 a = first.m_uiEdgeA;
        const plUInt32 b = first.m_uiEdgeB;
        const plUInt32 c = first.m_uiOpposite;
        const plUInt32 d = second.m_uiOpposite;
        const float fSideC = Cross2D(vertices[a].m_vPosition.GetAsVec2(), vertices[b].m_vPosition.GetAsVec2(), vertices[c].m_vPosition.GetAsVec2());
        const float fSideD = Cross2D(vertices[a].m_vPosition.GetAsVec2(), vertices[b].m_vPosition.GetAsVec2(), vertices[d].m_vPosition.GetAsVec2());
        if (fSideC * fSideD >= -0.0000001f)
          continue; // The two triangles do not form a strictly convex quad.
        const float fNewSideA = Cross2D(vertices[c].m_vPosition.GetAsVec2(), vertices[d].m_vPosition.GetAsVec2(), vertices[a].m_vPosition.GetAsVec2());
        const float fNewSideB = Cross2D(vertices[c].m_vPosition.GetAsVec2(), vertices[d].m_vPosition.GetAsVec2(), vertices[b].m_vPosition.GetAsVec2());
        if (fNewSideA * fNewSideB >= -0.0000001f)
          continue; // The replacement diagonal would leave the quad or overlap its triangles.

        const plUInt32 firstBase = first.m_uiTriangle * 3;
        const plUInt32 secondBase = second.m_uiTriangle * 3;
        const float fCurrentQuality = plMath::Min(
          TriangleQuality(vertices[ref_indices[firstBase]], vertices[ref_indices[firstBase + 1]], vertices[ref_indices[firstBase + 2]]),
          TriangleQuality(vertices[ref_indices[secondBase]], vertices[ref_indices[secondBase + 1]], vertices[ref_indices[secondBase + 2]]));
        const float fFlippedQuality = plMath::Min(TriangleQuality(vertices[c], vertices[d], vertices[a]),
          TriangleQuality(vertices[d], vertices[c], vertices[b]));
        if (fFlippedQuality <= fCurrentQuality + 0.001f)
          continue;

        SetCounterClockwiseTriangle(ref_indices, first.m_uiTriangle, c, d, a, vertices);
        SetCounterClockwiseTriangle(ref_indices, second.m_uiTriangle, d, c, b, vertices);
        touched[first.m_uiTriangle] = 1;
        touched[second.m_uiTriangle] = 1;
        bFlippedAny = true;
      }

      if (!bFlippedAny)
        break;
    }
  }

  float DistanceToBoundary(plArrayPtr<const plVec3> boundary, const plVec2& point)
  {
    float fBestDistanceSquared = plMath::HighValue<float>();
    for (plUInt32 i = 0; i < boundary.GetCount(); ++i)
    {
      const plVec2 a = boundary[i].GetAsVec2();
      const plVec2 b = boundary[(i + 1) % boundary.GetCount()].GetAsVec2();
      const plVec2 edge = b - a;
      const float fLengthSquared = edge.GetLengthSquared();
      const float t = fLengthSquared > plMath::SmallEpsilon<float>() ? plMath::Clamp((point - a).Dot(edge) / fLengthSquared, 0.0f, 1.0f) : 0.0f;
      fBestDistanceSquared = plMath::Min(fBestDistanceSquared, (point - (a + edge * t)).GetLengthSquared());
    }
    return plMath::Sqrt(fBestDistanceSquared);
  }
} // namespace

plResult plWaterGeometry::BuildRiverRibbon(plArrayPtr<const plWaterControlPoint> points,
  plDynamicArray<plWaterGeometryVertex>& out_vertices, plDynamicArray<plUInt32>& out_indices, const plWaterFiniteMeshSettings& settings)
{
  out_vertices.Clear();
  out_indices.Clear();

  if (points.GetCount() < 2)
    return PL_FAILURE;

  const float fTargetEdgeLength = plMath::Max(settings.m_fTargetEdgeLength, 0.1f);
  plDynamicArray<plWaterControlPoint> samples;
  samples.Reserve(points.GetCount() * 2);
  for (plUInt32 i = 0; i + 1 < points.GetCount(); ++i)
  {
    const float fLength = (points[i + 1].m_vPosition - points[i].m_vPosition).GetLength();
    const plUInt32 uiSteps = plMath::Clamp<plUInt32>(static_cast<plUInt32>(plMath::Ceil(fLength / fTargetEdgeLength)), 1, 256);
    if (samples.GetCount() + uiSteps > s_uiMaximumFiniteWaterVertices)
      return PL_FAILURE;
    for (plUInt32 step = 0; step < uiSteps; ++step)
    {
      const float t = static_cast<float>(step) / static_cast<float>(uiSteps);
      plWaterControlPoint& sample = samples.ExpandAndGetRef();
      sample.m_vPosition = plMath::Lerp(points[i].m_vPosition, points[i + 1].m_vPosition, t);
      sample.m_fLeftWidth = plMath::Lerp(points[i].m_fLeftWidth, points[i + 1].m_fLeftWidth, t);
      sample.m_fRightWidth = plMath::Lerp(points[i].m_fRightWidth, points[i + 1].m_fRightWidth, t);
      sample.m_fDepth = plMath::Lerp(points[i].m_fDepth, points[i + 1].m_fDepth, t);
      sample.m_fFlowScale = plMath::Lerp(points[i].m_fFlowScale, points[i + 1].m_fFlowScale, t);
    }
  }
  samples.PushBack(points[points.GetCount() - 1]);

  float fMaximumWidth = 0.0f;
  for (const plWaterControlPoint& sample : samples)
    fMaximumWidth = plMath::Max(fMaximumWidth, sample.m_fLeftWidth + sample.m_fRightWidth);
  const plUInt32 uiCrossSubdivisions = plMath::Clamp<plUInt32>(static_cast<plUInt32>(plMath::Ceil(fMaximumWidth / fTargetEdgeLength)), 2,
    plMath::Max<plUInt32>(settings.m_uiMaxRiverCrossSubdivisions, 2));
  const plUInt32 uiColumns = uiCrossSubdivisions + 1;
  if (samples.GetCount() > s_uiMaximumFiniteWaterVertices / uiColumns)
    return PL_FAILURE;
  out_vertices.SetCount(samples.GetCount() * uiColumns);
  out_indices.Reserve((samples.GetCount() - 1) * uiCrossSubdivisions * 6);

  float fDistanceAlongRiver = 0.0f;
  for (plUInt32 i = 0; i < samples.GetCount(); ++i)
  {
    const plVec2 current = samples[i].m_vPosition.GetAsVec2();
    const plVec2 previous = samples[i > 0 ? i - 1 : i].m_vPosition.GetAsVec2();
    const plVec2 next = samples[i + 1 < samples.GetCount() ? i + 1 : i].m_vPosition.GetAsVec2();

    const plVec2 incoming = SafeNormalized(current - previous, SafeNormalized(next - current, plVec2(1.0f, 0.0f)));
    const plVec2 outgoing = SafeNormalized(next - current, incoming);
    const plVec2 incomingNormal(-incoming.y, incoming.x);
    const plVec2 outgoingNormal(-outgoing.y, outgoing.x);

    plVec2 miter = SafeNormalized(incomingNormal + outgoingNormal, outgoingNormal);
    float fMiterScale = 1.0f / plMath::Max(plMath::Abs(miter.Dot(outgoingNormal)), 0.25f);
    const float fMiterLimit = plMath::Lerp(1.25f, 1.75f, plMath::Saturate(settings.m_fRiverCurvatureRefinement));
    fMiterScale = plMath::Min(fMiterScale, fMiterLimit);

    const float fLeftWidth = plMath::Max(samples[i].m_fLeftWidth, 0.01f);
    const float fRightWidth = plMath::Max(samples[i].m_fRightWidth, 0.01f);
    const plVec2 tangent = SafeNormalized(incoming + outgoing, outgoing);
    const plVec3 flow(tangent.x * samples[i].m_fFlowScale, tangent.y * samples[i].m_fFlowScale, 0.0f);

    if (i > 0)
      fDistanceAlongRiver += (samples[i].m_vPosition - samples[i - 1].m_vPosition).GetLength();

    for (plUInt32 column = 0; column < uiColumns; ++column)
    {
      const float t = static_cast<float>(column) / static_cast<float>(uiCrossSubdivisions);
      const float fLateralOffset = plMath::Lerp(fLeftWidth, -fRightWidth, t) * fMiterScale;
      const float fDistanceToShore = plMath::Min(t, 1.0f - t) * (fLeftWidth + fRightWidth);
      plWaterGeometryVertex& vertex = out_vertices[i * uiColumns + column];
      vertex.m_vPosition = samples[i].m_vPosition + plVec3(miter.x, miter.y, 0.0f) * fLateralOffset;
      vertex.m_vFlow = flow;
      vertex.m_vTexCoord = plVec2(fDistanceAlongRiver, t);
      vertex.m_fSignedShoreDistance = plMath::Lerp(fLeftWidth, -fRightWidth, t);
      vertex.m_fDepth = plMath::Max(samples[i].m_fDepth, 0.01f);
      vertex.m_fWaveAttenuation = plMath::SmoothStep(fDistanceToShore, 0.0f, plMath::Max(settings.m_fShoreWaveFadeDistance, 0.01f));
    }
  }

  for (plUInt32 row = 0; row + 1 < samples.GetCount(); ++row)
  {
    for (plUInt32 column = 0; column < uiCrossSubdivisions; ++column)
    {
      const plUInt32 i0 = row * uiColumns + column;
      const plUInt32 i1 = i0 + 1;
      const plUInt32 i2 = i0 + uiColumns;
      const plUInt32 i3 = i2 + 1;
      if (Cross2D(out_vertices[i0].m_vPosition.GetAsVec2(), out_vertices[i1].m_vPosition.GetAsVec2(), out_vertices[i2].m_vPosition.GetAsVec2()) <= 0.000001f ||
          Cross2D(out_vertices[i1].m_vPosition.GetAsVec2(), out_vertices[i3].m_vPosition.GetAsVec2(), out_vertices[i2].m_vPosition.GetAsVec2()) <= 0.000001f)
      {
        out_vertices.Clear();
        out_indices.Clear();
        return PL_FAILURE;
      }
      out_indices.PushBack(i0);
      out_indices.PushBack(i1);
      out_indices.PushBack(i2);
      out_indices.PushBack(i1);
      out_indices.PushBack(i3);
      out_indices.PushBack(i2);
    }
  }

  return PL_SUCCESS;
}

plResult plWaterGeometry::TriangulateLake(plArrayPtr<const plVec3> boundary,
  plDynamicArray<plWaterGeometryVertex>& out_vertices, plDynamicArray<plUInt32>& out_indices, float fEpsilon,
  const plWaterFiniteMeshSettings& settings)
{
  out_vertices.Clear();
  out_indices.Clear();

  if (boundary.GetCount() < 3)
    return PL_FAILURE;

  float fMinZ = boundary[0].z;
  float fMaxZ = boundary[0].z;
  for (const plVec3& point : boundary)
  {
    fMinZ = plMath::Min(fMinZ, point.z);
    fMaxZ = plMath::Max(fMaxZ, point.z);
  }
  if (fMaxZ - fMinZ > plMath::Max(fEpsilon, plMath::SmallEpsilon<float>()))
    return PL_FAILURE;

  plDynamicArray<plVec3> cleaned;
  cleaned.Reserve(boundary.GetCount());
  for (const plVec3& point : boundary)
  {
    if (cleaned.IsEmpty() || !cleaned.PeekBack().IsEqual(point, fEpsilon))
      cleaned.PushBack(point);
  }
  if (cleaned.GetCount() > 2 && cleaned[0].IsEqual(cleaned.PeekBack(), fEpsilon))
    cleaned.PopBack();

  bool bRemovedPoint = true;
  while (bRemovedPoint && cleaned.GetCount() > 3)
  {
    bRemovedPoint = false;
    for (plUInt32 i = 0; i < cleaned.GetCount(); ++i)
    {
      const plVec2 previous = cleaned[(i + cleaned.GetCount() - 1) % cleaned.GetCount()].GetAsVec2();
      const plVec2 current = cleaned[i].GetAsVec2();
      const plVec2 next = cleaned[(i + 1) % cleaned.GetCount()].GetAsVec2();
      if (plMath::Abs(Cross2D(previous, current, next)) <= fEpsilon)
      {
        cleaned.RemoveAtAndCopy(i);
        bRemovedPoint = true;
        break;
      }
    }
  }

  if (cleaned.GetCount() < 3)
    return PL_FAILURE;

  for (plUInt32 a = 0; a < cleaned.GetCount(); ++a)
  {
    const plUInt32 aNext = (a + 1) % cleaned.GetCount();
    for (plUInt32 b = a + 1; b < cleaned.GetCount(); ++b)
    {
      const plUInt32 bNext = (b + 1) % cleaned.GetCount();
      if (a == b || aNext == b || bNext == a)
        continue;
      if (a == 0 && bNext == 0)
        continue;

      if (SegmentsIntersect(cleaned[a].GetAsVec2(), cleaned[aNext].GetAsVec2(), cleaned[b].GetAsVec2(), cleaned[bNext].GetAsVec2(), fEpsilon))
        return PL_FAILURE;
    }
  }

  float fSignedArea = 0.0f;
  for (plUInt32 i = 0; i < cleaned.GetCount(); ++i)
  {
    const plVec2 a = cleaned[i].GetAsVec2();
    const plVec2 b = cleaned[(i + 1) % cleaned.GetCount()].GetAsVec2();
    fSignedArea += a.x * b.y - b.x * a.y;
  }
  if (plMath::Abs(fSignedArea) <= fEpsilon)
    return PL_FAILURE;

  out_vertices.SetCount(cleaned.GetCount());
  for (plUInt32 i = 0; i < cleaned.GetCount(); ++i)
  {
    out_vertices[i].m_vPosition = cleaned[i];
    out_vertices[i].m_vTexCoord = cleaned[i].GetAsVec2();
  }

  plDynamicArray<plUInt32> polygon;
  polygon.SetCountUninitialized(cleaned.GetCount());
  if (fSignedArea > 0.0f)
  {
    for (plUInt32 i = 0; i < cleaned.GetCount(); ++i)
      polygon[i] = i;
  }
  else
  {
    for (plUInt32 i = 0; i < cleaned.GetCount(); ++i)
      polygon[i] = cleaned.GetCount() - 1 - i;
  }

  plUInt32 uiGuard = polygon.GetCount() * polygon.GetCount();
  while (polygon.GetCount() > 3 && uiGuard-- > 0)
  {
    bool bFoundEar = false;
    for (plUInt32 i = 0; i < polygon.GetCount(); ++i)
    {
      const plUInt32 previousIndex = polygon[(i + polygon.GetCount() - 1) % polygon.GetCount()];
      const plUInt32 currentIndex = polygon[i];
      const plUInt32 nextIndex = polygon[(i + 1) % polygon.GetCount()];
      const plVec2 previous = cleaned[previousIndex].GetAsVec2();
      const plVec2 current = cleaned[currentIndex].GetAsVec2();
      const plVec2 next = cleaned[nextIndex].GetAsVec2();

      if (Cross2D(previous, current, next) <= fEpsilon)
        continue;

      bool bContainsVertex = false;
      for (plUInt32 j = 0; j < polygon.GetCount(); ++j)
      {
        const plUInt32 testIndex = polygon[j];
        if (testIndex == previousIndex || testIndex == currentIndex || testIndex == nextIndex)
          continue;

        if (PointInTriangle(cleaned[testIndex].GetAsVec2(), previous, current, next, fEpsilon))
        {
          bContainsVertex = true;
          break;
        }
      }
      if (bContainsVertex)
        continue;

      out_indices.PushBack(previousIndex);
      out_indices.PushBack(currentIndex);
      out_indices.PushBack(nextIndex);
      polygon.RemoveAtAndCopy(i);
      bFoundEar = true;
      break;
    }

    if (!bFoundEar)
    {
      out_vertices.Clear();
      out_indices.Clear();
      return PL_FAILURE;
    }
  }

  if (polygon.GetCount() != 3)
  {
    out_vertices.Clear();
    out_indices.Clear();
    return PL_FAILURE;
  }

  out_indices.PushBack(polygon[0]);
  out_indices.PushBack(polygon[1]);
  out_indices.PushBack(polygon[2]);

  const float fMaximumEdgeLengthSquared = plMath::Square(plMath::Max(settings.m_fTargetEdgeLength, 0.1f));
  bool bTopologyRegularized = false;
  for (plUInt32 iteration = 0; iteration < settings.m_uiMaxLakeRefinementIterations; ++iteration)
  {
    plHashTable<plUInt64, plUInt32> splitEdges;
    for (plUInt32 i = 0; i + 2 < out_indices.GetCount(); i += 3)
    {
      const plUInt32 triangle[3] = {out_indices[i], out_indices[i + 1], out_indices[i + 2]};
      for (plUInt32 edge = 0; edge < 3; ++edge)
      {
        const plUInt32 a = triangle[edge];
        const plUInt32 b = triangle[(edge + 1) % 3];
        const float fEdgeLengthSquared = (out_vertices[a].m_vPosition - out_vertices[b].m_vPosition).GetLengthSquared();
        if (fEdgeLengthSquared > fMaximumEdgeLengthSquared)
          splitEdges.Insert(MakeEdgeKey(a, b), plInvalidIndex);
      }
    }
    if (splitEdges.IsEmpty())
      break;
    if (out_vertices.GetCount() + splitEdges.GetCount() > s_uiMaximumFiniteWaterVertices)
    {
      // Preserve the finest complete topology that fits the finite-water budget. Detailed interaction meshes
      // should degrade gracefully on very large lakes rather than making the entire water body disappear.
      break;
    }

    for (auto it = splitEdges.GetIterator(); it.IsValid(); ++it)
    {
      const plUInt32 a = static_cast<plUInt32>(it.Key() >> 32);
      const plUInt32 b = static_cast<plUInt32>(it.Key());
      plWaterGeometryVertex& midpoint = out_vertices.ExpandAndGetRef();
      midpoint.m_vPosition = (out_vertices[a].m_vPosition + out_vertices[b].m_vPosition) * 0.5f;
      midpoint.m_vNormal = plVec3::MakeAxisZ();
      midpoint.m_vTexCoord = midpoint.m_vPosition.GetAsVec2();
      it.Value() = out_vertices.GetCount() - 1;
    }

    plDynamicArray<plUInt32> refined;
    refined.Reserve(out_indices.GetCount() * 2);
    auto emit = [&](plUInt32 a, plUInt32 b, plUInt32 c) { refined.PushBack(a); refined.PushBack(b); refined.PushBack(c); };
    for (plUInt32 i = 0; i + 2 < out_indices.GetCount(); i += 3)
    {
      const plUInt32 a = out_indices[i], b = out_indices[i + 1], c = out_indices[i + 2];
      plUInt32 ab = plInvalidIndex, bc = plInvalidIndex, ca = plInvalidIndex;
      const bool hasAB = splitEdges.TryGetValue(MakeEdgeKey(a, b), ab);
      const bool hasBC = splitEdges.TryGetValue(MakeEdgeKey(b, c), bc);
      const bool hasCA = splitEdges.TryGetValue(MakeEdgeKey(c, a), ca);
      const plUInt32 mask = (hasAB ? 1u : 0u) | (hasBC ? 2u : 0u) | (hasCA ? 4u : 0u);
      switch (mask)
      {
        case 0: emit(a, b, c); break;
        case 1: emit(a, ab, c); emit(ab, b, c); break;
        case 2: emit(b, bc, a); emit(bc, c, a); break;
        case 4: emit(c, ca, b); emit(ca, a, b); break;
        case 3: emit(b, bc, ab); emit(a, ab, c); emit(ab, bc, c); break;
        case 5: emit(a, ab, ca); emit(ab, b, c); emit(ab, c, ca); break;
        case 6: emit(c, ca, bc); emit(a, b, ca); emit(b, bc, ca); break;
        case 7: emit(a, ab, ca); emit(ab, b, bc); emit(ca, bc, c); emit(ab, bc, ca); break;
      }
    }
    out_indices = std::move(refined);
    if (!bTopologyRegularized && out_indices.GetCount() / 3 >= 8192)
    {
      // Regularize before very large lakes reach their final subdivision count. Subsequent midpoint refinement
      // preserves triangle quality, avoiding an expensive adjacency pass over hundreds of thousands of faces.
      RegularizeLakeTriangles(out_vertices, out_indices);
      bTopologyRegularized = true;
    }
  }
  if (!bTopologyRegularized)
    RegularizeLakeTriangles(out_vertices, out_indices);

  for (plWaterGeometryVertex& vertex : out_vertices)
  {
    const float fDistance = DistanceToBoundary(cleaned, vertex.m_vPosition.GetAsVec2());
    vertex.m_fWaveAttenuation = plMath::SmoothStep(fDistance, 0.0f, plMath::Max(settings.m_fShoreWaveFadeDistance, 0.01f));
  }
  return PL_SUCCESS;
}

void plWaterGeometry::BuildOceanClipmap(plUInt8 uiRingCount, plUInt16 uiPatchResolution, float fBasePatchSize,
  plDynamicArray<plWaterOceanClipmapPatch>& out_patches)
{
  out_patches.Clear();
  uiRingCount = plMath::Clamp<plUInt8>(uiRingCount, 1, 12);
  uiPatchResolution = plMath::Clamp<plUInt16>(uiPatchResolution, 4, 512);
  fBasePatchSize = plMath::Max(fBasePatchSize, 1.0f);
  out_patches.Reserve(16 + (uiRingCount - 1) * 12);

  for (plUInt8 uiLod = 0; uiLod < uiRingCount; ++uiLod)
  {
    const float fPatchSize = fBasePatchSize * static_cast<float>(1u << uiLod);
    for (plInt32 y = -2; y <= 1; ++y)
    {
      for (plInt32 x = -2; x <= 1; ++x)
      {
        if (uiLod > 0 && x >= -1 && x <= 0 && y >= -1 && y <= 0)
          continue;

        plWaterOceanClipmapPatch& patch = out_patches.ExpandAndGetRef();
        patch.m_vCenterOffset = plVec2((static_cast<float>(x) + 0.5f) * fPatchSize, (static_cast<float>(y) + 0.5f) * fPatchSize);
        patch.m_fPatchSize = fPatchSize;
        patch.m_uiResolution = uiPatchResolution;
        patch.m_uiLod = uiLod;

        if (uiLod + 1 < uiRingCount)
        {
          if (x == -2) patch.m_uiOuterSeamMask |= plWaterClipmapSeamFlags::West;
          if (x == 1) patch.m_uiOuterSeamMask |= plWaterClipmapSeamFlags::East;
          if (y == -2) patch.m_uiOuterSeamMask |= plWaterClipmapSeamFlags::South;
          if (y == 1) patch.m_uiOuterSeamMask |= plWaterClipmapSeamFlags::North;
        }

        if (uiLod > 0)
        {
          if (x == -2 && (y == -1 || y == 0)) patch.m_uiInnerSeamMask |= plWaterClipmapSeamFlags::East;
          if (x == 1 && (y == -1 || y == 0)) patch.m_uiInnerSeamMask |= plWaterClipmapSeamFlags::West;
          if (y == -2 && (x == -1 || x == 0)) patch.m_uiInnerSeamMask |= plWaterClipmapSeamFlags::North;
          if (y == 1 && (x == -1 || x == 0)) patch.m_uiInnerSeamMask |= plWaterClipmapSeamFlags::South;
        }
      }
    }
  }
}

bool plWaterGeometry::IsPointInPolygonXY(plArrayPtr<const plVec3> polygon, const plVec2& point, float fEpsilon)
{
  if (polygon.GetCount() < 3)
    return false;

  bool bInside = false;
  for (plUInt32 i = 0, j = polygon.GetCount() - 1; i < polygon.GetCount(); j = i++)
  {
    const plVec2 a = polygon[j].GetAsVec2();
    const plVec2 b = polygon[i].GetAsVec2();
    if (IsPointOnSegment(a, b, point, fEpsilon))
      return true;

    if ((a.y > point.y) != (b.y > point.y))
    {
      const float fIntersectionX = (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x;
      if (point.x < fIntersectionX)
        bInside = !bInside;
    }
  }
  return bInside;
}

bool plWaterGeometry::FindClosestPointOnRiver(plArrayPtr<const plWaterControlPoint> points, const plVec2& point,
  plWaterRiverLocation& out_location)
{
  out_location = plWaterRiverLocation();
  if (points.GetCount() < 2)
    return false;

  for (plUInt32 i = 0; i + 1 < points.GetCount(); ++i)
  {
    const plVec2 start = points[i].m_vPosition.GetAsVec2();
    const plVec2 end = points[i + 1].m_vPosition.GetAsVec2();
    const plVec2 segment = end - start;
    const float fLengthSquared = segment.GetLengthSquared();
    if (fLengthSquared <= plMath::SmallEpsilon<float>())
      continue;

    const float t = plMath::Clamp((point - start).Dot(segment) / fLengthSquared, 0.0f, 1.0f);
    const plVec2 closest = start + segment * t;
    const float fDistanceSquared = (point - closest).GetLengthSquared();
    if (fDistanceSquared >= out_location.m_fDistanceSquared)
      continue;

    out_location.m_uiSegment = i;
    out_location.m_fSegmentFraction = t;
    out_location.m_fDistanceSquared = fDistanceSquared;
    out_location.m_fLeftWidth = plMath::Lerp(points[i].m_fLeftWidth, points[i + 1].m_fLeftWidth, t);
    out_location.m_fRightWidth = plMath::Lerp(points[i].m_fRightWidth, points[i + 1].m_fRightWidth, t);
    out_location.m_fDepth = plMath::Lerp(points[i].m_fDepth, points[i + 1].m_fDepth, t);
    out_location.m_fFlowScale = plMath::Lerp(points[i].m_fFlowScale, points[i + 1].m_fFlowScale, t);
    out_location.m_vCenter = plMath::Lerp(points[i].m_vPosition, points[i + 1].m_vPosition, t);
    const plVec2 tangent = segment / plMath::Sqrt(fLengthSquared);
    out_location.m_vTangent = plVec3(tangent.x, tangent.y, 0.0f);
    const plVec2 lateral(-tangent.y, tangent.x);
    out_location.m_fSignedLateralDistance = (point - closest).Dot(lateral);
  }

  return out_location.m_uiSegment != plInvalidIndex;
}

PL_STATICLINK_FILE(WaterPlugin, WaterPlugin_Utils_Implementation_WaterGeometry);
