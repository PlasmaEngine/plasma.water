#include <WaterPlugin/WaterPluginPCH.h>

#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>
#include <WaterPlugin/Components/WaterInteractorComponent.h>
#include <WaterPlugin/World/WaterWorldModule.h>

namespace
{
  template <typename T>
  void FillWaterEvent(T& out_event, const plWaterSurfaceResult& result, float fImmersion)
  {
    out_event.m_hWaterBody = result.m_hBodyObject;
    out_event.m_WaterType = result.m_Type;
    out_event.m_uiNetworkId = result.m_uiNetworkId;
    out_event.m_fImmersion = fImmersion;
    out_event.m_fDepth = result.m_fDepth;
    out_event.m_vFlow = result.m_vFlowVelocity;
  }
} // namespace

// clang-format off
PL_IMPLEMENT_MESSAGE_TYPE(plMsgWaterEnter);
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plMsgWaterEnter, 1, plRTTIDefaultAllocator<plMsgWaterEnter>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("WaterBody", m_hWaterBody),
    PL_ENUM_MEMBER_PROPERTY("WaterType", plWaterBodyType, m_WaterType),
    PL_MEMBER_PROPERTY("NetworkId", m_uiNetworkId),
    PL_MEMBER_PROPERTY("Immersion", m_fImmersion),
    PL_MEMBER_PROPERTY("Depth", m_fDepth),
    PL_MEMBER_PROPERTY("Flow", m_vFlow),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_IMPLEMENT_MESSAGE_TYPE(plMsgWaterStay);
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plMsgWaterStay, 1, plRTTIDefaultAllocator<plMsgWaterStay>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("WaterBody", m_hWaterBody),
    PL_ENUM_MEMBER_PROPERTY("WaterType", plWaterBodyType, m_WaterType),
    PL_MEMBER_PROPERTY("NetworkId", m_uiNetworkId),
    PL_MEMBER_PROPERTY("Immersion", m_fImmersion),
    PL_MEMBER_PROPERTY("Depth", m_fDepth),
    PL_MEMBER_PROPERTY("Flow", m_vFlow),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_IMPLEMENT_MESSAGE_TYPE(plMsgWaterExit);
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plMsgWaterExit, 1, plRTTIDefaultAllocator<plMsgWaterExit>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("WaterBody", m_hWaterBody),
    PL_ENUM_MEMBER_PROPERTY("WaterType", plWaterBodyType, m_WaterType),
    PL_MEMBER_PROPERTY("NetworkId", m_uiNetworkId),
    PL_MEMBER_PROPERTY("Immersion", m_fImmersion),
    PL_MEMBER_PROPERTY("Depth", m_fDepth),
    PL_MEMBER_PROPERTY("Flow", m_vFlow),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_COMPONENT_TYPE(plWaterInteractorComponent, 1, plComponentMode::Dynamic)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ACCESSOR_PROPERTY("Radius", GetRadius, SetRadius)->AddAttributes(new plDefaultValueAttribute(0.5f), new plClampValueAttribute(0.01f, 10000.0f)),
    PL_ACCESSOR_PROPERTY("Strength", GetStrength, SetStrength)->AddAttributes(new plDefaultValueAttribute(1.0f), new plClampValueAttribute(0.0f, 10000.0f)),
    PL_ACCESSOR_PROPERTY("MinimumSpeed", GetMinimumSpeed, SetMinimumSpeed)->AddAttributes(new plDefaultValueAttribute(0.1f), new plClampValueAttribute(0.0f, 10000.0f)),
    PL_ENUM_ACCESSOR_PROPERTY("InteractionType", plWaterInteractionType, GetInteractionType, SetInteractionType),
  }
  PL_END_PROPERTIES;
  PL_BEGIN_MESSAGESENDERS
  {
    PL_MESSAGE_SENDER(m_EnterEventSender),
    PL_MESSAGE_SENDER(m_StayEventSender),
    PL_MESSAGE_SENDER(m_ExitEventSender),
  }
  PL_END_MESSAGESENDERS;
  PL_BEGIN_FUNCTIONS
  {
    PL_SCRIPT_FUNCTION_PROPERTY(GetIsInWater),
    PL_SCRIPT_FUNCTION_PROPERTY(GetCurrentImmersion),
    PL_SCRIPT_FUNCTION_PROPERTY(GetCurrentDepth),
    PL_SCRIPT_FUNCTION_PROPERTY(GetCurrentFlow),
    PL_SCRIPT_FUNCTION_PROPERTY(GetActiveBodyId),
    PL_SCRIPT_FUNCTION_PROPERTY(GetActiveNetworkId),
    PL_SCRIPT_FUNCTION_PROPERTY(GetActiveWaterBody),
  }
  PL_END_FUNCTIONS;
  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("Rendering/Water"),
    new plSphereManipulatorAttribute("Radius"),
    new plSphereVisualizerAttribute("Radius", plColorScheme::LightUI(plColorScheme::Cyan)),
  }
  PL_END_ATTRIBUTES;
}
PL_END_COMPONENT_TYPE;
// clang-format on

plWaterInteractorComponentManager::plWaterInteractorComponentManager(plWorld* pWorld)
  : plComponentManager<plWaterInteractorComponent, plBlockStorageType::Compact>(pWorld)
{
}

void plWaterInteractorComponentManager::Initialize()
{
  SUPER::Initialize();

  auto desc = PL_CREATE_MODULE_UPDATE_FUNCTION_DESC(plWaterInteractorComponentManager::Update, this);
  desc.m_Phase = plWorldUpdatePhase::PostTransform;
  desc.m_bOnlyUpdateWhenSimulating = false;
  desc.m_fPriority = 0.0f;
  RegisterUpdateFunction(desc);
}

void plWaterInteractorComponentManager::Update(const plWorldModule::UpdateContext& context)
{
  const plWaterWorldModule* pWaterModule = GetWorld()->GetModuleReadOnly<plWaterWorldModule>();
  if (pWaterModule == nullptr)
    return;

  const plUInt64 uiTick = pWaterModule->GetSimulationTick();
  if (m_bHasLastUpdateTick && uiTick / 2 == m_uiLastUpdateTick / 2)
    return;

  const plUInt64 uiDeltaTicks = m_bHasLastUpdateTick ? plMath::Clamp<plUInt64>(uiTick - m_uiLastUpdateTick, 1, 8) : 2;
  m_uiLastUpdateTick = uiTick;
  m_bHasLastUpdateTick = true;
  const plTime elapsed = plTime::MakeFromSeconds(static_cast<double>(uiDeltaTicks) * pWaterModule->GetFixedSimulationStep().GetSeconds());

  for (auto it = m_ComponentStorage.GetIterator(context.m_uiFirstComponentIndex, context.m_uiComponentCount); it.IsValid(); ++it)
  {
    plWaterInteractorComponent* pComponent = it;
    if (pComponent->IsActiveAndInitialized())
      pComponent->UpdateInteraction(elapsed);
  }
}

plWaterInteractorComponent::plWaterInteractorComponent() = default;

void plWaterInteractorComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);
  auto& stream = inout_stream.GetStream();
  stream << m_fRadius;
  stream << m_fStrength;
  stream << m_fMinimumSpeed;
  stream << m_InteractionType;
}

void plWaterInteractorComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);
  inout_stream.GetComponentTypeVersion(GetStaticRTTI());
  auto& stream = inout_stream.GetStream();
  stream >> m_fRadius;
  stream >> m_fStrength;
  stream >> m_fMinimumSpeed;
  stream >> m_InteractionType;
}

void plWaterInteractorComponent::SetRadius(float fRadius)
{
  m_fRadius = plMath::Max(fRadius, 0.01f);
}

void plWaterInteractorComponent::SetStrength(float fStrength)
{
  m_fStrength = plMath::Max(fStrength, 0.0f);
}

void plWaterInteractorComponent::SetMinimumSpeed(float fSpeed)
{
  m_fMinimumSpeed = plMath::Max(fSpeed, 0.0f);
}

plUInt64 plWaterInteractorComponent::GetActiveBodyId() const
{
  if (!m_ActiveBodyId.IsValid())
    return 0;
  return (static_cast<plUInt64>(m_ActiveBodyId.m_uiGeneration) << 32) | m_ActiveBodyId.m_uiIndex;
}

void plWaterInteractorComponent::OnActivated()
{
  SUPER::OnActivated();
  m_vLastPosition = GetOwner()->GetGlobalPosition();
  m_bHasLastPosition = true;
}

void plWaterInteractorComponent::OnDeactivated()
{
  if (m_bIsInWater)
  {
    plMsgWaterExit exitEvent;
    exitEvent.m_hWaterBody = m_hActiveWaterBody;
    exitEvent.m_WaterType = m_ActiveWaterType;
    exitEvent.m_uiNetworkId = m_uiActiveNetworkId;
    exitEvent.m_fImmersion = m_fCurrentImmersion;
    exitEvent.m_fDepth = m_fCurrentDepth;
    exitEvent.m_vFlow = m_vCurrentFlow;
    m_ExitEventSender.SendEventMessage(exitEvent, this, GetOwner());
  }

  m_bIsInWater = false;
  m_ActiveBodyId.Invalidate();
  m_hActiveWaterBody.Invalidate();
  m_bHasLastPosition = false;
  SUPER::OnDeactivated();
}

void plWaterInteractorComponent::UpdateInteraction(plTime deltaTime)
{
  const plVec3 position = GetOwner()->GetGlobalPosition();
  plVec3 velocity = plVec3::MakeZero();
  if (m_bHasLastPosition && deltaTime.IsPositive())
    velocity = (position - m_vLastPosition) / deltaTime.AsFloatInSeconds();
  m_vLastPosition = position;
  m_bHasLastPosition = true;

  plWaterWorldModule* pModule = GetWorld()->GetModule<plWaterWorldModule>();
  plWaterSurfaceResult result;
  bool bTouchingWater = false;
  float fImmersion = 0.0f;
  if (pModule != nullptr)
  {
    plWaterSurfaceQuery query;
    query.m_vPosition = position;
    query.m_fMaxVerticalDistance = plMath::Max(m_fRadius * 2.0f, 1.0f);
    query.m_BodyTypes.Add(plWaterBodyTypeFlags::Ocean);
    query.m_BodyTypes.Add(plWaterBodyTypeFlags::River);
    query.m_BodyTypes.Add(plWaterBodyTypeFlags::Lake);
    query.m_Flags.Add(plWaterQueryFlags::IncludeWaves);
    query.m_Flags.Add(plWaterQueryFlags::IncludeExclusionVolumes);
    query.m_Flags.Add(plWaterQueryFlags::IncludeJunctions);
    if (pModule->TrySampleSurface(query, result))
    {
      bTouchingWater = result.m_fDepth >= -m_fRadius && (result.m_fDepth <= 0.0f || result.m_bContained);
      fImmersion = bTouchingWater ? plMath::Saturate((result.m_fDepth + m_fRadius) / (2.0f * m_fRadius)) : 0.0f;
    }
  }

  const bool bChangedBody = m_bIsInWater && bTouchingWater && result.m_BodyId != m_ActiveBodyId;
  if (m_bIsInWater && (!bTouchingWater || bChangedBody))
  {
    plMsgWaterExit exitEvent;
    exitEvent.m_hWaterBody = m_hActiveWaterBody;
    exitEvent.m_WaterType = m_ActiveWaterType;
    exitEvent.m_uiNetworkId = m_uiActiveNetworkId;
    exitEvent.m_fImmersion = m_fCurrentImmersion;
    exitEvent.m_fDepth = m_fCurrentDepth;
    exitEvent.m_vFlow = m_vCurrentFlow;
    m_ExitEventSender.SendEventMessage(exitEvent, this, GetOwner());
    m_bIsInWater = false;
  }

  if (bTouchingWater)
  {
    const bool bEntered = !m_bIsInWater;
    m_bIsInWater = true;
    m_ActiveBodyId = result.m_BodyId;
    m_hActiveWaterBody = result.m_hBodyObject;
    m_ActiveWaterType = result.m_Type;
    m_uiActiveNetworkId = result.m_uiNetworkId;
    m_fCurrentImmersion = fImmersion;
    m_fCurrentDepth = result.m_fDepth;
    m_vCurrentFlow = result.m_vFlowVelocity;

    if (bEntered)
    {
      plMsgWaterEnter enterEvent;
      FillWaterEvent(enterEvent, result, fImmersion);
      m_EnterEventSender.SendEventMessage(enterEvent, this, GetOwner());
    }
    else
    {
      plMsgWaterStay stayEvent;
      FillWaterEvent(stayEvent, result, fImmersion);
      m_StayEventSender.SendEventMessage(stayEvent, this, GetOwner());
    }

    const float fSpeed = velocity.GetLength();
    if (pModule != nullptr && (bEntered || fSpeed >= m_fMinimumSpeed) && m_fStrength > 0.0f)
    {
      plWaterInteraction interaction;
      interaction.m_Type = m_InteractionType;
      if (bEntered)
      {
        interaction.m_Type = plWaterInteractionType::Splash;
      }
      else if (m_InteractionType == plWaterInteractionType::Splash)
      {
        // A splash is a DISCRETE event: it fires once per water entry. Movement updates arrive ~30x per second,
        // and emitting a splash ring for each tiles the surface into one solid disc of chop (and no gain knob
        // helps - it scales with the emission rate). While a splash-type interactor moves through water it
        // therefore emits wake events instead, which the sim models as a bounded surface depression.
        interaction.m_Type = plWaterInteractionType::Wake;
      }
      interaction.m_BodyId = result.m_BodyId;
      interaction.m_hSourceObject = GetOwner()->GetHandle();
      interaction.m_vPosition = result.m_vSurfacePosition;
      interaction.m_vDirection = velocity;
      interaction.m_vDirection.NormalizeIfNotZero(plVec3::MakeAxisX()).IgnoreResult();
      interaction.m_fRadius = m_fRadius;
      // Entry splashes are discrete impulses. Movement wakes are a CONTINUOUS energy stream, so their impulse
      // must scale with the covered time slice - otherwise total injected energy scales with emission rate and
      // a persistent emitter (a bobbing physics object never stops moving) pumps the wave field into an
      // ever-growing saturated plateau. Normalized to the manager's 30 Hz cadence (factor 1.0 there).
      const float fTimeSlice = deltaTime.AsFloatInSeconds() * 30.0f;
      interaction.m_fStrength = bEntered ? m_fStrength : m_fStrength * fSpeed * fTimeSlice;
      interaction.m_uiSourceStableId = GetOwner()->GetStableRandomSeed() ^ GetUniqueID();
      interaction.m_uiSourceSequence = ++m_uiSourceSequence;
      interaction.m_uiSimulationTick = result.m_uiSimulationTick;
      pModule->AddInteraction(interaction);
    }
  }
  else
  {
    m_ActiveBodyId.Invalidate();
    m_hActiveWaterBody.Invalidate();
    m_uiActiveNetworkId = 0;
    m_fCurrentImmersion = 0.0f;
    m_fCurrentDepth = 0.0f;
    m_vCurrentFlow.SetZero();
  }
}

PL_STATICLINK_FILE(WaterPlugin, WaterPlugin_Components_Implementation_WaterInteractorComponent);
