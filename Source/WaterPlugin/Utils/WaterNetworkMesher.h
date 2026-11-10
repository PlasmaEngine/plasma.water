#pragma once

#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/Strings/String.h>
#include <GameEngine/Interfaces/WaterWorldModule.h>
#include <WaterPlugin/Utils/WaterGeometry.h>
#include <WaterPlugin/WaterPluginDLL.h>

/// \brief One river or lake of a network, as the mesher needs it.
struct PL_WATER_DLL plWaterNetworkBody
{
  plEnum<plWaterBodyType> m_Type;               ///< River or Lake.
  plUInt8 m_uiProfileIndex = 0;                 ///< The body's entry in the profile table.
  plDynamicArray<plWaterControlPoint> m_Points; ///< A river's centre line from start to end, or a lake's outline.
  plWaterFiniteMeshSettings m_Settings;         ///< At LOD 0. Coarser LODs double the edge length per level.
  float m_fDefaultDepth = 5.0f;
};

/// \brief Where an end of a river meets a lake or the ocean.
struct PL_WATER_DLL plWaterNetworkJoin
{
  plUInt32 m_uiRiver = plInvalidIndex; ///< Index into the bodies passed to Build.
  bool m_bRiverEnd = true;             ///< The end of the river (water flows out of it there), or its start.
  plUInt32 m_uiLake = plInvalidIndex;  ///< Index into the bodies passed to Build. plInvalidIndex: the river meets the ocean.

  plUInt8 m_uiOceanProfileIndex = 0; ///< The ocean's entry in the profile table. Only for a join with the ocean.
  float m_fOceanHeight = 0.0f;

  /// How far the two waters mix on either side of the mouth, in metres along the river and into the receiver.
  float m_fMixLengthRiver = 2.0f;
  float m_fMixLengthReceiver = 6.0f;
  /// Share of the receiver's profile on the mouth line itself. 0.5 is an even mix; a strong sea pushes it up.
  float m_fMouthWeight = 0.5f;
  /// How ragged the front between the two waters is drawn, 0 (a smooth ramp) to 1.
  float m_fFrontBreakup = 0.5f;

  float m_fSnapRadius = 5.0f;      ///< The river end has to be this close to the lake's shore.
  float m_fHeightTolerance = 0.25f; ///< And this close to the receiver's water level.
};

/// \brief What became of a join.
struct PL_WATER_DLL plWaterNetworkJoinResult
{
  bool m_bValid = false;
  plString m_sProblem; ///< Why the join was left out. The bodies are then meshed as if it was not there.

  /// The mouth line: the two bank points on the receiver's shore (or at the river's end for the ocean), the direction
  /// from the river into the receiver, and the half width.
  plVec3 m_vMouthLeft = plVec3::MakeZero();
  plVec3 m_vMouthRight = plVec3::MakeZero();
  plVec2 m_vInward = plVec2(1.0f, 0.0f);

  /// Vertices on the mouth line that the river's mesh and the receiver's mesh both hold, and the largest distance
  /// between a vertex of one and its twin in the other. Zero when they are the same points, which they are by construction.
  plUInt32 m_uiSharedVertexCount = 0;
  float m_fMaximumSeamError = 0.0f;

  /// Ocean joins only: the part of the apron inside which the ocean does not need to draw. A strip of the river's
  /// width from a little way up the river to its end, which then widens as the apron does, in the form the ocean
  /// shader tests (see plWaterRenderData::Cutout).
  plVec2 m_vCutoutCenter = plVec2::MakeZero();
  plVec2 m_vCutoutDirection = plVec2(1.0f, 0.0f);
  float m_fCutoutHalfLength = 0.0f;
  float m_fCutoutStartHalfWidth = 0.0f;
  float m_fCutoutEndHalfWidth = 0.0f;
  float m_fCutoutHoldFraction = 0.0f; ///< Share of the length, from the start, that keeps the start width.
};

/// \brief The mesh of one body within its network.
struct PL_WATER_DLL plWaterNetworkPart
{
  bool m_bValid = false;

  /// What is drawn. Vertices carry their profile pair, and a point where two pairs meet is held once per pair.
  plDynamicArray<plWaterGeometryVertex> m_Vertices;
  plDynamicArray<plUInt32> m_Indices;

  /// The same triangles with one vertex per point. For the underwater volume, for bounds and for tests.
  plDynamicArray<plWaterGeometryVertex> m_TopologyVertices;
  plDynamicArray<plUInt32> m_TopologyIndices;

  /// Triangles left out because the ribbon folded over itself there (a bend tighter than the river is wide).
  plUInt32 m_uiFoldedTriangles = 0;
};

struct PL_WATER_DLL plWaterNetworkMesh
{
  plDynamicArray<plWaterNetworkPart> m_Parts;       ///< One per body, in the order the bodies were given.
  plDynamicArray<plWaterNetworkJoinResult> m_Joins; ///< One per join, in the order the joins were given.
};

/// \brief Builds the meshes of rivers and lakes that are joined to one another as one conforming mesh.
///
/// Where a river meets a lake, the lake's shore keeps two vertices at the river's banks, and the river's last strip
/// ends on exactly the vertices the lake has between them. So the two meshes share every vertex on the mouth line at
/// every LOD, nothing overlaps, and no cutout is needed. The shore between the banks is open: waves are not pinned
/// there. Across the mouth, vertices carry a profile pair and a weight, so the river's look fades into the lake's over
/// the join's mix lengths, on the river's side and as a plume on the lake's side.
///
/// Where a river meets the ocean, its mesh continues as an apron: a fan that carries the river's look out to sea and
/// is pure ocean along its outer band. The ocean, which is drawn first, can skip what the apron's inside covers.
///
/// A join that cannot be made (the river end is too far from the shore, or too wide for it) is reported and left out.
/// A body whose own mesh cannot be built is reported as invalid; the others are still built.
class PL_WATER_DLL plWaterNetworkMesher
{
public:
  /// uiLod 0 is the full mesh. Each further level doubles the edge length and halves the river's columns.
  static void Build(plArrayPtr<const plWaterNetworkBody> bodies, plArrayPtr<const plWaterNetworkJoin> joins, plUInt32 uiLod, plWaterNetworkMesh& out_mesh);

  /// The closed volume under a surface: the surface, a copy lowered by each vertex's depth, and walls along every
  /// edge that only one triangle uses.
  static bool BuildVolume(plArrayPtr<const plWaterGeometryVertex> topologyVertices, plArrayPtr<const plUInt32> topologyIndices, float fDefaultDepth,
    plDynamicArray<plWaterGeometryVertex>& out_vertices, plDynamicArray<plUInt32>& out_indices);

  /// Counts the places where the parts of a network do not meet vertex to vertex: an outer edge of one part with a
  /// vertex of the network lying inside it. Zero for a conforming mesh.
  static plUInt32 CountSeamDefects(const plWaterNetworkMesh& mesh, float fTolerance = 0.0005f);
};
