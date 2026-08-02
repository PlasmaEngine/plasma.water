#pragma once

#include <EditorEngineProcessFramework/EngineProcess/ViewRenderSettings.h>
#include <EditorFramework/DocumentWindow/EngineDocumentWindow.moc.h>
#include <EditorPluginWater/WaterProfileAsset/WaterProfilePresets.h>
#include <Foundation/Types/UniquePtr.h>

class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QSlider;
class QTabWidget;
class QTableWidget;
class QTimer;
class QToolButton;
class QStackedWidget;
class QVBoxLayout;
class plQtOrbitCamViewWidget;
class plQtPropertyGridWidget;
class plQtWaterProfilePreview;
class plQtWaterSpectrumGraph;
class plQtWaterDepthRampWidget;
class plWaterInteractContext;

class plQtWaterProfileAssetDocumentWindow : public plQtEngineDocumentWindow
{
  Q_OBJECT

public:
  plQtWaterProfileAssetDocumentWindow(plDocument* pDocument);
  ~plQtWaterProfileAssetDocumentWindow();

private:
  virtual void InternalRedraw() override;
  void SendRedrawMsg();
  void SendBodyTypeToEngine();
  void SendRuntimePreviewSettings();
  void PropertyEventHandler(const plDocumentObjectPropertyEvent& e);
  void UpdatePreview();
  void SendLiveResourcePreview();
  void RestoreResource();

  // Preset workflow
  void RebuildPresetList();
  void RefreshPresetTags();
  void BeginApplyPreset(plInt32 iPresetIndex);
  void ApplyPendingPreset(bool bKeepColors);
  void CancelPendingPreset();
  void OnSaveAsPreset();
  plInt32 GetSelectedPresetIndex() const;

  // Preview controls
  void OnBodyModeChanged(int iMode);
  void ApplyCameraBookmark(int iBookmark);
  void SendSceneToEngine();
  void SendWaterInteraction(const plVec3& vPosition, bool bContinuous);
  plInt32 GetEffectiveScene() const;

  // Diagnostics
  void UpdateStatusChips(const plWaterProfileResourceDescriptor& profile);
  void UpdateValidation(const plWaterProfileResourceDescriptor& profile);
  void UpdatePerformanceTable(const plWaterProfileResourceDescriptor& profile);

  // Presets
  plDynamicArray<plWaterProfilePreset> m_Presets; ///< Built-ins first, then project presets.
  plUInt32 m_uiBuiltInPresetCount = 0;
  plInt32 m_iAppliedPreset = -1;
  plInt32 m_iPendingPreset = -1;
  QListWidget* m_pPresetList = nullptr;
  QLineEdit* m_pPresetSearch = nullptr;
  QToolButton* m_pPresetFilterButtons[4] = {}; ///< All / Ocean / River / Lake
  QFrame* m_pDiffCard = nullptr;
  QLabel* m_pDiffTitle = nullptr;
  QVBoxLayout* m_pDiffRowsLayout = nullptr;

  // Preview
  plQtWaterProfilePreview* m_pPreview = nullptr;
  QComboBox* m_pBodyMode = nullptr;
  QComboBox* m_pScene = nullptr;
  QComboBox* m_pQuality = nullptr;
  QCheckBox* m_pFreezeTime = nullptr;
  QToolButton* m_pCameraBookmarks[4] = {}; ///< Grazing / Shore / Overhead / Underwater
  QSlider* m_pSunSlider = nullptr;
  QLabel* m_pSunLabel = nullptr;
  QStackedWidget* m_pPreviewStack = nullptr;
  plEngineViewConfig m_ViewConfig;
  plQtOrbitCamViewWidget* m_pViewWidget = nullptr;
  plUniquePtr<plWaterInteractContext> m_pInteractContext;
  QToolButton* m_pInteractButton = nullptr;

  // Status chips + depth ramp
  QToolButton* m_pChipValidation = nullptr;
  QToolButton* m_pChipWaves = nullptr;
  QToolButton* m_pChipWind = nullptr;
  QToolButton* m_pChipCascades = nullptr;
  QToolButton* m_pChipSpectrum = nullptr;
  QToolButton* m_pChipDensity = nullptr;
  plQtWaterDepthRampWidget* m_pDepthRamp = nullptr;

  // Properties dock
  plQtPropertyGridWidget* m_pPropertyGrid = nullptr;
  QLineEdit* m_pPropertyFilter = nullptr;
  QLabel* m_pTuningContextLabel = nullptr;

  // Diagnostics dock
  QTabWidget* m_pDiagnosticsTabs = nullptr;
  QListWidget* m_pValidationList = nullptr;
  plQtWaterSpectrumGraph* m_pSpectrumGraph = nullptr;
  QTableWidget* m_pPerformanceTable = nullptr;

  bool m_bApplyingPreset = false;
};
