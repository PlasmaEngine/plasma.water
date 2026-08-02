#include <EditorPluginWater/EditorPluginWaterPCH.h>

#include <EditorPluginWater/WaterProfileAsset/WaterProfileAsset.h>

#include <Foundation/Serialization/GraphPatch.h>
#include <GuiFoundation/UIServices/ImageCache.moc.h>

#include <QImage>
#include <QPainter>
#include <QPainterPath>

// Keep the asset type version in step with the transformed resource layout so the curator invalidates stale binaries.
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plWaterProfileAssetDocument, 5, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;

class plWaterProfileResourceDescriptorPatch_5_6 final : public plGraphPatch
{
public:
  plWaterProfileResourceDescriptorPatch_5_6()
    : plGraphPatch("plWaterProfileResourceDescriptor", 6)
  {
  }

  virtual void Patch(plGraphPatchContext& ref_context, plAbstractObjectGraph* pGraph, plAbstractObjectNode* pNode) const override
  {
    // Object graphs use zero for absent members, so explicitly preserve the intended non-zero default.
    if (pNode->FindProperty("ReflectedCaustics") == nullptr)
      pNode->AddProperty("ReflectedCaustics", 0.15f);
  }
};

plWaterProfileResourceDescriptorPatch_5_6 g_plWaterProfileResourceDescriptorPatch_5_6;

plWaterProfileAssetDocument::plWaterProfileAssetDocument(plStringView sDocumentPath)
  : plSimpleAssetDocument<plWaterProfileResourceDescriptor>(sDocumentPath, plAssetDocEngineConnection::Simple)
{
  m_bDescriptorAbiCompatible = plGetWaterProfileDescriptorRuntimeAbiVersion() == PL_WATER_PROFILE_DESCRIPTOR_ABI_VERSION &&
                               plGetWaterProfileDescriptorRuntimeSize() == sizeof(plWaterProfileResourceDescriptor);
  if (!m_bDescriptorAbiCompatible)
  {
    plLog::Error("WaterProfile descriptor ABI mismatch (editor: version {}, {} bytes; runtime: version {}, {} bytes). "
                 "Rebuild WaterPlugin and EditorPluginWater together before opening WaterProfile assets.",
      PL_WATER_PROFILE_DESCRIPTOR_ABI_VERSION, sizeof(plWaterProfileResourceDescriptor),
      plGetWaterProfileDescriptorRuntimeAbiVersion(), plGetWaterProfileDescriptorRuntimeSize());
  }
}

void plWaterProfileAssetDocument::InitializeAfterLoading(bool bFirstTimeCreation)
{
  // Applying RTTI properties with different descriptor layouts writes strings and other non-trivial members at
  // invalid offsets. Refuse initialization instead of allowing the document mirror to corrupt the heap.
  if (!m_bDescriptorAbiCompatible)
    return;

  SUPER::InitializeAfterLoading(bFirstTimeCreation);

  plWaterProfileResourceDescriptor* pProfile = GetProperties();

  // Also repair assets saved by the old implementation when they are unmistakably the all-zero object. New assets
  // receive the same values through reflection, but assigning the native descriptor keeps this migration atomic.
  const bool bWasSavedUninitialized = pProfile->m_uiWaveSeed == 0 && pProfile->m_vWindDirection.GetLengthSquared() == 0.0f &&
                                      pProfile->m_fBaseWavelength == 0.0f && pProfile->m_fIndexOfRefraction == 0.0f &&
                                      pProfile->m_fDensity == 0.0f && pProfile->m_uiCpuWaveCount == 0;

  if (bFirstTimeCreation || bWasSavedUninitialized)
  {
    const plWaterProfileResourceDescriptor defaults;
    *pProfile = defaults;
    ApplyNativePropertyChangesToObjectManager();
    GetCommandHistory()->ClearUndoHistory();
  }
  else if (pProfile->m_fEnvironmentReflectionWeight == 0.0f && pProfile->m_fMicroNormalScale == 0.0f &&
           pProfile->m_fMicroNormalStrength == 0.0f && pProfile->m_fSunGlitterStrength == 0.0f &&
           pProfile->m_fRefractionEdgeRejection == 0.0f && pProfile->m_FoamColor == plColor::Black &&
           pProfile->m_fFoamRoughness == 0.0f && pProfile->m_fFoamBreakupScale == 0.0f)
  {
    const plWaterProfileResourceDescriptor defaults;
    pProfile->m_fEnvironmentReflectionWeight = defaults.m_fEnvironmentReflectionWeight;
    pProfile->m_fMicroNormalScale = defaults.m_fMicroNormalScale;
    pProfile->m_fMicroNormalStrength = defaults.m_fMicroNormalStrength;
    pProfile->m_fSunGlitterStrength = defaults.m_fSunGlitterStrength;
    pProfile->m_fRefractionEdgeRejection = defaults.m_fRefractionEdgeRejection;
    pProfile->m_FoamColor = defaults.m_FoamColor;
    pProfile->m_fFoamRoughness = defaults.m_fFoamRoughness;
    pProfile->m_fFoamBreakupScale = defaults.m_fFoamBreakupScale;
    ApplyNativePropertyChangesToObjectManager();
  }
}

plTransformStatus plWaterProfileAssetDocument::InternalTransformAsset(plStreamWriter& stream, plStringView sOutputTag, const plPlatformProfile* pAssetProfile,
  const plAssetFileHeader& assetHeader, plBitflags<plTransformFlags> transformFlags)
{
  const plWaterProfileResourceDescriptor* pProfile = GetProperties();

  const plStatus validation = pProfile->Validate();
  if (validation.Failed())
    return plTransformStatus(validation);

  pProfile->Save(stream);
  return plTransformStatus(PL_SUCCESS);
}

plTransformStatus plWaterProfileAssetDocument::InternalCreateThumbnail(const ThumbnailInfo& thumbnailInfo)
{
  const plWaterProfileResourceDescriptor* pProfile = GetProperties();

  QImage image(plThumbnailSize, plThumbnailSize, QImage::Format_RGBA8888);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing, true);

  const QColor shallow = QColor::fromRgbF(pProfile->m_ShallowColor.r, pProfile->m_ShallowColor.g, pProfile->m_ShallowColor.b, 1.0f);
  const QColor deep = QColor::fromRgbF(pProfile->m_DeepColor.r, pProfile->m_DeepColor.g, pProfile->m_DeepColor.b, 1.0f);
  QLinearGradient gradient(0, 0, 0, image.height());
  gradient.setColorAt(0.0, QColor(42, 78, 108));
  gradient.setColorAt(0.42, shallow);
  gradient.setColorAt(1.0, deep);
  painter.fillRect(image.rect(), gradient);

  const float amplitude = plMath::Clamp(pProfile->m_fWaveAmplitude * 5.0f, 4.0f, 24.0f);
  for (plUInt32 layer = 0; layer < 4; ++layer)
  {
    QPainterPath wave;
    const float yBase = image.height() * (0.42f + layer * 0.14f);
    for (int x = -4; x <= image.width() + 4; x += 4)
    {
      const float y = yBase + plMath::Sin(plAngle::MakeFromRadian((x * 0.045f) + layer * 1.7f)) * amplitude / (layer + 1.0f);
      if (x == -4)
        wave.moveTo(x, y);
      else
        wave.lineTo(x, y);
    }
    painter.setPen(QPen(layer == 0 ? QColor(220, 249, 255, 220) : QColor(105, 204, 240, 150), layer == 0 ? 5.0 : 3.0));
    painter.drawPath(wave);
  }

  painter.setPen(QPen(QColor(255, 255, 255, 190), 2));
  painter.drawEllipse(QPointF(image.width() * 0.78, image.height() * 0.2), 10, 10);
  painter.end();

  return SaveThumbnail(image, thumbnailInfo);
}
