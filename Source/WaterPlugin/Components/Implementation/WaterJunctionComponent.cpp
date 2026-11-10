#include <WaterPlugin/WaterPluginPCH.h>

#include <Core/Messages/TransformChangedMessage.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>
#include <WaterPlugin/Components/WaterJunctionComponent.h>
#include <WaterPlugin/World/WaterWorldModule.h>

namespace
{
  bool ResolveAttachment(const plWaterBodySnapshot& body, plWaterAttachmentAnchor::Enum attachment, const plVec3& reference,
    plVec3& out_position, float& out_fLocalWidth)
  {
    out_fLocalWidth = 0.0f;

    if (body.m_Descriptor.m_Type == plWaterBodyType::Ocean)
    {
      out_position = plVec3(reference.x, reference.y, body.m_Descriptor.m_fBaseHeight);
      return true;
    }

    const auto& points = body.m_Descriptor.m_ControlPoints;
    if (points.IsEmpty())
      return false;

    plUInt32 uiIndex = 0;
    if (body.m_Descriptor.m_Type == plWaterBodyType::River)
    {
      if (attachment == plWaterAttachmentAnchor::RiverEnd)
      {
        uiIndex = points.GetCount() - 1;
      }
      else if (attachment == plWaterAttachmentAnchor::Auto)
      {
        const float fStartDistance = (points[0].m_vPosition - reference).GetLengthSquared();
        const float fEndDistance = (points.PeekBack().m_vPosition - reference).GetLengthSquared();
        uiIndex = fEndDistance < fStartDistance ? points.GetCount() - 1 : 0;
      }
    }
    else
    {
      // A lake attachment is a point on its shoreline, not merely the nearest sampled spline vertex. Snapping
      // to a vertex can rotate and stretch the generated mouth across dry land; the lake is then cut out under
      // a patch that the opaque terrain correctly rejects, leaving a large wedge-shaped hole.
      float fBestDistanceSquared = plMath::HighValue<float>();
      for (plUInt32 i = 0; i < points.GetCount(); ++i)
      {
        const plWaterControlPoint& a = points[i];
        const plWaterControlPoint& b = points[(i + 1) % points.GetCount()];
        const plVec3 edge = b.m_vPosition - a.m_vPosition;
        const float fEdgeLengthSquared = edge.GetLengthSquared();
        const float t = fEdgeLengthSquared > plMath::SmallEpsilon<float>()
                          ? plMath::Clamp((reference - a.m_vPosition).Dot(edge) / fEdgeLengthSquared, 0.0f, 1.0f)
                          : 0.0f;
        const plVec3 candidate = a.m_vPosition + edge * t;
        const float fDistanceSquared = (candidate - reference).GetLengthSquared();
        if (fDistanceSquared < fBestDistanceSquared)
        {
          fBestDistanceSquared = fDistanceSquared;
          out_position = candidate;
          out_fLocalWidth = plMath::Lerp(a.m_fLeftWidth + a.m_fRightWidth, b.m_fLeftWidth + b.m_fRightWidth, t);
        }
      }
      return fBestDistanceSquared < plMath::HighValue<float>();
    }

    const plWaterControlPoint& point = points[uiIndex];
    out_position = point.m_vPosition;
    out_fLocalWidth = point.m_fLeftWidth + point.m_fRightWidth;
    return true;
  }
} // namespace

// clang-format off
PL_BEGIN_STATIC_REFLECTED_ENUM(plWaterAttachmentAnchor, 1)
  PL_ENUM_CONSTANT(plWaterAttachmentAnchor::Auto),
  PL_ENUM_CONSTANT(plWaterAttachmentAnchor::RiverStart),
  PL_ENUM_CONSTANT(plWaterAttachmentAnchor::RiverEnd),
  PL_ENUM_CONSTANT(plWaterAttachmentAnchor::LakeBoundary),
  PL_ENUM_CONSTANT(plWaterAttachmentAnchor::OceanBoundary),
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_COMPONENT_TYPE(plWaterJunctionComponent, 2, plComponentMode::Static)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ACCESSOR_PROPERTY("WaterBodyA", DummyGetter, SetBodyAReference)->AddAttributes(new plGameObjectReferenceAttribute()),
    PL_ACCESSOR_PROPERTY("WaterBodyB", DummyGetter, SetBodyBReference)->AddAttributes(new plGameObjectReferenceAttribute()),
    PL_ENUM_ACCESSOR_PROPERTY("AnchorA", plWaterAttachmentAnchor, GetAnchorA, SetAnchorA),
    PL_ENUM_ACCESSOR_PROPERTY("AnchorB", plWaterAttachmentAnchor, GetAnchorB, SetAnchorB),
    PL_ACCESSOR_PROPERTY("BlendDistance", GetBlendDistance, SetBlendDistance)->AddAttributes(new plDefaultValueAttribute(5.0f), new plClampValueAttribute(0.0f, 100000.0f)),
    PL_ACCESSOR_PROPERTY("AutoBlendDistance", GetAutoBlendDistance, SetAutoBlendDistance)->AddAttributes(new plDefaultValueAttribute(true)),
    PL_ACCESSOR_PROPERTY("FrontBreakup", GetFrontBreakup, SetFrontBreakup)->AddAttributes(new plDefaultValueAttribute(0.5f), new plClampValueAttribute(0.0f, 1.0f)),
    PL_ACCESSOR_PROPERTY("AutoSnap", GetAutoSnap, SetAutoSnap)->AddAttributes(new plDefaultValueAttribute(true)),
    PL_ACCESSOR_PROPERTY("SnapRadius", GetSnapRadius, SetSnapRadius)->AddAttributes(new plDefaultValueAttribute(5.0f), new plClampValueAttribute(0.0f, 100000.0f)),
    PL_ACCESSOR_PROPERTY("ElevationTolerance", GetElevationTolerance, SetElevationTolerance)->AddAttributes(new plDefaultValueAttribute(0.25f), new plClampValueAttribute(0.0f, 100000.0f)),
  }
  PL_END_PROPERTIES;
  PL_BEGIN_MESSAGEHANDLERS
  {
    PL_MESSAGE_HANDLER(plMsgTransformChanged, OnTransformChanged),
  }
  PL_END_MESSAGEHANDLERS;
  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("Rendering/Water"),
    new plShapeIconAlwaysVisibleAttribute(),
    new plSphereManipulatorAttribute("SnapRadius"),
    new plSphereVisualizerAttribute("SnapRadius", plColorScheme::LightUI(plColorScheme::Cyan)),
  }
  PL_END_ATTRIBUTES;
}
PL_END_COMPONENT_TYPE;
// clang-format on

plWaterJunctionComponent::plWaterJunctionComponent() = default;

void plWaterJunctionComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);
  inout_stream.WriteGameObjectHandle(m_hBodyA);
  inout_stream.WriteGameObjectHandle(m_hBodyB);
  auto& stream = inout_stream.GetStream();
  stream << m_AnchorA;
  stream << m_AnchorB;
  stream << m_fBlendDistance;
  stream << m_fSnapRadius;
  stream << m_fElevationTolerance;
  stream << m_bAutoSnap;
  stream << m_bAutoBlendDistance;
  stream << m_fFrontBreakup;
}

void plWaterJunctionComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);
  const plUInt32 uiVersion = inout_stream.GetComponentTypeVersion(GetStaticRTTI());
  m_hBodyA = inout_stream.ReadGameObjectHandle();
  m_hBodyB = inout_stream.ReadGameObjectHandle();
  auto& stream = inout_stream.GetStream();
  stream >> m_AnchorA;
  stream >> m_AnchorB;
  stream >> m_fBlendDistance;
  stream >> m_fSnapRadius;
  stream >> m_fElevationTolerance;
  stream >> m_bAutoSnap;
  stream >> m_bAutoBlendDistance;
  if (uiVersion >= 2)
    stream >> m_fFrontBreakup;
}

void plWaterJunctionComponent::SetBodyAReference(const char* szReference)
{
  const auto resolver = GetWorld()->GetGameObjectReferenceResolver();
  if (resolver.IsValid())
    SetBodyA(resolver(szReference, GetHandle(), "WaterBodyA"));
}

void plWaterJunctionComponent::SetBodyBReference(const char* szReference)
{
  const auto resolver = GetWorld()->GetGameObjectReferenceResolver();
  if (resolver.IsValid())
    SetBodyB(resolver(szReference, GetHandle(), "WaterBodyB"));
}

void plWaterJunctionComponent::SetBodyA(plGameObjectHandle hObject)
{
  m_hBodyA = hObject;
  InvalidateJunction();
}

void plWaterJunctionComponent::SetBodyB(plGameObjectHandle hObject)
{
  m_hBodyB = hObject;
  InvalidateJunction();
}

void plWaterJunctionComponent::SetBlendDistance(float fDistance)
{
  m_fBlendDistance = plMath::Max(fDistance, 0.0f);
  InvalidateJunction();
}

void plWaterJunctionComponent::SetFrontBreakup(float fBreakup)
{
  m_fFrontBreakup = plMath::Saturate(fBreakup);
  InvalidateJunction();
}

void plWaterJunctionComponent::SetSnapRadius(float fDistance)
{
  m_fSnapRadius = plMath::Max(fDistance, 0.0f);
  InvalidateJunction();
}

void plWaterJunctionComponent::SetElevationTolerance(float fTolerance)
{
  m_fElevationTolerance = plMath::Max(fTolerance, 0.0f);
  InvalidateJunction();
}

void plWaterJunctionComponent::SetAutoSnap(bool bAutoSnap)
{
  m_bAutoSnap = bAutoSnap;
  InvalidateJunction();
}

void plWaterJunctionComponent::SetAutoBlendDistance(bool bAuto)
{
  m_bAutoBlendDistance = bAuto;
  InvalidateJunction();
}

void plWaterJunctionComponent::SetAnchorA(plEnum<plWaterAttachmentAnchor> anchor)
{
  m_AnchorA = anchor;
  InvalidateJunction();
}

void plWaterJunctionComponent::SetAnchorB(plEnum<plWaterAttachmentAnchor> anchor)
{
  m_AnchorB = anchor;
  InvalidateJunction();
}

void plWaterJunctionComponent::InvalidateJunction()
{
  if (!IsActiveAndInitialized())
    return;

  plWaterWorldModule* pModule = GetWorld()->GetOrCreateModule<plWaterWorldModule>();
  auto RefreshBodyBounds = [&]()
  {
    plGameObject* pBodyObject = nullptr;
    if (GetWorld()->TryGetObject(m_hBodyA, pBodyObject))
      pBodyObject->UpdateLocalBounds();
    if (GetWorld()->TryGetObject(m_hBodyB, pBodyObject))
      pBodyObject->UpdateLocalBounds();
  };
  plWaterBodyId bodyA;
  plWaterBodyId bodyB;
  plWaterBodySnapshot snapshotA;
  plWaterBodySnapshot snapshotB;
  plVec3 anchorA;
  plVec3 anchorB;
  float fWidthA = 0.0f;
  float fWidthB = 0.0f;
  const plVec3 reference = GetOwner()->GetGlobalPosition();

  const bool bValid = pModule->TryGetBodyId(m_hBodyA, bodyA) && pModule->TryGetBodyId(m_hBodyB, bodyB) && bodyA != bodyB &&
                      pModule->TryGetBodySnapshot(bodyA, snapshotA) && pModule->TryGetBodySnapshot(bodyB, snapshotB) &&
                      ResolveAttachment(snapshotA, m_AnchorA, reference, anchorA, fWidthA) &&
                      ResolveAttachment(snapshotB, m_AnchorB, reference, anchorB, fWidthB);

  if (!bValid || (m_bAutoSnap && (anchorA - anchorB).GetLengthSquared() > plMath::Square(m_fSnapRadius)))
  {
    pModule->UnregisterJunction(m_uiJunctionIndex);
    RefreshBodyBounds();
    return;
  }

  plWaterJunctionDescriptor descriptor;
  descriptor.m_hOwnerObject = GetOwner()->GetHandle();
  descriptor.m_BodyA = bodyA;
  descriptor.m_BodyB = bodyB;
  descriptor.m_vAnchorA = anchorA;
  descriptor.m_vAnchorB = anchorB;
  descriptor.m_vCenter = (anchorA + anchorB) * 0.5f;
  descriptor.m_vBlendDirection = anchorB - anchorA;
  if (descriptor.m_vBlendDirection.NormalizeIfNotZero(plVec3::MakeAxisX()).Failed())
    descriptor.m_vBlendDirection = plVec3::MakeAxisX();
  descriptor.m_uiAttachmentA = m_AnchorA.GetValue();
  descriptor.m_uiAttachmentB = m_AnchorB.GetValue();
  descriptor.m_fHeightTolerance = m_fElevationTolerance;
  descriptor.m_fFrontBreakup = m_fFrontBreakup;
  const float fRiverWidth = snapshotA.m_Descriptor.m_Type == plWaterBodyType::River
                              ? fWidthA
                              : (snapshotB.m_Descriptor.m_Type == plWaterBodyType::River ? fWidthB : 0.0f);
  descriptor.m_fBlendDistance = m_bAutoBlendDistance || m_fBlendDistance <= 0.0f
                                  ? plMath::Max(5.0f, fRiverWidth)
                                  : plMath::Max(m_fBlendDistance, 0.01f);

  if (m_uiJunctionIndex == plInvalidIndex)
    m_uiJunctionIndex = pModule->RegisterJunction(descriptor);
  else if (!pModule->UpdateJunction(m_uiJunctionIndex, descriptor))
    m_uiJunctionIndex = pModule->RegisterJunction(descriptor);

  RefreshBodyBounds();
}

void plWaterJunctionComponent::OnActivated()
{
  SUPER::OnActivated();
  GetOwner()->EnableStaticTransformChangesNotifications();
  InvalidateJunction();
}

void plWaterJunctionComponent::OnDeactivated()
{
  if (plWaterWorldModule* pModule = GetWorld()->GetModule<plWaterWorldModule>())
    pModule->UnregisterJunction(m_uiJunctionIndex);
  SUPER::OnDeactivated();
}

void plWaterJunctionComponent::OnTransformChanged(plMsgTransformChanged& msg)
{
  PL_IGNORE_UNUSED(msg);
  InvalidateJunction();
}

PL_STATICLINK_FILE(WaterPlugin, WaterPlugin_Components_Implementation_WaterJunctionComponent);
