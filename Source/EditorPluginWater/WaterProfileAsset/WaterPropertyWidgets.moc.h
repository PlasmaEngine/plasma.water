#pragma once

#include <EditorPluginWater/EditorPluginWaterDLL.h>

#include <GuiFoundation/PropertyGrid/Implementation/PropertyWidget.moc.h>

class QToolButton;
class QDial;

/// \brief Standard integer spin box plus a reroll button that picks a fresh random wave seed.
class plQtWaterSeedPropertyWidget : public plQtPropertyEditorIntSpinboxWidget
{
  Q_OBJECT

public:
  plQtWaterSeedPropertyWidget();

private Q_SLOTS:
  void onRerollClicked();

private:
  QToolButton* m_pRerollButton = nullptr;
};

/// \brief Standard X/Y spin boxes plus a compass dial for the wind direction. The dial writes a
/// normalized direction; the spin boxes still accept raw component input.
class plQtWaterWindDirectionPropertyWidget : public plQtStandardPropertyWidget
{
  Q_OBJECT

public:
  plQtWaterWindDirectionPropertyWidget();

protected:
  virtual void OnInit() override {}
  virtual void InternalSetValue(const plVariant& value) override;

private Q_SLOTS:
  void onSpinValueChanged();
  void onDialValueChanged(int iValue);

private:
  void UpdateDialFromSpins();

  QHBoxLayout* m_pLayout = nullptr;
  plQtDoubleSpinBox* m_pSpinX = nullptr;
  plQtDoubleSpinBox* m_pSpinY = nullptr;
  QDial* m_pDial = nullptr;
  bool m_bInSetValue = false;
};
