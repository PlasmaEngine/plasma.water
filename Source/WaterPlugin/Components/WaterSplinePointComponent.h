#pragma once

#include <Core/Messages/EventMessage.h>
#include <Core/World/Component.h>
#include <Core/World/World.h>
#include <WaterPlugin/WaterPluginDLL.h>

struct PL_WATER_DLL plMsgWaterSplineChanged : public plEventMessage
{
  PL_DECLARE_MESSAGE_TYPE(plMsgWaterSplineChanged, plEventMessage);
};

using plWaterSplinePointComponentManager = plComponentManager<class plWaterSplinePointComponent, plBlockStorageType::Compact>;

/// \brief Child control point consumed by a river or lake plWaterBodyComponent.
class PL_WATER_DLL plWaterSplinePointComponent : public plComponent
{
  PL_DECLARE_COMPONENT_TYPE(plWaterSplinePointComponent, plComponent, plWaterSplinePointComponentManager);

public:
  plWaterSplinePointComponent();

  virtual void SerializeComponent(plWorldWriter& inout_stream) const override;
  virtual void DeserializeComponent(plWorldReader& inout_stream) override;

  float GetLeftWidth() const { return m_fLeftWidth; }
  void SetLeftWidth(float fWidth);

  float GetRightWidth() const { return m_fRightWidth; }
  void SetRightWidth(float fWidth);

  float GetDepth() const { return m_fDepth; }
  void SetDepth(float fDepth);

  float GetFlowScale() const { return m_fFlowScale; }
  void SetFlowScale(float fFlowScale);

protected:
  virtual void OnActivated() override;
  void OnTransformChanged(plMsgTransformChanged& msg);
  void OnParentChanged(plMsgParentChanged& msg);
  void NotifySplineChanged();

  float m_fLeftWidth = 5.0f;
  float m_fRightWidth = 5.0f;
  float m_fDepth = 5.0f;
  float m_fFlowScale = 1.0f;
};
