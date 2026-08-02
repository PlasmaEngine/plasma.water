#include <EditorPluginWater/EditorPluginWaterPCH.h>

#include <EditorPluginWater/WaterProfileAsset/WaterProfileUiServices.h>

#include <GuiFoundation/PropertyGrid/PropertyMetaState.h>
#include <ToolsFoundation/Document/Document.h>
#include <ToolsFoundation/Object/DocumentObjectBase.h>
#include <ToolsFoundation/Object/DocumentObjectManager.h>

// Matches the tier configuration applied by plWaterProfileViewContext::SetQuality and the runtime quality settings.
const plWaterPreviewQualityTier plWaterProfileUiServices::s_QualityTiers[plWaterProfileUiServices::QualityTierCount] = {
  {"Mobile", 0, 0, "CPU analytic only"},
  {"Medium", 2, 128, "FFT half-rate"},
  {"High", 3, 256, "FFT full-rate"},
  {"Ultra", 4, 256, "FFT full-rate"},
};

namespace
{
  // Preview body type per open document. Pure UI-thread state, cleared when the window closes.
  static plMap<const plDocument*, plInt32> s_PreviewBodyTypes;

  // Mirrors the wave hash in WaterWorldModule.cpp so the spectrum chart shows the ACTUAL CPU wavelengths.
  plUInt32 HashWaterValue(plUInt32 uiValue)
  {
    uiValue ^= uiValue >> 16;
    uiValue *= 0x7feb352du;
    uiValue ^= uiValue >> 15;
    uiValue *= 0x846ca68bu;
    uiValue ^= uiValue >> 16;
    return uiValue;
  }

  float HashToUnitFloat(plUInt32 uiValue)
  {
    return static_cast<float>(HashWaterValue(uiValue) & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
  }
} // namespace

plUInt64 plWaterProfileUiServices::GetSpectrumMemoryBytes(plUInt32 uiTier, plUInt32 uiAuthoredCascades)
{
  uiTier = plMath::Min(uiTier, QualityTierCount - 1);
  const plWaterPreviewQualityTier& tier = s_QualityTiers[uiTier];
  const plUInt64 uiEffectiveCascades = plMath::Min<plUInt64>(uiAuthoredCascades, tier.m_uiMaxCascades);
  return static_cast<plUInt64>(tier.m_uiResolution) * tier.m_uiResolution * 8ull * uiEffectiveCascades * 2ull;
}

void plWaterProfileUiServices::SetPreviewBodyType(const plDocument* pDocument, plInt32 iBodyType)
{
  s_PreviewBodyTypes[pDocument] = plMath::Clamp(iBodyType, 0, 2);
}

plInt32 plWaterProfileUiServices::GetPreviewBodyType(const plDocument* pDocument)
{
  plInt32 iBodyType = 0;
  s_PreviewBodyTypes.TryGetValue(pDocument, iBodyType);
  return iBodyType;
}

void plWaterProfileUiServices::ClearPreviewBodyType(const plDocument* pDocument)
{
  s_PreviewBodyTypes.Remove(pDocument);
}

void plWaterProfileUiServices::PropertyMetaStateEventHandler(plPropertyMetaStateEvent& e)
{
  if (e.m_pObject == nullptr || !e.m_pObject->GetType()->GetTypeName().IsEqual("plWaterProfileResourceDescriptor"))
    return;

  const plDocument* pDocument = e.m_pObject->GetDocumentObjectManager()->GetDocument();
  const plInt32 iBodyType = GetPreviewBodyType(pDocument);

  auto& states = *e.m_pPropertyStates;
  auto Dim = [&states](const char* szProperty)
  { states[szProperty].m_Visibility = plPropertyUiState::Disabled; };

  switch (iBodyType)
  {
    case 0: // Ocean: no finite-body mesh, no flow field, no lake spectrum toggle.
      Dim("FiniteMeshCellSize");
      Dim("MaxRiverCrossSubdivisions");
      Dim("ShoreWaveFadeDistance");
      Dim("RiverCurvatureRefinement");
      Dim("FlowScale");
      Dim("EnableLakeSpectrum");
      break;

    case 1: // River: everything except the lake spectrum toggle applies.
      Dim("EnableLakeSpectrum");
      break;

    case 2: // Lake: closed body, no flow advection, no river-specific tessellation.
      Dim("FlowScale");
      Dim("MaxRiverCrossSubdivisions");
      Dim("RiverCurvatureRefinement");
      break;

    default:
      break;
  }
}

void plWaterProfileUiServices::GetCascadeBands(const plWaterProfileResourceDescriptor& profile, plDynamicArray<plVec2>& out_bands)
{
  out_bands.Clear();

  const plUInt32 uiCascades = plMath::Clamp<plUInt32>(profile.m_uiGpuCascadeCount, 0, 4);
  if (uiCascades == 0)
    return;

  const float fBase = plMath::Max(profile.m_fBaseWavelength, 0.1f);
  const float fSmallest = plMath::Clamp(profile.m_fSmallestGpuWavelength, 0.01f, fBase);
  const float fLogBase = plMath::Log2(fBase);
  const float fLogSmallest = plMath::Log2(fSmallest);

  for (plUInt32 i = 0; i < uiCascades; ++i)
  {
    const float fT0 = static_cast<float>(i) / uiCascades;
    const float fT1 = static_cast<float>(i + 1) / uiCascades;
    const float fMax = plMath::Pow(2.0f, plMath::Lerp(fLogBase, fLogSmallest, fT0));
    const float fMin = plMath::Pow(2.0f, plMath::Lerp(fLogBase, fLogSmallest, fT1));
    out_bands.PushBack(plVec2(fMin, fMax));
  }
}

void plWaterProfileUiServices::GetCpuWavelengths(const plWaterProfileResourceDescriptor& profile, plDynamicArray<float>& out_wavelengths)
{
  out_wavelengths.Clear();

  const plUInt32 uiWaveCount = plMath::Clamp<plUInt32>(profile.m_uiCpuWaveCount, 1, 16);
  for (plUInt32 i = 0; i < uiWaveCount; ++i)
  {
    const float fWaveLengthScale = plMath::Lerp(0.45f, 1.8f, HashToUnitFloat(profile.m_uiWaveSeed + i * 31u + 7u));
    out_wavelengths.PushBack(plMath::Max(profile.m_fBaseWavelength * fWaveLengthScale, 0.1f));
  }
}
