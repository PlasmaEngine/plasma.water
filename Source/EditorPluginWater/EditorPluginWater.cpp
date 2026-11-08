#include <EditorPluginWater/EditorPluginWaterPCH.h>

#include <EditorFramework/Actions/AssetActions.h>
#include <EditorFramework/Actions/ProjectActions.h>
#include <EditorPluginWater/Actions/WaterActions.h>
#include <EditorPluginWater/Preferences/WaterEditorPreferences.h>
#include <EditorPluginWater/WaterProfileAsset/WaterProfileShellPanels.h>
#include <EditorPluginWater/WaterProfileAsset/WaterProfileUiServices.h>
#include <EditorUi/Widgets.h>
#include <Foundation/Configuration/Plugin.h>
#include <GuiFoundation/Action/ActionMapManager.h>
#include <GuiFoundation/Action/CommandHistoryActions.h>
#include <GuiFoundation/Action/DocumentActions.h>
#include <GuiFoundation/Action/EditActions.h>
#include <GuiFoundation/Action/StandardMenus.h>
#include <GuiFoundation/PropertyGrid/PropertyMetaState.h>
#include <WaterPlugin/Resources/WaterProfileResource.h>

namespace
{
  plString s_sIconFolder;

  /// The package's editor icons (":/AssetIcons/...", ":/TypeIcons/..." are files under EditorIcons): the plugin binary
  /// sits in <package>/Bin/<config>, both in the source tree and in an installed copy.
  plString GetIconFolder()
  {
    plStringBuilder sBinary, sCopy;
    plPlugin::GetPluginPaths("plEditorPluginWater", sBinary, sCopy, 0);

    plStringBuilder sFolder = sBinary.GetFileDirectory();
    sFolder.PathParentDirectory(2);
    sFolder.AppendPath("EditorIcons");
    return sFolder;
  }

  void ToolsProjectEventHandler(const plToolsProjectEvent& e)
  {
    // Project preferences require the project directory, which is not established
    // while plugin bundles are being loaded during editor startup.
    if (e.m_Type == plToolsProjectEvent::Type::ProjectOpened)
      plPreferences::QueryPreferences<plWaterEditorPreferences>()->SyncCVars();
  }

  static void ConfigureWaterProfileAsset()
  {
    plActionMapManager::RegisterActionMap("WaterProfileAssetMenuBar").IgnoreResult();
    plStandardMenus::MapActions("WaterProfileAssetMenuBar", plStandardMenuTypes::Default | plStandardMenuTypes::Edit);
    plProjectActions::MapActions("WaterProfileAssetMenuBar");
    plDocumentActions::MapMenuActions("WaterProfileAssetMenuBar");
    plAssetActions::MapMenuActions("WaterProfileAssetMenuBar");
    plDocumentActions::MapToolsActions("WaterProfileAssetMenuBar");
    plCommandHistoryActions::MapActions("WaterProfileAssetMenuBar");
    plEditActions::MapActions("WaterProfileAssetMenuBar", false, false);

    plActionMapManager::RegisterActionMap("WaterProfileAssetToolBar").IgnoreResult();
    plDocumentActions::MapToolbarActions("WaterProfileAssetToolBar");
    plCommandHistoryActions::MapActions("WaterProfileAssetToolBar", "");
    plAssetActions::MapToolBarActions("WaterProfileAssetToolBar", true);
  }
} // namespace

PL_PLUGIN_ON_LOADED()
{
  s_sIconFolder = GetIconFolder();
  plEditorUi::AddIconFolder(s_sIconFolder);

  plToolsProject::GetSingleton()->s_Events.AddEventHandler(ToolsProjectEventHandler);
  ConfigureWaterProfileAsset();

  plWaterActions::RegisterActions();
  plWaterActions::MapMenuActions("EditorPluginScene_Scene2MenuBar");
  plWaterActions::MapMenuActions("EditorPluginScene_DocumentMenuBar");
  plWaterActions::MapToolbarActions("EditorPluginScene_Scene2ToolBar");

  // Dim water profile properties the previewed body type ignores.
  plPropertyMetaState::GetSingleton()->m_Events.AddEventHandler(plWaterProfileUiServices::PropertyMetaStateEventHandler);

  // The wave seed reroll and the wind direction dial are built into the editor's property
  // grid (plWaterSeedWidgetAttribute, plWaterWindDirectionWidgetAttribute).

  // the Water Profile window's panels in the engine shell
  plWaterProfileShellPanels::Register();
}

PL_PLUGIN_ON_UNLOADED()
{
  plWaterProfileShellPanels::Unregister();
  plPropertyMetaState::GetSingleton()->m_Events.RemoveEventHandler(plWaterProfileUiServices::PropertyMetaStateEventHandler);
  plWaterActions::UnregisterActions();
  plToolsProject::GetSingleton()->s_Events.RemoveEventHandler(ToolsProjectEventHandler);

  plEditorUi::RemoveIconFolder(s_sIconFolder);
  s_sIconFolder.Clear();
}
