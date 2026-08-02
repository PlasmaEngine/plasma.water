#include <WaterPlugin/WaterPluginPCH.h>

#include <Core/Messages/HierarchyChangedMessages.h>
#include <Core/Messages/TransformChangedMessage.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>
#include <WaterPlugin/Components/WaterSplinePointComponent.h>

PL_IMPLEMENT_MESSAGE_TYPE(plMsgWaterSplineChanged);

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plMsgWaterSplineChanged, 1, plRTTIDefaultAllocator<plMsgWaterSplineChanged>)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_COMPONENT_TYPE(plWaterSplinePointComponent, 1, plComponentMode::Static)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ACCESSOR_PROPERTY("LeftWidth", GetLeftWidth, SetLeftWidth)->AddAttributes(new plDefaultValueAttribute(5.0f), new plClampValueAttribute(0.01f, 100000.0f)),
    PL_ACCESSOR_PROPERTY("RightWidth", GetRightWidth, SetRightWidth)->AddAttributes(new plDefaultValueAttribute(5.0f), new plClampValueAttribute(0.01f, 100000.0f)),
    PL_ACCESSOR_PROPERTY("Depth", GetDepth, SetDepth)->AddAttributes(new plDefaultValueAttribute(5.0f), new plClampValueAttribute(0.01f, 100000.0f)),
    PL_ACCESSOR_PROPERTY("FlowScale", GetFlowScale, SetFlowScale)->AddAttributes(new plDefaultValueAttribute(1.0f)),
  }
  PL_END_PROPERTIES;
  PL_BEGIN_MESSAGEHANDLERS
  {
    PL_MESSAGE_HANDLER(plMsgTransformChanged, OnTransformChanged),
    PL_MESSAGE_HANDLER(plMsgParentChanged, OnParentChanged),
  }
  PL_END_MESSAGEHANDLERS;
  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("Rendering/Water"),
    new plShapeIconAlwaysVisibleAttribute(),
    new plDirectionVisualizerAttribute(plBasisAxis::NegativeY, 1.0f, plColor::Cyan, nullptr, "LeftWidth"),
    new plDirectionVisualizerAttribute(plBasisAxis::PositiveY, 1.0f, plColor::Cyan, nullptr, "RightWidth"),
    new plDirectionVisualizerAttribute(plBasisAxis::NegativeZ, 1.0f, plColor::Blue, nullptr, "Depth"),
    new plDirectionVisualizerAttribute(plBasisAxis::PositiveX, 2.0f, plColor::Green, nullptr, "FlowScale"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_COMPONENT_TYPE;
// clang-format on

plWaterSplinePointComponent::plWaterSplinePointComponent() = default;

void plWaterSplinePointComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);
  auto& stream = inout_stream.GetStream();
  stream << m_fLeftWidth;
  stream << m_fRightWidth;
  stream << m_fDepth;
  stream << m_fFlowScale;
}

void plWaterSplinePointComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);
  inout_stream.GetComponentTypeVersion(GetStaticRTTI());
  auto& stream = inout_stream.GetStream();
  stream >> m_fLeftWidth;
  stream >> m_fRightWidth;
  stream >> m_fDepth;
  stream >> m_fFlowScale;
}

void plWaterSplinePointComponent::SetLeftWidth(float fWidth)
{
  m_fLeftWidth = plMath::Max(fWidth, 0.01f);
  NotifySplineChanged();
}

void plWaterSplinePointComponent::SetRightWidth(float fWidth)
{
  m_fRightWidth = plMath::Max(fWidth, 0.01f);
  NotifySplineChanged();
}

void plWaterSplinePointComponent::SetDepth(float fDepth)
{
  m_fDepth = plMath::Max(fDepth, 0.01f);
  NotifySplineChanged();
}

void plWaterSplinePointComponent::SetFlowScale(float fFlowScale)
{
  m_fFlowScale = fFlowScale;
  NotifySplineChanged();
}

void plWaterSplinePointComponent::OnActivated()
{
  SUPER::OnActivated();
  GetOwner()->EnableStaticTransformChangesNotifications();
  GetOwner()->EnableParentChangesNotifications();
  NotifySplineChanged();
}

void plWaterSplinePointComponent::OnTransformChanged(plMsgTransformChanged& msg)
{
  PL_IGNORE_UNUSED(msg);
  NotifySplineChanged();
}

void plWaterSplinePointComponent::OnParentChanged(plMsgParentChanged& msg)
{
  if (msg.m_Type == plMsgParentChanged::Type::ParentUnlinked)
  {
    plGameObject* pOldParent = nullptr;
    if (GetWorld()->TryGetObject(msg.m_hParent, pOldParent))
    {
      plMsgWaterSplineChanged changed;
      pOldParent->SendEventMessage(changed, this);
    }
  }
  else
  {
    NotifySplineChanged();
  }
}

void plWaterSplinePointComponent::NotifySplineChanged()
{
  if (!IsActiveAndInitialized())
    return;
  plMsgWaterSplineChanged changed;
  GetOwner()->SendEventMessage(changed, this);
}

PL_STATICLINK_FILE(WaterPlugin, WaterPlugin_Components_Implementation_WaterSplinePointComponent);
