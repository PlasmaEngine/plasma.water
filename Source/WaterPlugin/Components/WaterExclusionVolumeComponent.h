#pragma once

#include <Core/World/Component.h>
#include <Core/World/World.h>
#include <WaterPlugin/WaterPluginDLL.h>

using plWaterExclusionVolumeComponentManager = plComponentManager<class plWaterExclusionVolumeComponent, plBlockStorageType::Compact>;

struct plMsgParentChanged;
struct plMsgTransformChanged;

/// \brief Oriented box that removes water containment and rendering from all or one parent water body.
class PL_WATER_DLL plWaterExclusionVolumeComponent : public plComponent
{
  PL_DECLARE_COMPONENT_TYPE(plWaterExclusionVolumeComponent, plComponent, plWaterExclusionVolumeComponentManager);

public:
  plWaterExclusionVolumeComponent();

  virtual void SerializeComponent(plWorldWriter& inout_stream) const override;
  virtual void DeserializeComponent(plWorldReader& inout_stream) override;

  const plVec3& GetExtents() const { return m_vExtents; }
  void SetExtents(const plVec3& vExtents);
  bool GetAffectAllBodies() const { return m_bAffectAllBodies; }
  void SetAffectAllBodies(bool bAffectAll);
  void InvalidateExclusion();

protected:
  virtual void OnActivated() override;
  virtual void OnDeactivated() override;
  void OnTransformChanged(plMsgTransformChanged& msg);
  void OnParentChanged(plMsgParentChanged& msg);

  plVec3 m_vExtents = plVec3(10.0f);
  bool m_bAffectAllBodies = true;
  plUInt32 m_uiExclusionIndex = plInvalidIndex;
};
