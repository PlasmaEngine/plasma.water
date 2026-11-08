#pragma once

#include <EditorPluginWater/EditorPluginWaterDLL.h>
#include <EditorUi/Content.h>

class plActionWindow;
class plAssetDocument;
class plDocument;
class plEditorUiPropertyGrid;
class plWaterPropertyFilter;

/// \brief The Water Profile Properties panel: the asset's status banner, the property filter and the property grid,
/// which follows the document's selection and the filter's text.
class plWaterProfileProperties : public plEditorUiContent
{
public:
  /// pContextWindow: the document window the grid's extensions act for.
  plWaterProfileProperties(plAssetDocument* pDocument, plActionWindow* pContextWindow);
  ~plWaterProfileProperties() override;

  plWaterPropertyFilter& GetFilter() const { return *m_pFilter; }

  /// Shows the text in the filter and filters the grid with it.
  void SetFilterText(plStringView sText);

protected:
  void OnUiUpdate() override;

private:
  plEditorUiContent* m_pStatus = nullptr;     ///< a child content
  plWaterPropertyFilter* m_pFilter = nullptr; ///< a child content
  plEditorUiPropertyGrid* m_pGrid = nullptr;  ///< a child content
  plVec2 m_vPlacedSize = plVec2(-1.0f);
};

/// \brief The Water Profile window's panels in the engine shell.
///
/// The provider shows the 3D preview ("View") with the window's 2D Surface schematic as a tab beside it and the
/// window's preview status below it; the window's preview controls as a row under the document's toolbar; Water
/// Profiles on the left; Water Diagnostics below the preview; and the window's properties on the right.
namespace plWaterProfileShellPanels
{
  void Register();
  void Unregister();

  /// Shows the 2D Surface schematic (0) or the 3D preview (1) in the document's shell frame, as the window's View
  /// control picks it. Nothing happens while the shell does not show the document.
  void ShowView(const plDocument* pDocument, plInt32 iView);
} // namespace plWaterProfileShellPanels
