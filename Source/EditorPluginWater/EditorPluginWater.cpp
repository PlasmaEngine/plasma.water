#include <EditorPluginWater/EditorPluginWaterPCH.h>

#include <EditorFramework/Actions/AssetActions.h>
#include <EditorFramework/Actions/ProjectActions.h>
#include <EditorPluginWater/Actions/WaterActions.h>
#include <EditorPluginWater/Preferences/WaterEditorPreferences.h>
#include <EditorPluginWater/WaterProfileAsset/WaterProfileUiServices.h>
#include <EditorPluginWater/WaterProfileAsset/WaterPropertyWidgets.moc.h>
#include <GuiFoundation/Action/ActionMapManager.h>
#include <GuiFoundation/Action/CommandHistoryActions.h>
#include <GuiFoundation/Action/DocumentActions.h>
#include <GuiFoundation/Action/EditActions.h>
#include <GuiFoundation/Action/StandardMenus.h>
#include <GuiFoundation/PropertyGrid/PropertyGridWidget.moc.h>
#include <GuiFoundation/PropertyGrid/PropertyMetaState.h>
#include <WaterPlugin/Resources/WaterProfileResource.h>

namespace
{
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
  plToolsProject::GetSingleton()->s_Events.AddEventHandler(ToolsProjectEventHandler);
  ConfigureWaterProfileAsset();

  plWaterActions::RegisterActions();
  plWaterActions::MapMenuActions("EditorPluginScene_Scene2MenuBar");
  plWaterActions::MapMenuActions("EditorPluginScene_DocumentMenuBar");
  plWaterActions::MapToolbarActions("EditorPluginScene_Scene2ToolBar");

  // Dim water profile properties the previewed body type ignores.
  plPropertyMetaState::GetSingleton()->m_Events.AddEventHandler(plWaterProfileUiServices::PropertyMetaStateEventHandler);

  // Custom value widgets: wave seed reroll + wind direction compass dial.
  plQtPropertyGridWidget::GetFactory().RegisterCreator(plGetStaticRTTI<plWaterSeedWidgetAttribute>(),
    [](const plRTTI* pRtti) -> plQtPropertyWidget* { return new plQtWaterSeedPropertyWidget(); });
  plQtPropertyGridWidget::GetFactory().RegisterCreator(plGetStaticRTTI<plWaterWindDirectionWidgetAttribute>(),
    [](const plRTTI* pRtti) -> plQtPropertyWidget* { return new plQtWaterWindDirectionPropertyWidget(); });
}

PL_PLUGIN_ON_UNLOADED()
{
  plQtPropertyGridWidget::GetFactory().UnregisterCreator(plGetStaticRTTI<plWaterSeedWidgetAttribute>());
  plQtPropertyGridWidget::GetFactory().UnregisterCreator(plGetStaticRTTI<plWaterWindDirectionWidgetAttribute>());
  plPropertyMetaState::GetSingleton()->m_Events.RemoveEventHandler(plWaterProfileUiServices::PropertyMetaStateEventHandler);
  plWaterActions::UnregisterActions();
  plToolsProject::GetSingleton()->s_Events.RemoveEventHandler(ToolsProjectEventHandler);
}
