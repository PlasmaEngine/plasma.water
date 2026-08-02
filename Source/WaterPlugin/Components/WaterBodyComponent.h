#pragma once

#include <GameEngine/Interfaces/WaterWorldModule.h>
#include <RendererCore/Components/RenderComponent.h>
#include <WaterPlugin/Resources/WaterProfileResource.h>
#include <WaterPlugin/WaterPluginDLL.h>

struct plMsgWaterSplineChanged;
struct plMsgSplineChanged;
struct plMsgExtractRenderData;
struct plMsgTransformChanged;
struct plWaterBodyDescriptor;

using plWaterBodyComponentManager = plComponentManager<class plWaterBodyComponent, plBlockStorageType::Compact>;

/// \brief Unified authored water body for oceans, spline rivers and closed spline lakes.
class PL_WATER_DLL plWaterBodyComponent : public plRenderComponent
{
  PL_DECLARE_COMPONENT_TYPE(plWaterBodyComponent, plRenderComponent, plWaterBodyComponentManager);

public:
  plWaterBodyComponent();

  virtual void SerializeComponent(plWorldWriter& inout_stream) const override;
  virtual void DeserializeComponent(plWorldReader& inout_stream) override;

  virtual plResult GetLocalBounds(plBoundingBoxSphere& out_bounds, bool& out_bAlwaysVisible, plMsgUpdateLocalBounds& ref_msg) override;

  void OnMsgExtractRenderData(plMsgExtractRenderData& msg) const;

  plEnum<plWaterBodyType> GetBodyType() const { return m_BodyType; }
  void SetBodyType(plEnum<plWaterBodyType> type);

  void SetProfileFile(const char* szFile);
  const char* GetProfileFile() const;
  void SetProfile(const plWaterProfileResourceHandle& hProfile);
  const plWaterProfileResourceHandle& GetProfile() const { return m_hProfile; }

  plInt16 GetPriority() const { return m_iPriority; }
  void SetPriority(plInt16 iPriority);

  float GetDefaultWidth() const { return m_fDefaultWidth; }
  void SetDefaultWidth(float fWidth);

  float GetDefaultDepth() const { return m_fDefaultDepth; }
  void SetDefaultDepth(float fDepth);

  float GetFlowSpeed() const { return m_fFlowSpeed; }
  void SetFlowSpeed(float fFlowSpeed);

  float GetOceanExtent() const { return m_fOceanExtent; }
  void SetOceanExtent(float fExtent);

  float GetOceanPatchSize() const { return m_fOceanPatchSize; }
  void SetOceanPatchSize(float fPatchSize);

  plUInt8 GetOceanRingCount() const { return m_uiOceanRingCount; }
  void SetOceanRingCount(plUInt8 uiRingCount);

  plUInt16 GetPatchResolution() const { return m_uiPatchResolution; }
  void SetPatchResolution(plUInt16 uiResolution);

  plWaterBodyId GetWaterBodyId() const { return m_BodyId; }
  plUInt64 GetNetworkId() const;

  /// Rebuilds a world-space descriptor consumed by plWaterWorldModule and renderer snapshots.
  void BuildWaterBodyDescriptor(plWaterBodyDescriptor& out_descriptor) const;
  void InvalidateWaterBody();

protected:
  virtual void OnActivated() override;
  virtual void OnDeactivated() override;

  void OnSplineChanged(plMsgWaterSplineChanged& msg);
  void OnStockSplineChanged(plMsgSplineChanged& msg);
  void OnTransformChanged(plMsgTransformChanged& msg);

  plEnum<plWaterBodyType> m_BodyType;
  plWaterProfileResourceHandle m_hProfile;
  plInt16 m_iPriority = 0;
  float m_fDefaultWidth = 10.0f;
  float m_fDefaultDepth = 5.0f;
  float m_fFlowSpeed = 1.0f;
  float m_fOceanExtent = 10000.0f;
  float m_fOceanPatchSize = 64.0f;
  plUInt8 m_uiOceanRingCount = 6;
  plUInt16 m_uiPatchResolution = 64;
  plWaterBodyId m_BodyId;
};
