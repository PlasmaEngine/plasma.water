#include <EditorPluginWater/EditorPluginWaterPCH.h>

#include <EditorPluginWater/WaterProfileAsset/WaterProfileAsset.h>

#include <Foundation/Serialization/GraphPatch.h>
#include <GuiFoundation/UIServices/ImageCache.h>
#include <Texture/Image/Image.h>

namespace
{
  /// A colour channel as the thumbnail stores it: the profile's colours are used as they are, without conversion.
  plUInt8 ToByte(float f)
  {
    return static_cast<plUInt8>(plMath::Clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f);
  }

  plColorGammaUB Lerp(const plColorGammaUB& a, const plColorGammaUB& b, float t)
  {
    return plColorGammaUB(static_cast<plUInt8>(plMath::Lerp<float>(a.r, b.r, t)), static_cast<plUInt8>(plMath::Lerp<float>(a.g, b.g, t)),
      static_cast<plUInt8>(plMath::Lerp<float>(a.b, b.b, t)), 255);
  }

  /// Blends the colour over the pixel; coverage scales the colour's alpha (anti-aliased edges).
  void BlendPixel(plImage& ref_image, plInt32 x, plInt32 y, const plColorGammaUB& color, float fCoverage)
  {
    if (x < 0 || y < 0 || x >= static_cast<plInt32>(ref_image.GetWidth()) || y >= static_cast<plInt32>(ref_image.GetHeight()) || fCoverage <= 0.0f)
      return;

    plColorGammaUB& pixel = *ref_image.GetPixelPointer<plColorGammaUB>(0, 0, 0, x, y);
    const float fAlpha = plMath::Min(fCoverage, 1.0f) * color.a / 255.0f;
    pixel.r = static_cast<plUInt8>(plMath::Lerp<float>(pixel.r, color.r, fAlpha));
    pixel.g = static_cast<plUInt8>(plMath::Lerp<float>(pixel.g, color.g, fAlpha));
    pixel.b = static_cast<plUInt8>(plMath::Lerp<float>(pixel.b, color.b, fAlpha));
  }

  /// Top to bottom: sky blue, the shallow colour at 42 %, the deep colour at the bottom.
  void FillGradient(plImage& ref_image, const plColorGammaUB& shallow, const plColorGammaUB& deep)
  {
    const plColorGammaUB top(42, 78, 108, 255);
    const float fHeight = static_cast<float>(ref_image.GetHeight());

    for (plUInt32 y = 0; y < ref_image.GetHeight(); ++y)
    {
      const float t = (y + 0.5f) / fHeight;
      const plColorGammaUB color = t < 0.42f ? Lerp(top, shallow, t / 0.42f) : Lerp(shallow, deep, (t - 0.42f) / 0.58f);

      for (plUInt32 x = 0; x < ref_image.GetWidth(); ++x)
        *ref_image.GetPixelPointer<plColorGammaUB>(0, 0, 0, x, y) = color;
    }
  }

  /// One wave line, fWidth pixels thick.
  void DrawWave(plImage& ref_image, float fBaseY, float fAmplitude, float fPhase, const plColorGammaUB& color, float fWidth)
  {
    auto WaveY = [&](float x)
    { return fBaseY + plMath::Sin(plAngle::MakeFromRadian(x * 0.045f + fPhase)) * fAmplitude; };

    for (plInt32 x = 0; x < static_cast<plInt32>(ref_image.GetWidth()); ++x)
    {
      const float fX = x + 0.5f;
      const float fY = WaveY(fX);
      const float fSlope = WaveY(fX + 0.5f) - WaveY(fX - 0.5f);
      const float fScale = 1.0f / plMath::Sqrt(1.0f + fSlope * fSlope);

      for (plInt32 y = static_cast<plInt32>(fY - fWidth) - 1; y <= static_cast<plInt32>(fY + fWidth) + 1; ++y)
      {
        const float fDistance = plMath::Abs(y + 0.5f - fY) * fScale;
        BlendPixel(ref_image, x, y, color, fWidth * 0.5f + 0.5f - fDistance);
      }
    }
  }

  /// A circle outline around the centre.
  void DrawRing(plImage& ref_image, const plVec2& vCenter, float fRadius, const plColorGammaUB& color, float fWidth)
  {
    const plInt32 iExtent = static_cast<plInt32>(fRadius + fWidth) + 1;

    for (plInt32 y = static_cast<plInt32>(vCenter.y) - iExtent; y <= static_cast<plInt32>(vCenter.y) + iExtent; ++y)
    {
      for (plInt32 x = static_cast<plInt32>(vCenter.x) - iExtent; x <= static_cast<plInt32>(vCenter.x) + iExtent; ++x)
      {
        const float fDistance = plMath::Abs((plVec2(x + 0.5f, y + 0.5f) - vCenter).GetLength() - fRadius);
        BlendPixel(ref_image, x, y, color, fWidth * 0.5f + 0.5f - fDistance);
      }
    }
  }
} // namespace

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

  plImageHeader header;
  header.SetImageFormat(plImageFormat::R8G8B8A8_UNORM);
  header.SetWidth(plThumbnailSize);
  header.SetHeight(plThumbnailSize);

  plImage image;
  image.ResetAndAlloc(header);

  const plColorGammaUB shallow(ToByte(pProfile->m_ShallowColor.r), ToByte(pProfile->m_ShallowColor.g), ToByte(pProfile->m_ShallowColor.b), 255);
  const plColorGammaUB deep(ToByte(pProfile->m_DeepColor.r), ToByte(pProfile->m_DeepColor.g), ToByte(pProfile->m_DeepColor.b), 255);
  FillGradient(image, shallow, deep);

  const float fSize = static_cast<float>(plThumbnailSize);
  const float fAmplitude = plMath::Clamp(pProfile->m_fWaveAmplitude * 5.0f, 4.0f, 24.0f);
  for (plUInt32 layer = 0; layer < 4; ++layer)
  {
    const float fBaseY = fSize * (0.42f + layer * 0.14f);
    const plColorGammaUB color = layer == 0 ? plColorGammaUB(220, 249, 255, 220) : plColorGammaUB(105, 204, 240, 150);
    DrawWave(image, fBaseY, fAmplitude / (layer + 1.0f), layer * 1.7f, color, layer == 0 ? 5.0f : 3.0f);
  }

  DrawRing(image, plVec2(fSize * 0.78f, fSize * 0.2f), 10.0f, plColorGammaUB(255, 255, 255, 190), 2.0f);

  return SaveThumbnail(image, thumbnailInfo);
}
