#pragma once

#include <EditorEngineProcessFramework/EngineProcess/ViewRenderSettings.h>
#include <EditorFramework/DocumentWindow/EngineDocumentWindow.h>
#include <EditorPluginWater/WaterProfileAsset/WaterProfilePresets.h>
#include <Foundation/Types/UniquePtr.h>

class plOrbitCamViewWidget;
class plWaterDiagnostics;
class plWaterPresetPanel;
class plWaterPreviewControls;
class plWaterPreviewStatus;
class plWaterProfileProperties;
class plWaterSchematicView;
class plWaterInteractContext;

/// \brief The Water Profile window: presets, the preview with its controls and status, the properties and the
/// diagnostics, all engine-drawn panels the engine shell shows (plWaterProfileShellPanels).
class plWaterProfileAssetDocumentWindow : public plEngineDocumentWindow
{
public:
  plWaterProfileAssetDocumentWindow(plDocument* pDocument);
  ~plWaterProfileAssetDocumentWindow();

private:
  virtual void InternalRedraw() override;
  void SendRedrawMsg();
  void SendBodyTypeToEngine();
  void SendRuntimePreviewSettings();
  void PropertyEventHandler(const plDocumentObjectPropertyEvent& e);
  void UpdatePreview();
  void SendLiveResourcePreview();
  void RestoreResource();

  // Panels
  void CreatePresets();
  void CreatePreviewControls();
  void CreatePreview();
  void CreatePreviewStatus();
  void CreateDiagnostics();

  // Preset workflow
  void RebuildPresetList();
  void BeginApplyPreset(plInt32 iPresetIndex);
  void ApplyPendingPreset(bool bKeepColors);
  void CancelPendingPreset();
  void OnSaveAsPreset();
  plInt32 GetSelectedPresetIndex() const;
  void UpdatePresetRows();

  // Preview controls
  void SetBodyMode(plInt32 iMode);
  void SetScene(plInt32 iScene);
  void SetQuality(plInt32 iQuality);
  void SetView(plInt32 iView);
  void SetSunElevation(plInt32 iDegrees);
  void SetFrozen(bool bFrozen);
  void SetInteract(bool bInteract);
  void SetShowCascades(bool bShow);
  void SetShowDirection(bool bShow);
  void ApplyCameraBookmark(int iBookmark);
  void SyncPreviewControls();
  void SendSceneToEngine();
  void SendWaterInteraction(const plVec3& vPosition, bool bContinuous);
  plInt32 GetEffectiveScene() const;

  // Properties and diagnostics
  void SetPropertyFilter(plStringView sText);
  void ShowDiagnosticsPage(int iPage);
  void OnStatusChipClicked(plUInt32 uiChip);
  void UpdateStatusChips(const plWaterProfileResourceDescriptor& profile);
  void UpdateValidation(const plWaterProfileResourceDescriptor& profile);
  void UpdatePerformanceTable(const plWaterProfileResourceDescriptor& profile);
  void UpdateSpectrum(const plWaterProfileResourceDescriptor& profile);

  // Preview state, shown by the controls and the engine
  plInt32 m_iBodyMode = 0;
  plInt32 m_iScene = 0;
  plInt32 m_iQuality = 2;
  plInt32 m_iView = 1; ///< 0 = 2D Surface, 1 = 3D Runtime
  plInt32 m_iSunElevation = 35;
  plInt32 m_iCameraBookmark = 1; ///< Shore: where the camera starts
  bool m_bFrozen = false;
  bool m_bInteract = false;
  bool m_bShowCascades = true;
  bool m_bShowDirection = true;

  // Presets
  plDynamicArray<plWaterProfilePreset> m_Presets; ///< Built-ins first, then project presets.
  plUInt32 m_uiBuiltInPresetCount = 0;
  plInt32 m_iAppliedPreset = -1;
  plInt32 m_iPendingPreset = -1;
  plWaterPresetPanel* m_pPresets = nullptr;

  // Preview
  plWaterSchematicView* m_pSchematic = nullptr; ///< the 2D Surface view
  plWaterPreviewControls* m_pControls = nullptr;
  plWaterPreviewStatus* m_pStatus = nullptr;
  plEngineViewConfig m_ViewConfig;
  plOrbitCamViewWidget* m_pViewWidget = nullptr;
  plUniquePtr<plWaterInteractContext> m_pInteractContext;

  // Properties and diagnostics
  plWaterProfileProperties* m_pProperties = nullptr;
  plWaterDiagnostics* m_pDiagnostics = nullptr;

  bool m_bApplyingPreset = false;
};
