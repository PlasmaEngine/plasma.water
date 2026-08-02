#pragma once

#include <Core/ResourceManager/ResourceHandle.h>
#include <Core/World/Component.h>
#include <Core/World/World.h>
#include <Foundation/Math/Vec2.h>
#include <Foundation/Math/Vec3.h>
#include <GameEngine/Interfaces/WaterWorldModule.h>
#include <RendererCore/Components/RenderComponent.h>
#include <RendererCore/Pipeline/RenderData.h>
#include <WaterPlugin/WaterPluginDLL.h>

struct plMsgExtractRenderData;
class plOceanComponent;
class plMeshResourceDescriptor;

using plMeshResourceHandle = plTypedResourceHandle<class plMeshResource>;
using plMaterialResourceHandle = plTypedResourceHandle<class plMaterialResource>;

class PL_WATER_DLL plOceanComponentManager : public plComponentManager<plOceanComponent, plBlockStorageType::Compact>
{
public:
  plOceanComponentManager(plWorld* pWorld);
  ~plOceanComponentManager();

  virtual void Initialize() override;
};

/// \brief Deprecated compatibility facade for legacy ocean scenes.
///
/// New content should use plWaterBodyComponent in Ocean mode. This component keeps the original
/// serialization layout and material override alive for one migration window.
class PL_WATER_DLL plOceanComponent : public plRenderComponent
{
  PL_DECLARE_COMPONENT_TYPE(plOceanComponent, plRenderComponent, plOceanComponentManager);

  //////////////////////////////////////////////////////////////////////////
  // plComponent

  virtual void SerializeComponent(plWorldWriter& stream) const override;
  virtual void DeserializeComponent(plWorldReader& stream) override;

  virtual void OnActivated() override;
  virtual void OnDeactivated() override;

  //////////////////////////////////////////////////////////////////////////
  // plRenderComponent
protected:
  virtual plResult GetLocalBounds(plBoundingBoxSphere& bounds, bool& bAlwaysVisible, plMsgUpdateLocalBounds& msg) override;
  void OnMsgExtractRenderData(plMsgExtractRenderData& msg) const;

  //////////////////////////////////////////////////////////////////////////
  // plOceanComponent

public:
  plOceanComponent();
  ~plOceanComponent();

  float GetWaterLevel() const { return m_fWaterLevel; }  // [ property ]
  void SetWaterLevel(float value);                       // [ property ]

  float GetTileSize() const { return m_fTileSize; }      // [ property ]
  void SetTileSize(float value);                         // [ property ]

  plUInt32 GetTileCountXZ() const { return m_uiTileCountXZ; }  // [ property ]
  void SetTileCountXZ(plUInt32 value);                          // [ property ]

  plVec2U32 GetTileTesselation() const { return m_vTileTesselation; }  // [ property ]
  void SetTileTesselation(plVec2U32 value);                             // [ property ]

  plVec2U32 GetTessellation() const { return m_vTileTesselation; } // [ property ]
  void SetTessellation(plVec2U32 value) { SetTileTesselation(value); } // [ property ]

  void SetMaterialFile(const char* szFile);              // [ property ]
  const char* GetMaterialFile() const;                   // [ property ]

  void SetMaterial(const plMaterialResourceHandle& hMaterial);
  plMaterialResourceHandle GetMaterial() const { return m_hMaterial; }

  float GetUpdateThreshold() const { return m_fUpdateThreshold; }  // [ property ]
  void SetUpdateThreshold(float value);                            // [ property ]

  plUInt32 GetLodDivisor() const { return m_uiLodDivisor; }        // [ property ]
  void SetLodDivisor(plUInt32 value);                              // [ property ]

  float GetLodDistance0() const { return m_fLodDistances[0]; }     // [ property ]
  void SetLodDistance0(float value);                               // [ property ]

  float GetLodDistance1() const { return m_fLodDistances[1]; }     // [ property ]
  void SetLodDistance1(float value);                               // [ property ]

  float GetLodDistance2() const { return m_fLodDistances[2]; }     // [ property ]
  void SetLodDistance2(float value);                               // [ property ]

  float GetLodDistance3() const { return m_fLodDistances[3]; }     // [ property ]
  void SetLodDistance3(float value);                               // [ property ]

  float GetWaveAmplitude() const { return m_fWaveAmplitude; }      // [ property ]
  void SetWaveAmplitude(float value);                              // [ property ]

protected:
  void InvalidateMesh();
  void InvalidateLegacyWater();
  plResult BuildTileMeshDescriptor(plMeshResourceDescriptor& desc, plUInt32 uiLodLevel) const;
  plMeshResourceHandle GenerateTileMesh(plUInt32 uiLodLevel) const;
  void GenerateAllLodMeshes();
  plUInt32 GetLodLevelForDistance(float fDistance) const;

  static constexpr plUInt32 s_uiNumLodLevels = 4;  // Number of LOD levels

  plMaterialResourceHandle m_hMaterial;
  plMeshResourceHandle m_hTileMeshes[s_uiNumLodLevels];  // LOD meshes from high to low detail

  float m_fWaterLevel = 0.0f;
  float m_fTileSize = 100.0f;
  plUInt32 m_uiTileCountXZ = 5;  // 5x5 = 25 tiles
  plVec2U32 m_vTileTesselation = plVec2U32(64);  // Base tessellation (highest LOD)

  float m_fUpdateThreshold = 50.0f;  // Update when camera moves this far
  plUInt32 m_uiLodDivisor = 2;  // Tessellation divisor per LOD level
  float m_fLodDistances[s_uiNumLodLevels] = {100.0f, 250.0f, 500.0f, 1000.0f};  // Distance thresholds for each LOD
  
  // Wave parameters
  float m_fWaveAmplitude = 0.28f;
  
  plVec3 m_vLastCameraPos = plVec3::MakeZero();

  plUInt32 m_uiMeshChangeCounter = 0;
  plWaterBodyId m_LegacyBodyId;

  // Track instance data from previous frame for cleanup
  mutable plDynamicArray<plInstanceDataOffset> m_PreviousFrameInstanceData;
};
