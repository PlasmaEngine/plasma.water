#include <WaterPlugin/WaterPluginPCH.h>

#include <WaterPlugin/Utils/WaterNetworkMesher.h>

#include <Foundation/Containers/HashTable.h>
#include <Foundation/Containers/HybridArray.h>

namespace
{
  constexpr plUInt32 s_uiMaximumPartVertices = 262144;

  float NetworkCross2D(const plVec2& a, const plVec2& b, const plVec2& c)
  {
    const plVec2 ab = b - a;
    const plVec2 ac = c - a;
    return ab.x * ac.y - ab.y * ac.x;
  }

  plVec2 NetworkSafeNormalized(const plVec2& value, const plVec2& fallback)
  {
    const float fLengthSquared = value.GetLengthSquared();
    if (fLengthSquared <= plMath::SmallEpsilon<float>())
      return fallback;
    return value / plMath::Sqrt(fLengthSquared);
  }

  float DistanceToSegment(const plVec2& a, const plVec2& b, const plVec2& p)
  {
    const plVec2 ab = b - a;
    const float fLengthSquared = ab.GetLengthSquared();
    const float t = fLengthSquared > plMath::SmallEpsilon<float>() ? plMath::Clamp((p - a).Dot(ab) / fLengthSquared, 0.0f, 1.0f) : 0.0f;
    return (p - (a + ab * t)).GetLength();
  }

  /// A vertex while a part is put together: what is drawn, plus what decides its profile pair later.
  struct WorkVertex
  {
    plWaterGeometryVertex m_Vertex;
    float m_fFromStart = 0.0f;    ///< Rivers: metres along the river from its start. Negative on an apron in front of the start.
    float m_fFromEnd = 0.0f;      ///< Rivers: metres from its end. Negative on an apron behind the end.
    float m_fApronWeight = -1.0f; ///< Apron vertices: share of the ocean's profile. Negative everywhere else.
  };

  struct WorkPart
  {
    plDynamicArray<WorkVertex> m_Vertices;
    plDynamicArray<plUInt32> m_Indices;
    plUInt32 m_uiFoldedTriangles = 0;

    /// Adds a triangle, wound counter-clockwise. Leaves out one without area; counts and leaves out one that is
    /// wound the wrong way when the caller says the winding is known (bExpectCounterClockwise).
    void AddTriangle(plUInt32 a, plUInt32 b, plUInt32 c, bool bExpectCounterClockwise)
    {
      const float fArea = NetworkCross2D(m_Vertices[a].m_Vertex.m_vPosition.GetAsVec2(), m_Vertices[b].m_Vertex.m_vPosition.GetAsVec2(), m_Vertices[c].m_Vertex.m_vPosition.GetAsVec2());
      if (plMath::Abs(fArea) <= 0.0000001f)
        return;
      if (fArea < 0.0f)
      {
        if (bExpectCounterClockwise)
        {
          ++m_uiFoldedTriangles;
          return;
        }
        plMath::Swap(b, c);
      }
      m_Indices.PushBack(a);
      m_Indices.PushBack(b);
      m_Indices.PushBack(c);
    }

    /// Triangulates the strip between two rows of vertices that run the same way (both from the river's left bank
    /// to its right), advancing along whichever row is behind.
    void Zip(plArrayPtr<const plUInt32> rowA, plArrayPtr<const plUInt32> rowB)
    {
      if (rowA.GetCount() < 1 || rowB.GetCount() < 1 || rowA.GetCount() + rowB.GetCount() < 3)
        return;

      auto parameters = [&](plArrayPtr<const plUInt32> row, plDynamicArray<float>& out_values)
      {
        out_values.SetCount(row.GetCount());
        float fLength = 0.0f;
        out_values[0] = 0.0f;
        for (plUInt32 i = 1; i < row.GetCount(); ++i)
        {
          fLength += (m_Vertices[row[i]].m_Vertex.m_vPosition - m_Vertices[row[i - 1]].m_Vertex.m_vPosition).GetLength();
          out_values[i] = fLength;
        }
        for (float& value : out_values)
          value = fLength > 0.0f ? value / fLength : 0.0f;
      };
      plDynamicArray<float> ta;
      plDynamicArray<float> tb;
      parameters(rowA, ta);
      parameters(rowB, tb);

      plUInt32 i = 0;
      plUInt32 j = 0;
      while (i + 1 < rowA.GetCount() || j + 1 < rowB.GetCount())
      {
        const bool bCanA = i + 1 < rowA.GetCount();
        const bool bCanB = j + 1 < rowB.GetCount();
        // Advance on the row whose next vertex comes first; this keeps the triangles as short as the two rows allow.
        const bool bAdvanceA = bCanA && (!bCanB || ta[i + 1] <= tb[j + 1]);
        if (bAdvanceA)
        {
          AddTriangle(rowA[i], rowA[i + 1], rowB[j], false);
          ++i;
        }
        else
        {
          AddTriangle(rowA[i], rowB[j + 1], rowB[j], false);
          ++j;
        }
      }
    }
  };

  //////////////////////////////////////////////////////////////////////////
  // Rivers

  struct RiverWork
  {
    bool m_bValid = false;
    WorkPart m_Part;
    plUInt32 m_uiColumns = 0; ///< Vertices per row.
    plUInt32 m_uiRows = 0;
    float m_fLength = 0.0f;
    float m_fStep = 1.0f; ///< Typical distance between rows.

    // Each end: the middle of the last row, the direction that leaves the river there, the direction to its left bank
    // (seen along the flow) and the widths.
    plVec3 m_vEndCenter[2] = {plVec3::MakeZero(), plVec3::MakeZero()};
    plVec2 m_vOutward[2] = {plVec2(1.0f, 0.0f), plVec2(1.0f, 0.0f)};
    plVec2 m_vLeft[2] = {plVec2(0.0f, 1.0f), plVec2(0.0f, 1.0f)};
    float m_fLeftWidth[2] = {0.0f, 0.0f};
    float m_fRightWidth[2] = {0.0f, 0.0f};
    float m_fFlowScale[2] = {1.0f, 1.0f};
    float m_fDepth[2] = {1.0f, 1.0f};

    // Rows [m_uiFirstRow, m_uiLastRow] are kept. A join with a lake takes rows away at that end.
    plUInt32 m_uiFirstRow = 0;
    plUInt32 m_uiLastRow = 0;

    plUInt32 Index(plUInt32 uiRow, plUInt32 uiColumn) const { return uiRow * m_uiColumns + uiColumn; }
    plVec3 RowCenter(plUInt32 uiRow) const
    {
      return (m_Part.m_Vertices[Index(uiRow, 0)].m_Vertex.m_vPosition + m_Part.m_Vertices[Index(uiRow, m_uiColumns - 1)].m_Vertex.m_vPosition) * 0.5f;
    }
  };

  void BuildRiverRows(const plWaterNetworkBody& body, float fLodScale, plUInt32 uiLod, RiverWork& out_river)
  {
    const plArrayPtr<const plWaterControlPoint> points = body.m_Points;
    if (points.GetCount() < 2)
      return;

    const float fTargetEdgeLength = plMath::Max(body.m_Settings.m_fTargetEdgeLength, 0.1f) * fLodScale;
    plDynamicArray<plWaterControlPoint> samples;
    for (plUInt32 i = 0; i + 1 < points.GetCount(); ++i)
    {
      const float fLength = (points[i + 1].m_vPosition - points[i].m_vPosition).GetLength();
      const plUInt32 uiSteps = plMath::Clamp<plUInt32>(static_cast<plUInt32>(plMath::Ceil(fLength / fTargetEdgeLength)), 1, 256);
      if (samples.GetCount() + uiSteps > s_uiMaximumPartVertices)
        return;
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
    const plUInt32 uiMaxCross = plMath::Max<plUInt32>(static_cast<plUInt32>(body.m_Settings.m_uiMaxRiverCrossSubdivisions) >> uiLod, 2u);
    const plUInt32 uiCrossSubdivisions = plMath::Clamp<plUInt32>(static_cast<plUInt32>(plMath::Ceil(fMaximumWidth / fTargetEdgeLength)), 2, uiMaxCross);
    const plUInt32 uiColumns = uiCrossSubdivisions + 1;
    if (samples.GetCount() > s_uiMaximumPartVertices / uiColumns)
      return;

    out_river.m_uiColumns = uiColumns;
    out_river.m_uiRows = samples.GetCount();
    out_river.m_Part.m_Vertices.SetCount(samples.GetCount() * uiColumns);

    plDynamicArray<plVec2> tangents;
    tangents.SetCount(samples.GetCount());
    plDynamicArray<float> distances;
    distances.SetCount(samples.GetCount());

    float fDistanceAlongRiver = 0.0f;
    for (plUInt32 i = 0; i < samples.GetCount(); ++i)
    {
      const plVec2 current = samples[i].m_vPosition.GetAsVec2();
      const plVec2 previous = samples[i > 0 ? i - 1 : i].m_vPosition.GetAsVec2();
      const plVec2 next = samples[i + 1 < samples.GetCount() ? i + 1 : i].m_vPosition.GetAsVec2();

      const plVec2 incoming = NetworkSafeNormalized(current - previous, NetworkSafeNormalized(next - current, plVec2(1.0f, 0.0f)));
      const plVec2 outgoing = NetworkSafeNormalized(next - current, incoming);
      const plVec2 incomingNormal(-incoming.y, incoming.x);
      const plVec2 outgoingNormal(-outgoing.y, outgoing.x);

      const plVec2 miter = NetworkSafeNormalized(incomingNormal + outgoingNormal, outgoingNormal);
      float fMiterScale = 1.0f / plMath::Max(plMath::Abs(miter.Dot(outgoingNormal)), 0.25f);
      const float fMiterLimit = plMath::Lerp(1.25f, 1.75f, plMath::Saturate(body.m_Settings.m_fRiverCurvatureRefinement));
      fMiterScale = plMath::Min(fMiterScale, fMiterLimit);

      const float fLeftWidth = plMath::Max(samples[i].m_fLeftWidth, 0.01f);
      const float fRightWidth = plMath::Max(samples[i].m_fRightWidth, 0.01f);
      const plVec2 tangent = NetworkSafeNormalized(incoming + outgoing, outgoing);
      tangents[i] = tangent;

      if (i > 0)
        fDistanceAlongRiver += (samples[i].m_vPosition - samples[i - 1].m_vPosition).GetLength();
      distances[i] = fDistanceAlongRiver;

      plVec3 vLeftBank = samples[i].m_vPosition + plVec3(miter.x, miter.y, 0.0f) * (fLeftWidth * fMiterScale);
      plVec3 vRightBank = samples[i].m_vPosition - plVec3(miter.x, miter.y, 0.0f) * (fRightWidth * fMiterScale);

      // On the inside of a bend tighter than the river is wide, the bank would run backwards and the ribbon would fold
      // over itself. Such a bank point stays where the previous one is: the strip pinches to a fan there, and stays a
      // valid surface.
      if (i > 0)
      {
        const plVec2 along = NetworkSafeNormalized(current - previous, tangent);
        const float fMinimumAdvance = 0.02f * fTargetEdgeLength;
        const plVec3 vPreviousLeft = out_river.m_Part.m_Vertices[(i - 1) * uiColumns].m_Vertex.m_vPosition;
        const plVec3 vPreviousRight = out_river.m_Part.m_Vertices[(i - 1) * uiColumns + uiColumns - 1].m_Vertex.m_vPosition;
        if ((vLeftBank - vPreviousLeft).GetAsVec2().Dot(along) < fMinimumAdvance)
          vLeftBank = plVec3(vPreviousLeft.x, vPreviousLeft.y, vLeftBank.z);
        if ((vRightBank - vPreviousRight).GetAsVec2().Dot(along) < fMinimumAdvance)
          vRightBank = plVec3(vPreviousRight.x, vPreviousRight.y, vRightBank.z);
      }

      for (plUInt32 column = 0; column < uiColumns; ++column)
      {
        const float t = static_cast<float>(column) / static_cast<float>(uiCrossSubdivisions);
        const float fDistanceToShore = plMath::Min(t, 1.0f - t) * (fLeftWidth + fRightWidth);
        WorkVertex& work = out_river.m_Part.m_Vertices[i * uiColumns + column];
        plWaterGeometryVertex& vertex = work.m_Vertex;
        vertex.m_vPosition = plMath::Lerp(vLeftBank, vRightBank, t);
        vertex.m_vFlow = plVec3(tangent.x * samples[i].m_fFlowScale, tangent.y * samples[i].m_fFlowScale, 0.0f);
        // Ribbon coordinates: metres along the river and metres to the left of its middle. The river's waves are a
        // function of these, so their crests stay square to the flow through every bend.
        vertex.m_vTexCoord = plVec2(fDistanceAlongRiver, plMath::Lerp(fLeftWidth, -fRightWidth, t));
        vertex.m_fSignedShoreDistance = plMath::Lerp(fLeftWidth, -fRightWidth, t);
        vertex.m_fDepth = plMath::Max(samples[i].m_fDepth, 0.01f);
        vertex.m_fWaveAttenuation = plMath::SmoothStep(fDistanceToShore, 0.0f, plMath::Max(body.m_Settings.m_fShoreWaveFadeDistance, 0.01f));
        work.m_fFromStart = fDistanceAlongRiver;
      }
    }

    out_river.m_fLength = fDistanceAlongRiver;
    out_river.m_fStep = fDistanceAlongRiver / static_cast<float>(plMath::Max<plUInt32>(samples.GetCount() - 1, 1));
    for (WorkVertex& work : out_river.m_Part.m_Vertices)
      work.m_fFromEnd = fDistanceAlongRiver - work.m_fFromStart;

    const plUInt32 uiLast = samples.GetCount() - 1;
    const plUInt32 uiEndRows[2] = {0, uiLast};
    for (plUInt32 uiEnd = 0; uiEnd < 2; ++uiEnd)
    {
      const plUInt32 uiRow = uiEndRows[uiEnd];
      const plVec2 tangent = tangents[uiRow];
      out_river.m_vEndCenter[uiEnd] = samples[uiRow].m_vPosition;
      out_river.m_vOutward[uiEnd] = uiEnd == 0 ? -tangent : tangent;
      out_river.m_vLeft[uiEnd] = plVec2(-tangent.y, tangent.x);
      out_river.m_fLeftWidth[uiEnd] = plMath::Max(samples[uiRow].m_fLeftWidth, 0.01f);
      out_river.m_fRightWidth[uiEnd] = plMath::Max(samples[uiRow].m_fRightWidth, 0.01f);
      out_river.m_fFlowScale[uiEnd] = samples[uiRow].m_fFlowScale;
      out_river.m_fDepth[uiEnd] = plMath::Max(samples[uiRow].m_fDepth, 0.01f);
    }
    out_river.m_uiFirstRow = 0;
    out_river.m_uiLastRow = uiLast;
    out_river.m_bValid = true;
  }

  //////////////////////////////////////////////////////////////////////////
  // Lakes

  struct LakeMouth
  {
    plUInt32 m_uiJoin = plInvalidIndex;
    float m_fArcStart = 0.0f; ///< Where the mouth begins and ends on the shore, walking the outline counter-clockwise.
    float m_fArcEnd = 0.0f;
    bool m_bLeftIsAtEnd = false; ///< The river's left bank is the point at m_fArcEnd (otherwise the one at m_fArcStart).
    plUInt32 m_uiStartVertex = plInvalidIndex; ///< Outline vertices at the two bank points.
    plUInt32 m_uiEndVertex = plInvalidIndex;
    plVec2 m_vInward = plVec2(1.0f, 0.0f);
    /// The river's flow where it meets the shore. Both copies of the mouth line and the lake's plume carry exactly this
    /// vector: the river's waves run along it, so a point of the line would otherwise move differently in the two parts.
    plVec3 m_vFlow = plVec3::MakeZero();
    /// The river's ribbon coordinates carried on into the lake: metres along the river at this end, the point the
    /// river's middle arrives at, and the directions of "along" and "left". A point of the lake near the mouth gets
    /// its ribbon coordinates from these, and so do both copies of the mouth line.
    float m_fRibbonAlong = 0.0f;
    plVec2 m_vRibbonOrigin = plVec2::MakeZero();
    plVec2 m_vRibbonForward = plVec2(1.0f, 0.0f);
    plVec2 m_vRibbonLeft = plVec2(0.0f, 1.0f);
    plVec2 RibbonCoordinates(const plVec3& vPosition) const
    {
      const plVec2 vOffset = vPosition.GetAsVec2() - m_vRibbonOrigin;
      return plVec2(m_fRibbonAlong + vOffset.Dot(m_vRibbonForward), vOffset.Dot(m_vRibbonLeft));
    }
    plHybridArray<plUInt32, 32> m_Chain; ///< The lake's vertices on the mouth line, from the river's left bank to its right.
    plHybridArray<plUInt32, 32> m_RiverChain; ///< The same points as vertices of the river's part.
  };

  struct LakeWork
  {
    bool m_bValid = false;
    WorkPart m_Part;
    plDynamicArray<plVec3> m_Outline; ///< Counter-clockwise, without duplicates, with the bank points of every mouth.
    plDynamicArray<bool> m_OpenEdge;  ///< Edge i runs from outline point i to i + 1. Open edges are mouth lines.
    plDynamicArray<float> m_ArcAt;    ///< Shore length from outline point 0 to point i.
    float m_fPerimeter = 0.0f;
    float m_fHeight = 0.0f;
    plHybridArray<LakeMouth, 4> m_Mouths;

    plVec3 PointAtArc(float fArc) const
    {
      fArc = plMath::Mod(fArc, m_fPerimeter);
      if (fArc < 0.0f)
        fArc += m_fPerimeter;
      for (plUInt32 i = 0; i < m_Outline.GetCount(); ++i)
      {
        const float fStart = m_ArcAt[i];
        const float fEnd = i + 1 < m_Outline.GetCount() ? m_ArcAt[i + 1] : m_fPerimeter;
        if (fArc <= fEnd || i + 1 == m_Outline.GetCount())
        {
          const float t = fEnd > fStart ? plMath::Saturate((fArc - fStart) / (fEnd - fStart)) : 0.0f;
          return plMath::Lerp(m_Outline[i], m_Outline[(i + 1) % m_Outline.GetCount()], t);
        }
      }
      return m_Outline[0];
    }

    /// The closest point of the shore to a position: its arc, the distance, and the shore's direction there.
    void Project(const plVec2& vPosition, float& out_fArc, float& out_fDistance, plVec2& out_vDirection) const
    {
      out_fDistance = plMath::HighValue<float>();
      out_fArc = 0.0f;
      out_vDirection = plVec2(1.0f, 0.0f);
      for (plUInt32 i = 0; i < m_Outline.GetCount(); ++i)
      {
        const plVec2 a = m_Outline[i].GetAsVec2();
        const plVec2 b = m_Outline[(i + 1) % m_Outline.GetCount()].GetAsVec2();
        const plVec2 ab = b - a;
        const float fLengthSquared = ab.GetLengthSquared();
        if (fLengthSquared <= plMath::SmallEpsilon<float>())
          continue;
        const float t = plMath::Clamp((vPosition - a).Dot(ab) / fLengthSquared, 0.0f, 1.0f);
        const float fDistance = (vPosition - (a + ab * t)).GetLength();
        if (fDistance < out_fDistance)
        {
          out_fDistance = fDistance;
          out_fArc = m_ArcAt[i] + plMath::Sqrt(fLengthSquared) * t;
          out_vDirection = ab / plMath::Sqrt(fLengthSquared);
        }
      }
    }

    void ComputeArcs()
    {
      m_ArcAt.SetCount(m_Outline.GetCount());
      float fArc = 0.0f;
      for (plUInt32 i = 0; i < m_Outline.GetCount(); ++i)
      {
        m_ArcAt[i] = fArc;
        fArc += (m_Outline[(i + 1) % m_Outline.GetCount()] - m_Outline[i]).GetAsVec2().GetLength();
      }
      m_fPerimeter = fArc;
    }

    /// Makes sure the outline has a point at this arc and returns its index. Indices of later points move up by one
    /// when a point is added, which the caller has to allow for.
    plUInt32 InsertPointAtArc(float fArc, bool& out_bInserted)
    {
      out_bInserted = false;
      fArc = plMath::Mod(fArc, m_fPerimeter);
      if (fArc < 0.0f)
        fArc += m_fPerimeter;

      const float fMerge = 0.01f;
      for (plUInt32 i = 0; i < m_Outline.GetCount(); ++i)
      {
        if (plMath::Abs(m_ArcAt[i] - fArc) <= fMerge)
          return i;
      }
      if (m_fPerimeter - fArc <= fMerge)
        return 0;

      plUInt32 uiEdge = m_Outline.GetCount() - 1;
      for (plUInt32 i = 0; i + 1 < m_Outline.GetCount(); ++i)
      {
        if (fArc < m_ArcAt[i + 1])
        {
          uiEdge = i;
          break;
        }
      }
      const plVec3 vPoint = PointAtArc(fArc);
      m_Outline.InsertAt(uiEdge + 1, vPoint);
      ComputeArcs();
      out_bInserted = true;
      return uiEdge + 1;
    }

    float DistanceToClosedShore(const plVec2& vPosition) const
    {
      float fBest = plMath::HighValue<float>();
      for (plUInt32 i = 0; i < m_Outline.GetCount(); ++i)
      {
        if (m_OpenEdge[i])
          continue;
        fBest = plMath::Min(fBest, DistanceToSegment(m_Outline[i].GetAsVec2(), m_Outline[(i + 1) % m_Outline.GetCount()].GetAsVec2(), vPosition));
      }
      return fBest;
    }
  };

  bool PrepareLakeOutline(const plWaterNetworkBody& body, LakeWork& out_lake)
  {
    const float fEpsilon = 0.001f;
    if (body.m_Points.GetCount() < 3)
      return false;

    plDynamicArray<plVec3>& cleaned = out_lake.m_Outline;
    cleaned.Clear();
    float fMinZ = body.m_Points[0].m_vPosition.z;
    float fMaxZ = fMinZ;
    for (const plWaterControlPoint& point : body.m_Points)
    {
      fMinZ = plMath::Min(fMinZ, point.m_vPosition.z);
      fMaxZ = plMath::Max(fMaxZ, point.m_vPosition.z);
      if (cleaned.IsEmpty() || !cleaned.PeekBack().IsEqual(point.m_vPosition, fEpsilon))
        cleaned.PushBack(point.m_vPosition);
    }
    if (fMaxZ - fMinZ > fEpsilon)
      return false;
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
        if (plMath::Abs(NetworkCross2D(previous, current, next)) <= fEpsilon)
        {
          cleaned.RemoveAtAndCopy(i);
          bRemovedPoint = true;
          break;
        }
      }
    }
    if (cleaned.GetCount() < 3)
      return false;

    float fSignedArea = 0.0f;
    for (plUInt32 i = 0; i < cleaned.GetCount(); ++i)
    {
      const plVec2 a = cleaned[i].GetAsVec2();
      const plVec2 b = cleaned[(i + 1) % cleaned.GetCount()].GetAsVec2();
      fSignedArea += a.x * b.y - b.x * a.y;
    }
    if (plMath::Abs(fSignedArea) <= fEpsilon)
      return false;
    if (fSignedArea < 0.0f)
    {
      for (plUInt32 i = 0; i < cleaned.GetCount() / 2; ++i)
        plMath::Swap(cleaned[i], cleaned[cleaned.GetCount() - 1 - i]);
    }

    out_lake.m_fHeight = cleaned[0].z;
    out_lake.ComputeArcs();
    return out_lake.m_fPerimeter > 0.0f;
  }

  //////////////////////////////////////////////////////////////////////////
  // Profile pairs

  /// Turns a part into what is drawn: every triangle gets one profile pair, every corner the weight of the pair's
  /// second profile, and a point is stored once per pair it takes part in.
  struct PairEmitter
  {
    plWaterNetworkPart* m_pPart = nullptr;

    // Per source vertex, the variants emitted so far. Nearly every vertex has one, a few on a mix boundary have two.
    struct Variant
    {
      PL_DECLARE_POD_TYPE();

      plUInt32 m_uiKey;
      plUInt32 m_uiIndex;
      plUInt32 m_uiNext;
    };
    plDynamicArray<plUInt32> m_First;
    plDynamicArray<Variant> m_Variants;

    plUInt32 Emit(const WorkVertex& source, plUInt32 uiSourceIndex, plUInt8 uiProfileA, plUInt8 uiProfileB, float fWeight, float fFrontBreakup = 0.0f)
    {
      const plUInt8 uiWeight = plMath::ColorFloatToByte(plMath::Saturate(fWeight));
      const plUInt32 uiKey = (static_cast<plUInt32>(uiProfileA) << 16) | (static_cast<plUInt32>(uiProfileB) << 8) | uiWeight;
      while (m_First.GetCount() <= uiSourceIndex)
        m_First.PushBack(plInvalidIndex);
      for (plUInt32 uiVariant = m_First[uiSourceIndex]; uiVariant != plInvalidIndex; uiVariant = m_Variants[uiVariant].m_uiNext)
      {
        if (m_Variants[uiVariant].m_uiKey == uiKey)
          return m_Variants[uiVariant].m_uiIndex;
      }

      plWaterGeometryVertex vertex = source.m_Vertex;
      vertex.m_uiProfileA = uiProfileA;
      vertex.m_uiProfileB = uiProfileB;
      vertex.m_fProfileBlend = plMath::Saturate(fWeight);
      vertex.m_fFrontBreakup = uiProfileA != uiProfileB ? plMath::Saturate(fFrontBreakup) : 0.0f;
      const plUInt32 uiIndex = m_pPart->m_Vertices.GetCount();
      m_pPart->m_Vertices.PushBack(vertex);
      m_Variants.PushBack({uiKey, uiIndex, m_First[uiSourceIndex]});
      m_First[uiSourceIndex] = m_Variants.GetCount() - 1;
      return uiIndex;
    }
  };

  void FinishTopology(const WorkPart& work, plWaterNetworkPart& out_part)
  {
    out_part.m_TopologyVertices.SetCount(work.m_Vertices.GetCount());
    for (plUInt32 i = 0; i < work.m_Vertices.GetCount(); ++i)
      out_part.m_TopologyVertices[i] = work.m_Vertices[i].m_Vertex;
    out_part.m_TopologyIndices = work.m_Indices;
    out_part.m_uiFoldedTriangles = work.m_uiFoldedTriangles;
  }
} // namespace

void plWaterNetworkMesher::Build(plArrayPtr<const plWaterNetworkBody> bodies, plArrayPtr<const plWaterNetworkJoin> joins, plUInt32 uiLod, plWaterNetworkMesh& out_mesh)
{
  out_mesh.m_Parts.Clear();
  out_mesh.m_Parts.SetCount(bodies.GetCount());
  out_mesh.m_Joins.Clear();
  out_mesh.m_Joins.SetCount(joins.GetCount());

  uiLod = plMath::Min<plUInt32>(uiLod, 6);
  const float fLodScale = static_cast<float>(1u << uiLod);

  plDynamicArray<RiverWork> rivers;
  rivers.SetCount(bodies.GetCount());
  plDynamicArray<LakeWork> lakes;
  lakes.SetCount(bodies.GetCount());

  for (plUInt32 uiBody = 0; uiBody < bodies.GetCount(); ++uiBody)
  {
    if (bodies[uiBody].m_Type == plWaterBodyType::River)
      BuildRiverRows(bodies[uiBody], fLodScale, uiLod, rivers[uiBody]);
    else if (bodies[uiBody].m_Type == plWaterBodyType::Lake)
      lakes[uiBody].m_bValid = PrepareLakeOutline(bodies[uiBody], lakes[uiBody]);
  }

  // Which join, if any, sits at each end of each river.
  plDynamicArray<plUInt32> riverEndJoin;
  riverEndJoin.SetCount(bodies.GetCount() * 2);
  for (plUInt32& uiJoin : riverEndJoin)
    uiJoin = plInvalidIndex;

  //////////////////////////////////////////////////////////////////////////
  // Mouths: where each joined river end lies on its lake's shore.

  for (plUInt32 uiJoin = 0; uiJoin < joins.GetCount(); ++uiJoin)
  {
    const plWaterNetworkJoin& join = joins[uiJoin];
    plWaterNetworkJoinResult& result = out_mesh.m_Joins[uiJoin];

    if (join.m_uiRiver >= bodies.GetCount() || bodies[join.m_uiRiver].m_Type != plWaterBodyType::River || !rivers[join.m_uiRiver].m_bValid)
    {
      result.m_sProblem = "The river of the join has no mesh.";
      continue;
    }
    const RiverWork& river = rivers[join.m_uiRiver];
    const plUInt32 uiEnd = join.m_bRiverEnd ? 1 : 0;
    if (riverEndJoin[join.m_uiRiver * 2 + uiEnd] != plInvalidIndex)
    {
      result.m_sProblem = "This end of the river already has a join.";
      continue;
    }

    const plVec3 vEnd = river.m_vEndCenter[uiEnd];
    result.m_vInward = river.m_vOutward[uiEnd];

    if (join.m_uiLake == plInvalidIndex)
    {
      if (plMath::Abs(vEnd.z - join.m_fOceanHeight) > join.m_fHeightTolerance)
      {
        result.m_sProblem = "The river end is not at the height of the ocean.";
        continue;
      }
      const plVec3 vLeft(river.m_vLeft[uiEnd].x, river.m_vLeft[uiEnd].y, 0.0f);
      result.m_vMouthLeft = vEnd + vLeft * river.m_fLeftWidth[uiEnd];
      result.m_vMouthRight = vEnd - vLeft * river.m_fRightWidth[uiEnd];
      result.m_bValid = true;
      riverEndJoin[join.m_uiRiver * 2 + uiEnd] = uiJoin;
      continue;
    }

    if (join.m_uiLake >= bodies.GetCount() || bodies[join.m_uiLake].m_Type != plWaterBodyType::Lake || !lakes[join.m_uiLake].m_bValid)
    {
      result.m_sProblem = "The lake of the join has no outline that can be meshed.";
      continue;
    }
    LakeWork& lake = lakes[join.m_uiLake];
    if (plMath::Abs(vEnd.z - lake.m_fHeight) > join.m_fHeightTolerance)
    {
      result.m_sProblem = "The river end is not at the height of the lake.";
      continue;
    }

    float fArc = 0.0f;
    float fDistance = 0.0f;
    plVec2 vShoreDirection;
    lake.Project(vEnd.GetAsVec2(), fArc, fDistance, vShoreDirection);
    if (fDistance > join.m_fSnapRadius)
    {
      result.m_sProblem = "The river end is farther from the lake's shore than the snap radius.";
      continue;
    }

    const float fLeftWidth = river.m_fLeftWidth[uiEnd];
    const float fRightWidth = river.m_fRightWidth[uiEnd];
    if (fLeftWidth + fRightWidth > lake.m_fPerimeter * 0.45f)
    {
      result.m_sProblem = "The river is too wide for the lake's shore.";
      continue;
    }

    LakeMouth mouth;
    mouth.m_uiJoin = uiJoin;
    // Walking the shore counter-clockwise, the lake is on the left. The river's left bank is ahead on that walk when
    // its left direction points the way the shore runs.
    mouth.m_bLeftIsAtEnd = river.m_vLeft[uiEnd].Dot(vShoreDirection) > 0.0f;
    mouth.m_fArcStart = fArc - (mouth.m_bLeftIsAtEnd ? fRightWidth : fLeftWidth);
    mouth.m_fArcEnd = fArc + (mouth.m_bLeftIsAtEnd ? fLeftWidth : fRightWidth);

    // Two mouths may not share shore.
    bool bOverlaps = false;
    for (const LakeMouth& other : lake.m_Mouths)
    {
      const float fGap = 0.05f;
      for (float fShift : {-lake.m_fPerimeter, 0.0f, lake.m_fPerimeter})
      {
        if (mouth.m_fArcStart + fShift < other.m_fArcEnd + fGap && mouth.m_fArcEnd + fShift > other.m_fArcStart - fGap)
          bOverlaps = true;
      }
    }
    if (bOverlaps)
    {
      result.m_sProblem = "The river's mouth overlaps the mouth of another river on the same shore.";
      continue;
    }

    // The direction into the lake: across the shore, to the left of the counter-clockwise walk.
    mouth.m_vInward = plVec2(-vShoreDirection.y, vShoreDirection.x);
    {
      const plVec2 vForward = uiEnd == 1 ? river.m_vOutward[uiEnd] : -river.m_vOutward[uiEnd];
      const plVec2 vFlow = vForward * river.m_fFlowScale[uiEnd];
      mouth.m_vFlow = plVec3(vFlow.x, vFlow.y, 0.0f);
      mouth.m_fRibbonAlong = uiEnd == 1 ? river.m_fLength : 0.0f;
      mouth.m_vRibbonOrigin = vEnd.GetAsVec2();
      mouth.m_vRibbonForward = vForward;
      mouth.m_vRibbonLeft = river.m_vLeft[uiEnd];
    }
    lake.m_Mouths.PushBack(mouth);
    result.m_vInward = mouth.m_vInward;
    result.m_bValid = true;
    riverEndJoin[join.m_uiRiver * 2 + uiEnd] = uiJoin;
  }

  //////////////////////////////////////////////////////////////////////////
  // Lakes: outline with the bank points of every mouth, triangulation, and the vertices on each mouth line.

  for (plUInt32 uiBody = 0; uiBody < bodies.GetCount(); ++uiBody)
  {
    if (bodies[uiBody].m_Type != plWaterBodyType::Lake || !lakes[uiBody].m_bValid)
      continue;
    LakeWork& lake = lakes[uiBody];
    const plWaterNetworkBody& body = bodies[uiBody];

    // Arcs are measured on the outline before any bank point is added, and adding a point does not move the others.
    struct BankPoint
    {
      PL_DECLARE_POD_TYPE();

      float m_fArc;
      plUInt32 m_uiMouth;
      bool m_bEnd;
    };
    plHybridArray<BankPoint, 8> bankPoints;
    for (plUInt32 uiMouth = 0; uiMouth < lake.m_Mouths.GetCount(); ++uiMouth)
    {
      bankPoints.PushBack({lake.m_Mouths[uiMouth].m_fArcStart, uiMouth, false});
      bankPoints.PushBack({lake.m_Mouths[uiMouth].m_fArcEnd, uiMouth, true});
    }
    for (const BankPoint& bank : bankPoints)
    {
      bool bInserted = false;
      lake.InsertPointAtArc(bank.m_fArc, bInserted);
    }
    // Now that the outline is final, find the bank points again by position.
    for (LakeMouth& mouth : lake.m_Mouths)
    {
      bool bInserted = false;
      mouth.m_uiStartVertex = lake.InsertPointAtArc(mouth.m_fArcStart, bInserted);
      mouth.m_uiEndVertex = lake.InsertPointAtArc(mouth.m_fArcEnd, bInserted);
    }

    lake.m_OpenEdge.SetCount(lake.m_Outline.GetCount());
    for (bool& bOpen : lake.m_OpenEdge)
      bOpen = false;
    for (const LakeMouth& mouth : lake.m_Mouths)
    {
      for (plUInt32 i = mouth.m_uiStartVertex; i != mouth.m_uiEndVertex; i = (i + 1) % lake.m_Outline.GetCount())
        lake.m_OpenEdge[i] = true;
    }

    plWaterFiniteMeshSettings settings = body.m_Settings;
    settings.m_fTargetEdgeLength = plMath::Max(settings.m_fTargetEdgeLength, 0.1f) * fLodScale;
    plDynamicArray<plWaterGeometryVertex> vertices;
    plDynamicArray<plUInt32> indices;
    if (plWaterGeometry::TriangulateLakePolygon(lake.m_Outline, vertices, indices, 0.001f, settings).Failed())
    {
      lake.m_bValid = false;
      for (const LakeMouth& mouth : lake.m_Mouths)
      {
        out_mesh.m_Joins[mouth.m_uiJoin].m_bValid = false;
        out_mesh.m_Joins[mouth.m_uiJoin].m_sProblem = "The lake's outline could not be triangulated.";
        const plWaterNetworkJoin& join = joins[mouth.m_uiJoin];
        riverEndJoin[join.m_uiRiver * 2 + (join.m_bRiverEnd ? 1 : 0)] = plInvalidIndex;
      }
      continue;
    }

    lake.m_Part.m_Vertices.SetCount(vertices.GetCount());
    const float fShoreFade = plMath::Max(body.m_Settings.m_fShoreWaveFadeDistance, 0.01f);
    for (plUInt32 i = 0; i < vertices.GetCount(); ++i)
    {
      WorkVertex& work = lake.m_Part.m_Vertices[i];
      work.m_Vertex = vertices[i];
      work.m_Vertex.m_fDepth = plMath::Max(body.m_fDefaultDepth, 0.01f);
      // Waves are pinned along the shore, and left free along a mouth line: there is water on both sides of it.
      const float fDistance = lake.DistanceToClosedShore(vertices[i].m_vPosition.GetAsVec2());
      work.m_Vertex.m_fWaveAttenuation = plMath::SmoothStep(fDistance, 0.0f, fShoreFade);
    }
    lake.m_Part.m_Indices = indices;

    // The vertices the triangulation left on each mouth line, in order from the river's left bank to its right.
    for (LakeMouth& mouth : lake.m_Mouths)
    {
      struct OnLine
      {
        PL_DECLARE_POD_TYPE();

        float m_fArc;
        plUInt32 m_uiVertex;
      };
      plHybridArray<OnLine, 32> onLine;
      const float fStartArc = lake.m_ArcAt[mouth.m_uiStartVertex];
      for (plUInt32 uiVertex = 0; uiVertex < vertices.GetCount(); ++uiVertex)
      {
        const plVec2 p = vertices[uiVertex].m_vPosition.GetAsVec2();
        for (plUInt32 i = mouth.m_uiStartVertex; i != mouth.m_uiEndVertex; i = (i + 1) % lake.m_Outline.GetCount())
        {
          const plVec2 a = lake.m_Outline[i].GetAsVec2();
          const plVec2 b = lake.m_Outline[(i + 1) % lake.m_Outline.GetCount()].GetAsVec2();
          if (DistanceToSegment(a, b, p) <= 0.001f)
          {
            float fArc = lake.m_ArcAt[i] + (p - a).GetLength() - fStartArc;
            if (fArc < -0.0005f)
              fArc += lake.m_fPerimeter;
            onLine.PushBack({fArc, uiVertex});
            break;
          }
        }
      }
      onLine.Sort([](const OnLine& lhs, const OnLine& rhs) { return lhs.m_fArc < rhs.m_fArc; });
      for (const OnLine& entry : onLine)
        mouth.m_Chain.PushBack(entry.m_uiVertex);
      if (mouth.m_bLeftIsAtEnd)
      {
        for (plUInt32 i = 0; i < mouth.m_Chain.GetCount() / 2; ++i)
          plMath::Swap(mouth.m_Chain[i], mouth.m_Chain[mouth.m_Chain.GetCount() - 1 - i]);
      }

      plWaterNetworkJoinResult& result = out_mesh.m_Joins[mouth.m_uiJoin];
      if (mouth.m_Chain.GetCount() < 2)
      {
        result.m_bValid = false;
        result.m_sProblem = "The lake has no vertices on the mouth line.";
        const plWaterNetworkJoin& join = joins[mouth.m_uiJoin];
        riverEndJoin[join.m_uiRiver * 2 + (join.m_bRiverEnd ? 1 : 0)] = plInvalidIndex;
        continue;
      }
      result.m_vMouthLeft = lake.m_Part.m_Vertices[mouth.m_Chain[0]].m_Vertex.m_vPosition;
      result.m_vMouthRight = lake.m_Part.m_Vertices[mouth.m_Chain.PeekBack()].m_Vertex.m_vPosition;
    }
  }

  //////////////////////////////////////////////////////////////////////////
  // Rivers: strips, the last strip onto each mouth line, and aprons into the ocean.

  // Per river end, the lake's mouth line as vertices of the river's own part.
  for (plUInt32 uiBody = 0; uiBody < bodies.GetCount(); ++uiBody)
  {
    if (bodies[uiBody].m_Type != plWaterBodyType::River || !rivers[uiBody].m_bValid)
      continue;
    RiverWork& river = rivers[uiBody];
    const plWaterNetworkBody& body = bodies[uiBody];
    WorkPart& part = river.m_Part;
    const plUInt32 uiColumns = river.m_uiColumns;

    plHybridArray<plUInt32, 32> chain[2];
    for (plUInt32 uiEnd = 0; uiEnd < 2; ++uiEnd)
    {
      const plUInt32 uiJoin = riverEndJoin[uiBody * 2 + uiEnd];
      if (uiJoin == plInvalidIndex || joins[uiJoin].m_uiLake == plInvalidIndex)
        continue;

      LakeWork& lake = lakes[joins[uiJoin].m_uiLake];
      LakeMouth* pMouth = nullptr;
      for (LakeMouth& mouth : lake.m_Mouths)
      {
        if (mouth.m_uiJoin == uiJoin)
          pMouth = &mouth;
      }
      if (pMouth == nullptr || pMouth->m_Chain.GetCount() < 2)
        continue;

      // Rows that lie in the lake, or closer to the mouth line than half a step, make way for it.
      plVec2 vChainMiddle = plVec2::MakeZero();
      for (plUInt32 uiVertex : pMouth->m_Chain)
        vChainMiddle += lake.m_Part.m_Vertices[uiVertex].m_Vertex.m_vPosition.GetAsVec2();
      vChainMiddle /= static_cast<float>(pMouth->m_Chain.GetCount());
      auto rowHasToGo = [&](plUInt32 uiRow)
      {
        const plVec2 vCenter = river.RowCenter(uiRow).GetAsVec2();
        return (vCenter - vChainMiddle).GetLength() < river.m_fStep * 0.6f || plWaterGeometry::IsPointInPolygonXY(lake.m_Outline, vCenter, 0.001f);
      };
      if (uiEnd == 1)
      {
        // The row at the end point always goes: the mouth line takes its place.
        if (river.m_uiLastRow > 0)
          --river.m_uiLastRow;
        while (river.m_uiLastRow > river.m_uiFirstRow + 1 && rowHasToGo(river.m_uiLastRow))
          --river.m_uiLastRow;
      }
      else
      {
        if (river.m_uiFirstRow + 1 < river.m_uiRows)
          ++river.m_uiFirstRow;
        while (river.m_uiFirstRow + 1 < river.m_uiLastRow && rowHasToGo(river.m_uiFirstRow))
          ++river.m_uiFirstRow;
      }

      for (plUInt32 uiVertex : pMouth->m_Chain)
      {
        // The same point as in the lake's part: position and wave attenuation are taken from there, and the flow is
        // the mouth's, so both parts move it the same way.
        WorkVertex work = lake.m_Part.m_Vertices[uiVertex];
        work.m_Vertex.m_vFlow = pMouth->m_vFlow;
        work.m_Vertex.m_fDepth = river.m_fDepth[uiEnd];
        work.m_fFromStart = uiEnd == 0 ? 0.0f : river.m_fLength;
        work.m_fFromEnd = uiEnd == 1 ? 0.0f : river.m_fLength;
        work.m_Vertex.m_vTexCoord = pMouth->RibbonCoordinates(work.m_Vertex.m_vPosition);
        chain[uiEnd].PushBack(part.m_Vertices.GetCount());
        pMouth->m_RiverChain.PushBack(part.m_Vertices.GetCount());
        part.m_Vertices.PushBack(work);
      }
    }

    // A river of two or three rows with a mouth at each end has no row left to itself: both mouths then fan out
    // from the one row in the middle.
    if (river.m_uiFirstRow > river.m_uiLastRow)
      river.m_uiFirstRow = river.m_uiLastRow = (river.m_uiFirstRow + river.m_uiLastRow) / 2;

    for (plUInt32 uiRow = river.m_uiFirstRow; uiRow < river.m_uiLastRow; ++uiRow)
    {
      for (plUInt32 uiColumn = 0; uiColumn + 1 < uiColumns; ++uiColumn)
      {
        const plUInt32 i0 = river.Index(uiRow, uiColumn);
        const plUInt32 i1 = i0 + 1;
        const plUInt32 i2 = i0 + uiColumns;
        const plUInt32 i3 = i2 + 1;
        part.AddTriangle(i0, i1, i2, true);
        part.AddTriangle(i1, i3, i2, true);
      }
    }

    plHybridArray<plUInt32, 64> row;
    auto collectRow = [&](plUInt32 uiRow)
    {
      row.Clear();
      for (plUInt32 uiColumn = 0; uiColumn < uiColumns; ++uiColumn)
        row.PushBack(river.Index(uiRow, uiColumn));
    };
    if (!chain[1].IsEmpty())
    {
      collectRow(river.m_uiLastRow);
      part.Zip(row, chain[1]);
    }
    if (!chain[0].IsEmpty())
    {
      collectRow(river.m_uiFirstRow);
      part.Zip(chain[0], row);
    }

    // Aprons: the river's mesh carried on into the ocean.
    for (plUInt32 uiEnd = 0; uiEnd < 2; ++uiEnd)
    {
      const plUInt32 uiJoin = riverEndJoin[uiBody * 2 + uiEnd];
      if (uiJoin == plInvalidIndex || joins[uiJoin].m_uiLake != plInvalidIndex)
        continue;
      const plWaterNetworkJoin& join = joins[uiJoin];
      plWaterNetworkJoinResult& result = out_mesh.m_Joins[uiJoin];

      const float fLength = plMath::Max(join.m_fMixLengthReceiver, river.m_fStep * 3.0f);
      const plUInt32 uiApronRows = plMath::Clamp<plUInt32>(static_cast<plUInt32>(plMath::Ceil(fLength / plMath::Max(river.m_fStep, 0.05f))), 3, 96);
      const plVec2 vOutward = river.m_vOutward[uiEnd];
      const plVec2 vLeft = river.m_vLeft[uiEnd];
      const plUInt32 uiEndRow = uiEnd == 1 ? river.m_uiLastRow : river.m_uiFirstRow;
      const float fLeftWidth = river.m_fLeftWidth[uiEnd];
      const float fRightWidth = river.m_fRightWidth[uiEnd];
      // The fan opens to twice the river's width, and by at least a metre a side.
      const float fFlare = plMath::Max((fLeftWidth + fRightWidth) * 0.5f, 1.0f);
      const float fMouthWeight = plMath::Clamp(join.m_fMouthWeight, 0.05f, 0.95f);
      const float fPureBand = 0.25f; // The last quarter of the apron's length is pure ocean,
      const float fPureSide = 0.3f;  // and so is the outer 30% of each side.
      const float fEndHeight = river.m_vEndCenter[uiEnd].z;
      const plVec2 vFlow = (uiEnd == 1 ? vOutward : -vOutward) * river.m_fFlowScale[uiEnd];
      // The fan is fully open where the ocean's cutout ends, and straight from there. The cutout widens by the same
      // curve over the same stretch, so it stays inside the fan all the way.
      const float fSpreadEnd = 1.0f - fPureBand * 0.5f;

      plUInt32 uiPreviousRowStart = plInvalidIndex;
      for (plUInt32 uiApronRow = 1; uiApronRow <= uiApronRows; ++uiApronRow)
      {
        const float u = static_cast<float>(uiApronRow) / static_cast<float>(uiApronRows);
        const float fSpread = plMath::SmoothStep(u, 0.0f, fSpreadEnd);
        const float fRowLeft = fLeftWidth + fFlare * fSpread;
        const float fRowRight = fRightWidth + fFlare * fSpread;
        const plVec2 vCenter = river.m_vEndCenter[uiEnd].GetAsVec2() + vOutward * (fLength * u);
        const float fHeight = plMath::Lerp(fEndHeight, join.m_fOceanHeight, plMath::SmoothStep(u, 0.0f, 0.3f));
        const float fAlong = plMath::Lerp(fMouthWeight, 1.0f, plMath::SmoothStep(u, 0.0f, 1.0f - fPureBand));

        const plUInt32 uiRowStart = part.m_Vertices.GetCount();
        for (plUInt32 uiColumn = 0; uiColumn < uiColumns; ++uiColumn)
        {
          const float t = static_cast<float>(uiColumn) / static_cast<float>(uiColumns - 1);
          const float fSide = plMath::Abs(t * 2.0f - 1.0f); // 0 in the middle, 1 on either edge
          const float fAcross = plMath::SmoothStep(fSide, 1.0f - fPureSide, 1.0f - fPureSide * 0.5f) * plMath::SmoothStep(u, 0.0f, 0.2f);
          const plVec2 vPosition = vCenter + vLeft * plMath::Lerp(fRowLeft, -fRowRight, t);

          WorkVertex work;
          work.m_Vertex.m_vPosition = plVec3(vPosition.x, vPosition.y, fHeight);
          work.m_Vertex.m_vFlow = plVec3(vFlow.x, vFlow.y, 0.0f);
          work.m_Vertex.m_fDepth = river.m_fDepth[uiEnd];
          // The apron carries the river's ribbon coordinates out to sea. Left stays left: at a river's start the fan
          // runs against the flow, and "along" counts down.
          work.m_Vertex.m_vTexCoord = plVec2(uiEnd == 1 ? river.m_fLength + fLength * u : -fLength * u, plMath::Lerp(fRowLeft, -fRowRight, t));
          work.m_Vertex.m_fSignedShoreDistance = plMath::Lerp(fRowLeft, -fRowRight, t);
          // The banks end where the river ends. Past them the water is open and its waves are free.
          const float fRiverAttenuation = part.m_Vertices[river.Index(uiEndRow, uiColumn)].m_Vertex.m_fWaveAttenuation;
          work.m_Vertex.m_fWaveAttenuation = plMath::Lerp(fRiverAttenuation, 1.0f, plMath::SmoothStep(u, 0.0f, 0.25f));
          work.m_fFromStart = uiEnd == 0 ? -fLength * u : river.m_fLength + fLength * u;
          work.m_fFromEnd = uiEnd == 1 ? -fLength * u : river.m_fLength + fLength * u;
          work.m_fApronWeight = plMath::Max(fAlong, fAcross);
          part.m_Vertices.PushBack(work);
        }

        for (plUInt32 uiColumn = 0; uiColumn + 1 < uiColumns; ++uiColumn)
        {
          // Towards the sea from the river's end, columns run left to right as on the river; at the start they are mirrored.
          const plUInt32 a0 = uiPreviousRowStart == plInvalidIndex ? river.Index(uiEndRow, uiColumn) : uiPreviousRowStart + uiColumn;
          const plUInt32 a1 = a0 + 1;
          const plUInt32 b0 = uiRowStart + uiColumn;
          const plUInt32 b1 = b0 + 1;
          part.AddTriangle(a0, a1, b0, false);
          part.AddTriangle(a1, b1, b0, false);
        }
        uiPreviousRowStart = uiRowStart;
      }

      // What the ocean may skip: the apron's inside, from a little way up the river to where the apron turns pure ocean.
      const float fUpstream = plMath::Min((fLeftWidth + fRightWidth), river.m_fLength * 0.25f);
      const float fCutEnd = fLength * fSpreadEnd;
      const plVec2 vMiddleOffset = vLeft * ((fLeftWidth - fRightWidth) * 0.5f);
      const plVec2 vCutStart = river.m_vEndCenter[uiEnd].GetAsVec2() + vMiddleOffset - vOutward * fUpstream;
      const plVec2 vCutEnd = river.m_vEndCenter[uiEnd].GetAsVec2() + vMiddleOffset + vOutward * fCutEnd;
      const float fInside = 1.0f - fPureSide * 0.25f;
      const float fHalfWidthAtRiver = (fLeftWidth + fRightWidth) * 0.5f;
      result.m_vCutoutCenter = (vCutStart + vCutEnd) * 0.5f;
      result.m_vCutoutDirection = vOutward;
      result.m_fCutoutHalfLength = (fUpstream + fCutEnd) * 0.5f;
      // Up the river the cutout is as wide as the river, a little less. From the river's end it widens as the fan
      // does, by the same share of both widths, so no ocean is left out where the fan does not draw.
      result.m_fCutoutStartHalfWidth = fHalfWidthAtRiver * fInside;
      result.m_fCutoutEndHalfWidth = (fHalfWidthAtRiver + fFlare) * fInside;
      result.m_fCutoutHoldFraction = fUpstream / plMath::Max(fUpstream + fCutEnd, 0.0001f);
    }

    PL_IGNORE_UNUSED(body);
  }

  //////////////////////////////////////////////////////////////////////////
  // Profile pairs, and the parts as they are drawn.

  for (plUInt32 uiBody = 0; uiBody < bodies.GetCount(); ++uiBody)
  {
    const plWaterNetworkBody& body = bodies[uiBody];
    plWaterNetworkPart& out_part = out_mesh.m_Parts[uiBody];
    PairEmitter emitter;
    emitter.m_pPart = &out_part;

    if (body.m_Type == plWaterBodyType::River && rivers[uiBody].m_bValid)
    {
      const RiverWork& river = rivers[uiBody];
      const WorkPart& work = river.m_Part;
      if (work.m_Indices.IsEmpty())
        continue;

      struct EndMix
      {
        bool m_bJoined = false;
        plUInt8 m_uiReceiver = 0;
        float m_fLength = 0.0f;
        float m_fMouthWeight = 0.5f;
        float m_fFrontBreakup = 0.5f;
      };
      EndMix ends[2];
      for (plUInt32 uiEnd = 0; uiEnd < 2; ++uiEnd)
      {
        const plUInt32 uiJoin = riverEndJoin[uiBody * 2 + uiEnd];
        if (uiJoin == plInvalidIndex)
          continue;
        const plWaterNetworkJoin& join = joins[uiJoin];
        ends[uiEnd].m_bJoined = true;
        ends[uiEnd].m_uiReceiver = join.m_uiLake != plInvalidIndex ? bodies[join.m_uiLake].m_uiProfileIndex : join.m_uiOceanProfileIndex;
        // Two mouths on one short river share it: neither mix may pass the middle.
        ends[uiEnd].m_fLength = plMath::Clamp(join.m_fMixLengthRiver, 0.01f, plMath::Max(river.m_fLength * 0.45f, 0.01f));
        ends[uiEnd].m_fMouthWeight = plMath::Clamp(join.m_fMouthWeight, 0.05f, 0.95f);
        ends[uiEnd].m_fFrontBreakup = join.m_fFrontBreakup;
      }

      auto weightAt = [&](const WorkVertex& vertex, plUInt32 uiEnd)
      {
        if (vertex.m_fApronWeight >= 0.0f)
          return vertex.m_fApronWeight;
        const float fDistance = uiEnd == 0 ? vertex.m_fFromStart : vertex.m_fFromEnd;
        return ends[uiEnd].m_fMouthWeight * (1.0f - plMath::SmoothStep(fDistance, 0.0f, ends[uiEnd].m_fLength));
      };

      for (plUInt32 i = 0; i + 2 < work.m_Indices.GetCount(); i += 3)
      {
        const plUInt32 corners[3] = {work.m_Indices[i], work.m_Indices[i + 1], work.m_Indices[i + 2]};
        float fFromStart = plMath::HighValue<float>();
        float fFromEnd = plMath::HighValue<float>();
        for (plUInt32 uiCorner : corners)
        {
          fFromStart = plMath::Min(fFromStart, work.m_Vertices[uiCorner].m_fFromStart);
          fFromEnd = plMath::Min(fFromEnd, work.m_Vertices[uiCorner].m_fFromEnd);
        }
        // Each triangle belongs to the mix of the end it is nearer to, if that end is joined and the mix reaches any
        // of its corners. A triangle on the mouth line always does, so the line holds the same weight in both parts.
        const plUInt32 uiEnd = fFromStart <= fFromEnd ? 0 : 1;
        const float fDistance = uiEnd == 0 ? fFromStart : fFromEnd;
        const bool bMixes = ends[uiEnd].m_bJoined && fDistance < ends[uiEnd].m_fLength;
        for (plUInt32 uiCorner : corners)
        {
          const WorkVertex& vertex = work.m_Vertices[uiCorner];
          if (bMixes)
            out_part.m_Indices.PushBack(emitter.Emit(vertex, uiCorner, body.m_uiProfileIndex, ends[uiEnd].m_uiReceiver, weightAt(vertex, uiEnd), ends[uiEnd].m_fFrontBreakup));
          else
            out_part.m_Indices.PushBack(emitter.Emit(vertex, uiCorner, body.m_uiProfileIndex, body.m_uiProfileIndex, 0.0f));
        }
      }
      FinishTopology(work, out_part);
      out_part.m_bValid = true;
    }
    else if (body.m_Type == plWaterBodyType::Lake && lakes[uiBody].m_bValid)
    {
      LakeWork& lake = lakes[uiBody];
      WorkPart& work = lake.m_Part;
      if (work.m_Indices.IsEmpty())
        continue;

      struct Plume
      {
        plUInt8 m_uiRiver = 0;
        float m_fLength = 1.0f;
        float m_fMouthWeight = 0.5f;
        float m_fFrontBreakup = 0.5f;
        plVec3 m_vFlow = plVec3::MakeZero();
        const LakeMouth* m_pMouth = nullptr;
      };
      plHybridArray<Plume, 4> plumes;
      for (const LakeMouth& mouth : lake.m_Mouths)
      {
        const plWaterNetworkJoinResult& result = out_mesh.m_Joins[mouth.m_uiJoin];
        if (!result.m_bValid)
          continue;
        const plWaterNetworkJoin& join = joins[mouth.m_uiJoin];
        Plume& plume = plumes.ExpandAndGetRef();
        plume.m_uiRiver = bodies[join.m_uiRiver].m_uiProfileIndex;
        plume.m_fLength = plMath::Max(join.m_fMixLengthReceiver, 0.01f);
        plume.m_fMouthWeight = plMath::Clamp(join.m_fMouthWeight, 0.05f, 0.95f);
        plume.m_fFrontBreakup = join.m_fFrontBreakup;
        plume.m_vFlow = mouth.m_vFlow;
        plume.m_pMouth = &mouth;
      }

      // The mouth line follows the shore, which may turn a corner within it.
      auto distanceToMouth = [&](const plVec2& vPosition, plUInt32 uiPlume)
      {
        const auto& chain = plumes[uiPlume].m_pMouth->m_Chain;
        float fBest = plMath::HighValue<float>();
        for (plUInt32 i = 0; i + 1 < chain.GetCount(); ++i)
        {
          fBest = plMath::Min(fBest, DistanceToSegment(work.m_Vertices[chain[i]].m_Vertex.m_vPosition.GetAsVec2(), work.m_Vertices[chain[i + 1]].m_Vertex.m_vPosition.GetAsVec2(), vPosition));
        }
        return fBest;
      };

      // How much of a river's water is at a point of the lake: all of it on the mouth line, none a mix length away,
      // and none where another mouth is nearer.
      auto plumeAt = [&](const plVec2& vPosition, plUInt32 uiPlume)
      {
        const float fDistance = distanceToMouth(vPosition, uiPlume);
        // On the line itself the answer is exactly one: the river's part holds the mouth weight there and so must this one.
        if (fDistance <= 0.0005f)
          return 1.0f;
        float fPlume = 1.0f - plMath::SmoothStep(fDistance, 0.0f, plumes[uiPlume].m_fLength);
        for (plUInt32 uiOther = 0; uiOther < plumes.GetCount() && fPlume > 0.0f; ++uiOther)
        {
          if (uiOther == uiPlume)
            continue;
          const float fOther = distanceToMouth(vPosition, uiOther);
          fPlume *= plMath::Saturate((fOther - fDistance) / (plumes[uiPlume].m_fLength * 0.5f));
        }
        return fPlume;
      };

      for (plUInt32 i = 0; i + 2 < work.m_Indices.GetCount(); i += 3)
      {
        const plUInt32 corners[3] = {work.m_Indices[i], work.m_Indices[i + 1], work.m_Indices[i + 2]};

        plUInt32 uiBestPlume = plInvalidIndex;
        float fBest = 0.0f;
        for (plUInt32 uiPlume = 0; uiPlume < plumes.GetCount(); ++uiPlume)
        {
          float fSum = 0.0f;
          for (plUInt32 uiCorner : corners)
            fSum += plumeAt(work.m_Vertices[uiCorner].m_Vertex.m_vPosition.GetAsVec2(), uiPlume);
          if (fSum > fBest)
          {
            fBest = fSum;
            uiBestPlume = uiPlume;
          }
        }

        for (plUInt32 uiCorner : corners)
        {
          const WorkVertex& vertex = work.m_Vertices[uiCorner];
          if (uiBestPlume == plInvalidIndex)
          {
            out_part.m_Indices.PushBack(emitter.Emit(vertex, uiCorner, body.m_uiProfileIndex, body.m_uiProfileIndex, 0.0f));
            continue;
          }

          // The pair is (river, lake), as on the river's side of the mouth: the weight is the lake's share, which is the
          // mouth weight on the mouth line and one where the plume ends.
          const Plume& plume = plumes[uiBestPlume];
          const float fPlume = plumeAt(vertex.m_Vertex.m_vPosition.GetAsVec2(), uiBestPlume);
          WorkVertex flowing = vertex;
          flowing.m_Vertex.m_vFlow = plume.m_vFlow;
          flowing.m_Vertex.m_vTexCoord = plume.m_pMouth->RibbonCoordinates(vertex.m_Vertex.m_vPosition);
          out_part.m_Indices.PushBack(emitter.Emit(flowing, uiCorner, plume.m_uiRiver, body.m_uiProfileIndex, 1.0f - (1.0f - plume.m_fMouthWeight) * fPlume, plume.m_fFrontBreakup));
        }
      }
      FinishTopology(work, out_part);
      out_part.m_bValid = true;
    }
  }

  //////////////////////////////////////////////////////////////////////////
  // The seam, measured on what was built: every vertex of a mouth line in the river's part against its twin in the lake's.

  for (plUInt32 uiBody = 0; uiBody < bodies.GetCount(); ++uiBody)
  {
    if (bodies[uiBody].m_Type != plWaterBodyType::Lake || !lakes[uiBody].m_bValid)
      continue;
    for (const LakeMouth& mouth : lakes[uiBody].m_Mouths)
    {
      plWaterNetworkJoinResult& result = out_mesh.m_Joins[mouth.m_uiJoin];
      if (!result.m_bValid)
        continue;
      const plWaterNetworkPart& riverPart = out_mesh.m_Parts[joins[mouth.m_uiJoin].m_uiRiver];
      const plWaterNetworkPart& lakePart = out_mesh.m_Parts[uiBody];
      if (!riverPart.m_bValid || !lakePart.m_bValid || mouth.m_RiverChain.GetCount() != mouth.m_Chain.GetCount() || mouth.m_Chain.GetCount() < 2)
      {
        result.m_bValid = false;
        result.m_sProblem = "The river's mesh and the lake's mesh do not share the mouth line.";
        continue;
      }

      float fWorst = 0.0f;
      for (plUInt32 i = 0; i < mouth.m_Chain.GetCount(); ++i)
      {
        const plWaterGeometryVertex& a = riverPart.m_TopologyVertices[mouth.m_RiverChain[i]];
        const plWaterGeometryVertex& b = lakePart.m_TopologyVertices[mouth.m_Chain[i]];
        fWorst = plMath::Max(fWorst, (a.m_vPosition - b.m_vPosition).GetLength());
        fWorst = plMath::Max(fWorst, plMath::Abs(a.m_fWaveAttenuation - b.m_fWaveAttenuation));
        // The river's waves are a function of the ribbon coordinates: the lake's copy of the point has to get the same
        // ones in the triangles that blend towards the river.
        fWorst = plMath::Max(fWorst, (a.m_vTexCoord - mouth.RibbonCoordinates(b.m_vPosition)).GetLength());
      }
      result.m_fMaximumSeamError = fWorst;
      result.m_uiSharedVertexCount = mouth.m_Chain.GetCount();
    }
  }
}

bool plWaterNetworkMesher::BuildVolume(plArrayPtr<const plWaterGeometryVertex> topologyVertices, plArrayPtr<const plUInt32> topologyIndices, float fDefaultDepth,
  plDynamicArray<plWaterGeometryVertex>& out_vertices, plDynamicArray<plUInt32>& out_indices)
{
  out_vertices.Clear();
  out_indices.Clear();
  if (topologyVertices.IsEmpty() || topologyIndices.GetCount() < 3 || topologyIndices.GetCount() % 3 != 0 || topologyVertices.GetCount() > s_uiMaximumPartVertices)
    return false;

  const plUInt32 uiCount = topologyVertices.GetCount();
  out_vertices.Reserve(uiCount * 2);
  out_vertices.PushBackRange(topologyVertices);
  for (const plWaterGeometryVertex& source : topologyVertices)
  {
    plWaterGeometryVertex bottom = source;
    bottom.m_vPosition.z -= source.m_fDepth > 0.0f ? source.m_fDepth : plMath::Max(fDefaultDepth, 0.01f);
    bottom.m_vFlow = plVec3::MakeZero();
    bottom.m_fWaveAttenuation = 0.0f;
    out_vertices.PushBack(bottom);
  }

  plHashTable<plUInt64, plUInt32> edgeUse;
  edgeUse.Reserve(topologyIndices.GetCount());
  auto edgeKey = [](plUInt32 a, plUInt32 b)
  {
    return (static_cast<plUInt64>(plMath::Min(a, b)) << 32) | plMath::Max(a, b);
  };

  out_indices.Reserve(topologyIndices.GetCount() * 2);
  for (plUInt32 i = 0; i + 2 < topologyIndices.GetCount(); i += 3)
  {
    const plUInt32 a = topologyIndices[i], b = topologyIndices[i + 1], c = topologyIndices[i + 2];
    if (a >= uiCount || b >= uiCount || c >= uiCount)
      return false;
    out_indices.PushBack(a);
    out_indices.PushBack(b);
    out_indices.PushBack(c);
    out_indices.PushBack(a + uiCount);
    out_indices.PushBack(c + uiCount);
    out_indices.PushBack(b + uiCount);
    for (const plUInt64 uiKey : {edgeKey(a, b), edgeKey(b, c), edgeKey(c, a)})
    {
      plUInt32 uiUse = 0;
      edgeUse.TryGetValue(uiKey, uiUse);
      edgeUse[uiKey] = uiUse + 1;
    }
  }

  // A wall under every edge that only one triangle uses: the outline of the surface, whatever its shape.
  for (plUInt32 i = 0; i + 2 < topologyIndices.GetCount(); i += 3)
  {
    const plUInt32 triangle[3] = {topologyIndices[i], topologyIndices[i + 1], topologyIndices[i + 2]};
    for (plUInt32 uiEdge = 0; uiEdge < 3; ++uiEdge)
    {
      const plUInt32 a = triangle[uiEdge];
      const plUInt32 b = triangle[(uiEdge + 1) % 3];
      plUInt32 uiUse = 0;
      if (!edgeUse.TryGetValue(edgeKey(a, b), uiUse) || uiUse != 1)
        continue;
      out_indices.PushBack(a);
      out_indices.PushBack(a + uiCount);
      out_indices.PushBack(b);
      out_indices.PushBack(b);
      out_indices.PushBack(a + uiCount);
      out_indices.PushBack(b + uiCount);
    }
  }
  return true;
}

plUInt32 plWaterNetworkMesher::CountSeamDefects(const plWaterNetworkMesh& mesh, float fTolerance)
{
  // Weld the whole network by position, then look at the edges that only one triangle uses. An edge with another
  // vertex of the network lying on it is where two parts meet without sharing their vertices (a T-junction).
  plDynamicArray<plVec3> points;
  plHashTable<plUInt64, plUInt32> pointLookup;
  const float fCell = plMath::Max(fTolerance, 0.00001f) * 2.0f;
  auto weld = [&](const plVec3& vPosition)
  {
    const plInt64 x = static_cast<plInt64>(plMath::Round(vPosition.x / fCell));
    const plInt64 y = static_cast<plInt64>(plMath::Round(vPosition.y / fCell));
    const plUInt64 uiKey = (static_cast<plUInt64>(x & 0xFFFFFFFF) << 32) | static_cast<plUInt64>(y & 0xFFFFFFFF);
    plUInt32 uiIndex = 0;
    if (pointLookup.TryGetValue(uiKey, uiIndex))
      return uiIndex;
    uiIndex = points.GetCount();
    points.PushBack(vPosition);
    pointLookup.Insert(uiKey, uiIndex);
    return uiIndex;
  };

  plHashTable<plUInt64, plUInt32> edgeUse;
  plDynamicArray<plUInt32> triangles;
  for (const plWaterNetworkPart& part : mesh.m_Parts)
  {
    if (!part.m_bValid)
      continue;
    for (plUInt32 i = 0; i + 2 < part.m_TopologyIndices.GetCount(); i += 3)
    {
      const plUInt32 corners[3] = {
        weld(part.m_TopologyVertices[part.m_TopologyIndices[i]].m_vPosition),
        weld(part.m_TopologyVertices[part.m_TopologyIndices[i + 1]].m_vPosition),
        weld(part.m_TopologyVertices[part.m_TopologyIndices[i + 2]].m_vPosition)};
      if (corners[0] == corners[1] || corners[1] == corners[2] || corners[2] == corners[0])
        continue;
      for (plUInt32 uiEdge = 0; uiEdge < 3; ++uiEdge)
      {
        const plUInt32 a = corners[uiEdge];
        const plUInt32 b = corners[(uiEdge + 1) % 3];
        const plUInt64 uiKey = (static_cast<plUInt64>(plMath::Min(a, b)) << 32) | plMath::Max(a, b);
        plUInt32 uiUse = 0;
        edgeUse.TryGetValue(uiKey, uiUse);
        edgeUse[uiKey] = uiUse + 1;
      }
    }
  }

  // Bucket the points so that the test for "a vertex on this edge" only looks nearby.
  const float fBucket = 2.0f;
  plHashTable<plUInt64, plDynamicArray<plUInt32>> buckets;
  auto bucketKey = [&](plInt32 x, plInt32 y)
  {
    return (static_cast<plUInt64>(static_cast<plUInt32>(x)) << 32) | static_cast<plUInt32>(y);
  };
  for (plUInt32 i = 0; i < points.GetCount(); ++i)
  {
    const plInt32 x = static_cast<plInt32>(plMath::Floor(points[i].x / fBucket));
    const plInt32 y = static_cast<plInt32>(plMath::Floor(points[i].y / fBucket));
    buckets[bucketKey(x, y)].PushBack(i);
  }

  plUInt32 uiDefects = 0;
  for (auto it = edgeUse.GetIterator(); it.IsValid(); ++it)
  {
    if (it.Value() != 1)
      continue;
    const plUInt32 a = static_cast<plUInt32>(it.Key() >> 32);
    const plUInt32 b = static_cast<plUInt32>(it.Key());
    const plVec2 pa = points[a].GetAsVec2();
    const plVec2 pb = points[b].GetAsVec2();
    const plInt32 x0 = static_cast<plInt32>(plMath::Floor(plMath::Min(pa.x, pb.x) / fBucket));
    const plInt32 x1 = static_cast<plInt32>(plMath::Floor(plMath::Max(pa.x, pb.x) / fBucket));
    const plInt32 y0 = static_cast<plInt32>(plMath::Floor(plMath::Min(pa.y, pb.y) / fBucket));
    const plInt32 y1 = static_cast<plInt32>(plMath::Floor(plMath::Max(pa.y, pb.y) / fBucket));
    bool bDefect = false;
    for (plInt32 y = y0; y <= y1 && !bDefect; ++y)
    {
      for (plInt32 x = x0; x <= x1 && !bDefect; ++x)
      {
        const plDynamicArray<plUInt32>* pBucket = nullptr;
        if (!buckets.TryGetValue(bucketKey(x, y), pBucket))
          continue;
        for (plUInt32 uiPoint : *pBucket)
        {
          if (uiPoint == a || uiPoint == b)
            continue;
          const plVec2 p = points[uiPoint].GetAsVec2();
          const plVec2 ab = pb - pa;
          const float t = (p - pa).Dot(ab) / plMath::Max(ab.GetLengthSquared(), plMath::SmallEpsilon<float>());
          if (t <= 0.001f || t >= 0.999f)
            continue;
          if ((p - (pa + ab * t)).GetLength() <= fTolerance)
          {
            bDefect = true;
            break;
          }
        }
      }
    }
    if (bDefect)
      ++uiDefects;
  }
  return uiDefects;
}

PL_STATICLINK_FILE(WaterPlugin, WaterPlugin_Utils_Implementation_WaterNetworkMesher);
