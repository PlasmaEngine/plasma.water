#include <EditorPluginWater/EditorPluginWaterPCH.h>

#include <EditorPluginWater/InputContexts/WaterInteractContext.h>
#include <EditorPluginWater/Preferences/WaterProfilePreferences.h>
#include <EditorPluginWater/WaterProfileAsset/WaterProfileAsset.h>
#include <EditorPluginWater/WaterProfileAsset/WaterProfileAssetWindow.moc.h>
#include <EditorPluginWater/WaterProfileAsset/WaterProfileUiServices.h>

#include <EditorFramework/Assets/AssetCurator.h>
#include <EditorFramework/Assets/AssetStatusIndicator.moc.h>
#include <EditorFramework/DocumentWindow/OrbitCamViewWidget.moc.h>
#include <EditorFramework/InputContexts/EditorInputContext.h>
#include <EditorFramework/IPC/EngineProcessConnection.h>
#include <Foundation/IO/MemoryStream.h>
#include <Foundation/Math/ColorScheme.h>
#include <Foundation/Utilities/AssetFileHeader.h>
#include <Foundation/Utilities/ConversionUtils.h>
#include <GuiFoundation/ActionViews/MenuBarActionMapView.moc.h>
#include <GuiFoundation/ActionViews/ToolBarActionMapView.moc.h>
#include <GuiFoundation/DockPanels/DocumentPanel.moc.h>
#include <GuiFoundation/PropertyGrid/PropertyGridWidget.moc.h>

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QSlider>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

#include <functional>

namespace
{
  QColor ToQColor(const plColor& color)
  {
    const plColorGammaUB gamma = color;
    return QColor(gamma.r, gamma.g, gamma.b);
  }

  QColor GetWaterAccentColor()
  {
    return ToQColor(plColorScheme::GetCategoryColor("Rendering/Water", plColorScheme::CategoryColorUsage::MenuEntryIcon));
  }

  QString FormatVariantShort(const plVariant& value)
  {
    if (value.IsA<plColor>())
    {
      const plColorGammaUB gamma = value.Get<plColor>();
      return QString("#%1%2%3").arg(gamma.r, 2, 16, QChar('0')).arg(gamma.g, 2, 16, QChar('0')).arg(gamma.b, 2, 16, QChar('0'));
    }
    if (value.IsA<plVec2>())
    {
      const plVec2 v = value.Get<plVec2>();
      return QString("%1, %2").arg(v.x, 0, 'f', 2).arg(v.y, 0, 'f', 2);
    }
    if (value.IsA<plVec3>())
    {
      const plVec3 v = value.Get<plVec3>();
      return QString("%1, %2, %3").arg(v.x, 0, 'f', 3).arg(v.y, 0, 'f', 3).arg(v.z, 0, 'f', 3);
    }
    if (value.IsA<float>() || value.IsA<double>())
      return QString::number(value.ConvertTo<double>(), 'f', 3);
    if (value.IsA<bool>())
      return value.Get<bool>() ? QStringLiteral("on") : QStringLiteral("off");
    if (value.IsNumber())
      return QString::number(value.ConvertTo<plInt64>());

    plStringBuilder sTemp;
    plConversionUtils::ToString(value, sTemp);
    return QString::fromUtf8(sTemp.GetData());
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

  QIcon MakePresetIcon(const plWaterProfileResourceDescriptor& profile)
  {
    QPixmap icon(72, 42);
    icon.fill(Qt::transparent);
    QPainter painter(&icon);

    QLinearGradient gradient(0, 0, 0, icon.height());
    gradient.setColorAt(0, ToQColor(profile.m_ShallowColor));
    gradient.setColorAt(1, ToQColor(profile.m_DeepColor));
    painter.fillRect(icon.rect(), gradient);

    painter.setRenderHint(QPainter::Antialiasing, true);
    const float fFrequency = plMath::Clamp(2.5f / plMath::Max(profile.m_fBaseWavelength, 0.5f), 0.05f, 0.6f);
    const float fAmplitude = plMath::Clamp(1.5f + profile.m_fWaveAmplitude * 3.0f, 1.5f, 8.0f);
    painter.setPen(QPen(QColor(220, 244, 248, 180), 1.5));
    QPainterPath wave;
    for (int x = 0; x < icon.width(); ++x)
    {
      const float y = icon.height() * 0.45f + plMath::Sin(plAngle::MakeFromRadian(x * fFrequency)) * fAmplitude;
      if (x == 0)
        wave.moveTo(x, y);
      else
        wave.lineTo(x, y);
    }
    painter.drawPath(wave);
    return QIcon(icon);
  }
} // namespace

// //////////////////////////////////////////////////////////////////////
// plQtWaterProfilePreview: animated 2D schematic fallback view
// //////////////////////////////////////////////////////////////////////

class plQtWaterProfilePreview : public QWidget
{
public:
  explicit plQtWaterProfilePreview(QWidget* pParent)
    : QWidget(pParent)
  {
    setMinimumSize(480, 300);
    m_pTimer = new QTimer(this);
    m_pTimer->setInterval(33);
    connect(m_pTimer, &QTimer::timeout, this, [this]()
    {
      m_fPhase += 0.045f;
      update();
    });
    m_pTimer->start();
  }

  void SetProfile(const plWaterProfileResourceDescriptor& profile)
  {
    m_Shallow = QColor::fromRgbF(profile.m_ShallowColor.r, profile.m_ShallowColor.g, profile.m_ShallowColor.b, 1.0f);
    m_Deep = QColor::fromRgbF(profile.m_DeepColor.r, profile.m_DeepColor.g, profile.m_DeepColor.b, 1.0f);
    m_fAmplitude = plMath::Clamp(profile.m_fWaveAmplitude, 0.0f, 4.0f);
    m_fWavelength = plMath::Clamp(profile.m_fBaseWavelength, 0.05f, 1000.0f);
    m_fSteepness = plMath::Clamp(profile.m_fWaveSteepness, 0.0f, 1.0f);
    m_fWindSpeed = plMath::Clamp(profile.m_fWindSpeed, 0.0f, 50.0f);
    m_fRoughness = plMath::Clamp(profile.m_fRoughness, 0.0f, 1.0f);
    m_fFoam = plMath::Clamp(profile.m_fFoamStrength, 0.0f, 4.0f);
    update();
  }

  void SetBodyMode(int iMode) { m_iBodyMode = iMode; update(); }
  void SetQuality(int iQuality) { m_iQuality = iQuality; update(); }
  void SetUnderwater(bool bUnderwater) { m_bUnderwater = bUnderwater; update(); }
  void SetShowCascades(bool bShow) { m_bShowCascades = bShow; update(); }
  void SetShowDirection(bool bShow) { m_bShowDirection = bShow; update(); }
  void SetFrozen(bool bFrozen)
  {
    if (bFrozen)
      m_pTimer->stop();
    else
      m_pTimer->start();
  }

protected:
  virtual void paintEvent(QPaintEvent* pEvent) override
  {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QLinearGradient background(0, 0, 0, height());
    if (m_bUnderwater)
    {
      background.setColorAt(0.0, m_Shallow.darker(150));
      background.setColorAt(1.0, m_Deep.darker(180));
    }
    else
    {
      background.setColorAt(0.0, QColor(35, 66, 94));
      background.setColorAt(0.52, QColor(103, 158, 191));
      background.setColorAt(0.53, m_Shallow);
      background.setColorAt(1.0, m_Deep);
    }
    painter.fillRect(rect(), background);

    const float horizon = m_bUnderwater ? height() * 0.15f : height() * 0.52f;
    const float amplitude = 4.0f + m_fAmplitude * 8.0f;
    const int stride = plMath::Max(3, 10 - m_iQuality * 2);

    auto DrawWave = [&](float yOffset, float amplitudeScale, float wavelengthScale, float phaseOffset, QColor color, float strokeWidth)
    {
      QPainterPath path;
      const float wavelengthPixels = plMath::Clamp(m_fWavelength * 7.0f * wavelengthScale, 18.0f, width() * 2.0f);
      const float waveNumber = 2.0f * plMath::Pi<float>() / wavelengthPixels;
      const float phaseSpeed = (0.35f + m_fWindSpeed * 0.035f) / plMath::Sqrt(plMath::Max(wavelengthScale, 0.1f));
      for (int x = -stride; x <= width() + stride; x += stride)
      {
        float envelope = 1.0f;
        if (m_iBodyMode == 1)
          envelope = plMath::Clamp(1.0f - plMath::Abs(x - width() * 0.5f) / (width() * 0.45f), 0.0f, 1.0f);
        const float phase = x * waveNumber + m_fPhase * phaseSpeed + phaseOffset;
        const float fundamental = plMath::Sin(plAngle::MakeFromRadian(phase));
        const float sharpenedCrest = plMath::Sin(plAngle::MakeFromRadian(phase * 2.0f)) * m_fSteepness * 0.32f;
        const float y = horizon + yOffset + (fundamental + sharpenedCrest) * amplitude * amplitudeScale * envelope;
        if (x == -stride)
          path.moveTo(x, y);
        else
          path.lineTo(x, y);
      }
      painter.setPen(QPen(color, strokeWidth));
      painter.drawPath(path);
    };

    if (!m_bUnderwater)
    {
      DrawWave(0, 1.0f, 1.0f, 0.0f, QColor(225, 250, 255, 180 + static_cast<int>(m_fFoam * 15.0f)), 2.0f + m_fFoam);
      if (m_bShowCascades)
      {
        DrawWave(22, 0.55f, 0.53f, 1.7f, QColor(90, 196, 232, 170), 2.0f);
        DrawWave(52, 0.3f, 0.27f, 3.1f, QColor(55, 156, 206, 120), 1.5f);
      }

      if (m_iBodyMode == 1)
      {
        painter.setPen(QPen(QColor(108, 82, 57), 7));
        painter.drawLine(0, horizon + 20, width() * 0.18, height());
        painter.drawLine(width(), horizon + 20, width() * 0.82, height());
      }
      else if (m_iBodyMode == 2)
      {
        painter.setPen(QPen(QColor(94, 86, 60), 6));
        painter.drawEllipse(QRectF(width() * 0.08, horizon - 10, width() * 0.84, height() - horizon + 36));
      }

      if (m_bShowDirection)
      {
        const QColor directions[3] = {QColor(78, 145, 215), QColor(105, 195, 130), QColor(225, 118, 75)};
        const QPointF origin(width() * 0.56, horizon + 48);
        const QPointF tips[3] = {origin + QPointF(-90, -36), origin + QPointF(18, -48), origin + QPointF(105, -24)};
        for (int i = 0; i < 3; ++i)
        {
          painter.setPen(QPen(directions[i], 3));
          painter.drawLine(origin, tips[i]);
          painter.setBrush(directions[i]);
          painter.drawEllipse(tips[i], 4, 4);
        }
      }
    }
    else
    {
      painter.setPen(QPen(QColor(160, 230, 245, 45), 2));
      for (int x = -80; x < width(); x += 70)
        painter.drawLine(x, 0, x + 140, height());
      painter.setBrush(QColor(210, 248, 255, 90));
      painter.setPen(Qt::NoPen);
      for (int i = 0; i < 18; ++i)
      {
        const float x = plMath::Mod(i * 83.0f + m_fPhase * 19.0f, static_cast<float>(width()));
        const float y = plMath::Mod(i * 47.0f - m_fPhase * 31.0f + height() * 3.0f, static_cast<float>(height()));
        painter.drawEllipse(QPointF(x, y), 2 + (i % 3), 2 + (i % 3));
      }
    }

    painter.setPen(QColor(225, 244, 250));
    painter.drawText(QRect(14, 12, width() - 28, 24), Qt::AlignLeft | Qt::AlignVCenter,
      QString("%1  |  wavelength %2 m  |  steepness %3  |  roughness %4")
        .arg(m_iBodyMode == 0 ? "OCEAN" : (m_iBodyMode == 1 ? "RIVER" : "LAKE"))
        .arg(m_fWavelength, 0, 'f', 1)
        .arg(m_fSteepness, 0, 'f', 2)
        .arg(m_fRoughness, 0, 'f', 2));
  }

private:
  QTimer* m_pTimer = nullptr;
  QColor m_Shallow = QColor(30, 135, 180);
  QColor m_Deep = QColor(2, 25, 65);
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
};

// //////////////////////////////////////////////////////////////////////
// plQtWaterSpectrumGraph: wavelength coverage on a log axis, derived from
// the real cascade partition and the real CPU wave hash.
// //////////////////////////////////////////////////////////////////////

class plQtWaterSpectrumGraph : public QWidget
{
public:
  explicit plQtWaterSpectrumGraph(QWidget* pParent = nullptr)
    : QWidget(pParent)
  {
    setMinimumHeight(150);
    setMouseTracking(true);
  }

  /// Called with a property-filter string when the user clicks a cascade band.
  void SetBandClickedCallback(std::function<void(const QString&)> callback) { m_OnBandClicked = std::move(callback); }

  void SetData(const plWaterProfileResourceDescriptor& profile, plUInt32 uiEffectiveCascades, const char* szTierName)
  {
    plWaterProfileUiServices::GetCascadeBands(profile, m_CascadeBands);
    plWaterProfileUiServices::GetCpuWavelengths(profile, m_CpuWavelengths);
    m_fSmallest = plMath::Clamp(profile.m_fSmallestGpuWavelength, 0.01f, profile.m_fBaseWavelength);
    m_fBase = plMath::Max(profile.m_fBaseWavelength, 0.1f);
    m_uiEffectiveCascades = uiEffectiveCascades;
    m_sTierName = szTierName;
    update();
  }

protected:
  void paintEvent(QPaintEvent*) override
  {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(23, 27, 30));

    const QRectF graph(52, 22, width() - 70, height() - 56);
    if (graph.width() < 60 || graph.height() < 40)
      return;

    // Log-scale range with a little padding around [smallest, largest wavelength of interest].
    float fMaxWavelength = m_fBase;
    for (float fWavelength : m_CpuWavelengths)
      fMaxWavelength = plMath::Max(fMaxWavelength, fWavelength);

    const float fLogMin = plMath::Log2(plMath::Max(m_fSmallest * 0.8f, 0.01f));
    const float fLogMax = plMath::Log2(fMaxWavelength * 1.25f);
    const float fLogSpan = plMath::Max(fLogMax - fLogMin, 0.001f);

    auto XFor = [&](float fWavelength) -> qreal
    {
      const float t = (plMath::Log2(plMath::Max(fWavelength, 0.01f)) - fLogMin) / fLogSpan;
      return graph.left() + graph.width() * plMath::Clamp(t, 0.0f, 1.0f);
    };

    // Axis + decade ticks.
    painter.setPen(QColor(63, 70, 75));
    painter.drawLine(QPointF(graph.left(), graph.bottom()), QPointF(graph.right(), graph.bottom()));

    painter.setPen(QColor(120, 130, 136));
    const float tickValues[] = {0.05f, 0.1f, 0.5f, 1.0f, 5.0f, 10.0f, 50.0f, 100.0f, 500.0f};
    for (float fTick : tickValues)
    {
      if (plMath::Log2(fTick) < fLogMin || plMath::Log2(fTick) > fLogMax)
        continue;
      const qreal x = XFor(fTick);
      painter.setPen(QColor(45, 51, 55));
      painter.drawLine(QPointF(x, graph.top()), QPointF(x, graph.bottom()));
      painter.setPen(QColor(120, 130, 136));
      painter.drawText(QRectF(x - 24, graph.bottom() + 4, 48, 14), Qt::AlignCenter,
        fTick < 1.0f ? QString::number(fTick, 'f', 2) : QString::number(fTick, 'f', 0));
    }
    painter.drawText(QRectF(graph.left(), graph.bottom() + 17, graph.width(), 14), Qt::AlignCenter, QStringLiteral("wavelength (m, log scale)"));

    // Profile bounds.
    const QColor accent = GetWaterAccentColor();
    painter.setPen(QPen(accent, 1, Qt::DashLine));
    painter.drawLine(QPointF(XFor(m_fSmallest), graph.top()), QPointF(XFor(m_fSmallest), graph.bottom()));
    painter.drawLine(QPointF(XFor(m_fBase), graph.top()), QPointF(XFor(m_fBase), graph.bottom()));
    painter.setPen(accent);
    painter.drawText(QPointF(graph.left(), 14), QString("min %1 m").arg(m_fSmallest, 0, 'f', 2));
    painter.drawText(QPointF(graph.right() - 90, 14), QString("base %1 m").arg(m_fBase, 0, 'f', 1));

    // CPU wave markers (deterministic gameplay waves).
    painter.setBrush(QColor(223, 232, 228));
    painter.setPen(Qt::NoPen);
    const qreal cpuY = graph.top() + 8;
    for (float fWavelength : m_CpuWavelengths)
      painter.drawEllipse(QPointF(XFor(fWavelength), cpuY), 2.5, 2.5);
    painter.setPen(QColor(150, 165, 160));
    painter.drawText(QPointF(graph.left() + 4, cpuY + 4), QString("CPU waves x%1").arg(m_CpuWavelengths.GetCount()));

    // Cascade bands (cascade 0 = longest wavelengths).
    m_BandRects.Clear();
    const QColor cascadeColors[4] = {QColor(71, 132, 205), QColor(91, 177, 105), QColor(218, 105, 68), QColor(177, 112, 202)};
    const qreal bandHeight = plMath::Min<qreal>(18.0, (graph.height() - 26) / plMath::Max<plUInt32>(m_CascadeBands.GetCount(), 1u));
    for (plUInt32 i = 0; i < m_CascadeBands.GetCount(); ++i)
    {
      const plVec2 band = m_CascadeBands[i];
      const qreal x0 = XFor(band.x);
      const qreal x1 = XFor(band.y);
      const qreal y = graph.top() + 20 + i * (bandHeight + 4);
      const QRectF bandRect(x0, y, plMath::Max<qreal>(x1 - x0, 8.0), bandHeight);
      m_BandRects.PushBack(bandRect);

      const bool bCulled = i >= m_uiEffectiveCascades;
      if (bCulled)
      {
        painter.setBrush(QColor(58, 65, 69, 140));
        painter.setPen(QPen(QColor(229, 192, 123), 1, Qt::DashLine));
        painter.drawRoundedRect(bandRect, 3, 3);
        painter.setPen(QColor(229, 192, 123));
        painter.drawText(bandRect.adjusted(6, 0, -4, 0), Qt::AlignLeft | Qt::AlignVCenter,
          QString("cascade %1 \xC2\xB7 %2 \xE2\x80\x93 %3 m \xE2\x80\x94 culled by %4 tier")
            .arg(i)
            .arg(band.x, 0, 'f', 2)
            .arg(band.y, 0, 'f', 1)
            .arg(m_sTierName));
      }
      else
      {
        painter.setBrush(cascadeColors[i % 4]);
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(bandRect, 3, 3);
        painter.setPen(QColor(240, 246, 250));
        painter.drawText(bandRect.adjusted(6, 0, -4, 0), Qt::AlignLeft | Qt::AlignVCenter,
          QString("cascade %1 \xC2\xB7 %2 \xE2\x80\x93 %3 m").arg(i).arg(band.x, 0, 'f', 2).arg(band.y, 0, 'f', 1));
      }
    }

    if (m_CascadeBands.IsEmpty())
    {
      painter.setPen(QColor(150, 165, 160));
      painter.drawText(graph, Qt::AlignCenter, QString::fromUtf8("No GPU cascades authored \xE2\x80\x94 CPU analytic waves only."));
    }
  }

  virtual void mousePressEvent(QMouseEvent* pEvent) override
  {
    const plInt32 iBand = HitTestBand(pEvent->pos());
    if (iBand >= 0 && m_OnBandClicked)
    {
      // Culled band: jump to the cascade count; live band: jump to the wavelength range that defines it.
      const bool bCulled = static_cast<plUInt32>(iBand) >= m_uiEffectiveCascades;
      m_OnBandClicked(bCulled ? QStringLiteral("GpuCascade") : QStringLiteral("Wavelength"));
      return;
    }
    QWidget::mousePressEvent(pEvent);
  }

  virtual void mouseMoveEvent(QMouseEvent* pEvent) override
  {
    const plInt32 iBand = HitTestBand(pEvent->pos());
    if (iBand >= 0)
    {
      setCursor(Qt::PointingHandCursor);
      const plVec2 band = m_CascadeBands[iBand];
      const bool bCulled = static_cast<plUInt32>(iBand) >= m_uiEffectiveCascades;
      QString sText = QString::fromUtf8("Cascade %1 covers %2 \xE2\x80\x93 %3 m.").arg(iBand).arg(band.x, 0, 'f', 2).arg(band.y, 0, 'f', 1);
      if (bCulled)
        sText += QString::fromUtf8("\nNever simulated on the %1 tier \xE2\x80\x94 click to edit GPU Cascades.").arg(m_sTierName);
      else
        sText += QStringLiteral("\nClick to edit the wavelength range.");
      QToolTip::showText(mapToGlobal(pEvent->pos()), sText, this);
    }
    else
    {
      unsetCursor();
    }
    QWidget::mouseMoveEvent(pEvent);
  }

private:
  plInt32 HitTestBand(const QPoint& pos) const
  {
    for (plUInt32 i = 0; i < m_BandRects.GetCount() && i < m_CascadeBands.GetCount(); ++i)
    {
      if (m_BandRects[i].contains(pos))
        return static_cast<plInt32>(i);
    }
    return -1;
  }

  plHybridArray<plVec2, 4> m_CascadeBands;
  plHybridArray<QRectF, 4> m_BandRects;
  plHybridArray<float, 16> m_CpuWavelengths;
  std::function<void(const QString&)> m_OnBandClicked;
  float m_fSmallest = 0.5f;
  float m_fBase = 18.0f;
  plUInt32 m_uiEffectiveCascades = 3;
  QString m_sTierName = QStringLiteral("High");
};

// //////////////////////////////////////////////////////////////////////
// plQtWaterDepthRampWidget: computed water color by view depth
// //////////////////////////////////////////////////////////////////////

class plQtWaterDepthRampWidget : public QWidget
{
public:
  explicit plQtWaterDepthRampWidget(QWidget* pParent)
    : QWidget(pParent)
  {
    setFixedHeight(26);
    setToolTip(QString::fromUtf8("Computed water color by view depth: Shallow\xE2\x86\x92" "Deep tint with Beer\xE2\x80\x93Lambert absorption applied."));
  }

  void SetProfile(const plWaterProfileResourceDescriptor& profile)
  {
    m_Shallow = profile.m_ShallowColor;
    m_Deep = profile.m_DeepColor;
    m_vAbsorption = profile.m_vAbsorption;
    m_fRefractionDepth = plMath::Max(profile.m_fRefractionDepth, 0.01f);
    update();
  }

protected:
  void paintEvent(QPaintEvent*) override
  {
    QPainter painter(this);
    const QRectF ramp(0, 2, width(), 13);

    constexpr float fMaxDepth = 15.0f;
    QLinearGradient gradient(ramp.left(), 0, ramp.right(), 0);
    for (int i = 0; i <= 24; ++i)
    {
      const float t = i / 24.0f;
      const float fDepth = t * fMaxDepth;
      const float fMix = plMath::Saturate(fDepth / m_fRefractionDepth);
      plColor tint = plMath::Lerp(m_Shallow, m_Deep, fMix);
      tint.r *= plMath::Exp(-m_vAbsorption.x * fDepth);
      tint.g *= plMath::Exp(-m_vAbsorption.y * fDepth);
      tint.b *= plMath::Exp(-m_vAbsorption.z * fDepth);
      gradient.setColorAt(t, ToQColor(tint));
    }
    painter.fillRect(ramp, gradient);
    painter.setPen(QColor(0, 0, 0, 140));
    painter.drawRect(ramp.adjusted(0, 0, -1, -1));

    painter.setPen(QColor(139, 148, 156));
    const float labelDepths[] = {0.5f, 2.0f, 6.0f, 15.0f};
    for (float fDepth : labelDepths)
    {
      const qreal x = ramp.left() + ramp.width() * (fDepth / fMaxDepth);
      painter.drawText(QRectF(x - 24, ramp.bottom() + 1, 48, 10), Qt::AlignCenter, QString("%1 m").arg(fDepth, 0, 'f', fDepth < 1.0f ? 1 : 0));
    }
  }

private:
  plColor m_Shallow = plColor(0.08f, 0.34f, 0.42f);
  plColor m_Deep = plColor(0.008f, 0.035f, 0.075f);
  plVec3 m_vAbsorption = plVec3(0.35f, 0.07f, 0.025f);
  float m_fRefractionDepth = 6.0f;
};

// //////////////////////////////////////////////////////////////////////
// plQtWaterProfileAssetDocumentWindow
// //////////////////////////////////////////////////////////////////////

plQtWaterProfileAssetDocumentWindow::plQtWaterProfileAssetDocumentWindow(plDocument* pDocument)
  : plQtEngineDocumentWindow(static_cast<plAssetDocument*>(pDocument))
{
  GetDocument()->GetObjectManager()->m_PropertyEvents.AddEventHandler(plMakeDelegate(&plQtWaterProfileAssetDocumentWindow::PropertyEventHandler, this));

  auto* pPreferences = plPreferences::QueryPreferences<plWaterProfilePreferencesUser>(pDocument);
  plWaterProfileUiServices::SetPreviewBodyType(pDocument, pPreferences->GetBodyMode());

  {
    auto* pMenuBar = static_cast<plQtMenuBarActionMapView*>(menuBar());
    plActionContext context;
    context.m_sMapping = "WaterProfileAssetMenuBar";
    context.m_pDocument = pDocument;
    context.m_pWindow = this;
    pMenuBar->SetActionContext(context);
  }

  {
    auto* pToolBar = new plQtToolBarActionMapView("Toolbar", this);
    plActionContext context;
    context.m_sMapping = "WaterProfileAssetToolBar";
    context.m_pDocument = pDocument;
    context.m_pWindow = this;
    pToolBar->SetActionContext(context);
    pToolBar->setObjectName("WaterProfileAssetWindowToolBar");
    addToolBar(pToolBar);
  }

  // ---------------------------------------------------------------- presets dock
  // Presets are deliberately kept in a regular dock instead of a bespoke floating window so the editor remains
  // consistent with the rest of Plasma's asset tools and users can rearrange the workspace.
  QWidget* pPresetContainer = new QWidget(this);
  auto* pPresetLayout = new QVBoxLayout(pPresetContainer);
  pPresetLayout->setContentsMargins(6, 6, 6, 6);
  pPresetLayout->setSpacing(5);
  auto* pPresetTitle = new QLabel("WATER PROFILES", pPresetContainer);
  pPresetTitle->setStyleSheet("font-weight: 600; color: #b8c1c5; padding: 2px;");
  pPresetLayout->addWidget(pPresetTitle);

  m_pPresetSearch = new QLineEdit(pPresetContainer);
  m_pPresetSearch->setPlaceholderText("Search profiles...");
  m_pPresetSearch->setClearButtonEnabled(true);
  pPresetLayout->addWidget(m_pPresetSearch);

  {
    auto* pFilterLayout = new QHBoxLayout();
    pFilterLayout->setSpacing(3);
    const char* filterNames[4] = {"All", "Ocean", "River", "Lake"};
    for (int i = 0; i < 4; ++i)
    {
      m_pPresetFilterButtons[i] = new QToolButton(pPresetContainer);
      m_pPresetFilterButtons[i]->setText(filterNames[i]);
      m_pPresetFilterButtons[i]->setCheckable(true);
      m_pPresetFilterButtons[i]->setAutoExclusive(true);
      m_pPresetFilterButtons[i]->setToolButtonStyle(Qt::ToolButtonTextOnly);
      pFilterLayout->addWidget(m_pPresetFilterButtons[i]);
      connect(m_pPresetFilterButtons[i], &QToolButton::toggled, this, [this](bool) { RebuildPresetList(); });
    }
    m_pPresetFilterButtons[0]->setChecked(true);
    pFilterLayout->addStretch(1);
    pPresetLayout->addLayout(pFilterLayout);
  }

  m_pPresetList = new QListWidget(pPresetContainer);
  m_pPresetList->setIconSize(QSize(72, 42));
  m_pPresetList->setSpacing(2);
  m_pPresetList->setMinimumWidth(225);
  pPresetLayout->addWidget(m_pPresetList, 1);

  // Inline diff-confirmation card, shown while a preset apply is pending.
  m_pDiffCard = new QFrame(pPresetContainer);
  m_pDiffCard->setFrameShape(QFrame::StyledPanel);
  m_pDiffCard->setVisible(false);
  {
    auto* pCardLayout = new QVBoxLayout(m_pDiffCard);
    pCardLayout->setContentsMargins(8, 6, 8, 6);
    pCardLayout->setSpacing(3);
    m_pDiffTitle = new QLabel(m_pDiffCard);
    m_pDiffTitle->setWordWrap(true);
    m_pDiffTitle->setStyleSheet(QString("font-weight: 600; color: %1;").arg(GetWaterAccentColor().name()));
    pCardLayout->addWidget(m_pDiffTitle);
    m_pDiffRowsLayout = new QVBoxLayout();
    m_pDiffRowsLayout->setSpacing(1);
    pCardLayout->addLayout(m_pDiffRowsLayout);
    auto* pCardButtons = new QHBoxLayout();
    auto* pApply = new QPushButton("Apply", m_pDiffCard);
    auto* pKeepColors = new QPushButton("Keep Colors", m_pDiffCard);
    pKeepColors->setToolTip("Apply the preset but keep the current Shallow/Deep/Foam colors, Absorption and Scattering.");
    auto* pCancel = new QPushButton("Cancel", m_pDiffCard);
    pCardButtons->addWidget(pApply);
    pCardButtons->addWidget(pKeepColors);
    pCardButtons->addWidget(pCancel);
    pCardLayout->addLayout(pCardButtons);
    connect(pApply, &QPushButton::clicked, this, [this]() { ApplyPendingPreset(false); });
    connect(pKeepColors, &QPushButton::clicked, this, [this]() { ApplyPendingPreset(true); });
    connect(pCancel, &QPushButton::clicked, this, [this]() { CancelPendingPreset(); });
  }
  pPresetLayout->addWidget(m_pDiffCard);

  {
    auto* pPresetButtons = new QHBoxLayout();
    auto* pApplyPreset = new QPushButton("Apply Preset...", pPresetContainer);
    pApplyPreset->setToolTip("Shows what the selected preset would change before applying.");
    auto* pSaveAs = new QPushButton("Save As...", pPresetContainer);
    pSaveAs->setToolTip("Saves the current profile as a project preset (Editor/WaterPresets).");
    auto* pResetProfile = new QPushButton("Reset to Defaults", pPresetContainer);
    pPresetButtons->addWidget(pApplyPreset);
    pPresetButtons->addWidget(pSaveAs);
    pPresetLayout->addLayout(pPresetButtons);
    pPresetLayout->addWidget(pResetProfile);

    connect(pApplyPreset, &QPushButton::clicked, this, [this]()
    {
      const plInt32 iPreset = GetSelectedPresetIndex();
      if (iPreset >= 0)
        BeginApplyPreset(iPreset);
    });
    connect(pSaveAs, &QPushButton::clicked, this, [this]() { OnSaveAsPreset(); });
    connect(pResetProfile, &QPushButton::clicked, this, [this]() { BeginApplyPreset(0); });
  }

  connect(m_pPresetSearch, &QLineEdit::textChanged, this, [this](const QString&) { RebuildPresetList(); });
  connect(m_pPresetList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* pItem)
  {
    // Double-click applies without the confirmation card; undo still reverts it in one step.
    const int iPreset = pItem->data(Qt::UserRole).toInt();
    if (iPreset >= 0)
    {
      m_iPendingPreset = iPreset;
      ApplyPendingPreset(false);
    }
  });

  auto* pPresetPanel = new plQtDocumentPanel(this, pDocument);
  pPresetPanel->setObjectName("WaterProfilePresetsDockWidget");
  pPresetPanel->setWindowTitle("Water Profiles");
  pPresetPanel->setWidget(pPresetContainer);

  // ---------------------------------------------------------------- central preview
  QWidget* pPreviewContainer = new QWidget(this);
  auto* pPreviewLayout = new QVBoxLayout(pPreviewContainer);
  pPreviewLayout->setContentsMargins(8, 8, 8, 8);
  pPreviewLayout->addWidget(new plQtAssetStatusIndicator(static_cast<plAssetDocument*>(GetDocument())));

  QWidget* pControls = new QWidget(pPreviewContainer);
  auto* pControlsLayout = new QHBoxLayout(pControls);
  pControlsLayout->setContentsMargins(0, 0, 0, 0);

  m_pBodyMode = new QComboBox(pControls);
  m_pBodyMode->addItems({"Ocean", "River", "Lake"});

  m_pScene = new QComboBox(pControls);
  m_pScene->addItems({"Auto Scene", "Open Ocean", "Shoreline", "River Bend", "Lake Bowl"});
  m_pScene->setToolTip("Preview environment. Auto follows the body type; Shoreline makes shore fades, shore foam and absorption readable.");

  m_pQuality = new QComboBox(pControls);
  for (plUInt32 i = 0; i < plWaterProfileUiServices::QualityTierCount; ++i)
  {
    const auto& tier = plWaterProfileUiServices::s_QualityTiers[i];
    if (tier.m_uiMaxCascades == 0)
      m_pQuality->addItem(QString("%1 \xC2\xB7 CPU only").arg(tier.m_szName));
    else
      m_pQuality->addItem(QString("%1 \xC2\xB7 %2 casc \xC2\xB7 %3\xC2\xB2").arg(tier.m_szName).arg(tier.m_uiMaxCascades).arg(tier.m_uiResolution));
  }
  m_pQuality->setCurrentIndex(2);

  auto* pProjectionMode = new QComboBox(pControls);
  pProjectionMode->addItems({"2D Surface", "3D Runtime"});
  pProjectionMode->setCurrentIndex(1);

  m_pFreezeTime = new QCheckBox("Freeze", pControls);

  m_pInteractButton = new QToolButton(pControls);
  m_pInteractButton->setText("Interact");
  m_pInteractButton->setCheckable(true);
  m_pInteractButton->setToolTip(
    "Click or drag the water surface to spawn ripples (left mouse button; right button still orbits).\n"
    "Ripples keep animating even while Freeze is on.");

  auto* pShowCascades = new QCheckBox("Cascades", pControls);
  auto* pShowDirection = new QCheckBox("Direction", pControls);
  pShowCascades->setChecked(true);
  pShowDirection->setChecked(true);
  pShowCascades->setVisible(false);
  pShowDirection->setVisible(false);

  pControlsLayout->addWidget(new QLabel("Preview", pControls));
  pControlsLayout->addWidget(m_pBodyMode);
  pControlsLayout->addSpacing(8);
  pControlsLayout->addWidget(new QLabel("Scene", pControls));
  pControlsLayout->addWidget(m_pScene);
  pControlsLayout->addSpacing(8);
  pControlsLayout->addWidget(new QLabel("View", pControls));
  pControlsLayout->addWidget(pProjectionMode);
  pControlsLayout->addSpacing(8);
  pControlsLayout->addWidget(new QLabel("Quality", pControls));
  pControlsLayout->addWidget(m_pQuality);
  pControlsLayout->addSpacing(8);
  pControlsLayout->addWidget(m_pFreezeTime);
  pControlsLayout->addWidget(m_pInteractButton);
  pControlsLayout->addWidget(pShowCascades);
  pControlsLayout->addWidget(pShowDirection);
  pControlsLayout->addStretch(1);

  {
    const char* bookmarkNames[4] = {"Grazing", "Shore", "Overhead", "Underwater"};
    const char* bookmarkTips[4] = {
      "Low camera just above the surface \xE2\x80\x94 judge sun glitter, reflections and steepness.",
      "Mid-height view of the shoreline \xE2\x80\x94 judge shore fades, shore foam and refraction.",
      "Top-down view \xE2\x80\x94 judge wave patterns, wind direction and foam coverage.",
      "Submerged camera \xE2\x80\x94 judge underwater fog, caustics and the meniscus band."};
    for (int i = 0; i < 4; ++i)
    {
      m_pCameraBookmarks[i] = new QToolButton(pControls);
      m_pCameraBookmarks[i]->setText(bookmarkNames[i]);
      m_pCameraBookmarks[i]->setToolTip(bookmarkTips[i]);
      m_pCameraBookmarks[i]->setAutoRaise(true);
      pControlsLayout->addWidget(m_pCameraBookmarks[i]);
      connect(m_pCameraBookmarks[i], &QToolButton::clicked, this, [this, i]() { ApplyCameraBookmark(i); });
    }
  }

  pPreviewLayout->addWidget(pControls);

  m_pPreviewStack = new QStackedWidget(pPreviewContainer);
  m_pPreview = new plQtWaterProfilePreview(m_pPreviewStack);
  m_pPreviewStack->addWidget(m_pPreview);

  m_ViewConfig.m_Camera.LookAt(plVec3(-12.0f, -12.0f, 8.0f), plVec3::MakeZero(), plVec3(0, 0, 1));
  m_ViewConfig.ApplyPerspectiveSetting(55.0f, 0.05f, 500.0f);
  m_pViewWidget = new plQtOrbitCamViewWidget(this, &m_ViewConfig);
  m_pViewWidget->ConfigureFixed(plVec3::MakeZero(), plVec3::MakeZero(), plVec3(-12.0f, -12.0f, 8.0f));
  AddViewWidget(m_pViewWidget);
  // plQtViewWidgetContainer is itself an ADS CDockWidget and must never be used as a regular stacked page. Doing so
  // makes QStackedWidget visibility changes drive an unattached dock tab. The Water Profile central panel already
  // provides the one required dock; embed the engine view widget directly inside it.
  m_pViewWidget->setParent(m_pPreviewStack);
  m_pPreviewStack->addWidget(m_pViewWidget);
  m_pPreviewStack->setCurrentIndex(1);
  pPreviewLayout->addWidget(m_pPreviewStack, 1);

  // Interact mode: must sit at index 0 so it wins the left mouse button over the orbit-camera context
  // while enabled; disabled it passes everything through.
  m_pInteractContext = PL_DEFAULT_NEW(plWaterInteractContext, this, m_pViewWidget);
  m_pInteractContext->m_SendInteraction = plMakeDelegate(&plQtWaterProfileAssetDocumentWindow::SendWaterInteraction, this);
  m_pViewWidget->m_InputContexts.InsertAt(0, m_pInteractContext.Borrow());
  connect(m_pInteractButton, &QToolButton::toggled, this, [this](bool bChecked)
  { m_pInteractContext->SetEnabled(bChecked); });

  // Sun elevation control under the viewport.
  {
    auto* pSunLayout = new QHBoxLayout();
    pSunLayout->setContentsMargins(0, 2, 0, 0);
    m_pSunLabel = new QLabel(QString::fromUtf8("Sun 35\xC2\xB0"), pPreviewContainer);
    m_pSunLabel->setStyleSheet("color: #aebbc5;");
    m_pSunSlider = new QSlider(Qt::Horizontal, pPreviewContainer);
    m_pSunSlider->setRange(5, 85);
    m_pSunSlider->setValue(35);
    m_pSunSlider->setMaximumWidth(160);
    m_pSunSlider->setToolTip("Sun elevation above the horizon. Low angles emphasize glitter and reflections.");
    pSunLayout->addWidget(m_pSunLabel);
    pSunLayout->addWidget(m_pSunSlider);
    pSunLayout->addStretch(1);
    pPreviewLayout->addLayout(pSunLayout);
    connect(m_pSunSlider, &QSlider::valueChanged, this, [this](int iValue)
    {
      m_pSunLabel->setText(QString("Sun %1\xC2\xB0").arg(iValue));
      plPreferences::QueryPreferences<plWaterProfilePreferencesUser>(GetDocument())->SetSunElevation(iValue);
      SendSceneToEngine();
    });
  }

  // Status chips replace the old pipe-delimited summary string. Chips double as navigation.
  {
    auto* pChipsLayout = new QHBoxLayout();
    pChipsLayout->setContentsMargins(0, 2, 0, 0);
    pChipsLayout->setSpacing(4);

    auto MakeChip = [pPreviewContainer](const char* szToolTip) -> QToolButton*
    {
      auto* pChip = new QToolButton(pPreviewContainer);
      pChip->setAutoRaise(true);
      pChip->setToolButtonStyle(Qt::ToolButtonTextOnly);
      pChip->setToolTip(szToolTip);
      pChip->setCursor(Qt::PointingHandCursor);
      return pChip;
    };

    m_pChipValidation = MakeChip("Validation state. Click to open the Validation tab.");
    m_pChipWaves = MakeChip("Wave amplitude. Click to filter the properties to the wave spectrum.");
    m_pChipWind = MakeChip("Wind speed. Click to filter the properties to wind settings.");
    m_pChipCascades = MakeChip("Effective / authored GPU cascades at the current preview quality. Click to open the Spectrum tab.");
    m_pChipSpectrum = MakeChip("Estimated spectrum working set at the current preview quality. Click to open the Performance tab.");
    m_pChipDensity = MakeChip("Physics density. Click to filter the properties to physics settings.");

    pChipsLayout->addWidget(m_pChipValidation);
    pChipsLayout->addWidget(m_pChipWaves);
    pChipsLayout->addWidget(m_pChipWind);
    pChipsLayout->addWidget(m_pChipCascades);
    pChipsLayout->addWidget(m_pChipSpectrum);
    pChipsLayout->addWidget(m_pChipDensity);
    pChipsLayout->addStretch(1);
    pPreviewLayout->addLayout(pChipsLayout);

    connect(m_pChipValidation, &QToolButton::clicked, this, [this]() { m_pDiagnosticsTabs->setCurrentIndex(0); });
    connect(m_pChipCascades, &QToolButton::clicked, this, [this]() { m_pDiagnosticsTabs->setCurrentIndex(1); });
    connect(m_pChipSpectrum, &QToolButton::clicked, this, [this]() { m_pDiagnosticsTabs->setCurrentIndex(2); });
    connect(m_pChipWaves, &QToolButton::clicked, this, [this]() { m_pPropertyFilter->setText("Wave"); });
    connect(m_pChipWind, &QToolButton::clicked, this, [this]() { m_pPropertyFilter->setText("Wind"); });
    connect(m_pChipDensity, &QToolButton::clicked, this, [this]() { m_pPropertyFilter->setText("Density"); });
  }

  m_pDepthRamp = new plQtWaterDepthRampWidget(pPreviewContainer);
  pPreviewLayout->addWidget(m_pDepthRamp);

  auto* pCentralPanel = new plQtDocumentPanel(this, pDocument);
  pCentralPanel->setObjectName("WaterProfilePreviewDockWidget");
  pCentralPanel->setWindowTitle("Water Profile Preview");
  pCentralPanel->setWidget(pPreviewContainer);
  // ADS requires the central widget to be the very first dock registered with the manager. Registering even one
  // side panel first makes setCentralWidget() fail and allows the side panels to consume the preview area.
  auto* pCentralArea = m_pDockManager->setCentralWidget(pCentralPanel);
  auto* pPresetArea = m_pDockManager->addDockWidgetTab(ads::LeftDockWidgetArea, pPresetPanel);

  // ---------------------------------------------------------------- properties dock
  QWidget* pPropertyContainer = new QWidget(this);
  auto* pPropertyLayout = new QVBoxLayout(pPropertyContainer);
  pPropertyLayout->setContentsMargins(0, 0, 0, 0);
  pPropertyLayout->setSpacing(2);

  m_pPropertyFilter = new QLineEdit(pPropertyContainer);
  m_pPropertyFilter->setPlaceholderText("Filter properties...");
  m_pPropertyFilter->setClearButtonEnabled(true);
  pPropertyLayout->addWidget(m_pPropertyFilter);

  m_pTuningContextLabel = new QLabel(pPropertyContainer);
  m_pTuningContextLabel->setStyleSheet("color: #8b949c; padding: 1px 4px;");
  pPropertyLayout->addWidget(m_pTuningContextLabel);

  m_pPropertyGrid = new plQtPropertyGridWidget(pPropertyContainer, pDocument);
  pPropertyLayout->addWidget(m_pPropertyGrid, 1);

  connect(m_pPropertyFilter, &QLineEdit::textChanged, this, [this](const QString& sText)
  { m_pPropertyGrid->SetFilterText(sText.toUtf8().data()); });

  auto* pPropertyPanel = new plQtDocumentPanel(this, pDocument);
  pPropertyPanel->setObjectName("WaterProfilePropertiesDockWidget");
  pPropertyPanel->setWindowTitle("Water Profile Properties");
  pPropertyPanel->setWidget(pPropertyContainer);
  auto* pPropertyArea = m_pDockManager->addDockWidgetTab(ads::RightDockWidgetArea, pPropertyPanel);

  pDocument->GetSelectionManager()->SetSelection(pDocument->GetObjectManager()->GetRootObject()->GetChildren()[0]);

  // ---------------------------------------------------------------- preview control wiring
  connect(m_pBodyMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int iMode) { OnBodyModeChanged(iMode); });
  connect(m_pScene, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int iScene)
  {
    plPreferences::QueryPreferences<plWaterProfilePreferencesUser>(GetDocument())->SetScene(iScene);
    SendSceneToEngine();
  });
  connect(m_pQuality, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int iQuality)
  {
    plPreferences::QueryPreferences<plWaterProfilePreferencesUser>(GetDocument())->SetQuality(iQuality);
    m_pPreview->SetQuality(iQuality);
    SendRuntimePreviewSettings();
    UpdatePreview();
  });
  connect(pProjectionMode, qOverload<int>(&QComboBox::currentIndexChanged), this,
    [this, pShowCascades, pShowDirection](int iView)
    {
      m_pPreviewStack->setCurrentIndex(iView);
      const bool bSchematic = iView == 0;
      pShowCascades->setVisible(bSchematic);
      pShowDirection->setVisible(bSchematic);
    });
  connect(m_pFreezeTime, &QCheckBox::toggled, this, [this](bool bFrozen)
  {
    m_pPreview->SetFrozen(bFrozen);
    SendRuntimePreviewSettings();
  });
  connect(pShowCascades, &QCheckBox::toggled, m_pPreview, &plQtWaterProfilePreview::SetShowCascades);
  connect(pShowDirection, &QCheckBox::toggled, m_pPreview, &plQtWaterProfilePreview::SetShowDirection);

  // ---------------------------------------------------------------- diagnostics dock
  m_pDiagnosticsTabs = new QTabWidget(this);

  m_pValidationList = new QListWidget(m_pDiagnosticsTabs);
  m_pValidationList->setSelectionMode(QAbstractItemView::SingleSelection);
  m_pValidationList->setToolTip("Click an issue to filter the properties panel to the offending property.");
  m_pDiagnosticsTabs->addTab(m_pValidationList, "Validation");
  connect(m_pValidationList, &QListWidget::itemClicked, this, [this](QListWidgetItem* pItem)
  {
    const QString sProperty = pItem->data(Qt::UserRole).toString();
    if (!sProperty.isEmpty())
      m_pPropertyFilter->setText(sProperty);
  });

  m_pSpectrumGraph = new plQtWaterSpectrumGraph(m_pDiagnosticsTabs);
  m_pSpectrumGraph->SetBandClickedCallback([this](const QString& sFilter)
  { m_pPropertyFilter->setText(sFilter); });
  m_pDiagnosticsTabs->addTab(m_pSpectrumGraph, "Spectrum");

  m_pPerformanceTable = new QTableWidget(m_pDiagnosticsTabs);
  m_pPerformanceTable->setColumnCount(5);
  m_pPerformanceTable->setHorizontalHeaderLabels({"Tier", "Cascades", "Resolution", "Spectrum Memory", "Simulation"});
  m_pPerformanceTable->setRowCount(plWaterProfileUiServices::QualityTierCount);
  m_pPerformanceTable->verticalHeader()->setVisible(false);
  m_pPerformanceTable->horizontalHeader()->setStretchLastSection(true);
  m_pPerformanceTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_pPerformanceTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_pPerformanceTable->setSelectionMode(QAbstractItemView::SingleSelection);
  m_pPerformanceTable->setToolTip("Click a row to preview that quality tier. GPU timings are view-dependent \xE2\x80\x94 use the renderer profiler for measurements.");
  m_pDiagnosticsTabs->addTab(m_pPerformanceTable, "Performance");
  connect(m_pPerformanceTable, &QTableWidget::cellClicked, this, [this](int iRow, int)
  { m_pQuality->setCurrentIndex(iRow); });

  auto* pDiagnosticsPanel = new plQtDocumentPanel(this, pDocument);
  pDiagnosticsPanel->setObjectName("WaterProfileDiagnosticsDockWidget");
  pDiagnosticsPanel->setWindowTitle("Water Diagnostics");
  pDiagnosticsPanel->setWidget(m_pDiagnosticsTabs);
  auto* pDiagnosticsArea = m_pDockManager->addDockWidgetTab(ads::BottomDockWidgetArea, pDiagnosticsPanel);

  // Establish a useful first-open composition. Restored user layouts are applied later by plQtDocumentWindow and
  // therefore still take precedence over these defaults.
  m_pDockManager->setSplitterSizes(pCentralArea, {250, 820, 390});
  m_pDockManager->setSplitterSizes(pDiagnosticsArea, {690, 190});

  PL_IGNORE_UNUSED(pPresetArea);
  PL_IGNORE_UNUSED(pPropertyArea);

  // Restore the per-document preview configuration. setCurrentIndex/setValue fire the connected handlers,
  // which also send the settings to the engine.
  m_pQuality->setCurrentIndex(pPreferences->GetQuality());
  m_pScene->setCurrentIndex(pPreferences->GetScene());
  m_pSunSlider->setValue(pPreferences->GetSunElevation());
  m_pBodyMode->setCurrentIndex(pPreferences->GetBodyMode());

  RebuildPresetList();
  OnBodyModeChanged(m_pBodyMode->currentIndex());

  FinishWindowCreation();
  UpdatePreview();
}

plQtWaterProfileAssetDocumentWindow::~plQtWaterProfileAssetDocumentWindow()
{
  // The context is destroyed with this window, but the view widget is destroyed later by the base class —
  // unregister first so the view never dispatches into a dangling pointer.
  if (m_pInteractContext != nullptr)
  {
    m_pInteractContext->SetEnabled(false);
    m_pViewWidget->m_InputContexts.RemoveAndCopy(m_pInteractContext.Borrow());
    m_pInteractContext.Clear();
  }

  plWaterProfileUiServices::ClearPreviewBodyType(GetDocument());
  GetDocument()->GetObjectManager()->m_PropertyEvents.RemoveEventHandler(plMakeDelegate(&plQtWaterProfileAssetDocumentWindow::PropertyEventHandler, this));
  RestoreResource();
}

void plQtWaterProfileAssetDocumentWindow::InternalRedraw()
{
  plEditorInputContext::UpdateActiveInputContext();
  SendBodyTypeToEngine();
  SendRuntimePreviewSettings();
  SendRedrawMsg();
  plQtEngineDocumentWindow::InternalRedraw();
}

void plQtWaterProfileAssetDocumentWindow::SendRedrawMsg()
{
  if (plEditorEngineProcessConnection::GetSingleton()->IsProcessCrashed())
    return;

  for (plQtEngineViewWidget* pView : m_ViewWidgets)
  {
    pView->SetEnablePicking(false);
    pView->UpdateCameraInterpolation();
    pView->SyncToEngine();
  }
}

void plQtWaterProfileAssetDocumentWindow::SendBodyTypeToEngine()
{
  if (plEditorEngineProcessConnection::GetSingleton()->IsProcessCrashed())
    return;

  plDocumentConfigMsgToEngine msg;
  msg.m_sWhatToDo = "SetWaterBodyType";
  msg.m_iValue = m_pBodyMode != nullptr ? m_pBodyMode->currentIndex() : 0;
  GetEditorEngineConnection()->SendMessage(&msg);
}

void plQtWaterProfileAssetDocumentWindow::SendRuntimePreviewSettings()
{
  if (plEditorEngineProcessConnection::GetSingleton()->IsProcessCrashed())
    return;

  plDocumentConfigMsgToEngine qualityMsg;
  qualityMsg.m_sWhatToDo = "SetWaterQuality";
  qualityMsg.m_iValue = m_pQuality != nullptr ? m_pQuality->currentIndex() : 2;
  GetEditorEngineConnection()->SendMessage(&qualityMsg);

  plDocumentConfigMsgToEngine freezeMsg;
  freezeMsg.m_sWhatToDo = "SetWaterFrozen";
  freezeMsg.m_iValue = m_pFreezeTime != nullptr && m_pFreezeTime->isChecked() ? 1 : 0;
  GetEditorEngineConnection()->SendMessage(&freezeMsg);

  plDocumentConfigMsgToEngine sceneMsg;
  sceneMsg.m_sWhatToDo = "SetWaterScene";
  sceneMsg.m_iValue = GetEffectiveScene();
  GetEditorEngineConnection()->SendMessage(&sceneMsg);

  plDocumentConfigMsgToEngine sunMsg;
  sunMsg.m_sWhatToDo = "SetWaterSunElevation";
  sunMsg.m_iValue = m_pSunSlider != nullptr ? m_pSunSlider->value() : 35;
  GetEditorEngineConnection()->SendMessage(&sunMsg);
}

void plQtWaterProfileAssetDocumentWindow::SendSceneToEngine()
{
  if (plEditorEngineProcessConnection::GetSingleton()->IsProcessCrashed())
    return;

  plDocumentConfigMsgToEngine sceneMsg;
  sceneMsg.m_sWhatToDo = "SetWaterScene";
  sceneMsg.m_iValue = GetEffectiveScene();
  GetEditorEngineConnection()->SendMessage(&sceneMsg);

  plDocumentConfigMsgToEngine sunMsg;
  sunMsg.m_sWhatToDo = "SetWaterSunElevation";
  sunMsg.m_iValue = m_pSunSlider != nullptr ? m_pSunSlider->value() : 35;
  GetEditorEngineConnection()->SendMessage(&sunMsg);
}

void plQtWaterProfileAssetDocumentWindow::SendWaterInteraction(const plVec3& vPosition, bool bContinuous)
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

plInt32 plQtWaterProfileAssetDocumentWindow::GetEffectiveScene() const
{
  const int iScene = m_pScene != nullptr ? m_pScene->currentIndex() : 0;
  if (iScene > 0)
    return iScene - 1; // combo entries after "Auto" map 1:1 to the engine scene ids

  // Auto: pick the scene that shows the most profile behavior for the body type.
  switch (m_pBodyMode != nullptr ? m_pBodyMode->currentIndex() : 0)
  {
    case 1:
      return 2; // river bend
    case 2:
      return 3; // lake bowl
    default:
      return 1; // shoreline: shore fades, shore foam and the depth ramp are all visible
  }
}

void plQtWaterProfileAssetDocumentWindow::OnBodyModeChanged(int iMode)
{
  m_pPreview->SetBodyMode(iMode);
  SendBodyTypeToEngine();
  SendSceneToEngine();

  plPreferences::QueryPreferences<plWaterProfilePreferencesUser>(GetDocument())->SetBodyMode(iMode);
  plWaterProfileUiServices::SetPreviewBodyType(GetDocument(), iMode);

  const char* szBodyName = iMode == 1 ? "River" : (iMode == 2 ? "Lake" : "Ocean");
  m_pTuningContextLabel->setText(QString("Dimmed rows are ignored by %1 bodies (follows the preview body type).").arg(szBodyName));

  // The meta state is queried when the grid rebuilds; re-setting the selection forces that rebuild.
  auto* pSelectionManager = GetDocument()->GetSelectionManager();
  const plDocumentObject* pObject = GetDocument()->GetObjectManager()->GetRootObject()->GetChildren()[0];
  pSelectionManager->Clear();
  pSelectionManager->SetSelection(pObject);
}

void plQtWaterProfileAssetDocumentWindow::ApplyCameraBookmark(int iBookmark)
{
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

  m_pPreview->SetUnderwater(bUnderwater);
}

// ---------------------------------------------------------------- preset workflow

void plQtWaterProfileAssetDocumentWindow::RebuildPresetList()
{
  if (m_pPresetList == nullptr)
    return;

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

  const QString sSearch = m_pPresetSearch != nullptr ? m_pPresetSearch->text() : QString();
  int iTypeFilter = -1; // -1 = all
  for (int i = 1; i < 4; ++i)
  {
    if (m_pPresetFilterButtons[i] != nullptr && m_pPresetFilterButtons[i]->isChecked())
      iTypeFilter = i - 1;
  }

  m_pPresetList->clear();

  auto AddSectionHeader = [this](const char* szTitle)
  {
    auto* pHeader = new QListWidgetItem(szTitle);
    pHeader->setFlags(Qt::NoItemFlags);
    pHeader->setData(Qt::UserRole, -1);
    QFont font = pHeader->font();
    font.setPointSizeF(font.pointSizeF() * 0.85);
    font.setBold(true);
    pHeader->setFont(font);
    pHeader->setForeground(QColor(139, 148, 156));
    m_pPresetList->addItem(pHeader);
  };

  auto AddPresetItem = [this, &sSearch, iTypeFilter](plUInt32 uiPresetIndex) -> bool
  {
    const plWaterProfilePreset& preset = m_Presets[uiPresetIndex];
    if (iTypeFilter >= 0 && static_cast<int>(preset.m_BodyType) != iTypeFilter)
      return false;
    if (!sSearch.isEmpty() && !QString::fromUtf8(preset.m_sName.GetData()).contains(sSearch, Qt::CaseInsensitive))
      return false;

    auto* pItem = new QListWidgetItem(MakePresetIcon(preset.m_Descriptor),
      QString("%1\n%2").arg(QString::fromUtf8(preset.m_sName.GetData()), GetBodyTypeName(preset.m_BodyType)));
    pItem->setData(Qt::UserRole, static_cast<int>(uiPresetIndex));
    pItem->setSizeHint(QSize(190, 52));
    m_pPresetList->addItem(pItem);
    return true;
  };

  AddSectionHeader("BUILT-IN");
  for (plUInt32 i = 0; i < m_uiBuiltInPresetCount; ++i)
    AddPresetItem(i);

  if (m_Presets.GetCount() > m_uiBuiltInPresetCount)
  {
    AddSectionHeader("PROJECT");
    for (plUInt32 i = m_uiBuiltInPresetCount; i < m_Presets.GetCount(); ++i)
      AddPresetItem(i);
  }

  RefreshPresetTags();
}

void plQtWaterProfileAssetDocumentWindow::RefreshPresetTags()
{
  if (m_pPresetList == nullptr)
    return;

  const auto* pDocument = static_cast<const plWaterProfileAssetDocument*>(GetDocument());
  const plWaterProfileResourceDescriptor* pProfile = pDocument->GetProperties();

  plHybridArray<plWaterPresetDiffEntry, 8> diff;

  for (int i = 0; i < m_pPresetList->count(); ++i)
  {
    QListWidgetItem* pItem = m_pPresetList->item(i);
    const int iPreset = pItem->data(Qt::UserRole).toInt();
    if (iPreset < 0 || iPreset >= static_cast<int>(m_Presets.GetCount()))
      continue;

    const plWaterProfilePreset& preset = m_Presets[iPreset];
    QString sText = QString("%1\n%2").arg(QString::fromUtf8(preset.m_sName.GetData()), GetBodyTypeName(preset.m_BodyType));

    if (iPreset == m_iAppliedPreset && pProfile != nullptr)
    {
      plWaterProfilePresetLibrary::DiffDescriptors(*pProfile, preset.m_Descriptor, diff);
      if (diff.IsEmpty())
        sText += QString::fromUtf8("  \xC2\xB7  APPLIED");
      else
        sText += QString("  \xC2\xB7  MODIFIED (%1)").arg(diff.GetCount());
    }

    if (pItem->text() != sText)
      pItem->setText(sText);
  }
}

plInt32 plQtWaterProfileAssetDocumentWindow::GetSelectedPresetIndex() const
{
  if (QListWidgetItem* pItem = m_pPresetList->currentItem())
  {
    const int iPreset = pItem->data(Qt::UserRole).toInt();
    if (iPreset >= 0 && iPreset < static_cast<int>(m_Presets.GetCount()))
      return iPreset;
  }
  return -1;
}

void plQtWaterProfileAssetDocumentWindow::BeginApplyPreset(plInt32 iPresetIndex)
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
    RefreshPresetTags();
    return; // already matches, nothing to confirm
  }

  m_iPendingPreset = iPresetIndex;

  // Populate the confirmation card.
  while (QLayoutItem* pOld = m_pDiffRowsLayout->takeAt(0))
  {
    delete pOld->widget();
    delete pOld;
  }

  m_pDiffTitle->setText(QString("Apply \"%1\" \xE2\x80\x94 %2 value%3 change:")
                          .arg(QString::fromUtf8(m_Presets[iPresetIndex].m_sName.GetData()))
                          .arg(diff.GetCount())
                          .arg(diff.GetCount() == 1 ? "" : "s"));

  constexpr plUInt32 uiMaxRows = 8;
  for (plUInt32 i = 0; i < plMath::Min<plUInt32>(diff.GetCount(), uiMaxRows); ++i)
  {
    auto* pRow = new QLabel(QString("%1:  %2 \xE2\x86\x92 %3")
                              .arg(QString::fromUtf8(diff[i].m_sProperty.GetData()), FormatVariantShort(diff[i].m_OldValue), FormatVariantShort(diff[i].m_NewValue)),
      m_pDiffCard);
    pRow->setStyleSheet("color: #aebbc5; font-size: 11px;");
    m_pDiffRowsLayout->addWidget(pRow);
  }
  if (diff.GetCount() > uiMaxRows)
  {
    auto* pMore = new QLabel(QString("\xE2\x80\xA6 and %1 more").arg(diff.GetCount() - uiMaxRows), m_pDiffCard);
    pMore->setStyleSheet("color: #8b949c; font-size: 11px; font-style: italic;");
    m_pDiffRowsLayout->addWidget(pMore);
  }

  m_pDiffCard->setVisible(true);
}

void plQtWaterProfileAssetDocumentWindow::ApplyPendingPreset(bool bKeepColors)
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

  m_pBodyMode->setCurrentIndex(static_cast<int>(m_Presets[iPreset].m_BodyType));

  m_iAppliedPreset = iPreset;
  plPreferences::QueryPreferences<plWaterProfilePreferencesUser>(GetDocument())->SetAppliedPreset(m_Presets[iPreset].m_sName);
  CancelPendingPreset();
  UpdatePreview();
}

void plQtWaterProfileAssetDocumentWindow::CancelPendingPreset()
{
  m_iPendingPreset = -1;
  if (m_pDiffCard != nullptr)
    m_pDiffCard->setVisible(false);
}

void plQtWaterProfileAssetDocumentWindow::OnSaveAsPreset()
{
  bool bOk = false;
  const QString sName = QInputDialog::getText(this, "Save Water Preset", "Preset name:", QLineEdit::Normal, QString(), &bOk).trimmed();
  if (!bOk || sName.isEmpty())
    return;

  const auto* pDocument = static_cast<const plWaterProfileAssetDocument*>(GetDocument());
  const plWaterProfileResourceDescriptor* pProfile = pDocument->GetProperties();
  if (pProfile == nullptr)
    return;

  const auto bodyType = static_cast<plWaterProfilePreset::BodyType>(plMath::Clamp(m_pBodyMode->currentIndex(), 0, 2));
  if (plWaterProfilePresetLibrary::SaveProjectPreset(sName.toUtf8().data(), bodyType, *pProfile).Failed())
  {
    plLog::Error("Failed to save water preset '{}'. Is a project open?", sName.toUtf8().data());
    return;
  }

  RebuildPresetList();
}

// ---------------------------------------------------------------- refresh

void plQtWaterProfileAssetDocumentWindow::PropertyEventHandler(const plDocumentObjectPropertyEvent& e)
{
  // Applying a native descriptor rebuilds the document mirror property by property. During SwapStorage the native
  // property object is intentionally unavailable, so refreshing from these intermediate events would dereference a
  // transient null pointer. ApplyPendingPreset performs one refresh after the mirror transaction has completed instead.
  if (m_bApplyingPreset)
    return;

  UpdatePreview();
}

void plQtWaterProfileAssetDocumentWindow::UpdatePreview()
{
  const auto* pDocument = static_cast<const plWaterProfileAssetDocument*>(GetDocument());
  const plWaterProfileResourceDescriptor* pProfile = pDocument->GetProperties();
  if (pProfile == nullptr)
    return;

  const plWaterProfileResourceDescriptor& profile = *pProfile;
  m_pPreview->SetProfile(profile);
  m_pDepthRamp->SetProfile(profile);

  const plUInt32 uiQuality = static_cast<plUInt32>(plMath::Clamp(m_pQuality->currentIndex(), 0, static_cast<int>(plWaterProfileUiServices::QualityTierCount) - 1));
  const auto& tier = plWaterProfileUiServices::s_QualityTiers[uiQuality];
  const plUInt32 uiEffectiveCascades = plMath::Min<plUInt32>(profile.m_uiGpuCascadeCount, tier.m_uiMaxCascades);

  m_pSpectrumGraph->SetData(profile, uiEffectiveCascades, tier.m_szName);

  UpdateStatusChips(profile);
  UpdateValidation(profile);
  UpdatePerformanceTable(profile);
  RefreshPresetTags();

  SendLiveResourcePreview();
}

void plQtWaterProfileAssetDocumentWindow::UpdateStatusChips(const plWaterProfileResourceDescriptor& profile)
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

  if (uiErrors > 0)
  {
    m_pChipValidation->setText(QString("\xE2\x9C\x95 %1 ERROR%2").arg(uiErrors).arg(uiErrors == 1 ? "" : "S"));
    m_pChipValidation->setStyleSheet("color: #ff7b72; font-weight: 600;");
  }
  else if (uiWarnings > 0)
  {
    m_pChipValidation->setText(QString("\xE2\x96\xB2 VALID \xC2\xB7 %1 warning%2").arg(uiWarnings).arg(uiWarnings == 1 ? "" : "s"));
    m_pChipValidation->setStyleSheet("color: #e5c07b; font-weight: 600;");
  }
  else
  {
    m_pChipValidation->setText(QString::fromUtf8("\xE2\x9C\x93 VALID"));
    m_pChipValidation->setStyleSheet("color: #7bd88f; font-weight: 600;");
  }

  const plUInt32 uiQuality = static_cast<plUInt32>(plMath::Clamp(m_pQuality->currentIndex(), 0, static_cast<int>(plWaterProfileUiServices::QualityTierCount) - 1));
  const auto& tier = plWaterProfileUiServices::s_QualityTiers[uiQuality];
  const plUInt32 uiEffectiveCascades = plMath::Min<plUInt32>(profile.m_uiGpuCascadeCount, tier.m_uiMaxCascades);
  const double fSpectrumMiB = static_cast<double>(plWaterProfileUiServices::GetSpectrumMemoryBytes(uiQuality, profile.m_uiGpuCascadeCount)) / (1024.0 * 1024.0);

  m_pChipWaves->setText(QString("WAVES %1 m").arg(profile.m_fWaveAmplitude, 0, 'f', 2));
  m_pChipWind->setText(QString("WIND %1 m/s").arg(profile.m_fWindSpeed, 0, 'f', 1));
  m_pChipSpectrum->setText(QString("SPECTRUM %1 MiB").arg(fSpectrumMiB, 0, 'f', 1));
  m_pChipDensity->setText(QString("DENSITY %1 kg/m\xC2\xB3").arg(profile.m_fDensity, 0, 'f', 0));

  m_pChipCascades->setText(QString("CASCADES %1 / %2").arg(uiEffectiveCascades).arg(profile.m_uiGpuCascadeCount));
  if (uiEffectiveCascades < profile.m_uiGpuCascadeCount)
    m_pChipCascades->setStyleSheet("color: #e5c07b; font-weight: 600;");
  else
    m_pChipCascades->setStyleSheet(QString());
}

void plQtWaterProfileAssetDocumentWindow::UpdateValidation(const plWaterProfileResourceDescriptor& profile)
{
  plHybridArray<plWaterProfileIssue, 8> issues;
  profile.Validate(issues);

  m_pValidationList->clear();

  bool bHasErrors = false;
  for (const plWaterProfileIssue& issue : issues)
    bHasErrors = bHasErrors || issue.m_Severity == plWaterProfileIssue::Severity::Error;

  if (!bHasErrors)
  {
    auto* pItem = new QListWidgetItem(QString::fromUtf8("\xE2\x9C\x93  Profile valid \xE2\x80\x94 no blocking issues for ocean, river, or lake bodies."));
    pItem->setForeground(QColor(123, 216, 143));
    m_pValidationList->addItem(pItem);
  }

  for (const plWaterProfileIssue& issue : issues)
  {
    const bool bError = issue.m_Severity == plWaterProfileIssue::Severity::Error;
    auto* pItem = new QListWidgetItem(QString("%1  %2").arg(bError ? "\xE2\x9C\x95" : "\xE2\x96\xB2", QString::fromUtf8(issue.m_sMessage.GetData())));
    pItem->setForeground(bError ? QColor(255, 123, 114) : QColor(229, 192, 123));
    pItem->setData(Qt::UserRole, QString::fromUtf8(issue.m_sProperty.GetData()));
    pItem->setToolTip("Click to filter the properties panel to this property.");
    m_pValidationList->addItem(pItem);
  }

  // Editor-side lint: authored cascades the current preview tier never simulates.
  const plUInt32 uiQuality = static_cast<plUInt32>(plMath::Clamp(m_pQuality->currentIndex(), 0, static_cast<int>(plWaterProfileUiServices::QualityTierCount) - 1));
  const auto& tier = plWaterProfileUiServices::s_QualityTiers[uiQuality];
  if (profile.m_uiGpuCascadeCount > tier.m_uiMaxCascades)
  {
    auto* pItem = new QListWidgetItem(QString("\xE2\x96\xB2  %1 GPU cascades authored, but the %2 tier caps at %3 \xE2\x80\x94 the extra cascades are never simulated in this preview. See the Spectrum tab.")
                                        .arg(profile.m_uiGpuCascadeCount)
                                        .arg(tier.m_szName)
                                        .arg(tier.m_uiMaxCascades));
    pItem->setForeground(QColor(229, 192, 123));
    pItem->setData(Qt::UserRole, QStringLiteral("GpuCascadeCount"));
    m_pValidationList->addItem(pItem);
  }
}

void plQtWaterProfileAssetDocumentWindow::UpdatePerformanceTable(const plWaterProfileResourceDescriptor& profile)
{
  const int iCurrentTier = plMath::Clamp(m_pQuality->currentIndex(), 0, static_cast<int>(plWaterProfileUiServices::QualityTierCount) - 1);
  const QColor accent = GetWaterAccentColor();

  for (plUInt32 uiTier = 0; uiTier < plWaterProfileUiServices::QualityTierCount; ++uiTier)
  {
    const auto& tier = plWaterProfileUiServices::s_QualityTiers[uiTier];
    const plUInt32 uiEffectiveCascades = plMath::Min<plUInt32>(profile.m_uiGpuCascadeCount, tier.m_uiMaxCascades);
    const double fMiB = static_cast<double>(plWaterProfileUiServices::GetSpectrumMemoryBytes(uiTier, profile.m_uiGpuCascadeCount)) / (1024.0 * 1024.0);

    const QString cells[5] = {
      static_cast<int>(uiTier) == iCurrentTier ? QString("%1  \xE2\x97\x82 preview").arg(tier.m_szName) : QString::fromUtf8(tier.m_szName),
      QString("%1 / %2").arg(uiEffectiveCascades).arg(profile.m_uiGpuCascadeCount),
      tier.m_uiResolution == 0 ? QString::fromUtf8("\xE2\x80\x94") : QString("%1\xC2\xB2").arg(tier.m_uiResolution),
      QString("%1 MiB").arg(fMiB, 0, 'f', 1),
      QString::fromUtf8(tier.m_szSimMode),
    };

    for (int iColumn = 0; iColumn < 5; ++iColumn)
    {
      auto* pItem = new QTableWidgetItem(cells[iColumn]);
      if (static_cast<int>(uiTier) == iCurrentTier)
      {
        pItem->setForeground(accent);
        QFont font = pItem->font();
        font.setBold(true);
        pItem->setFont(font);
      }
      m_pPerformanceTable->setItem(uiTier, iColumn, pItem);
    }
  }

  m_pPerformanceTable->resizeColumnsToContents();
}

void plQtWaterProfileAssetDocumentWindow::SendLiveResourcePreview()
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

void plQtWaterProfileAssetDocumentWindow::RestoreResource()
{
  plRestoreResourceMsgToEngine msg;
  msg.m_sResourceType = "WaterProfile";

  plStringBuilder sResourceId;
  msg.m_sResourceID = plConversionUtils::ToString(GetDocument()->GetGuid(), sResourceId);
  plEditorEngineProcessConnection::GetSingleton()->SendMessage(&msg);
}
