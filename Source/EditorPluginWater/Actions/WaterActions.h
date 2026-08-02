#pragma once

#include <EditorPluginWater/EditorPluginWaterDLL.h>
#include <GuiFoundation/Action/BaseActions.h>

class plPreferences;

class PL_EDITORPLUGINWATER_DLL plWaterActions
{
public:
  static void RegisterActions();
  static void UnregisterActions();
  static void MapMenuActions(plStringView sMapping);
  static void MapToolbarActions(plStringView sMapping);

  static plActionDescriptorHandle s_hCategory;
  static plActionDescriptorHandle s_hMenu;
  static plActionDescriptorHandle s_hCreateCategory;
  static plActionDescriptorHandle s_hToolsCategory;
  static plActionDescriptorHandle s_hDebugMenu;
  static plActionDescriptorHandle s_hCreateOcean;
  static plActionDescriptorHandle s_hCreateRiver;
  static plActionDescriptorHandle s_hCreateLake;
  static plActionDescriptorHandle s_hCreateJunction;
  static plActionDescriptorHandle s_hCreateExclusion;
  static plActionDescriptorHandle s_hResnapJunction;
  static plActionDescriptorHandle s_hRepairRiverMetadata;
  static plActionDescriptorHandle s_hConvertLegacy;
  static plActionDescriptorHandle s_hConvertPipeline;
  static plActionDescriptorHandle s_hValidate;
  static plActionDescriptorHandle s_hDebugBodies;
  static plActionDescriptorHandle s_hDebugLod;
  static plActionDescriptorHandle s_hDebugFlow;
  static plActionDescriptorHandle s_hDebugJunctions;
  static plActionDescriptorHandle s_hDebugInteractions;
  static plActionDescriptorHandle s_hDebugUnderwater;
};

class PL_EDITORPLUGINWATER_DLL plWaterAction : public plButtonAction
{
  PL_ADD_DYNAMIC_REFLECTION(plWaterAction, plButtonAction);

public:
  enum class ActionType
  {
    CreateOcean,
    CreateRiver,
    CreateLake,
    CreateJunction,
    CreateExclusion,
    ResnapJunction,
    RepairRiverMetadata,
    ConvertLegacy,
    ConvertPipeline,
    Validate,
    DebugBodies,
    DebugLod,
    DebugFlow,
    DebugJunctions,
    DebugInteractions,
    DebugUnderwater,
  };

  plWaterAction(const plActionContext& context, const char* szName, ActionType type);
  ~plWaterAction();

  virtual void Execute(const plVariant& value) override;

private:
  bool IsDebugAction() const;
  void OnPreferenceChange(plPreferences* pPreferences);
  void UpdateDebugState();

  ActionType m_Type;
};
