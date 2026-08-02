#pragma once

#include <EditorFramework/Preferences/Preferences.h>
#include <EditorPluginWater/EditorPluginWaterDLL.h>

/// Per-document (per-user) state of the Water Profile editor window: which preset the profile derives from
/// and how the preview was configured. Restored when the asset is reopened.
class PL_EDITORPLUGINWATER_DLL plWaterProfilePreferencesUser : public plPreferences
{
  PL_ADD_DYNAMIC_REFLECTION(plWaterProfilePreferencesUser, plPreferences);

public:
  plWaterProfilePreferencesUser();

  void SetAppliedPreset(plStringView sName);
  plStringView GetAppliedPreset() const { return m_sAppliedPreset; }

  void SetBodyMode(plInt32 iMode);
  plInt32 GetBodyMode() const { return m_iBodyMode; }

  void SetScene(plInt32 iScene);
  plInt32 GetScene() const { return m_iScene; }

  void SetQuality(plInt32 iQuality);
  plInt32 GetQuality() const { return m_iQuality; }

  void SetSunElevation(plInt32 iDegrees);
  plInt32 GetSunElevation() const { return m_iSunElevation; }

private:
  plString m_sAppliedPreset;
  plInt32 m_iBodyMode = 0;
  plInt32 m_iScene = 0; ///< 0 = Auto (follows the body type)
  plInt32 m_iQuality = 2;
  plInt32 m_iSunElevation = 35;
};
