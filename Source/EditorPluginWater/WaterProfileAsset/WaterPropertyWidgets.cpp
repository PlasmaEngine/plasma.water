#include <EditorPluginWater/EditorPluginWaterPCH.h>

#include <EditorPluginWater/WaterProfileAsset/WaterPropertyWidgets.moc.h>

#include <Foundation/Math/Random.h>
#include <Foundation/Time/Time.h>
#include <GuiFoundation/Widgets/DoubleSpinBox.moc.h>

#include <QBoxLayout>
#include <QDial>
#include <QToolButton>

// //////////////////////////////////////////////////////////////////////
// plQtWaterSeedPropertyWidget
// //////////////////////////////////////////////////////////////////////

plQtWaterSeedPropertyWidget::plQtWaterSeedPropertyWidget()
  : plQtPropertyEditorIntSpinboxWidget(1, 0, 0x7FFFFFFF)
{
  m_pRerollButton = new QToolButton(this);
  m_pRerollButton->setText(QString::fromUtf8("\xE2\x9F\xB3")); // U+27F3 clockwise gapped circle arrow
  m_pRerollButton->setToolTip(QStringLiteral("Reroll: pick a random wave seed"));
  m_pRerollButton->setAutoRaise(true);
  m_pLayout->addWidget(m_pRerollButton);

  connect(m_pRerollButton, &QToolButton::clicked, this, &plQtWaterSeedPropertyWidget::onRerollClicked);
}

void plQtWaterSeedPropertyWidget::onRerollClicked()
{
  plRandom rng;
  rng.Initialize(static_cast<plUInt64>(plTime::Now().GetNanoseconds()));
  const plUInt32 uiSeed = rng.UIntInRange(0x7FFFFFFFu);

  BroadcastValueChanged(uiSeed);
}

// //////////////////////////////////////////////////////////////////////
// plQtWaterWindDirectionPropertyWidget
// //////////////////////////////////////////////////////////////////////

plQtWaterWindDirectionPropertyWidget::plQtWaterWindDirectionPropertyWidget()
{
  m_pLayout = new QHBoxLayout(this);
  m_pLayout->setContentsMargins(0, 0, 0, 0);
  m_pLayout->setSpacing(4);
  setLayout(m_pLayout);

  m_pDial = new QDial(this);
  m_pDial->setFixedSize(24, 24);
  m_pDial->setRange(0, 359);
  m_pDial->setWrapping(true);
  m_pDial->setNotchesVisible(false);
  m_pDial->setToolTip(QStringLiteral("Wind direction (world-space XY)"));

  m_pSpinX = new plQtDoubleSpinBox(this);
  m_pSpinX->setMinimum(-plMath::MaxValue<double>());
  m_pSpinX->setMaximum(plMath::MaxValue<double>());
  m_pSpinX->setSingleStep(0.1);
  m_pSpinX->setAccelerated(true);
  m_pSpinX->setDecimals(3);

  m_pSpinY = new plQtDoubleSpinBox(this);
  m_pSpinY->setMinimum(-plMath::MaxValue<double>());
  m_pSpinY->setMaximum(plMath::MaxValue<double>());
  m_pSpinY->setSingleStep(0.1);
  m_pSpinY->setAccelerated(true);
  m_pSpinY->setDecimals(3);

  m_pLayout->addWidget(m_pDial);
  m_pLayout->addWidget(m_pSpinX);
  m_pLayout->addWidget(m_pSpinY);

  connect(m_pSpinX, &QDoubleSpinBox::editingFinished, this, &plQtWaterWindDirectionPropertyWidget::onSpinValueChanged);
  connect(m_pSpinY, &QDoubleSpinBox::editingFinished, this, &plQtWaterWindDirectionPropertyWidget::onSpinValueChanged);
  connect(m_pDial, &QDial::valueChanged, this, &plQtWaterWindDirectionPropertyWidget::onDialValueChanged);
}

void plQtWaterWindDirectionPropertyWidget::InternalSetValue(const plVariant& value)
{
  plQtScopedBlockSignals blocker(m_pSpinX, m_pSpinY, m_pDial);

  if (value.IsValid() && value.CanConvertTo<plVec2>())
  {
    const plVec2 vDirection = value.ConvertTo<plVec2>();
    m_pSpinX->setValue(vDirection.x);
    m_pSpinY->setValue(vDirection.y);
    UpdateDialFromSpins();
  }
  else
  {
    m_pSpinX->setValueInvalid();
    m_pSpinY->setValueInvalid();
  }
}

void plQtWaterWindDirectionPropertyWidget::onSpinValueChanged()
{
  if (m_bInSetValue)
    return;

  const plVec2 vDirection(static_cast<float>(m_pSpinX->value()), static_cast<float>(m_pSpinY->value()));

  {
    plQtScopedBlockSignals blocker(m_pDial);
    UpdateDialFromSpins();
  }

  BroadcastValueChanged(vDirection);
}

void plQtWaterWindDirectionPropertyWidget::onDialValueChanged(int iValue)
{
  if (m_bInSetValue)
    return;

  const plAngle angle = plAngle::MakeFromDegree(static_cast<float>(iValue));
  const plVec2 vDirection(plMath::Cos(angle), plMath::Sin(angle));

  {
    plQtScopedBlockSignals blocker(m_pSpinX, m_pSpinY);
    m_pSpinX->setValue(vDirection.x);
    m_pSpinY->setValue(vDirection.y);
  }

  BroadcastValueChanged(vDirection);
}

void plQtWaterWindDirectionPropertyWidget::UpdateDialFromSpins()
{
  const plVec2 vDirection(static_cast<float>(m_pSpinX->value()), static_cast<float>(m_pSpinY->value()));
  if (vDirection.GetLengthSquared() < 0.000001f)
    return;

  const plAngle angle = plMath::ATan2(vDirection.y, vDirection.x);
  plInt32 iDegrees = static_cast<plInt32>(plMath::Round(angle.GetDegree()));
  while (iDegrees < 0)
    iDegrees += 360;

  m_pDial->setValue(iDegrees % 360);
}
