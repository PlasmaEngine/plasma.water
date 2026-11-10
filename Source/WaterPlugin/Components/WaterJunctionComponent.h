#pragma once

#include <Core/World/Component.h>
#include <Core/World/World.h>
#include <GameEngine/Interfaces/WaterWorldModule.h>
#include <WaterPlugin/WaterPluginDLL.h>

using plWaterJunctionComponentManager = plComponentManager<class plWaterJunctionComponent, plBlockStorageType::Compact>;

struct plMsgTransformChanged;

struct PL_WATER_DLL plWaterAttachmentAnchor
{
  using StorageType = plUInt8;

  enum Enum
  {
    Auto,
    RiverStart,
    RiverEnd,
    LakeBoundary,
    OceanBoundary,

    Default = Auto
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_WATER_DLL, plWaterAttachmentAnchor);

/// \brief Explicit transition between two authored water bodies.
class PL_WATER_DLL plWaterJunctionComponent : public plComponent
{
  PL_DECLARE_COMPONENT_TYPE(plWaterJunctionComponent, plComponent, plWaterJunctionComponentManager);

public:
  plWaterJunctionComponent();

  virtual void SerializeComponent(plWorldWriter& inout_stream) const override;
  virtual void DeserializeComponent(plWorldReader& inout_stream) override;

  void SetBodyAReference(const char* szReference);
  void SetBodyBReference(const char* szReference);
  const char* DummyGetter() const { return nullptr; }

  void SetBodyA(plGameObjectHandle hObject);
  void SetBodyB(plGameObjectHandle hObject);
  plGameObjectHandle GetBodyA() const { return m_hBodyA; }
  plGameObjectHandle GetBodyB() const { return m_hBodyB; }

  float GetBlendDistance() const { return m_fBlendDistance; }
  void SetBlendDistance(float fDistance);
  /// Blend Distance is the mix length of the junction: how far the two waters mix across the mouth, in metres. The
  /// receiver's side gets all of it, the river's side a part. With AutoBlendDistance it follows the river's width.
  /// Front Breakup is how ragged the front between the two waters is drawn: 0 is a smooth ramp, 1 is torn.
  float GetFrontBreakup() const { return m_fFrontBreakup; }
  void SetFrontBreakup(float fBreakup);

  float GetSnapRadius() const { return m_fSnapRadius; }
  void SetSnapRadius(float fDistance);
  float GetElevationTolerance() const { return m_fElevationTolerance; }
  void SetElevationTolerance(float fTolerance);
  bool GetAutoSnap() const { return m_bAutoSnap; }
  void SetAutoSnap(bool bAutoSnap);
  bool GetAutoBlendDistance() const { return m_bAutoBlendDistance; }
  void SetAutoBlendDistance(bool bAuto);

  plEnum<plWaterAttachmentAnchor> GetAnchorA() const { return m_AnchorA; }
  void SetAnchorA(plEnum<plWaterAttachmentAnchor> anchor);
  plEnum<plWaterAttachmentAnchor> GetAnchorB() const { return m_AnchorB; }
  void SetAnchorB(plEnum<plWaterAttachmentAnchor> anchor);

  void InvalidateJunction();

protected:
  virtual void OnActivated() override;
  virtual void OnDeactivated() override;
  void OnTransformChanged(plMsgTransformChanged& msg);

  plGameObjectHandle m_hBodyA;
  plGameObjectHandle m_hBodyB;
  float m_fBlendDistance = 5.0f;
  float m_fFrontBreakup = 0.5f;
  float m_fSnapRadius = 5.0f;
  float m_fElevationTolerance = 0.25f;
  bool m_bAutoSnap = true;
  bool m_bAutoBlendDistance = true;
  plEnum<plWaterAttachmentAnchor> m_AnchorA;
  plEnum<plWaterAttachmentAnchor> m_AnchorB;
  plUInt32 m_uiJunctionIndex = plInvalidIndex;
};
