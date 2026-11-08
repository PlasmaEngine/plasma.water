#include <EditorPluginWater/EditorPluginWaterPCH.h>

#include <EditorPluginWater/InputContexts/WaterInteractContext.h>
#include <EditorPluginWater/Preferences/WaterProfilePreferences.h>
#include <EditorPluginWater/WaterProfileAsset/WaterProfileAsset.h>
#include <EditorPluginWater/WaterProfileAsset/WaterProfileAssetWindow.h>
#include <EditorPluginWater/WaterProfileAsset/WaterProfileEngineUi.h>
#include <EditorPluginWater/WaterProfileAsset/WaterProfileShellPanels.h>
#include <EditorPluginWater/WaterProfileAsset/WaterProfileUiServices.h>

#include <EditorFramework/Assets/AssetCurator.h>
#include <EditorFramework/DocumentWindow/OrbitCamViewWidget.h>
#include <EditorFramework/InputContexts/EditorInputContext.h>
#include <EditorFramework/IPC/EngineProcessConnection.h>
#include <Foundation/IO/MemoryStream.h>
#include <Foundation/Utilities/AssetFileHeader.h>
#include <Foundation/Utilities/ConversionUtils.h>
#include <GuiFoundation/EngineUi/EnginePrompt.h>

namespace
{
  /// A value of a preset diff, short enough for one line of the diff card.
  plString FormatVariantShort(const plVariant& value)
  {
    plStringBuilder sText;

    if (value.IsA<plColor>())
    {
      const plColorGammaUB gamma = value.Get<plColor>();
      sText.SetFormat("#{}{}{}", plArgU(gamma.r, 2, true, 16), plArgU(gamma.g, 2, true, 16), plArgU(gamma.b, 2, true, 16));
    }
    else if (value.IsA<plVec2>())
    {
      const plVec2 v = value.Get<plVec2>();
      sText.SetFormat("{}, {}", plArgF(v.x, 2), plArgF(v.y, 2));
    }
    else if (value.IsA<plVec3>())
    {
      const plVec3 v = value.Get<plVec3>();
      sText.SetFormat("{}, {}, {}", plArgF(v.x, 3), plArgF(v.y, 3), plArgF(v.z, 3));
    }
    else if (value.IsA<float>() || value.IsA<double>())
      sText.SetFormat("{}", plArgF(value.ConvertTo<double>(), 3));
    else if (value.IsA<bool>())
      sText = value.Get<bool>() ? "on" : "off";
    else if (value.IsNumber())
      sText.SetFormat("{}", value.ConvertTo<plInt64>());
    else
      plConversionUtils::ToString(value, sText);

    return sText;
  }

  const char* GetBodyTypeName(plWaterProfilePreset::BodyType bodyType)
  {
    switch (bodyType)
    {
      case plWaterProfilePreset::BodyType::River:
        return "River";
      case plWaterProfilePreset::BodyType::Lake:
        return "Lake";
      default:
        return "Ocean";
    }
  }
} // namespace

// //////////////////////////////////////////////////////////////////////
// plWaterProfileAssetDocumentWindow
// //////////////////////////////////////////////////////////////////////

plWaterProfileAssetDocumentWindow::plWaterProfileAssetDocumentWindow(plDocument* pDocument)
  : plEngineDocumentWindow(static_cast<plAssetDocument*>(pDocument))
{
  GetDocument()->GetObjectManager()->m_PropertyEvents.AddEventHandler(plMakeDelegate(&plWaterProfileAssetDocumentWindow::PropertyEventHandler, this));

  // the per-document preview configuration
  auto* pPreferences = plPreferences::QueryPreferences<plWaterProfilePreferencesUser>(pDocument);
  m_iBodyMode = plMath::Clamp(pPreferences->GetBodyMode(), 0, 2);
  m_iScene = plMath::Clamp(pPreferences->GetScene(), 0, 4);
  m_iQuality = plMath::Clamp(pPreferences->GetQuality(), 0, static_cast<int>(plWaterProfileUiServices::QualityTierCount) - 1);
  m_iSunElevation = plMath::Clamp(pPreferences->GetSunElevation(), 5, 85);
  plWaterProfileUiServices::SetPreviewBodyType(pDocument, m_iBodyMode);

  SetMenuBarActions("WaterProfileAssetMenuBar");
  SetToolBarActions("WaterProfileAssetToolBar");

  // Presets are kept in a regular panel instead of a bespoke floating window so the editor remains consistent with the
  // rest of Plasma's asset tools and users can rearrange the workspace. The shell arranges the panels
  // (plWaterProfileShellPanels).
  CreatePresets();
  CreatePreviewControls();
  CreatePreview();
  CreatePreviewStatus();

  m_pProperties = AddPanel<plWaterProfileProperties>({"Properties", "Water Profile Properties", plEditorUiDockRole::Right}, static_cast<plAssetDocument*>(pDocument), this);
  pDocument->GetSelectionManager()->SetSelection(pDocument->GetObjectManager()->GetRootObject()->GetChildren()[0]);

  CreateDiagnostics();

  // Apply the restored preview configuration to the controls and the engine.
  m_pSchematic->SetQuality(m_iQuality);
  SyncPreviewControls();
  SendRuntimePreviewSettings();

  RebuildPresetList();
  SetBodyMode(m_iBodyMode);

  UpdatePreview();
}

void plWaterProfileAssetDocumentWindow::CreatePresets()
{
  m_pPresets = AddPanel<plWaterPresetPanel>({"Presets", "Water Profiles", plEditorUiDockRole::Left});

  m_pPresets->m_OnActivate = [this](plInt32 iPreset)
  { BeginApplyPreset(iPreset); };
  m_pPresets->m_OnApplyPreset = [this]()
  {
    const plInt32 iPreset = GetSelectedPresetIndex();
    if (iPreset >= 0)
      BeginApplyPreset(iPreset);
  };
  m_pPresets->m_OnSaveAs = [this]()
  { OnSaveAsPreset(); };
  m_pPresets->m_OnReset = [this]()
  { BeginApplyPreset(0); };
  m_pPresets->m_OnApplyPending = [this](bool bKeepColors)
  { ApplyPendingPreset(bKeepColors); };
  m_pPresets->m_OnCancelPending = [this]()
  { CancelPendingPreset(); };
}

void plWaterProfileAssetDocumentWindow::CreatePreviewControls()
{
  // one row of controls across the document, under its toolbar
  m_pControls = AddRow<plWaterPreviewControls>({"PreviewControls", "Preview Controls"}, plWaterPreviewControls::OneRowHeight);

  plHybridArray<plString, 4> qualityNames;
  for (plUInt32 i = 0; i < plWaterProfileUiServices::QualityTierCount; ++i)
  {
    const auto& tier = plWaterProfileUiServices::s_QualityTiers[i];
    plStringBuilder name;
    if (tier.m_uiMaxCascades == 0)
      name.SetFormat("{} \xC2\xB7 CPU only", tier.m_szName);
    else
      name.SetFormat("{} \xC2\xB7 {} casc \xC2\xB7 {}\xC2\xB2", tier.m_szName, tier.m_uiMaxCascades, tier.m_uiResolution);
    qualityNames.PushBack(name);
  }
  m_pControls->SetQualityNames(qualityNames);

  m_pControls->m_OnChanged = [this](plWaterPreviewControls::Control control, plInt32 iValue)
  {
    using Control = plWaterPreviewControls::Control;
    switch (control)
    {
      case Control::Body:
        SetBodyMode(iValue);
        break;
      case Control::Scene:
        SetScene(iValue);
        break;
      case Control::View:
        SetView(iValue);
        break;
      case Control::Quality:
        SetQuality(iValue);
        break;
      case Control::Freeze:
        SetFrozen(iValue != 0);
        break;
      case Control::Interact:
        SetInteract(iValue != 0);
        break;
      case Control::Cascades:
        SetShowCascades(iValue != 0);
        break;
      case Control::Direction:
        SetShowDirection(iValue != 0);
        break;
      case Control::Bookmark:
        ApplyCameraBookmark(iValue);
        break;
    }
  };
}

void plWaterProfileAssetDocumentWindow::CreatePreview()
{
  // the 2D Surface schematic, a tab beside the 3D preview
  m_pSchematic = AddPanel<plWaterSchematicView>({"Schematic", "2D Surface"});

  m_ViewConfig.m_Camera.LookAt(plVec3(-12.0f, -12.0f, 8.0f), plVec3::MakeZero(), plVec3(0, 0, 1));
  m_ViewConfig.ApplyPerspectiveSetting(55.0f, 0.05f, 500.0f);
  m_pViewWidget = new plOrbitCamViewWidget(this, &m_ViewConfig);
  m_pViewWidget->ConfigureFixed(plVec3::MakeZero(), plVec3::MakeZero(), plVec3(-12.0f, -12.0f, 8.0f));
  AddViewWidget(m_pViewWidget);

  // Interact mode: must sit at index 0 so it wins the left mouse button over the orbit-camera context
  // while enabled; disabled it passes everything through.
  m_pInteractContext = PL_DEFAULT_NEW(plWaterInteractContext, this, m_pViewWidget);
  m_pInteractContext->m_SendInteraction = plMakeDelegate(&plWaterProfileAssetDocumentWindow::SendWaterInteraction, this);
  m_pViewWidget->m_InputContexts.InsertAt(0, m_pInteractContext.Borrow());
}

void plWaterProfileAssetDocumentWindow::CreatePreviewStatus()
{
  // Sun elevation, status chips (they double as navigation) and the depth ramp under the preview.
  m_pStatus = AddPanel<plWaterPreviewStatus>({"PreviewStatus", "Preview Status", plEditorUiDockRole::Bottom});
  m_pStatus->m_OnSunChanged = [this](plInt32 iDegrees)
  { SetSunElevation(iDegrees); };
  m_pStatus->m_OnChipClicked = [this](plUInt32 uiChip)
  { OnStatusChipClicked(uiChip); };
}

void plWaterProfileAssetDocumentWindow::CreateDiagnostics()
{
  m_pDiagnostics = AddPanel<plWaterDiagnostics>({"Diagnostics", "Water Diagnostics", plEditorUiDockRole::Bottom});
  m_pDiagnostics->m_OnFilterProperties = [this](plStringView sProperty)
  { SetPropertyFilter(sProperty); };
  m_pDiagnostics->m_OnTierPicked = [this](plUInt32 uiTier)
  { SetQuality(static_cast<plInt32>(uiTier)); };
}

plWaterProfileAssetDocumentWindow::~plWaterProfileAssetDocumentWindow()
{
  // the panels' callbacks use this window's members
  DestroyPanels();

  // The context is destroyed with this window, but the view widget is destroyed later by the base class -
  // unregister first so the view never dispatches into a dangling pointer.
  if (m_pInteractContext != nullptr)
  {
    m_pInteractContext->SetEnabled(false);
    m_pViewWidget->m_InputContexts.RemoveAndCopy(m_pInteractContext.Borrow());
    m_pInteractContext.Clear();
  }

  plWaterProfileUiServices::ClearPreviewBodyType(GetDocument());
  GetDocument()->GetObjectManager()->m_PropertyEvents.RemoveEventHandler(plMakeDelegate(&plWaterProfileAssetDocumentWindow::PropertyEventHandler, this));
  RestoreResource();
}

void plWaterProfileAssetDocumentWindow::InternalRedraw()
{
  plEditorInputContext::UpdateActiveInputContext();
  SendBodyTypeToEngine();
  SendRuntimePreviewSettings();
  SendRedrawMsg();
  plEngineDocumentWindow::InternalRedraw();
}

void plWaterProfileAssetDocumentWindow::SendRedrawMsg()
{
  if (plEditorEngineProcessConnection::GetSingleton()->IsProcessCrashed())
    return;

  for (plEngineViewWidget* pView : m_ViewWidgets)
  {
    pView->SetEnablePicking(false);
    pView->UpdateCameraInterpolation();
    pView->SyncToEngine();
  }
}

void plWaterProfileAssetDocumentWindow::SendBodyTypeToEngine()
{
  if (plEditorEngineProcessConnection::GetSingleton()->IsProcessCrashed())
    return;

  plDocumentConfigMsgToEngine msg;
  msg.m_sWhatToDo = "SetWaterBodyType";
  msg.m_iValue = m_iBodyMode;
  GetEditorEngineConnection()->SendMessage(&msg);
}

void plWaterProfileAssetDocumentWindow::SendRuntimePreviewSettings()
{
  if (plEditorEngineProcessConnection::GetSingleton()->IsProcessCrashed())
    return;

  plDocumentConfigMsgToEngine qualityMsg;
  qualityMsg.m_sWhatToDo = "SetWaterQuality";
  qualityMsg.m_iValue = m_iQuality;
  GetEditorEngineConnection()->SendMessage(&qualityMsg);

  plDocumentConfigMsgToEngine freezeMsg;
  freezeMsg.m_sWhatToDo = "SetWaterFrozen";
  freezeMsg.m_iValue = m_bFrozen ? 1 : 0;
  GetEditorEngineConnection()->SendMessage(&freezeMsg);

  plDocumentConfigMsgToEngine sceneMsg;
  sceneMsg.m_sWhatToDo = "SetWaterScene";
  sceneMsg.m_iValue = GetEffectiveScene();
  GetEditorEngineConnection()->SendMessage(&sceneMsg);

  plDocumentConfigMsgToEngine sunMsg;
  sunMsg.m_sWhatToDo = "SetWaterSunElevation";
  sunMsg.m_iValue = m_iSunElevation;
  GetEditorEngineConnection()->SendMessage(&sunMsg);
}

void plWaterProfileAssetDocumentWindow::SendSceneToEngine()
{
  if (plEditorEngineProcessConnection::GetSingleton()->IsProcessCrashed())
    return;

  plDocumentConfigMsgToEngine sceneMsg;
  sceneMsg.m_sWhatToDo = "SetWaterScene";
  sceneMsg.m_iValue = GetEffectiveScene();
  GetEditorEngineConnection()->SendMessage(&sceneMsg);

  plDocumentConfigMsgToEngine sunMsg;
  sunMsg.m_sWhatToDo = "SetWaterSunElevation";
  sunMsg.m_iValue = m_iSunElevation;
  GetEditorEngineConnection()->SendMessage(&sunMsg);
}

void plWaterProfileAssetDocumentWindow::SendWaterInteraction(const plVec3& vPosition, bool bContinuous)
{
  if (plEditorEngineProcessConnection::GetSingleton()->IsProcessCrashed())
    return;

  plLog::Info("WaterInteract (editor): picked ({0}, {1}, {2}) {3}",
    plArgF(vPosition.x, 2), plArgF(vPosition.y, 2), plArgF(vPosition.z, 2), bContinuous ? "continuous" : "splash");

  plSimpleDocumentConfigMsgToEngine msg;
  msg.m_sWhatToDo = "WaterInteract";
  msg.m_sPayload = bContinuous ? "Continuous" : "Splash";
  msg.m_PayloadValue = vPosition;
  GetEditorEngineConnection()->SendMessage(&msg);
}

plInt32 plWaterProfileAssetDocumentWindow::GetEffectiveScene() const
{
  if (m_iScene > 0)
    return m_iScene - 1; // choices after "Auto" map 1:1 to the engine scene ids

  // Auto: pick the scene that shows the most profile behavior for the body type.
  switch (m_iBodyMode)
  {
    case 1:
      return 2; // river bend
    case 2:
      return 3; // lake bowl
    default:
      return 1; // shoreline: shore fades, shore foam and the depth ramp are all visible
  }
}

void plWaterProfileAssetDocumentWindow::SetBodyMode(plInt32 iMode)
{
  m_iBodyMode = plMath::Clamp(iMode, 0, 2);
  m_pSchematic->SetBodyMode(m_iBodyMode);
  SendBodyTypeToEngine();
  SendSceneToEngine();

  plPreferences::QueryPreferences<plWaterProfilePreferencesUser>(GetDocument())->SetBodyMode(m_iBodyMode);
  plWaterProfileUiServices::SetPreviewBodyType(GetDocument(), m_iBodyMode);

  const char* szBodyName = m_iBodyMode == 1 ? "River" : (m_iBodyMode == 2 ? "Lake" : "Ocean");
  plStringBuilder sContext;
  sContext.SetFormat("Dimmed rows are ignored by {} bodies (follows the preview body type).", szBodyName);
  m_pProperties->GetFilter().SetContextText(sContext);

  // The meta state is queried when the grid rebuilds; re-setting the selection forces that rebuild.
  auto* pSelectionManager = GetDocument()->GetSelectionManager();
  const plDocumentObject* pObject = GetDocument()->GetObjectManager()->GetRootObject()->GetChildren()[0];
  pSelectionManager->Clear();
  pSelectionManager->SetSelection(pObject);

  SyncPreviewControls();
}

void plWaterProfileAssetDocumentWindow::SetScene(plInt32 iScene)
{
  m_iScene = plMath::Clamp(iScene, 0, 4);
  plPreferences::QueryPreferences<plWaterProfilePreferencesUser>(GetDocument())->SetScene(m_iScene);
  SendSceneToEngine();
  SyncPreviewControls();
}

void plWaterProfileAssetDocumentWindow::SetQuality(plInt32 iQuality)
{
  m_iQuality = plMath::Clamp(iQuality, 0, static_cast<int>(plWaterProfileUiServices::QualityTierCount) - 1);
  plPreferences::QueryPreferences<plWaterProfilePreferencesUser>(GetDocument())->SetQuality(m_iQuality);
  m_pSchematic->SetQuality(m_iQuality);
  SendRuntimePreviewSettings();
  SyncPreviewControls();
  UpdatePreview();
}

void plWaterProfileAssetDocumentWindow::SetView(plInt32 iView)
{
  m_iView = plMath::Clamp(iView, 0, 1);
  plWaterProfileShellPanels::ShowView(GetDocument(), m_iView);
  SyncPreviewControls();
}

void plWaterProfileAssetDocumentWindow::SetSunElevation(plInt32 iDegrees)
{
  m_iSunElevation = plMath::Clamp(iDegrees, 5, 85);
  plPreferences::QueryPreferences<plWaterProfilePreferencesUser>(GetDocument())->SetSunElevation(m_iSunElevation);
  SendSceneToEngine();
  SyncPreviewControls();
}

void plWaterProfileAssetDocumentWindow::SetFrozen(bool bFrozen)
{
  m_bFrozen = bFrozen;
  m_pSchematic->SetFrozen(m_bFrozen);
  SendRuntimePreviewSettings();
  SyncPreviewControls();
}

void plWaterProfileAssetDocumentWindow::SetInteract(bool bInteract)
{
  m_bInteract = bInteract;
  m_pInteractContext->SetEnabled(m_bInteract);
  SyncPreviewControls();
}

void plWaterProfileAssetDocumentWindow::SetShowCascades(bool bShow)
{
  m_bShowCascades = bShow;
  m_pSchematic->SetShowCascades(m_bShowCascades);
  SyncPreviewControls();
}

void plWaterProfileAssetDocumentWindow::SetShowDirection(bool bShow)
{
  m_bShowDirection = bShow;
  m_pSchematic->SetShowDirection(m_bShowDirection);
  SyncPreviewControls();
}

void plWaterProfileAssetDocumentWindow::SyncPreviewControls()
{
  plWaterPreviewControls::State state;
  state.m_iBody = m_iBodyMode;
  state.m_iScene = m_iScene;
  state.m_iView = m_iView;
  state.m_iQuality = m_iQuality;
  state.m_iBookmark = m_iCameraBookmark;
  state.m_bFreeze = m_bFrozen;
  state.m_bInteract = m_bInteract;
  state.m_bCascades = m_bShowCascades;
  state.m_bDirection = m_bShowDirection;
  m_pControls->SetState(state);
  m_pStatus->SetSunElevation(m_iSunElevation);
}

void plWaterProfileAssetDocumentWindow::ApplyCameraBookmark(int iBookmark)
{
  m_iCameraBookmark = plMath::Clamp(iBookmark, 0, 3);

  bool bUnderwater = false;
  switch (iBookmark)
  {
    case 0: // Grazing
      m_ViewConfig.m_Camera.LookAt(plVec3(-16.0f, -6.0f, 1.2f), plVec3(4.0f, 2.0f, 0.2f), plVec3(0, 0, 1));
      break;
    case 2: // Overhead
      m_ViewConfig.m_Camera.LookAt(plVec3(-1.0f, -1.0f, 26.0f), plVec3::MakeZero(), plVec3(0, 0, 1));
      break;
    case 3: // Underwater
      m_ViewConfig.m_Camera.LookAt(plVec3(-7.0f, -7.0f, -2.0f), plVec3(0, 0, 0.25f), plVec3(0, 0, 1));
      bUnderwater = true;
      break;
    default: // Shore
      m_ViewConfig.m_Camera.LookAt(plVec3(-12.0f, -12.0f, 8.0f), plVec3::MakeZero(), plVec3(0, 0, 1));
      break;
  }

  m_pSchematic->SetUnderwater(bUnderwater);
  SyncPreviewControls();
}

void plWaterProfileAssetDocumentWindow::SetPropertyFilter(plStringView sText)
{
  m_pProperties->SetFilterText(sText);
}

void plWaterProfileAssetDocumentWindow::ShowDiagnosticsPage(int iPage)
{
  m_pDiagnostics->SetPage(static_cast<plWaterDiagnostics::Page>(iPage));
}

void plWaterProfileAssetDocumentWindow::OnStatusChipClicked(plUInt32 uiChip)
{
  // VALID, WAVES, WIND, CASCADES, SPECTRUM, DENSITY
  switch (uiChip)
  {
    case 0:
      ShowDiagnosticsPage(0);
      break;
    case 1:
      SetPropertyFilter("Wave");
      break;
    case 2:
      SetPropertyFilter("Wind");
      break;
    case 3:
      ShowDiagnosticsPage(1);
      break;
    case 4:
      ShowDiagnosticsPage(2);
      break;
    case 5:
      SetPropertyFilter("Density");
      break;
  }
}

// ---------------------------------------------------------------- preset workflow

void plWaterProfileAssetDocumentWindow::RebuildPresetList()
{
  // Merge built-ins and project presets into one indexable array.
  plDynamicArray<plWaterProfilePreset> builtIns;
  plWaterProfilePresetLibrary::GetBuiltInPresets(builtIns);
  plDynamicArray<plWaterProfilePreset> projectPresets;
  plWaterProfilePresetLibrary::LoadProjectPresets(projectPresets);

  m_Presets.Clear();
  m_Presets.PushBackRange(builtIns);
  m_uiBuiltInPresetCount = builtIns.GetCount();
  m_Presets.PushBackRange(projectPresets);

  // Provenance is stored by name (indices shift when project presets come and go).
  const plStringView sAppliedPreset = plPreferences::QueryPreferences<plWaterProfilePreferencesUser>(GetDocument())->GetAppliedPreset();
  m_iAppliedPreset = -1;
  if (!sAppliedPreset.IsEmpty())
  {
    for (plUInt32 i = 0; i < m_Presets.GetCount(); ++i)
    {
      if (m_Presets[i].m_sName == sAppliedPreset)
      {
        m_iAppliedPreset = static_cast<plInt32>(i);
        break;
      }
    }
  }

  // the panel filters by search and body type itself
  UpdatePresetRows();
}

void plWaterProfileAssetDocumentWindow::UpdatePresetRows()
{
  const auto* pDocument = static_cast<const plWaterProfileAssetDocument*>(GetDocument());
  const plWaterProfileResourceDescriptor* pProfile = pDocument->GetProperties();

  plHybridArray<plWaterPresetDiffEntry, 8> diff;
  plDynamicArray<plWaterUi::PresetRow> rows;
  rows.Reserve(m_Presets.GetCount());

  for (plUInt32 i = 0; i < m_Presets.GetCount(); ++i)
  {
    const plWaterProfilePreset& preset = m_Presets[i];
    plWaterUi::PresetRow& row = rows.ExpandAndGetRef();
    row.m_sName = preset.m_sName;
    row.m_uiPreset = i;
    row.m_uiBodyType = static_cast<plUInt8>(preset.m_BodyType);
    row.m_bProject = i >= m_uiBuiltInPresetCount;
    row.m_Shallow = preset.m_Descriptor.m_ShallowColor;
    row.m_Deep = preset.m_Descriptor.m_DeepColor;
    row.m_fWavelength = preset.m_Descriptor.m_fBaseWavelength;
    row.m_fAmplitude = preset.m_Descriptor.m_fWaveAmplitude;

    // the applied preset: APPLIED, or MODIFIED (n) once the profile differs
    plStringBuilder detail = GetBodyTypeName(preset.m_BodyType);
    if (static_cast<plInt32>(i) == m_iAppliedPreset && pProfile != nullptr)
    {
      plWaterProfilePresetLibrary::DiffDescriptors(*pProfile, preset.m_Descriptor, diff);
      if (diff.IsEmpty())
        detail.Append(" \xC2\xB7 APPLIED");
      else
        detail.AppendFormat(" \xC2\xB7 MODIFIED ({})", diff.GetCount());
    }
    row.m_sDetail = detail;
  }

  m_pPresets->SetRows(rows);
}

plInt32 plWaterProfileAssetDocumentWindow::GetSelectedPresetIndex() const
{
  const plInt32 iPreset = m_pPresets->GetSelectedPreset();
  return iPreset >= 0 && iPreset < static_cast<int>(m_Presets.GetCount()) ? iPreset : -1;
}

void plWaterProfileAssetDocumentWindow::BeginApplyPreset(plInt32 iPresetIndex)
{
  if (iPresetIndex < 0 || iPresetIndex >= static_cast<int>(m_Presets.GetCount()))
    return;

  const auto* pDocument = static_cast<const plWaterProfileAssetDocument*>(GetDocument());
  const plWaterProfileResourceDescriptor* pProfile = pDocument->GetProperties();
  if (pProfile == nullptr)
    return;

  plHybridArray<plWaterPresetDiffEntry, 16> diff;
  plWaterProfilePresetLibrary::DiffDescriptors(*pProfile, m_Presets[iPresetIndex].m_Descriptor, diff);

  if (diff.IsEmpty())
  {
    m_iAppliedPreset = iPresetIndex;
    plPreferences::QueryPreferences<plWaterProfilePreferencesUser>(GetDocument())->SetAppliedPreset(m_Presets[iPresetIndex].m_sName);
    UpdatePresetRows();
    return; // already matches, nothing to confirm
  }

  m_iPendingPreset = iPresetIndex;

  constexpr plUInt32 uiMaxRows = 8;

  plWaterUi::DiffCard card;

  plStringBuilder text;
  text.SetFormat("Apply \"{}\" \xE2\x80\x94 {} value{} change:", m_Presets[iPresetIndex].m_sName, diff.GetCount(), diff.GetCount() == 1 ? "" : "s");
  card.m_sTitle = text;

  for (plUInt32 i = 0; i < plMath::Min<plUInt32>(diff.GetCount(), uiMaxRows); ++i)
  {
    text.SetFormat("{}:  {} \xE2\x86\x92 {}", diff[i].m_sProperty, FormatVariantShort(diff[i].m_OldValue), FormatVariantShort(diff[i].m_NewValue));
    card.m_Rows.PushBack(text);
  }

  if (diff.GetCount() > uiMaxRows)
  {
    text.SetFormat("\xE2\x80\xA6 and {} more", diff.GetCount() - uiMaxRows);
    card.m_sMore = text;
  }

  m_pPresets->SetSelectedPreset(iPresetIndex);
  m_pPresets->ShowDiff(card);
}

void plWaterProfileAssetDocumentWindow::ApplyPendingPreset(bool bKeepColors)
{
  if (m_iPendingPreset < 0 || m_iPendingPreset >= static_cast<int>(m_Presets.GetCount()))
  {
    CancelPendingPreset();
    return;
  }

  const plInt32 iPreset = m_iPendingPreset;
  auto* pDocument = static_cast<plWaterProfileAssetDocument*>(GetDocument());

  plWaterProfileResourceDescriptor profile = m_Presets[iPreset].m_Descriptor;
  if (bKeepColors)
    plWaterProfilePresetLibrary::CopyColorProperties(*pDocument->GetProperties(), profile);

  // Applying a native descriptor rebuilds the document mirror property by property. During SwapStorage the native
  // property object is intentionally unavailable, so the property event handler must not refresh from those
  // intermediate events (see PropertyEventHandler).
  m_bApplyingPreset = true;
  *pDocument->GetProperties() = profile;
  pDocument->ApplyNativePropertyChangesToObjectManager();
  m_bApplyingPreset = false;

  SetBodyMode(static_cast<plInt32>(m_Presets[iPreset].m_BodyType));

  m_iAppliedPreset = iPreset;
  plPreferences::QueryPreferences<plWaterProfilePreferencesUser>(GetDocument())->SetAppliedPreset(m_Presets[iPreset].m_sName);
  CancelPendingPreset();
  UpdatePreview();
}

void plWaterProfileAssetDocumentWindow::CancelPendingPreset()
{
  m_iPendingPreset = -1;
  m_pPresets->HideDiff();
}

void plWaterProfileAssetDocumentWindow::OnSaveAsPreset()
{
  bool bOk = false;
  plStringBuilder sName = plEnginePrompt::GetText("Save Water Preset", "Preset name:", {}, &bOk);
  sName.Trim();
  if (!bOk || sName.IsEmpty())
    return;

  const auto* pDocument = static_cast<const plWaterProfileAssetDocument*>(GetDocument());
  const plWaterProfileResourceDescriptor* pProfile = pDocument->GetProperties();
  if (pProfile == nullptr)
    return;

  const auto bodyType = static_cast<plWaterProfilePreset::BodyType>(m_iBodyMode);
  if (plWaterProfilePresetLibrary::SaveProjectPreset(sName, bodyType, *pProfile).Failed())
  {
    plLog::Error("Failed to save water preset '{}'. Is a project open?", sName);
    return;
  }

  RebuildPresetList();
}

// ---------------------------------------------------------------- refresh

void plWaterProfileAssetDocumentWindow::PropertyEventHandler(const plDocumentObjectPropertyEvent& e)
{
  // Applying a native descriptor rebuilds the document mirror property by property. During SwapStorage the native
  // property object is intentionally unavailable, so refreshing from these intermediate events would dereference a
  // transient null pointer. ApplyPendingPreset performs one refresh after the mirror transaction has completed instead.
  if (m_bApplyingPreset)
    return;

  UpdatePreview();
}

void plWaterProfileAssetDocumentWindow::UpdatePreview()
{
  const auto* pDocument = static_cast<const plWaterProfileAssetDocument*>(GetDocument());
  const plWaterProfileResourceDescriptor* pProfile = pDocument->GetProperties();
  if (pProfile == nullptr)
    return;

  const plWaterProfileResourceDescriptor& profile = *pProfile;
  m_pSchematic->SetProfile(profile);
  m_pStatus->SetDepthProfile(profile);

  UpdateSpectrum(profile);
  UpdateStatusChips(profile);
  UpdateValidation(profile);
  UpdatePerformanceTable(profile);
  UpdatePresetRows();

  SendLiveResourcePreview();
}

void plWaterProfileAssetDocumentWindow::UpdateStatusChips(const plWaterProfileResourceDescriptor& profile)
{
  plHybridArray<plWaterProfileIssue, 8> issues;
  profile.Validate(issues);

  plUInt32 uiErrors = 0;
  plUInt32 uiWarnings = 0;
  for (const plWaterProfileIssue& issue : issues)
  {
    if (issue.m_Severity == plWaterProfileIssue::Severity::Error)
      ++uiErrors;
    else
      ++uiWarnings;
  }

  const auto& tier = plWaterProfileUiServices::s_QualityTiers[m_iQuality];
  const plUInt32 uiEffective = plMath::Min<plUInt32>(profile.m_uiGpuCascadeCount, tier.m_uiMaxCascades);
  const double fMiB = static_cast<double>(plWaterProfileUiServices::GetSpectrumMemoryBytes(m_iQuality, profile.m_uiGpuCascadeCount)) / (1024.0 * 1024.0);

  // VALID, WAVES, WIND, CASCADES, SPECTRUM, DENSITY; the first one's dot shows the validation state
  plWaterUi::Chip chips[6];
  plStringBuilder text;

  if (uiErrors > 0)
  {
    text.SetFormat("{} ERROR{}", uiErrors, uiErrors == 1 ? "" : "S");
    chips[0].m_Severity = plWaterUi::Severity::Error;
  }
  else if (uiWarnings > 0)
  {
    text.SetFormat("VALID \xC2\xB7 {} warning{}", uiWarnings, uiWarnings == 1 ? "" : "s");
    chips[0].m_Severity = plWaterUi::Severity::Warning;
  }
  else
  {
    text = "VALID";
  }
  chips[0].m_sText = text;
  chips[0].m_bState = true;
  chips[0].m_sTooltip = "Validation state. Click to open the Validation tab.";

  text.SetFormat("WAVES {} m", plArgF(profile.m_fWaveAmplitude, 2));
  chips[1].m_sText = text;
  chips[1].m_sTooltip = "Wave amplitude. Click to filter the properties to the wave spectrum.";

  text.SetFormat("WIND {} m/s", plArgF(profile.m_fWindSpeed, 1));
  chips[2].m_sText = text;
  chips[2].m_sTooltip = "Wind speed. Click to filter the properties to wind settings.";

  text.SetFormat("CASCADES {} / {}", uiEffective, profile.m_uiGpuCascadeCount);
  chips[3].m_sText = text;
  chips[3].m_sTooltip = "Effective / authored GPU cascades at the current preview quality. Click to open the Spectrum tab.";
  if (uiEffective < profile.m_uiGpuCascadeCount)
    chips[3].m_Severity = plWaterUi::Severity::Warning;

  text.SetFormat("SPECTRUM {} MiB", plArgF(fMiB, 1));
  chips[4].m_sText = text;
  chips[4].m_sTooltip = "Estimated spectrum working set at the current preview quality. Click to open the Performance tab.";

  text.SetFormat("DENSITY {} kg/m\xC2\xB3", plArgF(profile.m_fDensity, 0));
  chips[5].m_sText = text;
  chips[5].m_sTooltip = "Physics density. Click to filter the properties to physics settings.";

  m_pStatus->SetChips(chips);
}

void plWaterProfileAssetDocumentWindow::UpdateValidation(const plWaterProfileResourceDescriptor& profile)
{
  plHybridArray<plWaterProfileIssue, 8> issues;
  profile.Validate(issues);

  // log rows: the severity shows as the row's icon
  plHybridArray<plWaterUi::Issue, 8> rows;

  bool bHasErrors = false;
  for (const plWaterProfileIssue& issue : issues)
    bHasErrors = bHasErrors || issue.m_Severity == plWaterProfileIssue::Severity::Error;

  if (!bHasErrors)
    rows.ExpandAndGetRef().m_sText = "Profile valid \xE2\x80\x94 no blocking issues for ocean, river, or lake bodies.";

  for (const plWaterProfileIssue& issue : issues)
  {
    plWaterUi::Issue& row = rows.ExpandAndGetRef();
    row.m_sText = issue.m_sMessage;
    row.m_sProperty = issue.m_sProperty;
    row.m_Severity = issue.m_Severity == plWaterProfileIssue::Severity::Error ? plWaterUi::Severity::Error : plWaterUi::Severity::Warning;
  }

  // Editor-side lint: authored cascades the current preview tier never simulates.
  const auto& tier = plWaterProfileUiServices::s_QualityTiers[m_iQuality];
  if (profile.m_uiGpuCascadeCount > tier.m_uiMaxCascades)
  {
    plStringBuilder text;
    text.SetFormat("{} GPU cascades authored, but the {} tier caps at {} \xE2\x80\x94 the extra cascades are never simulated in this preview. See the Spectrum tab.", profile.m_uiGpuCascadeCount, tier.m_szName, tier.m_uiMaxCascades);

    plWaterUi::Issue& row = rows.ExpandAndGetRef();
    row.m_sText = text;
    row.m_sProperty = "GpuCascadeCount";
    row.m_Severity = plWaterUi::Severity::Warning;
  }

  m_pDiagnostics->SetIssues(rows);
}

void plWaterProfileAssetDocumentWindow::UpdateSpectrum(const plWaterProfileResourceDescriptor& profile)
{
  const auto& tier = plWaterProfileUiServices::s_QualityTiers[m_iQuality];

  plWaterUi::SpectrumData data;
  plWaterProfileUiServices::GetCascadeBands(profile, data.m_CascadeBands);
  plWaterProfileUiServices::GetCpuWavelengths(profile, data.m_CpuWavelengths);
  data.m_fSmallest = plMath::Clamp(profile.m_fSmallestGpuWavelength, 0.01f, profile.m_fBaseWavelength);
  data.m_fBase = plMath::Max(profile.m_fBaseWavelength, 0.1f);
  data.m_uiEffectiveCascades = plMath::Min<plUInt32>(profile.m_uiGpuCascadeCount, tier.m_uiMaxCascades);
  data.m_sTierName = tier.m_szName;
  m_pDiagnostics->SetSpectrum(data);
}

void plWaterProfileAssetDocumentWindow::UpdatePerformanceTable(const plWaterProfileResourceDescriptor& profile)
{
  const int iCurrentTier = m_iQuality;

  plHybridArray<plString, 20> cells;
  for (plUInt32 uiTier = 0; uiTier < plWaterProfileUiServices::QualityTierCount; ++uiTier)
  {
    const auto& tier = plWaterProfileUiServices::s_QualityTiers[uiTier];
    const plUInt32 uiEffectiveCascades = plMath::Min<plUInt32>(profile.m_uiGpuCascadeCount, tier.m_uiMaxCascades);
    const double fMiB = static_cast<double>(plWaterProfileUiServices::GetSpectrumMemoryBytes(uiTier, profile.m_uiGpuCascadeCount)) / (1024.0 * 1024.0);

    plStringBuilder text;
    if (static_cast<int>(uiTier) == iCurrentTier)
      text.SetFormat("{}  \xE2\x86\x90 preview", tier.m_szName);
    else
      text = tier.m_szName;
    cells.PushBack(text);

    text.SetFormat("{} / {}", uiEffectiveCascades, profile.m_uiGpuCascadeCount);
    cells.PushBack(text);

    if (tier.m_uiResolution == 0)
      text = "\xE2\x80\x94";
    else
      text.SetFormat("{}\xC2\xB2", tier.m_uiResolution);
    cells.PushBack(text);

    text.SetFormat("{} MiB", plArgF(fMiB, 1));
    cells.PushBack(text);

    cells.PushBack(tier.m_szSimMode);
  }

  m_pDiagnostics->SetPerformance(cells, static_cast<plUInt32>(iCurrentTier));
}

void plWaterProfileAssetDocumentWindow::SendLiveResourcePreview()
{
  if (plEditorEngineProcessConnection::GetSingleton()->IsProcessCrashed())
    return;

  const auto* pDocument = static_cast<const plWaterProfileAssetDocument*>(GetDocument());

  plResourceUpdateMsgToEngine msg;
  msg.m_sResourceType = "WaterProfile";

  plStringBuilder sResourceId;
  msg.m_sResourceID = plConversionUtils::ToString(GetDocument()->GetGuid(), sResourceId);

  plContiguousMemoryStreamStorage storage;
  plMemoryStreamWriter writer(&storage);

  plStringBuilder sAbsolutePath = pDocument->GetDocumentPath();
  sAbsolutePath.ChangeFileExtension("plWaterProfile");
  writer << sAbsolutePath;

  const plUInt64 uiHash = plAssetCurator::GetSingleton()->GetAssetTransformHash(pDocument->GetGuid());
  plAssetFileHeader header;
  header.SetFileHashAndVersion(uiHash, pDocument->GetAssetTypeVersion());
  header.Write(writer).IgnoreResult();

  pDocument->GetProperties()->Save(writer);
  msg.m_Data = plArrayPtr<const plUInt8>(storage.GetData(), storage.GetStorageSize32());
  plEditorEngineProcessConnection::GetSingleton()->SendMessage(&msg);
}

void plWaterProfileAssetDocumentWindow::RestoreResource()
{
  plRestoreResourceMsgToEngine msg;
  msg.m_sResourceType = "WaterProfile";

  plStringBuilder sResourceId;
  msg.m_sResourceID = plConversionUtils::ToString(GetDocument()->GetGuid(), sResourceId);
  plEditorEngineProcessConnection::GetSingleton()->SendMessage(&msg);
}
