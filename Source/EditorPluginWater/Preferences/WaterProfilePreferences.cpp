#include <EditorPluginWater/EditorPluginWaterPCH.h>

#include <EditorPluginWater/Preferences/WaterProfilePreferences.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plWaterProfilePreferencesUser, 1, plRTTIDefaultAllocator<plWaterProfilePreferencesUser>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("AppliedPreset", m_sAppliedPreset),
    PL_MEMBER_PROPERTY("BodyMode", m_iBodyMode)->AddAttributes(new plDefaultValueAttribute(0), new plClampValueAttribute(0, 2)),
    PL_MEMBER_PROPERTY("Scene", m_iScene)->AddAttributes(new plDefaultValueAttribute(0), new plClampValueAttribute(0, 4)),
    PL_MEMBER_PROPERTY("Quality", m_iQuality)->AddAttributes(new plDefaultValueAttribute(2), new plClampValueAttribute(0, 3)),
    PL_MEMBER_PROPERTY("SunElevation", m_iSunElevation)->AddAttributes(new plDefaultValueAttribute(35), new plClampValueAttribute(5, 85)),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plWaterProfilePreferencesUser::plWaterProfilePreferencesUser()
  : plPreferences(Domain::Document, "WaterProfile")
{
}

void plWaterProfilePreferencesUser::SetAppliedPreset(plStringView sName)
{
  m_sAppliedPreset = sName;
  TriggerPreferencesChangedEvent();
}

void plWaterProfilePreferencesUser::SetBodyMode(plInt32 iMode)
{
  m_iBodyMode = plMath::Clamp(iMode, 0, 2);
  TriggerPreferencesChangedEvent();
}

void plWaterProfilePreferencesUser::SetScene(plInt32 iScene)
{
  m_iScene = plMath::Clamp(iScene, 0, 4);
  TriggerPreferencesChangedEvent();
}

void plWaterProfilePreferencesUser::SetQuality(plInt32 iQuality)
{
  m_iQuality = plMath::Clamp(iQuality, 0, 3);
  TriggerPreferencesChangedEvent();
}

void plWaterProfilePreferencesUser::SetSunElevation(plInt32 iDegrees)
{
  m_iSunElevation = plMath::Clamp(iDegrees, 5, 85);
  TriggerPreferencesChangedEvent();
}
