#include <WaterPlugin/WaterPluginPCH.h>

#include <Core/Messages/HierarchyChangedMessages.h>
#include <Core/Messages/TransformChangedMessage.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>
#include <WaterPlugin/Components/WaterBodyComponent.h>
#include <WaterPlugin/Components/WaterExclusionVolumeComponent.h>
#include <WaterPlugin/World/WaterWorldModule.h>

// clang-format off
PL_BEGIN_COMPONENT_TYPE(plWaterExclusionVolumeComponent, 1, plComponentMode::Static)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ACCESSOR_PROPERTY("Extents", GetExtents, SetExtents)->AddAttributes(new plDefaultValueAttribute(plVec3(10.0f)), new plClampValueAttribute(plVec3(0.01f), plVariant())),
    PL_ACCESSOR_PROPERTY("AffectAllBodies", GetAffectAllBodies, SetAffectAllBodies)->AddAttributes(new plDefaultValueAttribute(true)),
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
    new plBoxManipulatorAttribute("Extents", 1.0f, false),
    new plBoxVisualizerAttribute("Extents", 1.0f, plColorScheme::LightUI(plColorScheme::Cyan)),
  }
  PL_END_ATTRIBUTES;
}
PL_END_COMPONENT_TYPE;
// clang-format on

plWaterExclusionVolumeComponent::plWaterExclusionVolumeComponent() = default;

void plWaterExclusionVolumeComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);
  auto& stream = inout_stream.GetStream();
  stream << m_vExtents;
  stream << m_bAffectAllBodies;
}

void plWaterExclusionVolumeComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);
  inout_stream.GetComponentTypeVersion(GetStaticRTTI());
  auto& stream = inout_stream.GetStream();
  stream >> m_vExtents;
  stream >> m_bAffectAllBodies;
}

void plWaterExclusionVolumeComponent::SetExtents(const plVec3& vExtents)
{
  m_vExtents = vExtents.CompMax(plVec3(0.01f));
  InvalidateExclusion();
}

void plWaterExclusionVolumeComponent::SetAffectAllBodies(bool bAffectAll)
{
  m_bAffectAllBodies = bAffectAll;
  InvalidateExclusion();
}

void plWaterExclusionVolumeComponent::InvalidateExclusion()
{
  if (!IsActiveAndInitialized())
    return;

  plWaterWorldModule* pModule = GetWorld()->GetOrCreateModule<plWaterWorldModule>();
  plWaterExclusionDescriptor descriptor;
  descriptor.m_hOwnerObject = GetOwner()->GetHandle();
  descriptor.m_GlobalTransform = GetOwner()->GetGlobalTransform();
  descriptor.m_vExtents = m_vExtents;

  if (!m_bAffectAllBodies)
  {
    const plGameObject* pObject = GetOwner()->GetParent();
    while (pObject != nullptr)
    {
      const plWaterBodyComponent* pBody = nullptr;
      if (pObject->TryGetComponentOfBaseType(pBody))
      {
        descriptor.m_TargetBody = pBody->GetWaterBodyId();
        break;
      }
      pObject = pObject->GetParent();
    }

    if (!descriptor.m_TargetBody.IsValid())
    {
      pModule->UnregisterExclusion(m_uiExclusionIndex);
      return;
    }
  }

  if (m_uiExclusionIndex == plInvalidIndex)
    m_uiExclusionIndex = pModule->RegisterExclusion(descriptor);
  else if (!pModule->UpdateExclusion(m_uiExclusionIndex, descriptor))
    m_uiExclusionIndex = pModule->RegisterExclusion(descriptor);
}

void plWaterExclusionVolumeComponent::OnActivated()
{
  SUPER::OnActivated();
  GetOwner()->EnableStaticTransformChangesNotifications();
  InvalidateExclusion();
}

void plWaterExclusionVolumeComponent::OnDeactivated()
{
  if (plWaterWorldModule* pModule = GetWorld()->GetModule<plWaterWorldModule>())
    pModule->UnregisterExclusion(m_uiExclusionIndex);
  SUPER::OnDeactivated();
}

void plWaterExclusionVolumeComponent::OnTransformChanged(plMsgTransformChanged& msg)
{
  PL_IGNORE_UNUSED(msg);
  InvalidateExclusion();
}

void plWaterExclusionVolumeComponent::OnParentChanged(plMsgParentChanged& msg)
{
  PL_IGNORE_UNUSED(msg);
  InvalidateExclusion();
}

PL_STATICLINK_FILE(WaterPlugin, WaterPlugin_Components_Implementation_WaterExclusionVolumeComponent);
