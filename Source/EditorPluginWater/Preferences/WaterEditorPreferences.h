#pragma once

#include <EditorFramework/IPC/EngineProcessConnection.h>
#include <EditorFramework/Preferences/Preferences.h>
#include <EditorPluginWater/EditorPluginWaterDLL.h>

/// Project-local visualization switches used by the water authoring tools.
///
/// The values are mirrored to Water runtime CVars without creating a direct
/// dependency on renderer debug implementation details.
class PL_EDITORPLUGINWATER_DLL plWaterEditorPreferences : public plPreferences
{
  PL_ADD_DYNAMIC_REFLECTION(plWaterEditorPreferences, plPreferences);

public:
  plWaterEditorPreferences();
  ~plWaterEditorPreferences();

  void SetShowBodies(bool bValue);
  bool GetShowBodies() const { return m_bShowBodies; }

  void SetShowLod(bool bValue);
  bool GetShowLod() const { return m_bShowLod; }

  void SetShowFlow(bool bValue);
  bool GetShowFlow() const { return m_bShowFlow; }

  void SetShowJunctions(bool bValue);
  bool GetShowJunctions() const { return m_bShowJunctions; }

  void SetShowInteractions(bool bValue);
  bool GetShowInteractions() const { return m_bShowInteractions; }

  void SetShowUnderwaterVolumes(bool bValue);
  bool GetShowUnderwaterVolumes() const { return m_bShowUnderwaterVolumes; }

  void SyncCVars();

private:
  void ProcessEventHandler(const plEditorEngineProcessConnection::Event& e);

  bool m_bShowBodies = true;
  bool m_bShowLod = false;
  bool m_bShowFlow = false;
  bool m_bShowJunctions = true;
  bool m_bShowInteractions = false;
  bool m_bShowUnderwaterVolumes = false;
};
