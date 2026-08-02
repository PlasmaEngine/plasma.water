#include <EditorPluginWater/EditorPluginWaterPCH.h>

#include <EditorPluginWater/Preferences/WaterEditorPreferences.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plWaterEditorPreferences, 1, plRTTIDefaultAllocator<plWaterEditorPreferences>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ACCESSOR_PROPERTY("ShowBodies", GetShowBodies, SetShowBodies)->AddAttributes(new plDefaultValueAttribute(true), new plGroupAttribute("Debug Overlays")),
    PL_ACCESSOR_PROPERTY("ShowLod", GetShowLod, SetShowLod)->AddAttributes(new plDefaultValueAttribute(false), new plGroupAttribute("Debug Overlays")),
    PL_ACCESSOR_PROPERTY("ShowFlow", GetShowFlow, SetShowFlow)->AddAttributes(new plDefaultValueAttribute(false), new plGroupAttribute("Debug Overlays")),
    PL_ACCESSOR_PROPERTY("ShowJunctions", GetShowJunctions, SetShowJunctions)->AddAttributes(new plDefaultValueAttribute(true), new plGroupAttribute("Debug Overlays")),
    PL_ACCESSOR_PROPERTY("ShowInteractions", GetShowInteractions, SetShowInteractions)->AddAttributes(new plDefaultValueAttribute(false), new plGroupAttribute("Debug Overlays")),
    PL_ACCESSOR_PROPERTY("ShowUnderwaterVolumes", GetShowUnderwaterVolumes, SetShowUnderwaterVolumes)->AddAttributes(new plDefaultValueAttribute(false), new plGroupAttribute("Debug Overlays")),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plWaterEditorPreferences::plWaterEditorPreferences()
  : plPreferences(Domain::Project, "Water")
{
  plEditorEngineProcessConnection::s_Events.AddEventHandler(plMakeDelegate(&plWaterEditorPreferences::ProcessEventHandler, this));
}

plWaterEditorPreferences::~plWaterEditorPreferences()
{
  plEditorEngineProcessConnection::s_Events.RemoveEventHandler(plMakeDelegate(&plWaterEditorPreferences::ProcessEventHandler, this));
}

void plWaterEditorPreferences::SetShowBodies(bool bValue)
{
  m_bShowBodies = bValue;
  SyncCVars();
}

void plWaterEditorPreferences::SetShowLod(bool bValue)
{
  m_bShowLod = bValue;
  SyncCVars();
}

void plWaterEditorPreferences::SetShowFlow(bool bValue)
{
  m_bShowFlow = bValue;
  SyncCVars();
}

void plWaterEditorPreferences::SetShowJunctions(bool bValue)
{
  m_bShowJunctions = bValue;
  SyncCVars();
}

void plWaterEditorPreferences::SetShowInteractions(bool bValue)
{
  m_bShowInteractions = bValue;
  SyncCVars();
}

void plWaterEditorPreferences::SetShowUnderwaterVolumes(bool bValue)
{
  m_bShowUnderwaterVolumes = bValue;
  SyncCVars();
}

void plWaterEditorPreferences::SyncCVars()
{
  TriggerPreferencesChangedEvent();

  auto SendBool = [](const char* szName, bool bValue)
  {
    plChangeCVarMsgToEngine msg;
    msg.m_sCVarName = szName;
    msg.m_NewValue = bValue;
    plEditorEngineProcessConnection::GetSingleton()->SendMessage(&msg);
  };

  SendBool("Water.Debug.ShowBodies", m_bShowBodies);
  SendBool("Water.Debug.ShowLod", m_bShowLod);
  SendBool("Water.Debug.ShowFlow", m_bShowFlow);
  SendBool("Water.Debug.ShowJunctions", m_bShowJunctions);
  SendBool("Water.Debug.ShowInteractions", m_bShowInteractions);
  SendBool("Water.Debug.ShowUnderwaterVolumes", m_bShowUnderwaterVolumes);
}

void plWaterEditorPreferences::ProcessEventHandler(const plEditorEngineProcessConnection::Event& e)
{
  if (e.m_Type == plEditorEngineProcessConnection::Event::Type::ProcessRestarted)
  {
    SyncCVars();
  }
}
