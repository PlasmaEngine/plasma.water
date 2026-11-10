#pragma once

#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/Math/Vec2.h>
#include <Foundation/Math/Vec3.h>
#include <Foundation/Types/ArrayPtr.h>
#include <WaterPlugin/WaterPluginDLL.h>

struct PL_WATER_DLL plWaterControlPoint
{
  plVec3 m_vPosition = plVec3::MakeZero();
  float m_fLeftWidth = 5.0f;
  float m_fRightWidth = 5.0f;
  float m_fDepth = 5.0f;
  float m_fFlowScale = 1.0f;
};

struct PL_WATER_DLL plWaterGeometryVertex
{
  plVec3 m_vPosition = plVec3::MakeZero();
  plVec3 m_vNormal = plVec3::MakeAxisZ();
  plVec3 m_vFlow = plVec3::MakeZero();
  plVec2 m_vTexCoord = plVec2::MakeZero();
  /// Positive at the left bank and negative at the right bank for river ribbons.
  float m_fSignedShoreDistance = 0.0f;
  float m_fDepth = 0.0f;
  /// Scales displacement at this vertex. Zero pins authored shorelines and volume bottoms.
  float m_fWaveAttenuation = 1.0f;
  /// Weight of the second profile of the pair: zero for ordinary body meshes, 0..1 across a transition.
  float m_fProfileBlend = 0.0f;
  /// The pair of profiles this vertex blends, as indices into the profile table (the index of a body in
  /// plWaterWorldModule). 255 means the profile of the draw that renders the mesh. All vertices of one triangle
  /// hold the same pair.
  plUInt8 m_uiProfileA = 255;
  plUInt8 m_uiProfileB = 255;
  /// How ragged the front between the two profiles is drawn, 0..1. The junction's Front Breakup.
  float m_fFrontBreakup = 0.0f;
};

struct PL_WATER_DLL plWaterFiniteMeshSettings
{
  float m_fTargetEdgeLength = 1.0f;
  float m_fShoreWaveFadeDistance = 2.0f;
  float m_fRiverCurvatureRefinement = 1.0f;
  plUInt16 m_uiMaxRiverCrossSubdivisions = 32;
  plUInt8 m_uiMaxLakeRefinementIterations = 8;
};

struct PL_WATER_DLL plWaterRiverLocation
{
  plUInt32 m_uiSegment = plInvalidIndex;
  float m_fSegmentFraction = 0.0f;
  float m_fDistanceSquared = plMath::HighValue<float>();
  float m_fLeftWidth = 0.0f;
  float m_fRightWidth = 0.0f;
  float m_fDepth = 0.0f;
  float m_fFlowScale = 0.0f;
  float m_fSignedLateralDistance = 0.0f;
  plVec3 m_vCenter = plVec3::MakeZero();
  plVec3 m_vTangent = plVec3::MakeAxisX();
};

struct PL_WATER_DLL plWaterClipmapSeamFlags
{
  enum Enum : plUInt8
  {
    None = 0,
    West = PL_BIT(0),
    East = PL_BIT(1),
    South = PL_BIT(2),
    North = PL_BIT(3),
  };
};

/// \brief Renderer-neutral description of a camera-relative ocean clipmap patch.
struct PL_WATER_DLL plWaterOceanClipmapPatch
{
  plVec2 m_vCenterOffset = plVec2::MakeZero();
  float m_fPatchSize = 1.0f;
  plUInt16 m_uiResolution = 32;
  plUInt8 m_uiLod = 0;
  plUInt8 m_uiInnerSeamMask = 0;
  plUInt8 m_uiOuterSeamMask = 0;
};

/// \brief Pure helper algorithms shared by water runtime, renderer and focused unit tests.
class PL_WATER_DLL plWaterGeometry
{
public:
  /// Builds an adaptively sampled, shared-vertex river grid with pinned banks.
  static plResult BuildRiverRibbon(plArrayPtr<const plWaterControlPoint> points, plDynamicArray<plWaterGeometryVertex>& out_vertices,
    plDynamicArray<plUInt32>& out_indices, const plWaterFiniteMeshSettings& settings = plWaterFiniteMeshSettings());

  /// Triangulates a simple concave XY polygon, refines long edges and regularizes internal diagonals.
  static plResult TriangulateLake(plArrayPtr<const plVec3> boundary, plDynamicArray<plWaterGeometryVertex>& out_vertices,
    plDynamicArray<plUInt32>& out_indices, float fEpsilon = 0.001f,
    const plWaterFiniteMeshSettings& settings = plWaterFiniteMeshSettings());

  /// The triangulation step of TriangulateLake on its own: takes the outline as given, without removing collinear or
  /// duplicate points first. So a point placed on a straight shore (the end of a river mouth, for one) stays a vertex,
  /// and out_vertices[i] is boundary[i] for every point of the outline. The outline has to be a simple polygon.
  /// Edges longer than the target length are split at their midpoint, on the outline as well as inside.
  static plResult TriangulateLakePolygon(plArrayPtr<const plVec3> boundary, plDynamicArray<plWaterGeometryVertex>& out_vertices,
    plDynamicArray<plUInt32>& out_indices, float fEpsilon = 0.001f,
    const plWaterFiniteMeshSettings& settings = plWaterFiniteMeshSettings());

  /// Generates central patches and nested rings. Seam masks identify edges that meet another LOD.
  static void BuildOceanClipmap(plUInt8 uiRingCount, plUInt16 uiPatchResolution, float fBasePatchSize,
    plDynamicArray<plWaterOceanClipmapPatch>& out_patches);

  static bool IsPointInPolygonXY(plArrayPtr<const plVec3> polygon, const plVec2& point, float fEpsilon = 0.001f);
  static bool FindClosestPointOnRiver(plArrayPtr<const plWaterControlPoint> points, const plVec2& point, plWaterRiverLocation& out_location);
};
