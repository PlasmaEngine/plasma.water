#pragma once

#include <Foundation/Containers/Deque.h>
#include <Foundation/Containers/HybridArray.h>
#include <Foundation/Math/BoundingBox.h>
#include <Foundation/Math/Transform.h>
#include <Foundation/Threading/Mutex.h>
#include <GameEngine/Interfaces/WaterWorldModule.h>
#include <RendererCore/Meshes/MeshBufferResource.h>
#include <WaterPlugin/Resources/WaterProfileResource.h>
#include <WaterPlugin/Utils/WaterGeometry.h>
#include <WaterPlugin/WaterPluginDLL.h>

class plOceanComponent;
struct plWaterAttachmentAnchor;
struct plWaterGpuProfile;

struct PL_WATER_DLL plWaterFiniteMeshLod
{
  plMeshBufferResourceHandle m_hSurfaceMesh;
  plMeshBufferResourceHandle m_hVolumeMesh;
  plUInt32 m_uiSurfacePrimitiveCount = 0;
  plUInt32 m_uiVolumePrimitiveCount = 0;
  float m_fMaximumDistanceScale = 2.0f;
  plUInt8 m_uiLevel = 0; ///< 0 is the full mesh. Each level doubles the edge length.
};

/// \brief World-space authored data used to register or update a water body.
struct PL_WATER_DLL plWaterBodyDescriptor
{
  plEnum<plWaterBodyType> m_Type;
  plGameObjectHandle m_hOwnerObject;
  plWaterProfileResourceHandle m_hProfile;
  plUInt64 m_uiNetworkId = 0;
  plInt16 m_iPriority = 0;
  float m_fBaseHeight = 0.0f;
  float m_fDefaultWidth = 10.0f;
  float m_fDefaultDepth = 5.0f;
  float m_fFlowSpeed = 1.0f;
  float m_fOceanExtent = 10000.0f;
  float m_fOceanPatchSize = 64.0f;
  plUInt8 m_uiOceanRingCount = 6;
  plUInt16 m_uiPatchResolution = 64;
  bool m_bOverrideWaveAmplitude = false;
  float m_fWaveAmplitudeOverride = 1.0f;
  /// Deprecated compatibility field. Modern water profiles and rendering ignore material overrides.
  plString m_sSurfaceMaterialOverride;
  plDynamicArray<plWaterControlPoint> m_ControlPoints;
};

/// \brief Copyable, read-only runtime snapshot consumed by render/extraction integrations.
struct PL_WATER_DLL plWaterBodySnapshot
{
  plWaterBodyId m_BodyId;
  plWaterBodyDescriptor m_Descriptor;
  plWaterProfileResourceDescriptor m_Profile;
  plUInt32 m_uiProfileChangeCounter = 0;
  plBoundingBox m_WorldBounds = plBoundingBox::MakeInvalid();
  plDynamicArray<plWaterGeometryVertex> m_Vertices;
  plDynamicArray<plUInt32> m_Indices;
  plDynamicArray<plWaterGeometryVertex> m_VolumeVertices;
  plDynamicArray<plUInt32> m_VolumeIndices;
  plMeshBufferResourceHandle m_hSurfaceMesh;
  plMeshBufferResourceHandle m_hVolumeMesh;
  plHybridArray<plWaterFiniteMeshLod, 3> m_FiniteMeshLods;
  plUInt32 m_uiGeometryRevision = 0;
  plDynamicArray<plWaterOceanClipmapPatch> m_OceanPatches;
  bool m_bGeometryValid = false;
};

/// \brief The part of plWaterBodySnapshot that is drawn from.
struct PL_WATER_DLL plWaterBodyRenderInfo
{
  plWaterBodyId m_BodyId;
  plEnum<plWaterBodyType> m_Type;
  float m_fBaseHeight = 0.0f;
  float m_fOceanPatchSize = 64.0f;
  float m_fFlowSpeed = 1.0f;
  plWaterProfileResourceDescriptor m_Profile;
  plBoundingBox m_WorldBounds = plBoundingBox::MakeInvalid();
  plHybridArray<plWaterFiniteMeshLod, 3> m_FiniteMeshLods;
  plDynamicArray<plWaterOceanClipmapPatch> m_OceanPatches;
  plUInt32 m_uiGeometryRevision = 0;
  bool m_bGeometryValid = false;
};

/// \brief The ocean that a river's apron covers. Same form as plWaterRenderData::Cutout.
struct PL_WATER_DLL plWaterOceanCutout
{
  PL_DECLARE_POD_TYPE();

  plVec2 m_vCenter = plVec2::MakeZero();
  plVec2 m_vDirection = plVec2(1.0f, 0.0f);
  float m_fHalfLength = 0.0f;
  float m_fStartHalfWidth = 0.0f;
  float m_fEndHalfWidth = 0.0f;
  float m_fHoldFraction = 0.0f;
};

struct PL_WATER_DLL plWaterJunctionDescriptor
{
  plGameObjectHandle m_hOwnerObject;
  plWaterBodyId m_BodyA;
  plWaterBodyId m_BodyB;
  plVec3 m_vCenter = plVec3::MakeZero();
  plVec3 m_vBlendDirection = plVec3::MakeAxisX();
  plVec3 m_vAnchorA = plVec3::MakeZero();
  plVec3 m_vAnchorB = plVec3::MakeZero();
  plUInt8 m_uiAttachmentA = 0;
  plUInt8 m_uiAttachmentB = 0;
  float m_fBlendDistance = 5.0f;
  float m_fHeightTolerance = 0.25f;
  float m_fFrontBreakup = 0.5f; ///< How ragged the front between the two waters is drawn, 0..1.
};

/// \brief What became of a junction when its network was meshed. Read-only, for extraction, queries, tests and debug views.
struct PL_WATER_DLL plWaterJunctionRenderSnapshot
{
  plWaterJunctionDescriptor m_Descriptor;
  /// A junction that is meshed joins a river (primary) to a lake or an ocean (secondary). Lake to lake, lake to ocean
  /// and river to river junctions have no mesh; queries still blend across them, and m_bValid is false.
  plWaterBodyId m_PrimaryBody;
  plWaterBodyId m_SecondaryBody;
  plEnum<plWaterBodyType> m_SecondaryType = plWaterBodyType::Lake;
  bool m_bRiverEnd = true; ///< The river's end is joined (water flows into the receiver), or its start.
  bool m_bValid = false;
  plUInt32 m_uiRevision = 0;
  plString m_sProblem; ///< Why the junction was left out of the mesh, when it was.

  /// The mouth line: the two bank points on the receiver's shore (at the river's end for an ocean) and the direction
  /// from the river into the receiver.
  plVec3 m_vMouthLeft = plVec3::MakeZero();
  plVec3 m_vMouthRight = plVec3::MakeZero();
  plVec2 m_vInward = plVec2(1.0f, 0.0f);

  /// Lake joins: vertices on the mouth line that the river's mesh and the lake's mesh both hold, per LOD, and the
  /// largest difference between a vertex of one and its twin in the other over all LODs (position and wave
  /// attenuation). Zero when the meshes conform, which they do by construction.
  plUInt32 m_uiSharedVertexCount[3] = {0, 0, 0};
  float m_fMaximumSeamError = 0.0f;

  /// How far the two waters mix on either side of the mouth line, and the receiver's share on the line itself.
  float m_fMixLengthRiver = 0.0f;
  float m_fMixLengthReceiver = 0.0f;
  float m_fMouthWeight = 0.5f;

  /// Ocean joins: the inside of the river's apron, which the ocean does not need to draw. See plWaterRenderData::Cutout.
  plVec2 m_vCutoutCenter = plVec2::MakeZero();
  plVec2 m_vCutoutDirection = plVec2(1.0f, 0.0f);
  float m_fCutoutHalfLength = 0.0f;
  float m_fStartHalfWidth = 0.0f;
  float m_fEndHalfWidth = 0.0f;
  float m_fCutoutHoldFraction = 0.0f;

  /// The current that queries see in the receiver. The field axis points from the mouth line into the receiver;
  /// DirectionSign is +1 where the river flows in and -1 where the receiver drains into the river.
  plVec2 m_vFlowFieldOrigin = plVec2::MakeZero();
  plVec2 m_vFlowFieldDirection = plVec2(1.0f, 0.0f);
  float m_fFlowFieldLength = 0.0f;
  float m_fFlowFieldHalfWidth = 0.0f;
  float m_fFlowFieldSpeed = 0.0f;
  float m_fFlowFieldDirectionSign = 1.0f;
};

struct PL_WATER_DLL plWaterExclusionDescriptor
{
  plGameObjectHandle m_hOwnerObject;
  plWaterBodyId m_TargetBody;
  plTransform m_GlobalTransform = plTransform::MakeIdentity();
  plVec3 m_vExtents = plVec3(10.0f);
};

/// \brief Concrete deterministic CPU water registry and query implementation.
class PL_WATER_DLL plWaterWorldModule : public plWaterWorldModuleInterface
{
  PL_DECLARE_WORLD_MODULE();
  PL_ADD_DYNAMIC_REFLECTION(plWaterWorldModule, plWaterWorldModuleInterface);

public:
  plWaterWorldModule(plWorld* pWorld);
  ~plWaterWorldModule();

  virtual void Initialize() override;

  virtual bool TrySampleSurface(const plWaterSurfaceQuery& query, plWaterSurfaceResult& out_result) const override;
  virtual bool TrySampleSurfaceAtTime(const plWaterSurfaceQuery& query, plTime simulationTime, plWaterSurfaceResult& out_result) const override;
  virtual bool TrySampleSurfaceAtTick(const plWaterSurfaceQuery& query, plUInt64 uiSimulationTick, plWaterSurfaceResult& out_result) const override;
  virtual plUInt64 GetSimulationTick() const override;
  virtual plTime GetFixedSimulationStep() const override;
  virtual void AddInteraction(plWaterInteraction interaction) override;
  virtual plUInt64 CopyInteractionsSince(plUInt64 uiAfterSequence, plDynamicArray<plWaterInteraction>& out_interactions) const override;

  /// \brief Sets the water's clock (to zero unless told otherwise) and drops what interactions there are. Waves are a
  /// function of this clock, so a test that compares pictures calls it to make a scene look the same whatever ran
  /// before it, and a test of what water looks like after ten minutes sets it to ten minutes.
  void ResetSimulationClock(plTime startTime = plTime::MakeZero());

  /// \brief Debug-display only: the frame-data extractor of a primary (main/editor) view publishes the center
  /// of the currently valid 100 m interaction window here so Water.Debug.ShowInteractions can draw it.
  void SetDebugInteractionWindow(const plVec2& vCenter) const;

  plWaterBodyId RegisterBody(const plWaterBodyDescriptor& descriptor);
  bool UpdateBody(plWaterBodyId bodyId, const plWaterBodyDescriptor& descriptor);
  void UnregisterBody(plWaterBodyId& inout_bodyId);

  /// Compatibility hook for the existing plOceanComponent. See final handoff for exact lifecycle calls.
  plWaterBodyId RegisterLegacyOcean(plOceanComponent* pComponent);
  bool UpdateLegacyOcean(plWaterBodyId bodyId, const plOceanComponent* pComponent);

  plUInt32 RegisterJunction(const plWaterJunctionDescriptor& descriptor);
  bool UpdateJunction(plUInt32 uiIndex, const plWaterJunctionDescriptor& descriptor);
  void UnregisterJunction(plUInt32& inout_uiIndex);

  plUInt32 RegisterExclusion(const plWaterExclusionDescriptor& descriptor);
  bool UpdateExclusion(plUInt32 uiIndex, const plWaterExclusionDescriptor& descriptor);
  void UnregisterExclusion(plUInt32& inout_uiIndex);

  bool TryGetBodyId(plGameObjectHandle hObject, plWaterBodyId& out_bodyId) const;
  bool TryGetBodySnapshot(plWaterBodyId bodyId, plWaterBodySnapshot& out_snapshot) const;
  void CopyBodySnapshots(plDynamicArray<plWaterBodySnapshot>& out_snapshots) const;

  /// \brief Writes the profile table that the water shaders read: entry i is the profile of body i.
  ///
  /// Returns the number of entries written (one past the highest body index in use, at most uiMaxEntries). The
  /// spectrum slot of an entry is left at zero and its cascade count holds what the profile asks for; the surface
  /// pass replaces both with what its simulation gave the profile.
  plUInt32 FillGpuProfileTable(plArrayPtr<plWaterGpuProfile> out_entries) const;
  /// \brief Every junction, meshed or not. A junction with m_bValid false and a problem text was left out of the mesh.
  void CopyJunctionRenderSnapshots(plDynamicArray<plWaterJunctionRenderSnapshot>& out_snapshots) const;

  /// \brief What extraction needs of a body, without the copy of its vertices that a full snapshot holds.
  bool TryGetBodyRenderInfo(plWaterBodyId bodyId, plWaterBodyRenderInfo& out_info) const;

  /// \brief Ocean joins: the insides of the aprons of the rivers that reach this ocean, for the ocean to leave out.
  void CopyOceanCutouts(plWaterBodyId oceanId, plDynamicArray<plWaterOceanCutout>& out_cutouts) const;

private:
  struct BodySlot
  {
    plWaterBodySnapshot m_Data;
    plUInt32 m_uiGeneration = 1;
    bool m_bInUse = false;
  };

  struct JunctionSlot
  {
    plWaterJunctionDescriptor m_Data;
    plWaterJunctionRenderSnapshot m_RenderData;
    bool m_bInUse = false;
  };

  struct ExclusionSlot
  {
    plWaterExclusionDescriptor m_Data;
    bool m_bInUse = false;
  };

  void Update(const plWorldModule::UpdateContext& context);
  void PrepareBody(BodySlot& slot, const plWaterBodyDescriptor& descriptor);

  /// Rivers and lakes that junctions join are meshed together, as one network (plWaterNetworkMesher). These rebuild
  /// the networks that the given bodies are in: their meshes at every LOD, their volumes, and what became of each junction.
  ///
  /// A draft builds only the coarsest level, which takes a fraction of the time. A body that changes while it already
  /// has a mesh gets a draft at the next update, a junction that changes gets one at once, and both get the full
  /// meshes when they have been left alone for a moment (s_fDraftSettleSeconds). So dragging a spline point does not
  /// wait for the full meshes at every step.
  void RebuildNetworks(plArrayPtr<const plUInt32> seedBodies, bool bDraft = false);
  void RebuildNetworksAround(plUInt32 uiBody, bool bDraft = false);
  void RebuildNetwork(plArrayPtr<const plUInt32> networkBodies, plArrayPtr<const plUInt32> networkJunctions, bool bDraft);
  void RebuildDraftNow(plArrayPtr<const plUInt32> seedBodies);
  void CollectNetwork(plUInt32 uiSeedBody, plDynamicArray<plUInt32>& out_bodies, plDynamicArray<plUInt32>& out_junctions) const;
  void CollectJunctionNeighbours(plUInt32 uiBody, plDynamicArray<plUInt32>& out_bodies) const;
  /// True for a junction between a river and a lake or ocean, which is what the mesher joins.
  bool ResolveJoin(const JunctionSlot& junction, plUInt32& out_uiRiver, plUInt32& out_uiReceiver, bool& out_bRiverEnd) const;
  static void ResetJunctionRenderData(JunctionSlot& junction);
  bool IsBodyIdValid(plWaterBodyId bodyId) const;
  bool SampleBody(const BodySlot& slot, const plWaterSurfaceQuery& query, plUInt64 uiSimulationTick, plWaterSurfaceResult& out_result) const;
  bool IsExcluded(const BodySlot& slot, const plWaterSurfaceQuery& query) const;
  void ApplyJunctions(const plWaterSurfaceQuery& query, plUInt64 uiSimulationTick, plWaterSurfaceResult& inout_result) const;
  plUInt64 ResolveConnectedNetworkId(plWaterBodyId bodyId) const;
  static void EvaluateWaves(const plWaterProfileResourceDescriptor& profile, const plVec2& position, const plVec2& primaryDirection,
    float fAmplitudeScale, double fTime,
    float& out_fHeight, plVec3& out_normal, plVec3& out_velocity);
  void FlushPendingInteractions();
  void DrawDebug() const;

  mutable plMutex m_Mutex;
  mutable plVec2 m_vDebugInteractionWindow = plVec2::MakeZero();
  mutable bool m_bDebugInteractionWindowValid = false;
  plDeque<BodySlot> m_Bodies;
  plDeque<JunctionSlot> m_Junctions;
  plDeque<ExclusionSlot> m_Exclusions;
  plDynamicArray<plWaterInteraction> m_PendingInteractions;
  plDynamicArray<plWaterInteraction> m_Interactions;
  plUInt64 m_uiNextInteractionSequence = 1;
  plUInt64 m_uiSimulationTick = 0;
  double m_fSimulationAccumulator = 0.0;
  plTime m_LastWallClockTime;
  /// Bodies that changed since the last update and have not had their draft yet.
  plHybridArray<plUInt32, 8> m_BodiesAwaitingDraft;
  /// Bodies whose networks hold draft meshes, and when the last draft was built.
  plHybridArray<plUInt32, 8> m_BodiesAwaitingFullMeshes;
  plTime m_LastDraftTime;
  static constexpr double s_fFixedStepSeconds = 1.0 / 60.0;
  static constexpr double s_fDraftSettleSeconds = 0.35;
};
