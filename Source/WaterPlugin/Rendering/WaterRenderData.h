#pragma once

#include <Core/ResourceManager/ResourceHandle.h>
#include <Foundation/Math/Color.h>
#include <RendererCore/Meshes/MeshBufferResource.h>
#include <RendererCore/Pipeline/RenderData.h>
#include <RendererCore/Textures/Texture2DResource.h>
#include <WaterPlugin/WaterPluginDLL.h>

/// \brief A procedural rectangular patch. Curved rivers and lake interiors are emitted as multiple patches.
class PL_WATER_DLL plWaterRenderData : public plInstanceableRenderData
{
  PL_ADD_DYNAMIC_REFLECTION(plWaterRenderData, plInstanceableRenderData);

public:
  static plRenderData::Category WaterSurface;

  plGALBufferHandle m_hBodyData;
  plGALBufferHandle m_hPatchData;
  plGALBufferHandle m_hSpectrumData;
  plGALTextureHandle m_hDisplacementTexture;
  plGALTextureHandle m_hInteractionTexture;
  plMeshBufferResourceHandle m_hFiniteMesh;
  plTexture2DResourceHandle m_hNormalTexture;
  plTexture2DResourceHandle m_hSecondaryNormalTexture;
  plTexture2DResourceHandle m_hFoamTexture;
  plTexture2DResourceHandle m_hSecondaryFoamTexture;

  plVec3 m_vPatchOrigin = plVec3::MakeZero();
  plVec3 m_vPatchAxisU = plVec3(1.0f, 0.0f, 0.0f);
  plVec3 m_vPatchAxisV = plVec3(0.0f, 1.0f, 0.0f);
  plVec2 m_vFlow = plVec2::MakeZero();
  plVec2 m_vWindDirection = plVec2(1.0f, 0.0f);
  plVec3 m_vAbsorption = plVec3(0.15f, 0.06f, 0.025f);
  plVec3 m_vScattering = plVec3(0.015f, 0.035f, 0.055f);
  plColor m_ShallowColor = plColor(0.08f, 0.34f, 0.42f, 0.65f);
  plColor m_DeepColor = plColor(0.008f, 0.035f, 0.075f, 0.95f);
  plVec3 m_vSecondaryAbsorption = plVec3(0.15f, 0.06f, 0.025f);
  plVec3 m_vSecondaryScattering = plVec3(0.015f, 0.035f, 0.055f);
  plVec2 m_vSecondaryWindDirection = plVec2(1.0f, 0.0f);
  /// The receiving body's exact (FlowSpeed, profile FlowScale) pair. Junction secondary detail must use the
  /// same baseline drift as that body's ordinary draw before adding the localized current plume.
  plVec2 m_vSecondaryFlow = plVec2::MakeZero();
  plColor m_SecondaryShallowColor = plColor(0.08f, 0.34f, 0.42f, 0.65f);
  plColor m_SecondaryDeepColor = plColor(0.008f, 0.035f, 0.075f, 0.95f);
  float m_fExtentU = 1.0f;
  float m_fExtentV = 1.0f;
  float m_fBaseHeight = 0.0f;
  float m_fWaveAmplitude = 0.15f;
  float m_fWaveLength = 8.0f;
  float m_fWaveSpeed = 1.0f;
  float m_fChoppiness = 0.5f;
  float m_fRoughness = 0.08f;
  float m_fIndexOfRefraction = 1.333f;
  float m_fReflectionStrength = 1.0f;
  float m_fEnvironmentReflectionWeight = 1.0f;
  float m_fMicroNormalScale = 1.0f;
  float m_fMicroNormalStrength = 0.22f;
  float m_fSunGlitterStrength = 0.35f;
  float m_fRefractionEdgeRejection = 0.65f;
  float m_fRefractionStrength = 0.012f;
  float m_fRefractionDepth = 6.0f;
  float m_fShoreFadeDepth = 1.2f;
  float m_fFoamStrength = 0.65f;
  float m_fFoamCrestThreshold = 0.78f;
  float m_fFoamShoreDepth = 0.75f;
  plColor m_FoamColor = plColor(0.85f, 0.9f, 0.92f, 1.0f);
  float m_fFoamRoughness = 0.72f;
  float m_fFoamBreakupScale = 0.18f;
  float m_fRippleMaxAmplitude = 0.2f;
  float m_fSimulationTime = 0.0f;
  float m_fUnderwaterFogDensity = 0.06f;
  float m_fUnderwaterDistortion = 0.18f;
  float m_fUnderwaterCaustics = 0.25f;
  float m_fReflectedCaustics = 0.15f;
  float m_fUnderwaterLightShafts = 0.12f;
  float m_fJunctionBlend = 0.0f;
  float m_fSecondaryWaveAmplitude = 0.15f;
  float m_fSecondaryWaveLength = 8.0f;
  float m_fSecondaryWaveSpeed = 1.0f;
  float m_fSecondaryChoppiness = 0.5f;
  float m_fSecondaryRoughness = 0.08f;
  float m_fSecondaryIndexOfRefraction = 1.333f;
  float m_fSecondaryReflectionStrength = 1.0f;
  float m_fSecondaryEnvironmentReflectionWeight = 1.0f;
  float m_fSecondaryMicroNormalScale = 1.0f;
  float m_fSecondaryMicroNormalStrength = 0.22f;
  float m_fSecondarySunGlitterStrength = 0.35f;
  float m_fSecondaryRefractionEdgeRejection = 0.65f;
  float m_fSecondaryRefractionStrength = 0.012f;
  float m_fSecondaryRefractionDepth = 6.0f;
  float m_fSecondaryShoreFadeDepth = 1.2f;
  float m_fSecondaryFoamStrength = 0.65f;
  float m_fSecondaryFoamCrestThreshold = 0.78f;
  float m_fSecondaryFoamShoreDepth = 0.75f;
  plColor m_SecondaryFoamColor = plColor(0.85f, 0.9f, 0.92f, 1.0f);
  float m_fSecondaryFoamRoughness = 0.72f;
  float m_fSecondaryFoamBreakupScale = 0.18f;
  float m_fSecondaryUnderwaterCaustics = 0.25f;
  float m_fSecondaryReflectedCaustics = 0.15f;
  struct Cutout
  {
    plVec2 m_vCenter = plVec2::MakeZero();
    plVec2 m_vDirection = plVec2(1.0f, 0.0f);
    float m_fHalfLength = 0.0f;
    float m_fStartHalfWidth = 0.0f;
    float m_fEndHalfWidth = 0.0f;
    float m_fEnabled = 0.0f;
  };

  struct JunctionFlow
  {
    plVec2 m_vOrigin = plVec2::MakeZero();
    plVec2 m_vDirection = plVec2(1.0f, 0.0f);
    float m_fLength = 0.0f;
    float m_fHalfWidth = 0.0f;
    float m_fSpeed = 0.0f;
    float m_fDirectionSign = 1.0f;
  };

  // A body can participate in up to two junctions (a river has two ends).
  Cutout m_JunctionCutouts[2];
  // Receiving lakes and their transition patches carry up to two localized inflow/outflow fields. These are
  // evaluated analytically in the tessellated vertex path and do not require a flow texture or simulation pass.
  JunctionFlow m_JunctionFlows[2];
  // Junction patches only: the secondary body's spectrum identity so the patch reproduces its GPU cascade
  // displacement at the seam, plus the seam-behavior parameters computed by the world module.
  plUInt32 m_uiSecondaryProfileIndex = plInvalidIndex;
  plUInt32 m_uiSecondaryGpuCascadeCount = 0;
  float m_fSecondaryRippleMaxAmplitude = 0.2f;
  float m_fMouthFoamStrength = 0.0f;
  /// River material carried into a receiving lake. The lake draw reverses the primary/secondary profiles, so
  /// both sides evaluate complementary weights from the same analytic field at their shared shoreline.
  float m_fJunctionMaterialMixStrength = 0.0f;
  plUInt32 m_uiJunctionMaterialFlowSlot = 0;
  plUInt32 m_uiBodyId = 0;
  /// Junction patches are extracted by their secondary (receiving) body. Retaining its ID also lets the
  /// underwater exit-depth pass keep the shared junction volume when the camera is in that body.
  plUInt32 m_uiSecondaryBodyId = plInvalidIndex;
  plUInt32 m_uiProfileIndex = 0;
  plUInt32 m_uiWaveSeed = 0xC0FFEEu;
  plUInt8 m_uiSpectrumMode = 0;
  plUInt32 m_uiGpuCascadeCount = 1;
  float m_fSmallestGpuWavelength = 0.5f;
  plUInt32 m_uiGeometryType = 0;
  plUInt32 m_uiCellsPerSide = 32;
  plUInt32 m_uiPrimitiveCount = 32 * 32 * 2;
  plUInt32 m_uiGeometryRevision = 0;
  plUInt8 m_uiLod = 0;
};
