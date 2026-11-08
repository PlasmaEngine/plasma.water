#pragma once

#include <EditorPluginWater/EditorPluginWaterDLL.h>
#include <EditorUi/Content.h>
#include <EditorUi/SearchField.h>
#include <EditorUi/Table.h>
#include <EditorUi/Tabs.h>
#include <GameUiPlugin/Elements/UiDrawList.h>
#include <WaterPlugin/Resources/WaterProfileResource.h>

#include <memory>

struct plEditorUiPointerEvent;
struct plMsgUiEvent;

/// \brief Shared pieces of the engine-drawn Water Profile window.
namespace plWaterUi
{
  enum class Severity : plUInt8
  {
    Ok,
    Warning,
    Error,
  };

  /// A status chip under the preview: a state dot, its text and what clicking it does.
  struct Chip
  {
    plString m_sText;
    plString m_sTooltip;
    Severity m_Severity = Severity::Ok;
    bool m_bState = false; ///< the dot shows the severity colour; other chips keep a neutral dot
  };

  /// A validation line: the property a click filters the properties to.
  struct Issue
  {
    plString m_sText;
    plString m_sProperty;
    Severity m_Severity = Severity::Ok;
  };

  /// A preset row of the Water Profiles list.
  struct PresetRow
  {
    plString m_sName;
    plString m_sDetail; ///< body type, then the APPLIED / MODIFIED (n) tag
    plUInt32 m_uiPreset = 0;
    plUInt8 m_uiBodyType = 0;
    bool m_bProject = false;
    plColorGammaUB m_Shallow;
    plColorGammaUB m_Deep;
    float m_fWavelength = 18.0f;
    float m_fAmplitude = 0.28f;
  };

  /// What applying the pending preset changes.
  struct DiffCard
  {
    plString m_sTitle;
    plHybridArray<plString, 8> m_Rows;
    plString m_sMore;
  };

  /// What the spectrum graph shows.
  struct SpectrumData
  {
    plHybridArray<plVec2, 4> m_CascadeBands;
    plHybridArray<float, 16> m_CpuWavelengths;
    float m_fSmallest = 0.5f;
    float m_fBase = 18.0f;
    plUInt32 m_uiEffectiveCascades = 3;
    plString m_sTierName;
  };
} // namespace plWaterUi

/// \brief The animated 2D schematic of a profile (View: 2D Surface).
class plWaterSchematicPreview
{
public:
  virtual ~plWaterSchematicPreview() = default;

  virtual void SetProfile(const plWaterProfileResourceDescriptor& profile) = 0;
  virtual void SetBodyMode(int iMode) = 0;
  virtual void SetQuality(int iQuality) = 0;
  virtual void SetUnderwater(bool bUnderwater) = 0;
  virtual void SetShowCascades(bool bShow) = 0;
  virtual void SetShowDirection(bool bShow) = 0;
  virtual void SetFrozen(bool bFrozen) = 0;
};

/// \brief Water Profiles dock: search, the body type filter, the BUILT-IN and PROJECT preset lists, the diff card of a
/// pending apply and Apply Preset..., Save As..., Reset to Defaults.
///
/// Each of the window's engine-drawn parts is an editor UI content: a panel or row the window owns (plDocumentWindow::
/// AddPanel, AddRow), which the engine shell shows in the document's frame.
class plWaterPresetPanel : public plEditorUiContent
{
public:
  plWaterPresetPanel();
  ~plWaterPresetPanel();

  /// Every preset, built-ins first; the panel filters them by the search text and the body type.
  void SetRows(plArrayPtr<const plWaterUi::PresetRow> rows);

  plInt32 GetSelectedPreset() const { return m_iSelectedPreset; }
  void SetSelectedPreset(plInt32 iPreset);

  void ShowDiff(const plWaterUi::DiffCard& card);
  void HideDiff();

  plDelegate<void(plInt32)> m_OnActivate; ///< double-click: the diff card of that preset (W5-D10)
  plDelegate<void()> m_OnApplyPreset;
  plDelegate<void()> m_OnSaveAs;
  plDelegate<void()> m_OnReset;
  plDelegate<void(bool)> m_OnApplyPending; ///< Apply (false) or Keep Colors (true)
  plDelegate<void()> m_OnCancelPending;

protected:
  void OnUiUpdate() override;
  void OnUiEvent(const plMsgUiEvent& event) override;

private:
  struct Line
  {
    plInt32 m_iRow = -1;   ///< index into m_Rows, -1 for a group header
    bool m_bProject = false; ///< the group of a header
  };

  void BuildDocument();
  float GetDiffHeight() const;
  float GetFooterHeight(float fWidth) const;
  plRectFloat GetListRect() const;
  void UpdateLines();
  void DrawRows();
  void ClampScroll();
  plInt32 LineAt(const plVec2& vPos) const;
  void OnPointer(const plEditorUiPointerEvent& e);
  void OnSearch(const plEditorUiSearchFieldEvent& e);
  plString GetIconTexture(const plWaterUi::PresetRow& row) const;

  plEditorUiSearchField m_Search;
  plDynamicArray<plWaterUi::PresetRow> m_Rows;
  plDynamicArray<Line> m_Lines;
  plInt32 m_iTypeFilter = -1; ///< -1 = all, else the body type
  plInt32 m_iSelectedPreset = -1;
  plInt32 m_iHoverLine = -1;
  bool m_bCollapsed[2] = {false, false}; ///< BUILT-IN, PROJECT
  float m_fScroll = 0.0f;
  bool m_bDiff = false;
  plWaterUi::DiffCard m_Diff;

  bool m_bRebuild = true;
  plVec2 m_vBuiltSize = plVec2::MakeZero();
  plUiDrawListWriter m_Writer;
  plEventSubscriptionID m_PointerSubscription = 0;

  /// Deferred actions run only while the panel lives.
  std::shared_ptr<bool> m_pAlive;
};

/// \brief The control row above the preview: Preview, Scene, View and Quality dropdowns, Freeze, Interact, Cascades and
/// Direction (2D only), and the camera bookmarks Grazing, Shore, Overhead, Underwater. It wraps onto a second row
/// when narrow.
class plWaterPreviewControls : public plEditorUiContent
{
public:
  enum class Control : plUInt8
  {
    Body,
    Scene,
    View,
    Quality,
    Freeze,
    Interact,
    Cascades,
    Direction,
    Bookmark,
  };

  struct State
  {
    plInt32 m_iBody = 0;
    plInt32 m_iScene = 0;
    plInt32 m_iView = 1;
    plInt32 m_iQuality = 2;
    plInt32 m_iBookmark = 1;
    bool m_bFreeze = false;
    bool m_bInteract = false;
    bool m_bCascades = true;
    bool m_bDirection = true;
  };

  plWaterPreviewControls();

  void SetQualityNames(plArrayPtr<const plString> names);
  void SetState(const State& state);
  const State& GetState() const { return m_State; }

  /// The height of the rows the controls take now, in logical units.
  float GetHeight() const;

  /// The height of one row of controls with the line below it.
  static constexpr float OneRowHeight = 31.0f;

  /// The control and its new value (an index, or 0 / 1 for the toggles).
  plDelegate<void(Control, plInt32)> m_OnChanged;

  /// The controls wrapped onto more or fewer rows: the new GetHeight().
  plDelegate<void(float)> m_OnHeightChanged;

protected:
  void OnUiUpdate() override;
  void OnUiEvent(const plMsgUiEvent& event) override;

private:
  struct Item
  {
    enum class Kind : plUInt8
    {
      Label,
      Dropdown,
      Check,
      Toggle,
      Separator,
      Bookmarks,
    };

    Kind m_Kind = Kind::Label;
    Control m_Control = Control::Body;
    plString m_sText;
    float m_fWidth = 0.0f;
  };

  void BuildDocument();
  void CollectItems(plDynamicArray<Item>& out_items) const;
  float GetBookmarksWidth() const;
  plArrayPtr<const plString> GetChoices(Control control) const;
  plInt32 GetValue(Control control) const;
  void OpenMenu(Control control, const plVec2& vPosition);
  void Change(Control control, plInt32 iValue);

  State m_State;
  plHybridArray<plString, 4> m_QualityNames;
  plUInt32 m_uiRows = 1;
  bool m_bRebuild = true;
  plVec2 m_vBuiltSize = plVec2::MakeZero();
  plHashTable<plString, plRectFloat> m_DropdownRects;
};

/// \brief The 2D schematic drawn by the engine: background, crest line, cascades, banks or shore, wind arrows,
/// underwater streaks and the profile line at the top. It animates while it is shown, until frozen.
class plWaterSchematicView : public plEditorUiContent, public plWaterSchematicPreview
{
public:
  plWaterSchematicView();
  ~plWaterSchematicView();

  void SetProfile(const plWaterProfileResourceDescriptor& profile) override;
  void SetBodyMode(int iMode) override;
  void SetQuality(int iQuality) override;
  void SetUnderwater(bool bUnderwater) override;
  void SetShowCascades(bool bShow) override;
  void SetShowDirection(bool bShow) override;
  void SetFrozen(bool bFrozen) override;

protected:
  void OnUiUpdate() override;

private:
  void BuildDocument();
  void Animate();
  void Draw();
  void DrawWave(const plRectFloat& rect, float fHorizon, float fYOffset, float fAmplitudeScale, float fWavelengthScale, float fPhaseOffset, const plColorGammaUB& color, float fWidth);

  bool m_bFrozen = false;
  plTime m_LastStep; ///< when the waves last moved on
  plColorGammaUB m_Shallow = plColorGammaUB(30, 135, 180);
  plColorGammaUB m_Deep = plColorGammaUB(2, 25, 65);
  float m_fPhase = 0.0f;
  float m_fAmplitude = 1.0f;
  float m_fWavelength = 18.0f;
  float m_fSteepness = 0.32f;
  float m_fWindSpeed = 5.5f;
  float m_fRoughness = 0.12f;
  float m_fFoam = 1.0f;
  int m_iBodyMode = 0;
  int m_iQuality = 2;
  bool m_bUnderwater = false;
  bool m_bShowCascades = true;
  bool m_bShowDirection = true;

  bool m_bRebuild = true;
  bool m_bDirty = true;
  plVec2 m_vBuiltSize = plVec2::MakeZero();
  plUiDrawListWriter m_Writer;
};

/// \brief Under the preview: the sun elevation slider, the status chips (they open diagnostics tabs or filter the
/// properties) and the depth ramp of the computed water colour.
class plWaterPreviewStatus : public plEditorUiContent
{
public:
  plWaterPreviewStatus();

  void SetSunElevation(plInt32 iDegrees);
  void SetChips(plArrayPtr<const plWaterUi::Chip> chips);
  void SetDepthProfile(const plWaterProfileResourceDescriptor& profile);

  /// The height of the slider row, the chip rows and the ramp, in logical units.
  float GetHeight() const;

  plDelegate<void(plInt32)> m_OnSunChanged;
  plDelegate<void(plUInt32)> m_OnChipClicked;

  /// The chips wrapped onto more or fewer rows: the new GetHeight().
  plDelegate<void(float)> m_OnHeightChanged;

protected:
  void OnUiUpdate() override;
  void OnUiEvent(const plMsgUiEvent& event) override;
  plString GetToolTipAt(const plVec2& vLogical) const override;

private:
  void BuildDocument();
  void DrawRamp();

  plInt32 m_iSun = 35;
  plHybridArray<plWaterUi::Chip, 6> m_Chips;
  plColor m_Shallow = plColor(0.08f, 0.34f, 0.42f);
  plColor m_Deep = plColor(0.008f, 0.035f, 0.075f);
  plVec3 m_vAbsorption = plVec3(0.35f, 0.07f, 0.025f);
  float m_fRefractionDepth = 6.0f;

  bool m_bRebuild = true;
  bool m_bRampDirty = true;
  plVec2 m_vBuiltSize = plVec2::MakeZero();
  plUInt32 m_uiChipRows = 1;
  plUiDrawListWriter m_Writer;
};

/// \brief Above the property grid: Filter properties... and the line that says which rows the body type ignores.
class plWaterPropertyFilter : public plEditorUiContent
{
public:
  plWaterPropertyFilter();
  ~plWaterPropertyFilter();

  void SetFilterText(plStringView sText);
  plString GetFilterText() const;
  void SetContextText(plStringView sText);

  /// The height of the filter row and the context line, in logical units.
  static float GetHeight();

  plDelegate<void(plStringView)> m_OnFilterChanged;

protected:
  void OnUiUpdate() override;

private:
  void OnSearch(const plEditorUiSearchFieldEvent& e);

  plEditorUiSearchField m_Search;
  plString m_sContext;
  bool m_bRebuild = true;
  plVec2 m_vBuiltSize = plVec2::MakeZero();
};

/// \brief Water Diagnostics dock: Validation (a click filters the properties), the Spectrum graph and the
/// Performance table (a click picks the preview quality).
class plWaterDiagnostics : public plEditorUiContent
{
public:
  enum class Page : plUInt8
  {
    Validation,
    Spectrum,
    Performance,
  };

  plWaterDiagnostics();
  ~plWaterDiagnostics();

  void SetPage(Page page);
  void SetIssues(plArrayPtr<const plWaterUi::Issue> issues);
  void SetSpectrum(const plWaterUi::SpectrumData& data);

  /// Five cells per tier: Tier, Cascades, Resolution, Spectrum Memory, Simulation.
  void SetPerformance(plArrayPtr<const plString> cells, plUInt32 uiCurrentTier);

  plDelegate<void(plStringView)> m_OnFilterProperties;
  plDelegate<void(plUInt32)> m_OnTierPicked;

protected:
  void OnUiUpdate() override;
  plString GetToolTipAt(const plVec2& vLogical) const override;

private:
  class TierAdapter;

  void BuildDocument();
  void UpdateTabs();
  plRectFloat GetBodyRect() const;
  void DrawValidation();
  void DrawSpectrum();
  plInt32 BandAt(const plVec2& vPos) const;
  void OnPointer(const plEditorUiPointerEvent& e);
  void OnTableEvent(const plEditorUiTableEvent& e);

  Page m_Page = Page::Validation;
  plEditorUiTabs m_Tabs;
  plDynamicArray<plWaterUi::Issue> m_Issues;
  plWaterUi::SpectrumData m_Spectrum;
  plHybridArray<plRectFloat, 4> m_BandRects;
  plInt32 m_iHoverIssue = -1;

  plUniquePtr<TierAdapter> m_pTierAdapter;
  plUniquePtr<plEditorUiTable> m_pTierTable;

  bool m_bRebuild = true;
  plVec2 m_vBuiltSize = plVec2::MakeZero();
  plUiDrawListWriter m_Writer;
  plEventSubscriptionID m_PointerSubscription = 0;
};
