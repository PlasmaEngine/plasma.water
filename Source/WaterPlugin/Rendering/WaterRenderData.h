#pragma once

#include <Core/ResourceManager/ResourceHandle.h>
#include <Foundation/Math/Color.h>
#include <RendererCore/Meshes/MeshBufferResource.h>
#include <RendererCore/Pipeline/RenderData.h>
#include <RendererCore/Textures/Texture2DResource.h>
#include <WaterPlugin/WaterPluginDLL.h>

/// \brief One water draw: an ocean clipmap patch, or the mesh of a river or lake.
///
/// How the water looks is not in here. A draw names its profile by index (m_uiProfileIndex, the body's entry in the
/// profile table that plWaterSurfacePass uploads), and the vertices of a finite mesh name the profile pairs they blend.
class PL_WATER_DLL plWaterRenderData : public plInstanceableRenderData
{
  PL_ADD_DYNAMIC_REFLECTION(plWaterRenderData, plInstanceableRenderData);

public:
  static plRenderData::Category WaterSurface;

  /// The order water draws are made in: back to front within a layer, and the layers in order (m_uiSortLayer). Water
  /// does not write depth, so what is drawn last shows. A river's apron has to come after the ocean it lies on, from
  /// wherever it is looked at.
  static plUInt64 GetSortingKey(const plRenderData* pRenderData, const plCamera& camera);

  /// m_uiGeometryType: what the shader is given to draw.
  enum GeometryType : plUInt32
  {
    GeometryOceanPatch = 0, ///< A procedural grid; the vertex shader makes the vertices.
    GeometrySurface = 1,    ///< The surface mesh of a river or lake.
    GeometryVolume = 3,     ///< The closed volume under it, for the exit depth of an underwater camera.
  };

  plMeshBufferResourceHandle m_hFiniteMesh;
  /// Detail normal and foam of the body that draws. A blend with another body's profile uses these for both sides:
  /// one draw binds one set.
  plTexture2DResourceHandle m_hNormalTexture;
  plTexture2DResourceHandle m_hFoamTexture;

  plVec3 m_vPatchOrigin = plVec3::MakeZero();
  plVec3 m_vPatchAxisU = plVec3(1.0f, 0.0f, 0.0f);
  plVec3 m_vPatchAxisV = plVec3(0.0f, 1.0f, 0.0f);
  float m_fExtentU = 1.0f;
  float m_fExtentV = 1.0f;
  float m_fBaseHeight = 0.0f;
  float m_fSimulationTime = 0.0f;
  /// Rivers and lakes: the edge length their mesh was built with, at the level of detail that is drawn.
  float m_fVertexSpacing = 1.0f;

  // What the surface pass reads off the draws of a frame before any shader runs: which profiles get a slot in the
  // wave simulation, and whether there is water in view that casts caustics.
  plVec2 m_vWindDirection = plVec2(1.0f, 0.0f);
  float m_fWaveAmplitude = 0.15f;
  float m_fWaveLength = 8.0f;
  float m_fWaveSpeed = 1.0f;
  float m_fChoppiness = 0.5f;
  plUInt32 m_uiWaveSeed = 0xC0FFEEu;
  plUInt8 m_uiSpectrumMode = 0;
  plUInt32 m_uiGpuCascadeCount = 1;
  float m_fSmallestGpuWavelength = 0.5f;
  float m_fFoamCrestThreshold = 0.78f;
  float m_fUnderwaterCaustics = 0.25f;
  float m_fReflectedCaustics = 0.15f;

  // For the underwater pass in a pipeline that has no water frame data extractor (a hand-built pipeline may leave it
  // out): what it needs to know about an ocean the camera is under.
  plVec3 m_vAbsorption = plVec3(0.15f, 0.06f, 0.025f);
  plVec3 m_vScattering = plVec3(0.015f, 0.035f, 0.055f);
  float m_fUnderwaterFogDensity = 0.06f;
  float m_fUnderwaterDistortion = 0.18f;
  float m_fUnderwaterLightShafts = 0.12f;

  /// A trapezoid that an ocean patch leaves out: the inside of a river's apron. From m_vCenter, half the length either
  /// way along m_vDirection, half the start width at the river's side and half the end width at the sea's.
  struct Cutout
  {
    plVec2 m_vCenter = plVec2::MakeZero();
    plVec2 m_vDirection = plVec2(1.0f, 0.0f);
    float m_fHalfLength = 0.0f;
    float m_fStartHalfWidth = 0.0f;
    float m_fEndHalfWidth = 0.0f;
    float m_fEnabled = 0.0f;
  };
  Cutout m_Cutouts[2];

  plUInt32 m_uiBodyId = 0;
  plUInt32 m_uiProfileIndex = 0;
  plUInt32 m_uiGeometryType = GeometryOceanPatch;
  plUInt32 m_uiCellsPerSide = 32;
  /// Ocean patches: the body's Patch Resolution, from which the cells of every ring follow (plWaterGeometry::GetOceanRingCells).
  plUInt32 m_uiOceanResolution = 64;
  plUInt32 m_uiPrimitiveCount = 32 * 32 * 2;
  plUInt32 m_uiGeometryRevision = 0;
  plUInt8 m_uiLod = 0;
  /// Ocean patches: the edges that meet a ring of another cell size (plWaterClipmapSeamFlags).
  plUInt8 m_uiInnerSeamMask = 0;
  plUInt8 m_uiOuterSeamMask = 0;
  /// Draws of a higher layer come after every draw of a lower one, whatever their distance. Ocean 0, rivers and lakes 1.
  plUInt8 m_uiSortLayer = 0;
};
