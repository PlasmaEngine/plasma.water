#include <EditorPluginWater/EditorPluginWaterPCH.h>

#include <EditorPluginWater/WaterProfileAsset/WaterProfileEngineUi.h>
#include <EditorUi/Menu.h>
#include <EditorUi/Runtime.h>
#include <EditorUi/Surface.h>
#include <EditorUi/Widgets.h>
#include <Foundation/Math/ColorScheme.h>
#include <Foundation/Utilities/ConversionUtils.h>
#include <GameUiPlugin/Components/UiCanvasComponent.h>

namespace
{
  using namespace plEditorUi;

  constexpr float s_fRowHeight = 31.0f; ///< a tool row: 3 px padding, a control, 3 px padding, the line below
  constexpr float s_fPresetRowHeight = 52.0f;
  constexpr float s_fGroupHeight = 24.0f;
  constexpr float s_fWheelStep = 40.0f;
  constexpr const char* s_szInteractIcon = ":/EditorPluginWater/Touch.svg";

  void Place(plUiElement* pElement, const plRectFloat& rect)
  {
    pElement->m_vAnchorMin = pElement->m_vAnchorMax = plVec2::MakeZero();
    pElement->m_vOffsets = plVec4(rect.x, rect.y, rect.width, rect.height);
  }

  /// A hairline at rect.
  plUiPanel* Rule(plUiElement* pParent, const plRectFloat& rect)
  {
    plUiPanel* pLine = Panel(pParent);
    Place(pLine, rect);
    pLine->m_Color = plEditorUi::Line();
    return pLine;
  }

  float LabelWidth(plStringView sText, plUInt32 uiSize = FontSize - 1)
  {
    return plMath::Ceil(MeasureText(sText, uiSize));
  }

  plColorGammaUB WithAlpha(plColorGammaUB color, plUInt8 uiAlpha)
  {
    color.a = uiAlpha;
    return color;
  }

  plColorGammaUB Mix(const plColorGammaUB& a, const plColorGammaUB& b, float t)
  {
    return plColorGammaUB(static_cast<plUInt8>(a.r + (b.r - a.r) * t), static_cast<plUInt8>(a.g + (b.g - a.g) * t), static_cast<plUInt8>(a.b + (b.b - a.b) * t), static_cast<plUInt8>(a.a + (b.a - a.a) * t));
  }

  plColorGammaUB Darker(const plColorGammaUB& color, float fFactor)
  {
    return plColorGammaUB(static_cast<plUInt8>(color.r / fFactor), static_cast<plUInt8>(color.g / fFactor), static_cast<plUInt8>(color.b / fFactor), color.a);
  }

  plColorGammaUB SeverityColor(plWaterUi::Severity severity)
  {
    switch (severity)
    {
      case plWaterUi::Severity::Error:
        return Danger();
      case plWaterUi::Severity::Warning:
        return Warning();
      default:
        return Axis(1);
    }
  }

  /// A colour band from top to bottom, as horizontal strips.
  void VerticalGradient(plUiDrawListWriter& ref_writer, const plRectFloat& rect, const plColorGammaUB& top, const plColorGammaUB& bottom, plUInt32 uiSteps = 32)
  {
    if (rect.height <= 0.0f)
      return;

    const float fStep = rect.height / uiSteps;
    for (plUInt32 i = 0; i < uiSteps; ++i)
      ref_writer.Rect(plRectFloat(rect.x, rect.y + i * fStep, rect.width, fStep + 0.5f), Mix(top, bottom, (i + 0.5f) / uiSteps));
  }

  /// A text button (plate with a hairline border); the primary one on the selection fill with an accent border.
  plUiButton* ToolButton(plUiElement* pParent, const plRectFloat& rect, plStringView sLabel, plStringView sAction, bool bPrimary = false)
  {
    plUiButton* pButton = TextButton(pParent, sLabel, sAction);
    Place(pButton, rect);

    if (bPrimary)
    {
      pButton->m_Color = Selection();
      pButton->m_BorderColor = WithAlpha(Accent(), 115);
    }

    return pButton;
  }

  float ToolButtonWidth(plStringView sLabel)
  {
    return LabelWidth(sLabel) + 22.0f;
  }

  /// A dropdown: a field well with the choice and a chevron.
  plUiButton* Dropdown(plUiElement* pParent, const plRectFloat& rect, plStringView sValue, plStringView sAction)
  {
    plUiButton* pButton = Button(pParent, "", sAction);
    Place(pButton, rect);
    pButton->ClearChildren();
    pButton->m_Color = Field();
    pButton->m_HoverColor = Raised();
    pButton->m_fBorderWidth = 1.0f;
    pButton->m_BorderColor = plEditorUi::Line();
    pButton->m_vCornerRadius = plVec4(4);

    plUiText* pText = Text(pButton, sValue);
    Fill(pText, plVec4(8, 0, 22, 0));
    pText->m_uiFontSize = FontSize - 1;

    plUiImage* pChevron = ButtonIcon(pButton, Icon::Expand);
    pChevron->m_vAnchorMin = pChevron->m_vAnchorMax = plVec2(1, 0.5f);
    pChevron->m_vOffsets = plVec4(-18, -6, 12, 12);
    pChevron->m_Color = Dim();
    return pButton;
  }

  float DropdownWidth(plArrayPtr<const plString> choices)
  {
    float fWidth = 40.0f;
    for (const plString& sChoice : choices)
      fWidth = plMath::Max(fWidth, LabelWidth(sChoice));
    return fWidth + 32.0f;
  }

  /// A check box (style pass D9: a field well, the selection fill with the accent tick when on) and its label.
  plUiButton* CheckButton(plUiElement* pParent, const plRectFloat& rect, plStringView sLabel, plStringView sAction, bool bChecked)
  {
    plUiButton* pButton = Button(pParent, "", sAction);
    Place(pButton, rect);
    pButton->ClearChildren();
    pButton->m_Color = plColorGammaUB(0, 0, 0, 0);
    pButton->m_hStyle = GetGhostStyle();

    plUiPanel* pBox = Panel(pButton);
    pBox->m_vAnchorMin = pBox->m_vAnchorMax = plVec2(0, 0.5f);
    pBox->m_vOffsets = plVec4(2, -7, 14, 14);
    pBox->m_Color = bChecked ? Selection() : Field();
    pBox->m_fBorderWidth = 1.0f;
    pBox->m_BorderColor = bChecked ? WithAlpha(Accent(), 153) : Border();
    pBox->m_vCornerRadius = plVec4(3);

    if (bChecked)
    {
      plUiImage* pTick = SvgImage(pBox, IconPath(Icon::Check));
      Fill(pTick, plVec4(1, 1, 1, 1));
      pTick->m_Color = Accent();
    }

    plUiText* pText = Text(pButton, sLabel);
    Fill(pText, plVec4(22, 0, 2, 0));
    pText->m_uiFontSize = FontSize - 1;
    pText->m_Color = Muted();
    return pButton;
  }

  float CheckWidth(plStringView sLabel)
  {
    return LabelWidth(sLabel) + 28.0f;
  }

  /// A borderless icon toggle: on, the accent icon on the selection plate.
  plUiButton* IconToggle(plUiElement* pParent, const plRectFloat& rect, plStringView sIcon, plStringView sAction, bool bOn)
  {
    if (bOn)
    {
      plUiPanel* pPlate = Panel(pParent);
      Place(pPlate, rect);
      pPlate->m_Color = Selection();
      pPlate->m_vCornerRadius = plVec4(4);
    }

    plUiButton* pButton = Button(pParent, "", sAction);
    Place(pButton, rect);
    pButton->ClearChildren();
    pButton->m_Color = plColorGammaUB(0, 0, 0, 0);
    pButton->m_hStyle = GetGhostStyle();

    plUiImage* pIcon = ButtonIcon(pButton, sIcon);
    pIcon->m_Color = bOn ? Accent() : Muted();
    return pButton;
  }

  /// A segmented control: a field well holding one button per label; the current one raised with an accent
  /// underline. Buttons raise "<prefix>.<index>".
  void Segmented(plUiElement* pParent, const plRectFloat& rect, plArrayPtr<const plString> labels, plInt32 iCurrent, plStringView sPrefix, plBlackboard& ref_board, plArrayPtr<const plString> tooltips = {})
  {
    plUiPanel* pWell = Panel(pParent);
    Place(pWell, rect);
    pWell->m_Color = Field();
    pWell->m_fBorderWidth = 1.0f;
    pWell->m_BorderColor = plEditorUi::Line();
    pWell->m_vCornerRadius = plVec4(6);

    float x = 2.0f;
    for (plUInt32 i = 0; i < labels.GetCount(); ++i)
    {
      plStringBuilder label = labels[i];
      label.ToUpper();
      const float fWidth = LabelWidth(label, MinFontSize + 1) + 20.0f;
      const plRectFloat item(x, 2.0f, fWidth, rect.height - 4.0f);
      const bool bOn = static_cast<plInt32>(i) == iCurrent;

      if (bOn)
      {
        plUiPanel* pPlate = Panel(pWell);
        Place(pPlate, item);
        pPlate->m_Color = Raised();
        pPlate->m_vCornerRadius = plVec4(4);

        plUiPanel* pEdge = Panel(pPlate);
        pEdge->m_vAnchorMin.y = 1;
        pEdge->m_vOffsets = plVec4(0, -2, 0, 2);
        pEdge->m_Color = Accent();
      }

      plStringBuilder action;
      action.SetFormat("{}.{}", sPrefix, i);

      plUiButton* pButton = Button(pWell, label, action);
      Place(pButton, item);
      pButton->m_Color = plColorGammaUB(0, 0, 0, 0);
      pButton->m_hStyle = GetGhostStyle();
      auto* pText = static_cast<plUiText*>(pButton->m_Children[0]);
      Fill(pText);
      pText->m_uiFontSize = MinFontSize + 1;
      pText->m_Color = bOn ? Foreground() : Muted();

      if (i < tooltips.GetCount())
        SetToolTip(ref_board, action, tooltips[i]);

      x += fWidth + 2.0f;
    }
  }

  float SegmentedWidth(plArrayPtr<const plString> labels)
  {
    float fWidth = 2.0f;
    for (const plString& sLabel : labels)
    {
      plStringBuilder label = sLabel;
      label.ToUpper();
      fWidth += LabelWidth(label, MinFontSize + 1) + 22.0f;
    }
    return fWidth + 2.0f;
  }

  /// The texture of a preset icon's background: shallow at the top to deep at the bottom.
  plString GradientTexture(const plColorGammaUB& top, const plColorGammaUB& bottom)
  {
    plStringBuilder name;
    name.SetFormat("WaterPreset/{}-{}-{}-{}-{}-{}", top.r, top.g, top.b, bottom.r, bottom.g, bottom.b);

    plColorGammaUB pixels[42];
    for (plUInt32 i = 0; i < 42; ++i)
      pixels[i] = Mix(plColorGammaUB(top.r, top.g, top.b, 255), plColorGammaUB(bottom.r, bottom.g, bottom.b, 255), i / 41.0f);

    ImageTexture(name, 1, 42, pixels);
    return plStringBuilder("EditorUi/Image/", name);
  }

  plString IconTexture(Icon icon)
  {
    plString sId;
    SvgTexture(IconPath(icon), &sId);
    return sId;
  }
} // namespace

//////////////////////////////////////////////////////////////////////////
// plWaterPresetPanel

plWaterPresetPanel::plWaterPresetPanel()
  : plEditorUiContent("WaterPresets")
  , m_Search(*GetSurface(), "Water.Search")
  , m_pAlive(std::make_shared<bool>(true))
{
  m_Search.m_Events.AddEventHandler(plMakeDelegate(&plWaterPresetPanel::OnSearch, this));

  if (IsReady())
    m_PointerSubscription = GetSurface()->m_PointerEvents.AddEventHandler(plMakeDelegate(&plWaterPresetPanel::OnPointer, this));
}

plWaterPresetPanel::~plWaterPresetPanel()
{
  if (IsReady())
    GetSurface()->m_PointerEvents.RemoveEventHandler(m_PointerSubscription);

  m_Search.m_Events.RemoveEventHandler(plMakeDelegate(&plWaterPresetPanel::OnSearch, this));
}

void plWaterPresetPanel::SetRows(plArrayPtr<const plWaterUi::PresetRow> rows)
{
  m_Rows = rows;
  UpdateLines();
  DrawRows();
}

void plWaterPresetPanel::SetSelectedPreset(plInt32 iPreset)
{
  m_iSelectedPreset = iPreset;
  DrawRows();
}

void plWaterPresetPanel::ShowDiff(const plWaterUi::DiffCard& card)
{
  m_Diff = card;
  m_bDiff = true;
  m_bRebuild = true;
}

void plWaterPresetPanel::HideDiff()
{
  if (!m_bDiff)
    return;

  m_bDiff = false;
  m_bRebuild = true;
}

void plWaterPresetPanel::OnSearch(const plEditorUiSearchFieldEvent& e)
{
  if (e.m_Type != plEditorUiSearchFieldEvent::Type::TextChanged)
    return;

  m_fScroll = 0.0f;
  UpdateLines();
  DrawRows();
}

void plWaterPresetPanel::UpdateLines()
{
  m_Lines.Clear();
  m_iHoverLine = -1;

  const plString sSearch = m_Search.GetText();

  // BUILT-IN, then PROJECT when the project has presets
  for (const bool bProject : {false, true})
  {
    bool bAny = false;
    for (const plWaterUi::PresetRow& row : m_Rows)
      bAny = bAny || row.m_bProject == bProject;

    if (bProject && !bAny)
      continue;

    Line& header = m_Lines.ExpandAndGetRef();
    header.m_bProject = bProject;

    if (m_bCollapsed[bProject ? 1 : 0])
      continue;

    for (plUInt32 i = 0; i < m_Rows.GetCount(); ++i)
    {
      const plWaterUi::PresetRow& row = m_Rows[i];
      if (row.m_bProject != bProject)
        continue;
      if (m_iTypeFilter >= 0 && row.m_uiBodyType != m_iTypeFilter)
        continue;
      if (!sSearch.IsEmpty() && row.m_sName.FindSubString_NoCase(sSearch) == nullptr)
        continue;

      Line& line = m_Lines.ExpandAndGetRef();
      line.m_iRow = static_cast<plInt32>(i);
      line.m_bProject = bProject;
    }
  }

  ClampScroll();
}

float plWaterPresetPanel::GetDiffHeight() const
{
  if (!m_bDiff)
    return 0.0f;

  const plUInt32 uiLines = m_Diff.m_Rows.GetCount() + (m_Diff.m_sMore.IsEmpty() ? 0 : 1);
  return 8.0f + 26.0f + 6.0f + uiLines * 15.0f + 6.0f + ControlHeight + 8.0f + 8.0f;
}

float plWaterPresetPanel::GetFooterHeight(float fWidth) const
{
  const float fAll = ToolButtonWidth("Apply Preset...") + ToolButtonWidth("Save As...") + ToolButtonWidth("Reset to Defaults") + 8.0f + 12.0f;
  return fAll <= fWidth ? s_fRowHeight : 2.0f * s_fRowHeight - 3.0f;
}

plRectFloat plWaterPresetPanel::GetListRect() const
{
  const plVec2 size = GetSurface()->GetLogicalSize();
  const float fTop = 2.0f * s_fRowHeight;
  const float fBottom = GetDiffHeight() + GetFooterHeight(size.x);
  return plRectFloat(0, fTop, size.x, plMath::Max(size.y - fTop - fBottom, 1.0f));
}

void plWaterPresetPanel::ClampScroll()
{
  if (!IsReady())
    return;

  float fContent = 0.0f;
  for (const Line& line : m_Lines)
    fContent += line.m_iRow < 0 ? s_fGroupHeight : s_fPresetRowHeight;

  m_fScroll = plMath::Clamp(m_fScroll, 0.0f, plMath::Max(fContent - GetListRect().height, 0.0f));
}

plInt32 plWaterPresetPanel::LineAt(const plVec2& vPos) const
{
  const plRectFloat list = GetListRect();
  if (!list.Contains(vPos))
    return -1;

  float y = list.y - m_fScroll;
  for (plUInt32 i = 0; i < m_Lines.GetCount(); ++i)
  {
    const float fHeight = m_Lines[i].m_iRow < 0 ? s_fGroupHeight : s_fPresetRowHeight;
    if (vPos.y >= y && vPos.y < y + fHeight)
      return static_cast<plInt32>(i);
    y += fHeight;
  }

  return -1;
}

void plWaterPresetPanel::OnUiUpdate()
{
  if (!IsReady())
    return;

  if (m_bRebuild || GetSurface()->GetLogicalSize() != m_vBuiltSize)
    BuildDocument();
}

void plWaterPresetPanel::BuildDocument()
{
  m_bRebuild = false;
  m_vBuiltSize = GetSurface()->GetLogicalSize();
  const float fWidth = plMath::Max(m_vBuiltSize.x, 120.0f);
  auto& board = GetSurface()->GetBlackboard();

  plUiPanel* pRoot = Panel();
  pRoot->m_Color = Surface();

  // search
  {
    plUiPanel* pRow = Panel(pRoot);
    Place(pRow, plRectFloat(6, 3, fWidth - 12, ControlHeight));
    pRow->m_Color = plColorGammaUB(0, 0, 0, 0);
    pRow->m_bClipChildren = false;
    m_Search.Build(pRow, "Search profiles...");
    Rule(pRoot, plRectFloat(0, s_fRowHeight - 1, fWidth, 1));
  }

  // body type filter
  {
    const plString labels[] = {"All", "Ocean", "River", "Lake"};
    Segmented(pRoot, plRectFloat(6, s_fRowHeight + 3, SegmentedWidth(labels), ControlHeight), labels, m_iTypeFilter + 1, "Water.Filter", board);
    Rule(pRoot, plRectFloat(0, 2 * s_fRowHeight - 1, fWidth, 1));
  }

  // the list, drawn
  const plRectFloat list = GetListRect();
  {
    plUiPanel* pList = Panel(pRoot);
    Place(pList, list);
    pList->m_Color = Surface();
    plEditorUi::DrawList(pList, "Water.Presets", "Water.PresetsDraw");
  }

  // the diff card of a pending apply
  if (m_bDiff)
  {
    const float fTop = list.y + list.height + 8.0f;
    plUiPanel* pCard = Card(pRoot, plRectFloat(8, fTop, fWidth - 16, GetDiffHeight() - 16.0f));
    pCard->m_Color = Plate();

    plUiText* pTitle = Text(pCard, m_Diff.m_sTitle);
    Place(pTitle, plRectFloat(10, 0, fWidth - 36, 26));
    pTitle->m_uiFontSize = FontSize - 1;
    Rule(pCard, plRectFloat(0, 26, fWidth - 16, 1));

    float y = 32.0f;
    for (const plString& sRow : m_Diff.m_Rows)
    {
      plUiText* pRowText = Text(pCard, sRow);
      Place(pRowText, plRectFloat(10, y, fWidth - 36, 15));
      pRowText->m_hFont = GetCodeFont();
      pRowText->m_uiFontSize = MinFontSize + 1;
      pRowText->m_Color = Muted();
      y += 15.0f;
    }

    if (!m_Diff.m_sMore.IsEmpty())
    {
      plUiText* pMore = Text(pCard, m_Diff.m_sMore);
      Place(pMore, plRectFloat(10, y, fWidth - 36, 15));
      pMore->m_hFont = GetCodeFont();
      pMore->m_uiFontSize = MinFontSize + 1;
      pMore->m_Color = Dim();
      y += 15.0f;
    }

    y += 6.0f;
    float x = 10.0f;
    ToolButton(pCard, plRectFloat(x, y, ToolButtonWidth("Apply"), ControlHeight), "Apply", "Water.DiffApply", true);
    x += ToolButtonWidth("Apply") + 6.0f;
    ToolButton(pCard, plRectFloat(x, y, ToolButtonWidth("Keep Colors"), ControlHeight), "Keep Colors", "Water.DiffKeep");
    x += ToolButtonWidth("Keep Colors") + 6.0f;
    ToolButton(pCard, plRectFloat(x, y, ToolButtonWidth("Cancel"), ControlHeight), "Cancel", "Water.DiffCancel");

    SetToolTip(board, "Water.DiffKeep", "Apply the preset but keep the current Shallow/Deep/Foam colors, Absorption and Scattering.");
  }

  // Apply Preset..., Save As..., Reset to Defaults
  {
    const float fFooter = GetFooterHeight(fWidth);
    const float fTop = m_vBuiltSize.y - fFooter;
    Rule(pRoot, plRectFloat(0, fTop, fWidth, 1));

    float x = 6.0f;
    float y = fTop + 4.0f;
    ToolButton(pRoot, plRectFloat(x, y, ToolButtonWidth("Apply Preset..."), ControlHeight), "Apply Preset...", "Water.ApplyPreset");
    x += ToolButtonWidth("Apply Preset...") + 4.0f;
    ToolButton(pRoot, plRectFloat(x, y, ToolButtonWidth("Save As..."), ControlHeight), "Save As...", "Water.SaveAs");
    x += ToolButtonWidth("Save As...") + 4.0f;

    if (fFooter > s_fRowHeight)
    {
      x = 6.0f;
      y += s_fRowHeight - 3.0f;
    }

    ToolButton(pRoot, plRectFloat(x, y, ToolButtonWidth("Reset to Defaults"), ControlHeight), "Reset to Defaults", "Water.Reset");

    SetToolTip(board, "Water.ApplyPreset", "Shows what the selected preset would change before applying.");
    SetToolTip(board, "Water.SaveAs", "Saves the current profile as a project preset (Editor/WaterPresets).");
  }

  SetDocument(Finish(pRoot));
  ClampScroll();
  DrawRows();
}

void plWaterPresetPanel::DrawRows()
{
  if (!IsReady())
    return;

  const plRectFloat list = GetListRect();
  m_Writer.Clear();

  float y = -m_fScroll;
  for (plUInt32 i = 0; i < m_Lines.GetCount(); ++i)
  {
    const Line& line = m_Lines[i];

    if (line.m_iRow < 0)
    {
      // a group header: chevron and the uppercase group name
      if (y + s_fGroupHeight > 0 && y < list.height)
      {
        plStringBuilder title(m_bCollapsed[line.m_bProject ? 1 : 0] ? TreeCollapsed : TreeExpanded, line.m_bProject ? "PROJECT" : "BUILT-IN");
        m_Writer.Text(plRectFloat(12, y, list.width - 20, s_fGroupHeight), title, Dim(), MinFontSize + 1);
      }
      y += s_fGroupHeight;
      continue;
    }

    const plWaterUi::PresetRow& row = m_Rows[line.m_iRow];
    if (y + s_fPresetRowHeight > 0 && y < list.height)
    {
      const bool bSelected = static_cast<plInt32>(row.m_uiPreset) == m_iSelectedPreset;
      if (bSelected)
        m_Writer.Rect(plRectFloat(0, y, list.width, s_fPresetRowHeight), Selection());
      else if (static_cast<plInt32>(i) == m_iHoverLine)
        m_Writer.Rect(plRectFloat(0, y, list.width, s_fPresetRowHeight), WithAlpha(Raised(), 140));

      // the icon: shallow to deep with a wave line from the wavelength and amplitude
      const plRectFloat icon(8, y + 5, 72, 42);
      m_Writer.Image(icon, GradientTexture(row.m_Shallow, row.m_Deep));

      const float fFrequency = plMath::Clamp(2.5f / plMath::Max(row.m_fWavelength, 0.5f), 0.05f, 0.6f);
      const float fAmplitude = plMath::Clamp(1.5f + row.m_fAmplitude * 3.0f, 1.5f, 8.0f);
      plHybridArray<plVec2, 80> wave;
      for (plUInt32 x = 0; x <= 72; x += 2)
        wave.PushBack(plVec2(icon.x + x, icon.y + icon.height * 0.45f + plMath::Sin(plAngle::MakeFromRadian(x * fFrequency)) * fAmplitude));
      m_Writer.Polyline(wave, plColorGammaUB(220, 244, 248, 180), 1.5f);
      m_Writer.RectOutline(icon, plEditorUi::Line(), 1.0f, 4.0f);

      m_Writer.Text(plRectFloat(88, y + 8, list.width - 96, 18), row.m_sName, Foreground(), FontSize);
      m_Writer.Text(plRectFloat(88, y + 27, list.width - 96, 16), row.m_sDetail, Dim(), MinFontSize + 1, plUiTextHAlign::Left, plUiTextVAlign::Center, true);
    }
    y += s_fPresetRowHeight;
  }

  if (m_Lines.GetCount() <= 1)
    m_Writer.Text(plRectFloat(12, y, list.width - 20, 22), "No preset matches", Dim(), FontSize - 1);

  m_Writer.Publish(GetSurface()->GetBlackboard(), plMakeHashedString("Water.PresetsDraw"));
}

void plWaterPresetPanel::OnPointer(const plEditorUiPointerEvent& e)
{
  using Type = plEditorUiPointerEvent::Type;

  const bool bOverList = e.m_sElement.IsEmpty() || e.m_sElement == "Water.Presets";
  const plInt32 iLine = bOverList ? LineAt(e.m_vPosition) : -1;

  switch (e.m_Type)
  {
    case Type::Move:
      if (iLine != m_iHoverLine)
      {
        m_iHoverLine = iLine;
        DrawRows();
      }
      return;

    case Type::Press:
    case Type::DoubleClick:
    {
      if (iLine < 0 || e.m_Button != plEditorUiPointerEvent::Button::Left)
        return;

      const Line& line = m_Lines[iLine];
      if (line.m_iRow < 0)
      {
        if (e.m_Type == Type::Press)
        {
          bool& bCollapsed = m_bCollapsed[line.m_bProject ? 1 : 0];
          bCollapsed = !bCollapsed;
          UpdateLines();
          DrawRows();
        }
        return;
      }

      m_iSelectedPreset = static_cast<plInt32>(m_Rows[line.m_iRow].m_uiPreset);
      DrawRows();

      // W5-D10: a double-click shows the diff card too
      if (e.m_Type == Type::DoubleClick && m_OnActivate.IsValid())
        m_OnActivate(m_iSelectedPreset);
      return;
    }

    case Type::Wheel:
      if (!GetListRect().Contains(e.m_vPosition))
        return;
      m_fScroll -= e.m_fWheel * s_fWheelStep;
      ClampScroll();
      DrawRows();
      return;

    default:
      return;
  }
}

void plWaterPresetPanel::OnUiEvent(const plMsgUiEvent& event)
{
  const plStringView sId = event.m_sIdentifier.GetView();

  if (sId.StartsWith("Water.Filter."))
  {
    plInt32 iFilter = 0;
    plConversionUtils::StringToInt(sId.GetSubString(13, 2), iFilter).IgnoreResult();
    m_iTypeFilter = iFilter - 1;
    m_fScroll = 0.0f;
    UpdateLines();
    m_bRebuild = true;
  }
  else if (sId == "Water.ApplyPreset" && m_OnApplyPreset.IsValid())
    m_OnApplyPreset();
  else if (sId == "Water.SaveAs" && m_OnSaveAs.IsValid())
  {
    // the prompt opens after the click, and only while the panel lives
    std::weak_ptr<bool> alive = m_pAlive;
    plEditorUiRuntime::RunAfterFrame([alive, this]()
      {
        if (!alive.expired())
          m_OnSaveAs(); });
  }
  else if (sId == "Water.Reset" && m_OnReset.IsValid())
    m_OnReset();
  else if (sId == "Water.DiffApply" && m_OnApplyPending.IsValid())
    m_OnApplyPending(false);
  else if (sId == "Water.DiffKeep" && m_OnApplyPending.IsValid())
    m_OnApplyPending(true);
  else if (sId == "Water.DiffCancel" && m_OnCancelPending.IsValid())
    m_OnCancelPending();
}

//////////////////////////////////////////////////////////////////////////
// plWaterPreviewControls

namespace
{
  const plString s_BodyChoices[] = {"Ocean", "River", "Lake"};
  const plString s_SceneChoices[] = {"Auto Scene", "Open Ocean", "Shoreline", "River Bend", "Lake Bowl"};
  const plString s_ViewChoices[] = {"2D Surface", "3D Runtime"};
  const plString s_BookmarkNames[] = {"Grazing", "Shore", "Overhead", "Underwater"};
  const plString s_BookmarkTips[] = {
    "Low camera just above the surface \xE2\x80\x94 judge sun glitter, reflections and steepness.",
    "Mid-height view of the shoreline \xE2\x80\x94 judge shore fades, shore foam and refraction.",
    "Top-down view \xE2\x80\x94 judge wave patterns, wind direction and foam coverage.",
    "Submerged camera \xE2\x80\x94 judge underwater fog, caustics and the meniscus band."};

  constexpr float s_fControlRow = 30.0f;

  plString ControlAction(plWaterPreviewControls::Control control)
  {
    plStringBuilder s;
    s.SetFormat("Water.Ctl.{}", static_cast<plUInt32>(control));
    return s;
  }
} // namespace

plWaterPreviewControls::plWaterPreviewControls()
  : plEditorUiContent("WaterPreviewControls")
{
}

void plWaterPreviewControls::SetQualityNames(plArrayPtr<const plString> names)
{
  m_QualityNames = names;
  m_bRebuild = true;
}

void plWaterPreviewControls::SetState(const State& state)
{
  m_State = state;
  m_bRebuild = true;
}

float plWaterPreviewControls::GetHeight() const
{
  return m_uiRows * s_fControlRow + 1.0f;
}

plArrayPtr<const plString> plWaterPreviewControls::GetChoices(Control control) const
{
  switch (control)
  {
    case Control::Body:
      return s_BodyChoices;
    case Control::Scene:
      return s_SceneChoices;
    case Control::View:
      return s_ViewChoices;
    case Control::Quality:
      return m_QualityNames;
    default:
      return {};
  }
}

plInt32 plWaterPreviewControls::GetValue(Control control) const
{
  switch (control)
  {
    case Control::Body:
      return m_State.m_iBody;
    case Control::Scene:
      return m_State.m_iScene;
    case Control::View:
      return m_State.m_iView;
    case Control::Quality:
      return m_State.m_iQuality;
    case Control::Freeze:
      return m_State.m_bFreeze ? 1 : 0;
    case Control::Interact:
      return m_State.m_bInteract ? 1 : 0;
    case Control::Cascades:
      return m_State.m_bCascades ? 1 : 0;
    case Control::Direction:
      return m_State.m_bDirection ? 1 : 0;
    case Control::Bookmark:
      return m_State.m_iBookmark;
  }
  return 0;
}

float plWaterPreviewControls::GetBookmarksWidth() const
{
  return SegmentedWidth(s_BookmarkNames);
}

void plWaterPreviewControls::CollectItems(plDynamicArray<Item>& out_items) const
{
  auto add = [&](Item::Kind kind, Control control, plStringView sText, float fWidth)
  {
    Item& item = out_items.ExpandAndGetRef();
    item.m_Kind = kind;
    item.m_Control = control;
    item.m_sText = sText;
    item.m_fWidth = fWidth;
  };

  const Control dropdowns[] = {Control::Body, Control::Scene, Control::View, Control::Quality};
  const char* labels[] = {"Preview", "Scene", "View", "Quality"};
  for (plUInt32 i = 0; i < 4; ++i)
  {
    add(Item::Kind::Label, dropdowns[i], labels[i], LabelWidth(labels[i]) + 4.0f);
    add(Item::Kind::Dropdown, dropdowns[i], {}, DropdownWidth(GetChoices(dropdowns[i])));
  }

  add(Item::Kind::Separator, Control::Body, {}, 9.0f);
  add(Item::Kind::Check, Control::Freeze, "Freeze", CheckWidth("Freeze"));
  add(Item::Kind::Toggle, Control::Interact, {}, 26.0f);

  if (m_State.m_iView == 0)
  {
    add(Item::Kind::Check, Control::Cascades, "Cascades", CheckWidth("Cascades"));
    add(Item::Kind::Check, Control::Direction, "Direction", CheckWidth("Direction"));
  }

  add(Item::Kind::Bookmarks, Control::Bookmark, {}, GetBookmarksWidth());
}

void plWaterPreviewControls::OnUiUpdate()
{
  if (!IsReady())
    return;

  if (m_bRebuild || GetSurface()->GetLogicalSize().x != m_vBuiltSize.x)
    BuildDocument();
}

void plWaterPreviewControls::BuildDocument()
{
  m_bRebuild = false;
  m_vBuiltSize = GetSurface()->GetLogicalSize();
  const float fWidth = plMath::Max(m_vBuiltSize.x, 200.0f);
  auto& board = GetSurface()->GetBlackboard();

  plDynamicArray<Item> items;
  CollectItems(items);

  plUiPanel* pRoot = Panel();
  pRoot->m_Color = Surface();

  // left to right, wrapping; the bookmarks go to the right end of the last row
  float x = 6.0f;
  plUInt32 uiRow = 0;
  m_DropdownRects.Clear();

  for (const Item& item : items)
  {
    // a label sits close to its dropdown; a dropdown keeps its distance to the next label
    const float fGap = item.m_Kind == Item::Kind::Label ? 2.0f : (item.m_Kind == Item::Kind::Dropdown ? 12.0f : 6.0f);
    if (x + item.m_fWidth > fWidth - 6.0f && x > 6.0f)
    {
      x = 6.0f;
      ++uiRow;
    }

    const float y = uiRow * s_fControlRow + 3.0f;
    plRectFloat rect(x, y, item.m_fWidth, ControlHeight);
    const plString sAction = ControlAction(item.m_Control);

    switch (item.m_Kind)
    {
      case Item::Kind::Label:
      {
        plUiText* pText = Text(pRoot, item.m_sText);
        Place(pText, rect);
        pText->m_uiFontSize = FontSize - 1;
        pText->m_Color = Muted();
        break;
      }

      case Item::Kind::Dropdown:
      {
        const plArrayPtr<const plString> choices = GetChoices(item.m_Control);
        const plInt32 iValue = GetValue(item.m_Control);
        Dropdown(pRoot, rect, iValue >= 0 && iValue < static_cast<plInt32>(choices.GetCount()) ? choices[iValue].GetView() : plStringView(), sAction);
        m_DropdownRects[sAction] = rect;
        break;
      }

      case Item::Kind::Check:
        CheckButton(pRoot, rect, item.m_sText, sAction, GetValue(item.m_Control) != 0);
        break;

      case Item::Kind::Toggle:
        IconToggle(pRoot, rect, s_szInteractIcon, sAction, m_State.m_bInteract);
        SetToolTip(board, sAction, "Interact: click or drag the water surface to spawn ripples (left mouse button; right button still orbits).\nRipples keep animating even while Freeze is on.");
        break;

      case Item::Kind::Separator:
        Rule(pRoot, plRectFloat(x + 4.0f, y + 4.0f, 1.0f, ControlHeight - 8.0f));
        break;

      case Item::Kind::Bookmarks:
        rect.x = plMath::Max(x, fWidth - 6.0f - item.m_fWidth);
        Segmented(pRoot, rect, s_BookmarkNames, m_State.m_iBookmark, "Water.Bookmark", board, s_BookmarkTips);
        break;
    }

    x += item.m_fWidth + fGap;
  }

  SetToolTip(board, ControlAction(Control::Scene), "Preview environment. Auto follows the body type; Shoreline makes shore fades, shore foam and absorption readable.");

  const plUInt32 uiRows = uiRow + 1;
  Rule(pRoot, plRectFloat(0, uiRows * s_fControlRow, fWidth, 1));
  SetDocument(Finish(pRoot));

  if (uiRows != m_uiRows)
  {
    m_uiRows = uiRows;
    if (m_OnHeightChanged.IsValid())
      m_OnHeightChanged(GetHeight());
  }
}

void plWaterPreviewControls::OpenMenu(Control control, const plVec2& vPosition)
{
  const plArrayPtr<const plString> choices = GetChoices(control);
  const plInt32 iValue = GetValue(control);

  plEditorUiMenu menu;
  for (plUInt32 i = 0; i < choices.GetCount(); ++i)
  {
    plEditorUiMenuItem& item = menu.AddAction(choices[i], i);
    item.m_bCheckable = true;
    item.m_bChecked = static_cast<plInt32>(i) == iValue;
  }

  plEditorUiMenuView::Show(*GetSurface(), vPosition, menu, [this, control](plUInt32 uiPicked)
    { Change(control, static_cast<plInt32>(uiPicked)); });
}

void plWaterPreviewControls::Change(Control control, plInt32 iValue)
{
  switch (control)
  {
    case Control::Body:
      m_State.m_iBody = iValue;
      break;
    case Control::Scene:
      m_State.m_iScene = iValue;
      break;
    case Control::View:
      m_State.m_iView = iValue;
      break;
    case Control::Quality:
      m_State.m_iQuality = iValue;
      break;
    case Control::Freeze:
      m_State.m_bFreeze = iValue != 0;
      break;
    case Control::Interact:
      m_State.m_bInteract = iValue != 0;
      break;
    case Control::Cascades:
      m_State.m_bCascades = iValue != 0;
      break;
    case Control::Direction:
      m_State.m_bDirection = iValue != 0;
      break;
    case Control::Bookmark:
      m_State.m_iBookmark = iValue;
      break;
  }

  m_bRebuild = true;
  if (m_OnChanged.IsValid())
    m_OnChanged(control, iValue);
}

void plWaterPreviewControls::OnUiEvent(const plMsgUiEvent& event)
{
  const plStringView sId = event.m_sIdentifier.GetView();

  if (sId.StartsWith("Water.Bookmark."))
  {
    plInt32 iBookmark = 0;
    plConversionUtils::StringToInt(sId.GetSubString(15, 2), iBookmark).IgnoreResult();
    Change(Control::Bookmark, iBookmark);
    return;
  }

  if (!sId.StartsWith("Water.Ctl."))
    return;

  plInt32 iControl = 0;
  plConversionUtils::StringToInt(sId.GetSubString(10, 2), iControl).IgnoreResult();
  const Control control = static_cast<Control>(iControl);

  switch (control)
  {
    case Control::Body:
    case Control::Scene:
    case Control::View:
    case Control::Quality:
    {
      const plRectFloat* pRect = m_DropdownRects.GetValue(plString(sId));
      if (pRect != nullptr)
        OpenMenu(control, plVec2(pRect->x, pRect->y + pRect->height + 2.0f));
      break;
    }

    default:
      Change(control, GetValue(control) != 0 ? 0 : 1);
      break;
  }
}

//////////////////////////////////////////////////////////////////////////
// plWaterSchematicView

namespace
{
  constexpr plTime s_SchematicStep = plTime::MakeFromMilliseconds(33);
}

plWaterSchematicView::plWaterSchematicView()
  : plEditorUiContent("WaterSchematic")
{
  m_LastStep = plTime::Now();
}

plWaterSchematicView::~plWaterSchematicView() = default;

void plWaterSchematicView::SetProfile(const plWaterProfileResourceDescriptor& profile)
{
  const plColorGammaUB shallow = profile.m_ShallowColor;
  const plColorGammaUB deep = profile.m_DeepColor;
  m_Shallow = plColorGammaUB(shallow.r, shallow.g, shallow.b);
  m_Deep = plColorGammaUB(deep.r, deep.g, deep.b);
  m_fAmplitude = plMath::Clamp(profile.m_fWaveAmplitude, 0.0f, 4.0f);
  m_fWavelength = plMath::Clamp(profile.m_fBaseWavelength, 0.05f, 1000.0f);
  m_fSteepness = plMath::Clamp(profile.m_fWaveSteepness, 0.0f, 1.0f);
  m_fWindSpeed = plMath::Clamp(profile.m_fWindSpeed, 0.0f, 50.0f);
  m_fRoughness = plMath::Clamp(profile.m_fRoughness, 0.0f, 1.0f);
  m_fFoam = plMath::Clamp(profile.m_fFoamStrength, 0.0f, 4.0f);
  m_bDirty = true;
}

void plWaterSchematicView::SetBodyMode(int iMode)
{
  m_iBodyMode = iMode;
  m_bDirty = true;
}

void plWaterSchematicView::SetQuality(int iQuality)
{
  m_iQuality = iQuality;
  m_bDirty = true;
}

void plWaterSchematicView::SetUnderwater(bool bUnderwater)
{
  m_bUnderwater = bUnderwater;
  m_bDirty = true;
}

void plWaterSchematicView::SetShowCascades(bool bShow)
{
  m_bShowCascades = bShow;
  m_bDirty = true;
}

void plWaterSchematicView::SetShowDirection(bool bShow)
{
  m_bShowDirection = bShow;
  m_bDirty = true;
}

void plWaterSchematicView::SetFrozen(bool bFrozen)
{
  m_bFrozen = bFrozen;
  m_LastStep = plTime::Now();
}

void plWaterSchematicView::OnUiUpdate()
{
  if (!IsReady())
    return;

  if (m_bRebuild || GetSurface()->GetLogicalSize() != m_vBuiltSize)
    BuildDocument();

  Animate();

  if (m_bDirty)
    Draw();
}

void plWaterSchematicView::Animate()
{
  // hosts update a surface only while it is shown, so the waves move only then: a step every 33 ms
  const plTime now = plTime::Now();
  if (m_bFrozen || now - m_LastStep < s_SchematicStep)
    return;

  m_LastStep = now;
  m_fPhase += 0.045f;
  m_bDirty = true;
}

void plWaterSchematicView::BuildDocument()
{
  m_bRebuild = false;
  m_vBuiltSize = GetSurface()->GetLogicalSize();

  plUiPanel* pRoot = Panel();
  pRoot->m_Color = Background();
  plEditorUi::DrawList(pRoot, "Water.Schematic", "Water.SchematicDraw");
  SetDocument(Finish(pRoot));

  m_bDirty = true;
}

void plWaterSchematicView::DrawWave(const plRectFloat& rect, float fHorizon, float fYOffset, float fAmplitudeScale, float fWavelengthScale, float fPhaseOffset, const plColorGammaUB& color, float fWidth)
{
  const float fAmplitude = 4.0f + m_fAmplitude * 8.0f;
  const int iStride = plMath::Max(3, 10 - m_iQuality * 2);
  const float fWavelengthPixels = plMath::Clamp(m_fWavelength * 7.0f * fWavelengthScale, 18.0f, rect.width * 2.0f);
  const float fWaveNumber = 2.0f * plMath::Pi<float>() / fWavelengthPixels;
  const float fPhaseSpeed = (0.35f + m_fWindSpeed * 0.035f) / plMath::Sqrt(plMath::Max(fWavelengthScale, 0.1f));

  plHybridArray<plVec2, 512> points;
  for (int x = -iStride; x <= static_cast<int>(rect.width) + iStride; x += iStride)
  {
    float fEnvelope = 1.0f;
    if (m_iBodyMode == 1)
      fEnvelope = plMath::Clamp(1.0f - plMath::Abs(x - rect.width * 0.5f) / (rect.width * 0.45f), 0.0f, 1.0f);

    const float fPhase = x * fWaveNumber + m_fPhase * fPhaseSpeed + fPhaseOffset;
    const float fFundamental = plMath::Sin(plAngle::MakeFromRadian(fPhase));
    const float fCrest = plMath::Sin(plAngle::MakeFromRadian(fPhase * 2.0f)) * m_fSteepness * 0.32f;
    points.PushBack(plVec2(static_cast<float>(x), fHorizon + fYOffset + (fFundamental + fCrest) * fAmplitude * fAmplitudeScale * fEnvelope));
  }

  m_Writer.Polyline(points, color, fWidth);
}

void plWaterSchematicView::Draw()
{
  m_bDirty = false;
  if (!IsReady())
    return;

  const plVec2 size = GetSurface()->GetLogicalSize();
  const plRectFloat rect(0, 0, size.x, size.y);
  m_Writer.Clear();

  const float fHorizon = m_bUnderwater ? rect.height * 0.15f : rect.height * 0.52f;

  // sky and water, or the submerged tint
  if (m_bUnderwater)
  {
    VerticalGradient(m_Writer, rect, Darker(m_Shallow, 1.5f), Darker(m_Deep, 1.8f));
  }
  else
  {
    VerticalGradient(m_Writer, plRectFloat(0, 0, rect.width, fHorizon), plColorGammaUB(35, 66, 94), plColorGammaUB(103, 158, 191));
    VerticalGradient(m_Writer, plRectFloat(0, fHorizon, rect.width, rect.height - fHorizon), m_Shallow, m_Deep);
  }

  if (!m_bUnderwater)
  {
    DrawWave(rect, fHorizon, 0, 1.0f, 1.0f, 0.0f, plColorGammaUB(225, 250, 255, static_cast<plUInt8>(plMath::Min(180.0f + m_fFoam * 15.0f, 255.0f))), 2.0f + m_fFoam);

    if (m_bShowCascades)
    {
      DrawWave(rect, fHorizon, 22, 0.55f, 0.53f, 1.7f, plColorGammaUB(90, 196, 232, 170), 2.0f);
      DrawWave(rect, fHorizon, 52, 0.3f, 0.27f, 3.1f, plColorGammaUB(55, 156, 206, 120), 1.5f);
    }

    if (m_iBodyMode == 1)
    {
      const plColorGammaUB bank(108, 82, 57);
      m_Writer.Line(plVec2(0, fHorizon + 20), plVec2(rect.width * 0.18f, rect.height), bank, 7.0f);
      m_Writer.Line(plVec2(rect.width, fHorizon + 20), plVec2(rect.width * 0.82f, rect.height), bank, 7.0f);
    }
    else if (m_iBodyMode == 2)
    {
      // the lake shore
      const plVec2 center(rect.width * 0.5f, fHorizon - 10 + (rect.height - fHorizon + 36) * 0.5f);
      const plVec2 radius(rect.width * 0.42f, (rect.height - fHorizon + 36) * 0.5f);
      plHybridArray<plVec2, 66> ellipse;
      for (plUInt32 i = 0; i <= 64; ++i)
      {
        const plAngle a = plAngle::MakeFromRadian(i / 64.0f * 2.0f * plMath::Pi<float>());
        ellipse.PushBack(center + plVec2(plMath::Cos(a) * radius.x, plMath::Sin(a) * radius.y));
      }
      m_Writer.Polyline(ellipse, plColorGammaUB(94, 86, 60), 6.0f);
    }

    if (m_bShowDirection)
    {
      // the three wind direction arrows in the axis colours
      const plVec2 origin(rect.width * 0.56f, fHorizon + 48);
      const plVec2 tips[3] = {origin + plVec2(-90, -36), origin + plVec2(18, -48), origin + plVec2(105, -24)};
      for (plUInt32 i = 0; i < 3; ++i)
      {
        m_Writer.Line(origin, tips[i], Axis(i), 3.0f);
        m_Writer.Circle(tips[i], 4.0f, Axis(i));
      }
    }
  }
  else
  {
    for (float x = -80.0f; x < rect.width; x += 70.0f)
      m_Writer.Line(plVec2(x, 0), plVec2(x + 140, rect.height), plColorGammaUB(160, 230, 245, 45), 2.0f);

    for (plUInt32 i = 0; i < 18; ++i)
    {
      const float x = plMath::Mod(i * 83.0f + m_fPhase * 19.0f, rect.width);
      const float y = plMath::Mod(i * 47.0f - m_fPhase * 31.0f + rect.height * 3.0f, rect.height);
      m_Writer.Circle(plVec2(x, y), 2.0f + (i % 3), plColorGammaUB(210, 248, 255, 90));
    }
  }

  // the profile line on a dark plate
  plStringBuilder line;
  line.SetFormat("{}  |  wavelength {} m  |  steepness {}  |  roughness {}", m_iBodyMode == 0 ? "OCEAN" : (m_iBodyMode == 1 ? "RIVER" : "LAKE"), plArgF(m_fWavelength, 1), plArgF(m_fSteepness, 2), plArgF(m_fRoughness, 2));
  const float fTextWidth = plMath::Ceil(MeasureCodeText(line, MinFontSize + 1)) + 12.0f;
  m_Writer.Rect(plRectFloat(8, 8, fTextWidth, 18), plColorGammaUB(10, 10, 11, 191));
  m_Writer.Text(plRectFloat(14, 8, fTextWidth - 6, 18), line, plColorGammaUB(236, 236, 239), MinFontSize + 1, plUiTextHAlign::Left, plUiTextVAlign::Center, true);

  m_Writer.Publish(GetSurface()->GetBlackboard(), plMakeHashedString("Water.SchematicDraw"));
}

//////////////////////////////////////////////////////////////////////////
// plWaterPreviewStatus

namespace
{
  constexpr float s_fRampHeight = 26.0f;
  constexpr float s_fChipHeight = 22.0f;
  constexpr float s_fMaxDepth = 15.0f;

  float ChipWidth(const plWaterUi::Chip& chip)
  {
    return plMath::Ceil(MeasureCodeText(chip.m_sText, MinFontSize)) + 33.0f;
  }
} // namespace

plWaterPreviewStatus::plWaterPreviewStatus()
  : plEditorUiContent("WaterPreviewStatus")
{
}

float plWaterPreviewStatus::GetHeight() const
{
  return s_fControlRow + m_uiChipRows * (s_fChipHeight + 6.0f) + 4.0f + s_fRampHeight + 1.0f;
}

void plWaterPreviewStatus::SetSunElevation(plInt32 iDegrees)
{
  m_iSun = iDegrees;

  if (IsReady())
  {
    auto& board = GetSurface()->GetBlackboard();
    board.SetEntryValue("Water.Sun", static_cast<float>(m_iSun));

    plStringBuilder text;
    text.SetFormat("Sun {}\xC2\xB0", m_iSun);
    board.SetEntryValue("Water.SunText", plString(text));
  }
}

void plWaterPreviewStatus::SetChips(plArrayPtr<const plWaterUi::Chip> chips)
{
  bool bSame = chips.GetCount() == m_Chips.GetCount();
  for (plUInt32 i = 0; bSame && i < chips.GetCount(); ++i)
    bSame = chips[i].m_sText == m_Chips[i].m_sText && chips[i].m_Severity == m_Chips[i].m_Severity && chips[i].m_bState == m_Chips[i].m_bState;

  if (bSame)
    return;

  m_Chips = chips;
  m_bRebuild = true;
}

void plWaterPreviewStatus::SetDepthProfile(const plWaterProfileResourceDescriptor& profile)
{
  m_Shallow = profile.m_ShallowColor;
  m_Deep = profile.m_DeepColor;
  m_vAbsorption = profile.m_vAbsorption;
  m_fRefractionDepth = plMath::Max(profile.m_fRefractionDepth, 0.01f);
  m_bRampDirty = true;
}

void plWaterPreviewStatus::OnUiUpdate()
{
  if (!IsReady())
    return;

  if (m_bRebuild || GetSurface()->GetLogicalSize() != m_vBuiltSize)
    BuildDocument();

  // the sun slider writes its value into the blackboard while it is dragged
  const plVariant value = GetSurface()->GetBlackboard().GetEntryValue(plTempHashedString("Water.Sun"), static_cast<float>(m_iSun));
  const plInt32 iSun = plMath::Clamp(static_cast<plInt32>(plMath::Round(value.ConvertTo<float>())), 5, 85);
  if (iSun != m_iSun)
  {
    SetSunElevation(iSun);
    if (m_OnSunChanged.IsValid())
      m_OnSunChanged(iSun);
  }

  if (m_bRampDirty)
    DrawRamp();
}

void plWaterPreviewStatus::BuildDocument()
{
  m_bRebuild = false;
  m_vBuiltSize = GetSurface()->GetLogicalSize();
  const float fWidth = plMath::Max(m_vBuiltSize.x, 200.0f);
  auto& board = GetSurface()->GetBlackboard();

  plUiPanel* pRoot = Panel();
  pRoot->m_Color = Surface();

  // sun elevation
  {
    plUiText* pLabel = Text(pRoot, "", "Water.SunText");
    Place(pLabel, plRectFloat(6, 3, 64, ControlHeight));
    pLabel->m_uiFontSize = FontSize - 1;
    pLabel->m_Color = Muted();

    plUiSlider* pSlider = PL_DEFAULT_NEW(plUiSlider);
    pRoot->m_Children.PushBack(pSlider);
    pSlider->m_sName = "Water.SunSlider";
    Place(pSlider, plRectFloat(72, 3 + ControlHeight * 0.5f - 4.0f, 200, 8));
    pSlider->m_fMin = 5.0f;
    pSlider->m_fMax = 85.0f;
    pSlider->m_sValueBinding = "Water.Sun";
    pSlider->m_Color = Raised();
    pSlider->m_FillColor = Border();
    pSlider->m_HandleColor = Muted();
    pSlider->m_vCornerRadius = plVec4(4);
    SetToolTip(board, "Water.SunSlider", "Sun elevation above the horizon. Low angles emphasize glitter and reflections.");
  }

  // chips, wrapping
  float x = 6.0f;
  float y = s_fControlRow + 3.0f;
  plUInt32 uiRows = 1;
  for (plUInt32 i = 0; i < m_Chips.GetCount(); ++i)
  {
    const plWaterUi::Chip& chip = m_Chips[i];
    const float fChip = ChipWidth(chip);
    if (x + fChip > fWidth - 6.0f && x > 6.0f)
    {
      x = 6.0f;
      y += s_fChipHeight + 6.0f;
      ++uiRows;
    }

    plStringBuilder action;
    action.SetFormat("Water.Chip.{}", i);

    plUiButton* pChip = Button(pRoot, "", action);
    Place(pChip, plRectFloat(x, y, fChip, s_fChipHeight));
    pChip->ClearChildren();
    pChip->m_Color = Field();
    pChip->m_HoverColor = Raised();
    pChip->m_fBorderWidth = 1.0f;
    pChip->m_BorderColor = plEditorUi::Line();
    pChip->m_vCornerRadius = plVec4(s_fChipHeight * 0.5f);

    plUiPanel* pDot = Panel(pChip);
    pDot->m_vAnchorMin = pDot->m_vAnchorMax = plVec2(0, 0.5f);
    pDot->m_vOffsets = plVec4(9, -3.5f, 7, 7);
    pDot->m_Color = chip.m_bState || chip.m_Severity != plWaterUi::Severity::Ok ? SeverityColor(chip.m_Severity) : Muted();
    pDot->m_vCornerRadius = plVec4(3.5f);

    plUiText* pText = Text(pChip, chip.m_sText);
    Fill(pText, plVec4(22, 0, 6, 0));
    pText->m_hFont = GetCodeFont();
    pText->m_uiFontSize = MinFontSize;
    pText->m_Color = chip.m_Severity == plWaterUi::Severity::Ok ? Muted() : SeverityColor(chip.m_Severity);

    SetToolTip(board, action, chip.m_sTooltip);
    x += fChip + 6.0f;
  }

  // the depth ramp
  {
    const float fTop = s_fControlRow + uiRows * (s_fChipHeight + 6.0f) + 4.0f;
    Rule(pRoot, plRectFloat(0, fTop, fWidth, 1));

    plUiPanel* pRamp = Panel(pRoot);
    Place(pRamp, plRectFloat(0, fTop + 1, fWidth, s_fRampHeight));
    plEditorUi::DrawList(pRamp, "Water.Ramp", "Water.RampDraw");
  }

  SetDocument(Finish(pRoot));
  SetSunElevation(m_iSun);

  if (uiRows != m_uiChipRows)
  {
    m_uiChipRows = uiRows;
    if (m_OnHeightChanged.IsValid())
      m_OnHeightChanged(GetHeight());
  }

  m_bRampDirty = true;
}

void plWaterPreviewStatus::DrawRamp()
{
  m_bRampDirty = false;
  if (!IsReady())
    return;

  const float fWidth = GetSurface()->GetLogicalSize().x;
  m_Writer.Clear();

  // shallow to deep tint over the view depth, with Beer-Lambert absorption
  auto TintAt = [&](float fDepth)
  {
    const float fMix = plMath::Saturate(fDepth / m_fRefractionDepth);
    plColor tint = plMath::Lerp(m_Shallow, m_Deep, fMix);
    tint.r *= plMath::Exp(-m_vAbsorption.x * fDepth);
    tint.g *= plMath::Exp(-m_vAbsorption.y * fDepth);
    tint.b *= plMath::Exp(-m_vAbsorption.z * fDepth);
    tint.a = 1.0f;
    return plColorGammaUB(tint);
  };

  constexpr plUInt32 uiSteps = 24;
  for (plUInt32 i = 0; i < uiSteps; ++i)
  {
    const float t0 = i / static_cast<float>(uiSteps);
    const float t1 = (i + 1) / static_cast<float>(uiSteps);
    m_Writer.HorizontalGradient(plRectFloat(fWidth * t0, 0, fWidth * (t1 - t0) + 0.5f, s_fRampHeight), TintAt(t0 * s_fMaxDepth), TintAt(t1 * s_fMaxDepth));
  }

  const float labelDepths[] = {0.5f, 2.0f, 6.0f, 15.0f};
  for (float fDepth : labelDepths)
  {
    const float x = plMath::Min(fWidth * (fDepth / s_fMaxDepth), fWidth - 40.0f);
    plStringBuilder label;
    label.SetFormat("{} m", plArgF(fDepth, fDepth < 1.0f ? 1 : 0));
    m_Writer.Rect(plRectFloat(x, 0, 1, 6), plColorGammaUB(255, 255, 255, 217));
    m_Writer.Text(plRectFloat(x + 3, 6, 40, 16), label, plColorGammaUB(255, 255, 255, 217), MinFontSize, plUiTextHAlign::Left, plUiTextVAlign::Center, true);
  }

  m_Writer.Publish(GetSurface()->GetBlackboard(), plMakeHashedString("Water.RampDraw"));
}

plString plWaterPreviewStatus::GetToolTipAt(const plVec2& vLogical) const
{
  const float fTop = s_fControlRow + m_uiChipRows * (s_fChipHeight + 6.0f) + 4.0f;
  if (vLogical.y >= fTop)
    return "Computed water color by view depth: Shallow\xE2\x86\x92" "Deep tint with Beer\xE2\x80\x93Lambert absorption applied.";

  return plEditorUiContent::GetToolTipAt(vLogical);
}

void plWaterPreviewStatus::OnUiEvent(const plMsgUiEvent& event)
{
  const plStringView sId = event.m_sIdentifier.GetView();
  if (!sId.StartsWith("Water.Chip."))
    return;

  plInt32 iChip = 0;
  plConversionUtils::StringToInt(sId.GetSubString(11, 2), iChip).IgnoreResult();
  if (m_OnChipClicked.IsValid())
    m_OnChipClicked(static_cast<plUInt32>(iChip));
}

//////////////////////////////////////////////////////////////////////////
// plWaterPropertyFilter

namespace
{
  constexpr float s_fContextHeight = 22.0f;
}

plWaterPropertyFilter::plWaterPropertyFilter()
  : plEditorUiContent("WaterPropertyFilter")
  , m_Search(*GetSurface(), "Water.PropertyFilter")
{
  m_Search.m_Events.AddEventHandler(plMakeDelegate(&plWaterPropertyFilter::OnSearch, this));
}

plWaterPropertyFilter::~plWaterPropertyFilter()
{
  m_Search.m_Events.RemoveEventHandler(plMakeDelegate(&plWaterPropertyFilter::OnSearch, this));
}

float plWaterPropertyFilter::GetHeight()
{
  return s_fRowHeight + s_fContextHeight + 1.0f;
}

void plWaterPropertyFilter::SetFilterText(plStringView sText)
{
  m_Search.SetText(sText);
}

plString plWaterPropertyFilter::GetFilterText() const
{
  return m_Search.GetText();
}

void plWaterPropertyFilter::SetContextText(plStringView sText)
{
  m_sContext = sText;
  if (IsReady())
    GetSurface()->GetBlackboard().SetEntryValue("Water.Context", m_sContext);
}

void plWaterPropertyFilter::OnSearch(const plEditorUiSearchFieldEvent& e)
{
  if (e.m_Type == plEditorUiSearchFieldEvent::Type::TextChanged && m_OnFilterChanged.IsValid())
    m_OnFilterChanged(m_Search.GetText());
}

void plWaterPropertyFilter::OnUiUpdate()
{
  if (!IsReady())
    return;

  if (!m_bRebuild && GetSurface()->GetLogicalSize() == m_vBuiltSize)
    return;

  m_bRebuild = false;
  m_vBuiltSize = GetSurface()->GetLogicalSize();
  const float fWidth = plMath::Max(m_vBuiltSize.x, 120.0f);

  plUiPanel* pRoot = Panel();
  pRoot->m_Color = Surface();

  plUiPanel* pRow = Panel(pRoot);
  Place(pRow, plRectFloat(6, 3, fWidth - 12, ControlHeight));
  pRow->m_Color = plColorGammaUB(0, 0, 0, 0);
  pRow->m_bClipChildren = false;
  m_Search.Build(pRow, "Filter properties...");
  Rule(pRoot, plRectFloat(0, s_fRowHeight - 1, fWidth, 1));

  plUiText* pContext = Text(pRoot, "", "Water.Context");
  Place(pContext, plRectFloat(10, s_fRowHeight, fWidth - 20, s_fContextHeight));
  pContext->m_uiFontSize = FontSize - 1;
  pContext->m_Color = Muted();
  Rule(pRoot, plRectFloat(0, s_fRowHeight + s_fContextHeight, fWidth, 1));

  SetDocument(Finish(pRoot));
  SetContextText(m_sContext);
}

//////////////////////////////////////////////////////////////////////////
// plWaterDiagnostics

/// Quality tiers: Tier, Cascades, Resolution, Spectrum Memory, Simulation; read-only.
class plWaterDiagnostics::TierAdapter : public plEditorUiTableAdapter
{
public:
  plUInt32 GetRowCount() const override { return m_Cells.GetCount() / 5; }
  plString GetCell(plUInt32 uiRow, plUInt32 uiColumn) const override { return m_Cells[uiRow * 5 + uiColumn]; }
  plStatus SetCell(plUInt32, plUInt32, plStringView) override { return plStatus("The tiers are fixed."); }

  plDynamicArray<plString> m_Cells;
};

plWaterDiagnostics::plWaterDiagnostics()
  : plEditorUiContent("WaterDiagnostics")
  , m_Tabs(*GetSurface(), "Water.DiagTabs")
{
  m_pTierAdapter = PL_DEFAULT_NEW(TierAdapter);
  m_pTierTable = PL_DEFAULT_NEW(plEditorUiTable, *GetSurface(), "Water.Tiers", *m_pTierAdapter);

  plEditorUiTableColumn columns[5];
  const char* titles[] = {"Tier", "Cascades", "Resolution", "Spectrum Memory", "Simulation"};
  const float widths[] = {150.0f, 90.0f, 90.0f, 130.0f, 220.0f};
  for (plUInt32 i = 0; i < 5; ++i)
  {
    columns[i].m_sTitle = titles[i];
    columns[i].m_fWidth = widths[i];
    columns[i].m_bRightAligned = i >= 1 && i <= 3;
  }
  m_pTierTable->SetPersistKey("WaterProfile/TierColumns");
  m_pTierTable->SetColumns(columns);
  m_pTierTable->m_Events.AddEventHandler(plMakeDelegate(&plWaterDiagnostics::OnTableEvent, this));

  m_Tabs.m_CurrentChanged.AddEventHandler([this](plUInt32 uiPage)
    {
      m_Page = static_cast<Page>(uiPage);
      m_bRebuild = true; });

  UpdateTabs();

  if (IsReady())
    m_PointerSubscription = GetSurface()->m_PointerEvents.AddEventHandler(plMakeDelegate(&plWaterDiagnostics::OnPointer, this));
}

plWaterDiagnostics::~plWaterDiagnostics()
{
  if (IsReady())
    GetSurface()->m_PointerEvents.RemoveEventHandler(m_PointerSubscription);

  m_pTierTable->m_Events.RemoveEventHandler(plMakeDelegate(&plWaterDiagnostics::OnTableEvent, this));
}

void plWaterDiagnostics::SetPage(Page page)
{
  m_Page = page;
  m_Tabs.SetCurrent(static_cast<plUInt32>(page));
  m_bRebuild = true;
}

void plWaterDiagnostics::SetIssues(plArrayPtr<const plWaterUi::Issue> issues)
{
  m_Issues = issues;
  m_iHoverIssue = -1;
  UpdateTabs();

  if (m_Page == Page::Validation)
    DrawValidation();
}

void plWaterDiagnostics::SetSpectrum(const plWaterUi::SpectrumData& data)
{
  m_Spectrum = data;

  if (m_Page == Page::Spectrum)
    DrawSpectrum();
}

void plWaterDiagnostics::SetPerformance(plArrayPtr<const plString> cells, plUInt32 uiCurrentTier)
{
  PL_IGNORE_UNUSED(uiCurrentTier);

  m_pTierAdapter->m_Cells = cells;
  if (m_Page == Page::Performance)
    m_pTierTable->Refresh();
}

void plWaterDiagnostics::UpdateTabs()
{
  plUInt32 uiProblems = 0;
  bool bError = false;
  for (const plWaterUi::Issue& issue : m_Issues)
  {
    if (issue.m_Severity != plWaterUi::Severity::Ok)
      ++uiProblems;
    bError = bError || issue.m_Severity == plWaterUi::Severity::Error;
  }

  plEditorUiTab tabs[3];
  tabs[0].m_sLabel = "Validation";
  tabs[1].m_sLabel = "Spectrum";
  tabs[2].m_sLabel = "Performance";

  // the count only shows while there are warnings or errors
  if (uiProblems > 0)
  {
    plStringBuilder count;
    count.SetFormat("{}", uiProblems);
    tabs[0].m_sCount = count;
    tabs[0].m_bCountIsProblem = bError;
  }

  m_Tabs.SetTabs(tabs);
  m_Tabs.SetCurrent(static_cast<plUInt32>(m_Page));
  m_bRebuild = true;
}

plRectFloat plWaterDiagnostics::GetBodyRect() const
{
  const plVec2 size = GetSurface()->GetLogicalSize();
  const float fTop = plEditorUiTabs::Height + 1.0f;
  return plRectFloat(0, fTop, size.x, plMath::Max(size.y - fTop, 1.0f));
}

void plWaterDiagnostics::OnUiUpdate()
{
  if (!IsReady())
    return;

  if (m_bRebuild || GetSurface()->GetLogicalSize() != m_vBuiltSize)
    BuildDocument();
}

void plWaterDiagnostics::BuildDocument()
{
  m_bRebuild = false;
  m_vBuiltSize = GetSurface()->GetLogicalSize();
  const float fWidth = plMath::Max(m_vBuiltSize.x, 120.0f);

  plUiPanel* pRoot = Panel();
  pRoot->m_Color = Surface();

  plUiPanel* pHeader = Panel(pRoot);
  Place(pHeader, plRectFloat(0, 0, fWidth, plEditorUiTabs::Height));
  pHeader->m_Color = Background();
  plUiPanel* pTabs = m_Tabs.Build(pHeader, fWidth - 12.0f);
  pTabs->m_vAnchorMin = plVec2(0, 0);
  pTabs->m_vAnchorMax = plVec2(1, 0);
  pTabs->m_vOffsets = plVec4(6, 0, -6, plEditorUiTabs::Height);
  Rule(pRoot, plRectFloat(0, plEditorUiTabs::Height, fWidth, 1));

  const plRectFloat body = GetBodyRect();
  plUiPanel* pBody = Panel(pRoot);
  Place(pBody, body);
  pBody->m_Color = Surface();

  if (m_Page == Page::Performance)
  {
    m_pTierTable->Build(pBody);
  }
  else
  {
    plEditorUi::DrawList(pBody, "Water.Diagnostics", "Water.DiagnosticsDraw");
  }

  SetDocument(Finish(pRoot));

  if (m_Page == Page::Validation)
    DrawValidation();
  else if (m_Page == Page::Spectrum)
    DrawSpectrum();
}

void plWaterDiagnostics::DrawValidation()
{
  if (!IsReady())
    return;

  const plRectFloat body = GetBodyRect();
  m_Writer.Clear();

  // log rows: a severity icon, then the message
  float y = 0.0f;
  for (plUInt32 i = 0; i < m_Issues.GetCount(); ++i)
  {
    const plWaterUi::Issue& issue = m_Issues[i];
    if (static_cast<plInt32>(i) == m_iHoverIssue && !issue.m_sProperty.IsEmpty())
      m_Writer.Rect(plRectFloat(0, y, body.width, RowHeight), WithAlpha(Raised(), 160));

    const Icon icon = issue.m_Severity == plWaterUi::Severity::Error ? Icon::Error : (issue.m_Severity == plWaterUi::Severity::Warning ? Icon::Warning : Icon::Check);
    m_Writer.Image(plRectFloat(8, y + 4, 14, 14), IconTexture(icon), SeverityColor(issue.m_Severity));
    m_Writer.Text(plRectFloat(30, y, body.width - 40, RowHeight), issue.m_sText, issue.m_Severity == plWaterUi::Severity::Ok ? Muted() : Foreground(), FontSize - 1);
    y += RowHeight;
  }

  m_Writer.Publish(GetSurface()->GetBlackboard(), plMakeHashedString("Water.DiagnosticsDraw"));
}

void plWaterDiagnostics::DrawSpectrum()
{
  if (!IsReady())
    return;

  const plRectFloat body = GetBodyRect();
  m_Writer.Clear();
  m_BandRects.Clear();

  const plRectFloat graph(52, 22, body.width - 70, body.height - 56);
  if (graph.width < 60 || graph.height < 40)
  {
    m_Writer.Publish(GetSurface()->GetBlackboard(), plMakeHashedString("Water.DiagnosticsDraw"));
    return;
  }

  m_Writer.Rect(plRectFloat(0, 0, body.width, body.height), Background());

  // log scale around [smallest, largest wavelength of interest]
  float fMaxWavelength = m_Spectrum.m_fBase;
  for (float fWavelength : m_Spectrum.m_CpuWavelengths)
    fMaxWavelength = plMath::Max(fMaxWavelength, fWavelength);

  const float fLogMin = plMath::Log2(plMath::Max(m_Spectrum.m_fSmallest * 0.8f, 0.01f));
  const float fLogMax = plMath::Log2(fMaxWavelength * 1.25f);
  const float fLogSpan = plMath::Max(fLogMax - fLogMin, 0.001f);

  auto XFor = [&](float fWavelength)
  {
    const float t = (plMath::Log2(plMath::Max(fWavelength, 0.01f)) - fLogMin) / fLogSpan;
    return graph.x + graph.width * plMath::Clamp(t, 0.0f, 1.0f);
  };

  // axis and ticks
  m_Writer.Line(plVec2(graph.x, graph.Bottom()), plVec2(graph.Right(), graph.Bottom()), Border());

  const float tickValues[] = {0.05f, 0.1f, 0.5f, 1.0f, 5.0f, 10.0f, 50.0f, 100.0f, 500.0f};
  for (float fTick : tickValues)
  {
    if (plMath::Log2(fTick) < fLogMin || plMath::Log2(fTick) > fLogMax)
      continue;

    const float x = XFor(fTick);
    m_Writer.Line(plVec2(x, graph.y), plVec2(x, graph.Bottom()), WithAlpha(plEditorUi::Line(), 160));
    m_Writer.Rect(plRectFloat(x, graph.Bottom(), 1, 5), Dim());

    plStringBuilder label;
    label.SetFormat("{}", plArgF(fTick, fTick < 1.0f ? 2 : 0));
    m_Writer.Text(plRectFloat(x - 24, graph.Bottom() + 4, 48, 14), label, Dim(), MinFontSize, plUiTextHAlign::Center, plUiTextVAlign::Center, true);
  }
  m_Writer.Text(plRectFloat(graph.x, graph.Bottom() + 18, graph.width, 14), "wavelength (m, log scale)", Dim(), MinFontSize, plUiTextHAlign::Center);

  // profile bounds
  const plColorGammaUB accent = Accent();
  const float xMin = XFor(m_Spectrum.m_fSmallest);
  const float xBase = XFor(m_Spectrum.m_fBase);
  m_Writer.Line(plVec2(xMin, graph.y - 10), plVec2(xMin, graph.Bottom()), accent, 1.0f, 3.0f, 3.0f);
  m_Writer.Line(plVec2(xBase, graph.y - 10), plVec2(xBase, graph.Bottom()), accent, 1.0f, 3.0f, 3.0f);

  plStringBuilder bound;
  bound.SetFormat("min {} m", plArgF(m_Spectrum.m_fSmallest, 2));
  m_Writer.Text(plRectFloat(xMin + 4, 2, 120, 14), bound, accent, MinFontSize, plUiTextHAlign::Left, plUiTextVAlign::Center, true);
  bound.SetFormat("base {} m", plArgF(m_Spectrum.m_fBase, 1));
  m_Writer.Text(plRectFloat(plMath::Min(xBase + 4, body.width - 96), 2, 92, 14), bound, accent, MinFontSize, plUiTextHAlign::Left, plUiTextVAlign::Center, true);

  // the deterministic CPU waves
  const float fCpuY = graph.Bottom() - 14.0f;
  for (float fWavelength : m_Spectrum.m_CpuWavelengths)
    m_Writer.Circle(plVec2(XFor(fWavelength), fCpuY), 3.0f, Muted());

  plStringBuilder cpu;
  cpu.SetFormat("CPU waves x{}", m_Spectrum.m_CpuWavelengths.GetCount());
  m_Writer.Text(plRectFloat(graph.x + 4, fCpuY - 22, 160, 14), cpu, Muted(), MinFontSize, plUiTextHAlign::Left, plUiTextVAlign::Center, true);

  // cascade bands, cascade 0 covers the longest wavelengths; culled bands keep a dashed warning outline
  const plColorGammaUB effects = plColorScheme::GetCategoryColor("Effects", plColorScheme::CategoryColorUsage::BorderColor);
  const float fBandHeight = 20.0f;
  for (plUInt32 i = 0; i < m_Spectrum.m_CascadeBands.GetCount(); ++i)
  {
    const plVec2 band = m_Spectrum.m_CascadeBands[i];
    const float x0 = XFor(band.x);
    const float x1 = XFor(band.y);
    const plRectFloat bandRect(x0, graph.y + 4 + i * (fBandHeight + 6), plMath::Max(x1 - x0, 8.0f), fBandHeight);
    m_BandRects.PushBack(plRectFloat(bandRect.x + body.x, bandRect.y + body.y, bandRect.width, bandRect.height));

    const bool bCulled = i >= m_Spectrum.m_uiEffectiveCascades;
    plStringBuilder text;
    if (bCulled)
    {
      const plColorGammaUB warning = Warning();
      const plVec2 corners[] = {bandRect.GetTopLeft(), plVec2(bandRect.Right(), bandRect.y), plVec2(bandRect.Right(), bandRect.Bottom()), plVec2(bandRect.x, bandRect.Bottom()), bandRect.GetTopLeft()};
      m_Writer.Polyline(corners, warning, 1.0f, 4.0f, 3.0f);
      text.SetFormat("cascade {} \xC2\xB7 {} \xE2\x80\x93 {} m \xE2\x80\x94 culled by {} tier", i, plArgF(band.x, 2), plArgF(band.y, 1), m_Spectrum.m_sTierName);
      m_Writer.Text(plRectFloat(bandRect.x + 6, bandRect.y, plMath::Max(graph.Right() - bandRect.x, 60.0f), fBandHeight), text, warning, MinFontSize, plUiTextHAlign::Left, plUiTextVAlign::Center, true);
    }
    else
    {
      m_Writer.Rect(bandRect, WithAlpha(effects, 56), 4.0f);
      m_Writer.RectOutline(bandRect, effects, 1.0f, 4.0f);
      text.SetFormat("cascade {} \xC2\xB7 {} \xE2\x80\x93 {} m", i, plArgF(band.x, 2), plArgF(band.y, 1));
      m_Writer.Text(plRectFloat(bandRect.x + 6, bandRect.y, plMath::Max(graph.Right() - bandRect.x, 60.0f), fBandHeight), text, Foreground(), MinFontSize, plUiTextHAlign::Left, plUiTextVAlign::Center, true);
    }
  }

  if (m_Spectrum.m_CascadeBands.IsEmpty())
    m_Writer.Text(graph, "No GPU cascades authored \xE2\x80\x94 CPU analytic waves only.", Muted(), FontSize - 1, plUiTextHAlign::Center);

  m_Writer.Publish(GetSurface()->GetBlackboard(), plMakeHashedString("Water.DiagnosticsDraw"));
}

plInt32 plWaterDiagnostics::BandAt(const plVec2& vPos) const
{
  for (plUInt32 i = 0; i < m_BandRects.GetCount(); ++i)
  {
    if (m_BandRects[i].Contains(vPos))
      return static_cast<plInt32>(i);
  }
  return -1;
}

plString plWaterDiagnostics::GetToolTipAt(const plVec2& vLogical) const
{
  if (m_Page == Page::Spectrum)
  {
    const plInt32 iBand = BandAt(vLogical);
    if (iBand >= 0)
    {
      const plVec2 band = m_Spectrum.m_CascadeBands[iBand];
      plStringBuilder text;
      text.SetFormat("Cascade {} covers {} \xE2\x80\x93 {} m.", iBand, plArgF(band.x, 2), plArgF(band.y, 1));
      if (static_cast<plUInt32>(iBand) >= m_Spectrum.m_uiEffectiveCascades)
        text.AppendFormat("\nNever simulated on the {} tier \xE2\x80\x94 click to edit GPU Cascades.", m_Spectrum.m_sTierName);
      else
        text.Append("\nClick to edit the wavelength range.");
      return text;
    }
  }

  if (m_Page == Page::Validation && GetBodyRect().Contains(vLogical))
  {
    const plInt32 iIssue = static_cast<plInt32>((vLogical.y - GetBodyRect().y) / RowHeight);
    if (iIssue >= 0 && iIssue < static_cast<plInt32>(m_Issues.GetCount()) && !m_Issues[iIssue].m_sProperty.IsEmpty())
      return "Click to filter the properties panel to this property.";
  }

  if (m_Page == Page::Performance && GetBodyRect().Contains(vLogical))
    return "Click a row to preview that quality tier. GPU timings are view-dependent \xE2\x80\x94 use the renderer profiler for measurements.";

  return plEditorUiContent::GetToolTipAt(vLogical);
}

void plWaterDiagnostics::OnPointer(const plEditorUiPointerEvent& e)
{
  using Type = plEditorUiPointerEvent::Type;

  const plRectFloat body = GetBodyRect();

  if (m_Page == Page::Validation)
  {
    plInt32 iIssue = -1;
    if (body.Contains(e.m_vPosition))
    {
      iIssue = static_cast<plInt32>((e.m_vPosition.y - body.y) / RowHeight);
      if (iIssue >= static_cast<plInt32>(m_Issues.GetCount()))
        iIssue = -1;
    }

    if (e.m_Type == Type::Move && iIssue != m_iHoverIssue)
    {
      m_iHoverIssue = iIssue;
      DrawValidation();
    }
    else if (e.m_Type == Type::Press && e.m_Button == plEditorUiPointerEvent::Button::Left && iIssue >= 0 && !m_Issues[iIssue].m_sProperty.IsEmpty() && m_OnFilterProperties.IsValid())
    {
      m_OnFilterProperties(m_Issues[iIssue].m_sProperty);
    }
    return;
  }

  if (m_Page == Page::Spectrum && e.m_Type == Type::Press && e.m_Button == plEditorUiPointerEvent::Button::Left)
  {
    // a culled band leads to the cascade count, a live one to the wavelength range that defines it
    const plInt32 iBand = BandAt(e.m_vPosition);
    if (iBand >= 0 && m_OnFilterProperties.IsValid())
      m_OnFilterProperties(static_cast<plUInt32>(iBand) >= m_Spectrum.m_uiEffectiveCascades ? "GpuCascade" : "Wavelength");
  }
}

void plWaterDiagnostics::OnTableEvent(const plEditorUiTableEvent& e)
{
  if (e.m_Type == plEditorUiTableEvent::Type::LayoutChanged)
  {
    m_bRebuild = true;
    return;
  }

  if (e.m_Type == plEditorUiTableEvent::Type::SelectionChanged && m_Page == Page::Performance && m_OnTierPicked.IsValid() && m_pTierTable->GetCurrentRow() < m_pTierAdapter->GetRowCount())
    m_OnTierPicked(m_pTierTable->GetCurrentRow());
}
