#pragma once

#include <Core/Messages/EventMessage.h>
#include <Core/World/Component.h>
#include <Core/World/World.h>
#include <GameEngine/Interfaces/WaterWorldModule.h>
#include <WaterPlugin/WaterPluginDLL.h>

/// \brief Sent when an interactor crosses into a water body.
struct PL_WATER_DLL plMsgWaterEnter : public plEventMessage
{
  PL_DECLARE_MESSAGE_TYPE(plMsgWaterEnter, plEventMessage);

  plGameObjectHandle m_hWaterBody;
  plEnum<plWaterBodyType> m_WaterType;
  plUInt64 m_uiNetworkId = 0;
  float m_fImmersion = 0.0f;
  float m_fDepth = 0.0f;
  plVec3 m_vFlow = plVec3::MakeZero();
};

/// \brief Sent at the bounded interactor update rate while an object remains in water.
struct PL_WATER_DLL plMsgWaterStay : public plEventMessage
{
  PL_DECLARE_MESSAGE_TYPE(plMsgWaterStay, plEventMessage);

  plGameObjectHandle m_hWaterBody;
  plEnum<plWaterBodyType> m_WaterType;
  plUInt64 m_uiNetworkId = 0;
  float m_fImmersion = 0.0f;
  float m_fDepth = 0.0f;
  plVec3 m_vFlow = plVec3::MakeZero();
};

/// \brief Sent once with the previous water state when an interactor leaves water.
struct PL_WATER_DLL plMsgWaterExit : public plEventMessage
{
  PL_DECLARE_MESSAGE_TYPE(plMsgWaterExit, plEventMessage);

  plGameObjectHandle m_hWaterBody;
  plEnum<plWaterBodyType> m_WaterType;
  plUInt64 m_uiNetworkId = 0;
  float m_fImmersion = 0.0f;
  float m_fDepth = 0.0f;
  plVec3 m_vFlow = plVec3::MakeZero();
};

class PL_WATER_DLL plWaterInteractorComponentManager : public plComponentManager<class plWaterInteractorComponent, plBlockStorageType::Compact>
{
public:
  using SUPER = plComponentManager<plWaterInteractorComponent, plBlockStorageType::Compact>;

  plWaterInteractorComponentManager(plWorld* pWorld);
  virtual void Initialize() override;

private:
  void Update(const plWorldModule::UpdateContext& context);

  plUInt64 m_uiLastUpdateTick = 0;
  bool m_bHasLastUpdateTick = false;
};

/// \brief Emits deterministic splash/wake events from owner movement; GPU ripples remain cosmetic.
class PL_WATER_DLL plWaterInteractorComponent : public plComponent
{
  PL_DECLARE_COMPONENT_TYPE(plWaterInteractorComponent, plComponent, plWaterInteractorComponentManager);

public:
  plWaterInteractorComponent();

  virtual void SerializeComponent(plWorldWriter& inout_stream) const override;
  virtual void DeserializeComponent(plWorldReader& inout_stream) override;

  float GetRadius() const { return m_fRadius; }
  void SetRadius(float fRadius);
  float GetStrength() const { return m_fStrength; }
  void SetStrength(float fStrength);
  float GetMinimumSpeed() const { return m_fMinimumSpeed; }
  void SetMinimumSpeed(float fSpeed);
  plEnum<plWaterInteractionType> GetInteractionType() const { return m_InteractionType; }
  void SetInteractionType(plEnum<plWaterInteractionType> type) { m_InteractionType = type; }

  bool GetIsInWater() const { return m_bIsInWater; }
  float GetCurrentImmersion() const { return m_fCurrentImmersion; }
  float GetCurrentDepth() const { return m_fCurrentDepth; }
  const plVec3& GetCurrentFlow() const { return m_vCurrentFlow; }
  plUInt64 GetActiveBodyId() const;
  plUInt64 GetActiveNetworkId() const { return m_uiActiveNetworkId; }
  plGameObjectHandle GetActiveWaterBody() const { return m_hActiveWaterBody; }

private:
  friend class plWaterInteractorComponentManager;
  void UpdateInteraction(plTime deltaTime);

protected:
  virtual void OnActivated() override;
  virtual void OnDeactivated() override;

private:

  float m_fRadius = 0.5f;
  float m_fStrength = 1.0f;
  float m_fMinimumSpeed = 0.1f;
  plEnum<plWaterInteractionType> m_InteractionType;

  plEventMessageSender<plMsgWaterEnter> m_EnterEventSender;
  plEventMessageSender<plMsgWaterStay> m_StayEventSender;
  plEventMessageSender<plMsgWaterExit> m_ExitEventSender;

  plVec3 m_vLastPosition = plVec3::MakeZero();
  plVec3 m_vCurrentFlow = plVec3::MakeZero();
  plWaterBodyId m_ActiveBodyId;
  plGameObjectHandle m_hActiveWaterBody;
  plEnum<plWaterBodyType> m_ActiveWaterType;
  plUInt64 m_uiActiveNetworkId = 0;
  float m_fCurrentImmersion = 0.0f;
  float m_fCurrentDepth = 0.0f;
  plUInt32 m_uiSourceSequence = 0;
  bool m_bHasLastPosition = false;
  bool m_bIsInWater = false;
};
