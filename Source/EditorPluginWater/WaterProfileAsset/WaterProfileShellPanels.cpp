#include <EditorPluginWater/EditorPluginWaterPCH.h>

#include <EditorFramework/Assets/AssetStatusIndicator.h>
#include <EditorFramework/EngineShell/AssetDocumentContent.h>
#include <EditorFramework/EngineShell/DocumentFrame.h>
#include <EditorFramework/EngineShell/EngineShell.h>
#include <EditorFramework/EngineShell/QuadViewPanel.h>
#include <EditorPluginWater/WaterProfileAsset/WaterProfileAsset.h>
#include <EditorPluginWater/WaterProfileAsset/WaterProfileEngineUi.h>
#include <EditorPluginWater/WaterProfileAsset/WaterProfileShellPanels.h>
#include <EditorUi/DockManager.h>
#include <EditorUi/Widgets.h>
#include <GuiFoundation/DocumentWindow/DocumentWindow.h>
#include <GuiFoundation/PropertyGrid/EnginePropertyGridWidget.h>

//////////////////////////////////////////////////////////////////////////
// plWaterProfileProperties

plWaterProfileProperties::plWaterProfileProperties(plAssetDocument* pDocument, plActionWindow* pContextWindow)
  : plEditorUiContent("WaterProfileProperties")
{
  m_pStatus = AddChild(plUniquePtr<plEditorUiContent>(PL_DEFAULT_NEW(plEditorUiAssetStatus, pDocument)));
  m_pFilter = static_cast<plWaterPropertyFilter*>(AddChild(plUniquePtr<plEditorUiContent>(PL_DEFAULT_NEW(plWaterPropertyFilter))));

  plUniquePtr<plEditorUiPropertyGrid> pGrid = PL_DEFAULT_NEW(plEditorUiPropertyGrid);
  pGrid->SetContextWindow(pContextWindow);
  pGrid->FollowSelection(pDocument);
  m_pGrid = static_cast<plEditorUiPropertyGrid*>(AddChild(std::move(pGrid)));

  // the grid shows the rows the filter's text matches
  m_pFilter->m_OnFilterChanged = [this](plStringView sText)
  { m_pGrid->SetFilter(sText); };
}

plWaterProfileProperties::~plWaterProfileProperties() = default;

void plWaterProfileProperties::SetFilterText(plStringView sText)
{
  m_pFilter->SetFilterText(sText);
  m_pGrid->SetFilter(sText);
}

void plWaterProfileProperties::OnUiUpdate()
{
  if (!IsReady() || GetSurface()->GetLogicalSize() == m_vPlacedSize)
    return;

  m_vPlacedSize = GetSurface()->GetLogicalSize();

  // the banner, the filter and the grid, top to bottom
  float y = plMath::Min(plEditorUiAssetStatus::BannerHeight, m_vPlacedSize.y);
  SetChildRect(m_pStatus, plRectFloat(0, 0, m_vPlacedSize.x, y));

  const float fFilter = plMath::Min(plWaterPropertyFilter::GetHeight(), m_vPlacedSize.y - y);
  SetChildRect(m_pFilter, plRectFloat(0, y, m_vPlacedSize.x, fFilter));
  y += fFilter;

  SetChildRect(m_pGrid, plRectFloat(0, y, m_vPlacedSize.x, plMath::Max(m_vPlacedSize.y - y, 0.0f)));
}

//////////////////////////////////////////////////////////////////////////
// Shell panels

namespace
{
  /// The window's layout: presets left of the preview, the properties right of it, the preview status and the
  /// diagnostics below it.
  void ArrangePanels(plEditorUiDockManager& ref_panels)
  {
    ref_panels.DockPanel("Schematic", "View", plEditorUiDockSide::Tab);
    ref_panels.DockPanel("Presets", "View", plEditorUiDockSide::Left, 0.2f);
    ref_panels.DockPanel("Properties", "View", plEditorUiDockSide::Right, 0.32f);
    ref_panels.DockPanel("Diagnostics", "View", plEditorUiDockSide::Bottom, 0.28f);
    ref_panels.DockPanel("PreviewStatus", "View", plEditorUiDockSide::Bottom, 0.16f);
  }

  void SetPanelIcons(plEditorUiDockManager& ref_panels)
  {
    using plEditorUi::Icon;

    ref_panels.SetPanelIcon("Schematic", plEditorUi::IconPath(Icon::Curve));
    ref_panels.SetPanelIcon("PreviewStatus", plEditorUi::IconPath(Icon::Info));
    ref_panels.SetPanelIcon("Presets", plEditorUi::IconPath(Icon::Preset));
    ref_panels.SetPanelIcon("Diagnostics", plEditorUi::IconPath(Icon::Debug));
  }

  /// The 2D Surface schematic (0) or the 3D preview (1) as the current tab of the preview's area.
  void ShowViewPanel(plEditorUiDockManager& ref_panels, plInt32 iView)
  {
    const char* szPanel = iView == 0 ? "Schematic" : "View";
    if (ref_panels.GetPanelSurface(szPanel) != nullptr && ref_panels.IsPanelOpen(szPanel))
      ref_panels.SetCurrentPanel(szPanel);
  }

  /// The panels and rows the window declares (plDocumentWindow::AddPanel, AddRow).
  void AddWindowPanels(plEditorUiDocumentFrame& ref_frame, const plDocumentWindow& window)
  {
    for (const plDocumentWindow::Panel& panel : window.GetPanels())
    {
      if (panel.m_pContent != nullptr)
        ref_frame.AddWindowPanel(panel.m_Desc, *panel.m_pContent);
    }

    for (const plDocumentWindow::Panel& row : window.GetRows())
      ref_frame.AddWindowRow(row.m_Desc, *row.m_pContent, row.m_fRowHeight);
  }

  class plWaterProfileContentProvider : public plEditorUiDocumentContentProvider
  {
  public:
    bool Matches(const plDocument* pDocument) const override
    {
      return plDynamicCast<const plWaterProfileAssetDocument*>(pDocument) != nullptr && plEngineAssetDocumentContentProvider::FindEngineWindow(pDocument) != nullptr;
    }

    void CreatePanels(plEditorUiDocumentFrame& ref_frame) const override
    {
      if (plUniquePtr<plEditorUiContent> pView = plEditorUiQuadViewPanel::CreateViewContent(plEngineAssetDocumentContentProvider::FindEngineWindow(ref_frame.GetDocument())))
        ref_frame.AddContent({"View", "3D Runtime"}, std::move(pView));

      const plDocumentWindow* pWindow = plDocumentWindow::FindWindowByDocument(ref_frame.GetDocument());
      if (pWindow == nullptr)
        return;

      AddWindowPanels(ref_frame, *pWindow);

      plEditorUiDockManager& panels = ref_frame.GetPanels();
      SetPanelIcons(panels);

      if (panels.GetPanelSurface("View") == nullptr)
        return;

      ArrangePanels(panels);

      const plWaterPreviewControls* pControls = pWindow->FindPanelContent<plWaterPreviewControls>();
      ShowViewPanel(panels, pControls != nullptr ? pControls->GetState().m_iView : 1);
    }
  };

  plWaterProfileContentProvider s_WaterProfileContentProvider;
} // namespace

void plWaterProfileShellPanels::Register()
{
  plEditorUiDocumentContentProvider::Register(&s_WaterProfileContentProvider);
}

void plWaterProfileShellPanels::Unregister()
{
  plEditorUiDocumentContentProvider::Unregister(&s_WaterProfileContentProvider);
}

void plWaterProfileShellPanels::ShowView(const plDocument* pDocument, plInt32 iView)
{
  const plEditorUiEngineShell* pShell = plEditorUiEngineShell::GetSingleton();
  plEditorUiDocumentFrame* pFrame = pShell != nullptr ? pShell->FindFrame(pDocument) : nullptr;
  if (pFrame != nullptr)
    ShowViewPanel(pFrame->GetPanels(), iView);
}
