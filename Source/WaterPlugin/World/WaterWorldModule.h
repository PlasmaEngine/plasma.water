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
};

struct PL_WATER_DLL plWaterJunctionRenderSnapshot
{
  plWaterJunctionDescriptor m_Descriptor;
  /// Exact referenced bodies used by the generated patch. Primary is the river; secondary is the receiving
  /// lake/ocean that owns extraction. Lake patches terminate on duplicated vertices from its shoreline mesh.
  plWaterBodyId m_PrimaryBody;
  plWaterBodyId m_SecondaryBody;
  plWaterProfileResourceDescriptor m_PrimaryProfile;
  plWaterProfileResourceDescriptor m_SecondaryProfile;
  plEnum<plWaterBodyType> m_SecondaryType = plWaterBodyType::Lake;
  plMeshBufferResourceHandle m_hSurfaceMesh;
  plMeshBufferResourceHandle m_hVolumeMesh;
  plVec2 m_vCutoutCenter = plVec2::MakeZero();
  plVec2 m_vCutoutDirection = plVec2(1.0f, 0.0f);
  float m_fCutoutHalfLength = 0.0f;
  float m_fStartHalfWidth = 0.0f;
  float m_fEndHalfWidth = 0.0f;
  /// Insets the river cutout at its welded edge so the patch retains a narrow, crack-free overlap with the river.
  float m_fCutoutMargin = 0.0f;
  /// Foam intensity where river outflow and incoming waves collide (0 when either side has no energy).
  float m_fMouthFoamStrength = 0.0f;
  /// Spectrum cascades of the secondary body, so the patch can reproduce its GPU displacement at the seam.
  plUInt32 m_uiSecondaryGpuCascadeCount = 0;
  plUInt32 m_uiSurfacePrimitiveCount = 0;
  plUInt32 m_uiVolumePrimitiveCount = 0;
  plUInt32 m_uiRevision = 0;
  bool m_bValid = false;
  /// Localized current carried into the receiving lake. The field axis always points from the shoreline into
  /// the lake; DirectionSign is +1 for river inflow and -1 for lake outflow through a river-start junction.
  /// Kept at the end so POD-only flow metadata does not disturb the legacy snapshot member layout.
  plVec2 m_vFlowFieldOrigin = plVec2::MakeZero();
  plVec2 m_vFlowFieldDirection = plVec2(1.0f, 0.0f);
  float m_fFlowFieldLength = 0.0f;
  float m_fFlowFieldHalfWidth = 0.0f;
  float m_fFlowFieldSpeed = 0.0f;
  float m_fFlowFieldDirectionSign = 1.0f;
  /// Validation/debug metadata for the exact lake shoreline zipper. Zero for ocean junctions.
  plVec2 m_vStitchedLakeLeft = plVec2::MakeZero();
  plVec2 m_vStitchedLakeRight = plVec2::MakeZero();
  plUInt32 m_uiStitchedRiverVertexCount = 0;
  plUInt32 m_uiStitchedLakeVertexCount = 0;
  float m_fMaximumLakeSeamError = 0.0f;
  /// Merged junction remesh: one continuous mesh containing the primary river (genuinely cut at the weld
  /// row) plus the transition patch and the duplicated lake shoreline. When set, the PRIMARY body submits
  /// this mesh instead of its own finite LODs, no analytic cutout is applied and the separate patch draw is
  /// suppressed - the former river/patch overlap seam cannot exist. Only produced while the river
  /// participates in no other junction (one secondary profile per draw).
  plMeshBufferResourceHandle m_hMergedSurfaceMesh;
  plMeshBufferResourceHandle m_hMergedVolumeMesh;
  plUInt32 m_uiMergedSurfacePrimitiveCount = 0;
  plUInt32 m_uiMergedVolumePrimitiveCount = 0;
  bool m_bMergedIntoPrimary = false;
  /// Length of the final transition strip whose vertices are already 100% receiving-lake material.
  float m_fLakeMaterialConvergenceDistance = 0.0f;
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
  void CopyJunctionRenderSnapshots(plDynamicArray<plWaterJunctionRenderSnapshot>& out_snapshots) const;

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
  void PrepareJunction(JunctionSlot& slot);
  void PrepareOtherJunctions(const JunctionSlot* pExcept);
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
  static constexpr double s_fFixedStepSeconds = 1.0 / 60.0;
};
