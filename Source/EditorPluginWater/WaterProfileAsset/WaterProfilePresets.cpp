#include <EditorPluginWater/EditorPluginWaterPCH.h>

#include <EditorPluginWater/WaterProfileAsset/WaterProfilePresets.h>

#include <Foundation/IO/FileSystem/DeferredFileWriter.h>
#include <Foundation/IO/FileSystem/FileReader.h>
#include <Foundation/IO/OSFile.h>
#include <Foundation/Reflection/ReflectionUtils.h>
#include <Foundation/Serialization/ReflectionSerializer.h>
#include <Foundation/Strings/PathUtils.h>
#include <ToolsFoundation/Project/ToolsProject.h>

/// Serialized wrapper so a project preset file carries its display metadata alongside the descriptor.
struct plWaterProfilePresetFile
{
  plString m_sName;
  plInt32 m_iBodyType = 0;
  plWaterProfileResourceDescriptor m_Descriptor;
};

PL_DECLARE_REFLECTABLE_TYPE(PL_NO_LINKAGE, plWaterProfilePresetFile);

// clang-format off
PL_BEGIN_STATIC_REFLECTED_TYPE(plWaterProfilePresetFile, plNoBase, 1, plRTTIDefaultAllocator<plWaterProfilePresetFile>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Name", m_sName),
    PL_MEMBER_PROPERTY("BodyType", m_iBodyType),
    PL_MEMBER_PROPERTY("Descriptor", m_Descriptor),
  }
  PL_END_PROPERTIES;
}
PL_END_STATIC_REFLECTED_TYPE;
// clang-format on

namespace
{
  plResult GetProjectPresetDirectory(plStringBuilder& out_sPath)
  {
    if (!plToolsProject::IsProjectOpen())
      return PL_FAILURE;

    out_sPath = plToolsProject::GetSingleton()->GetProjectDirectory();
    out_sPath.AppendPath("Editor", "WaterPresets");
    return PL_SUCCESS;
  }
} // namespace

void plWaterProfilePresetLibrary::GetBuiltInPresets(plDynamicArray<plWaterProfilePreset>& out_presets)
{
  out_presets.Clear();

  auto AddPreset = [&out_presets](const char* szName, plWaterProfilePreset::BodyType bodyType) -> plWaterProfileResourceDescriptor&
  {
    plWaterProfilePreset& preset = out_presets.ExpandAndGetRef();
    preset.m_sName = szName;
    preset.m_BodyType = bodyType;
    return preset.m_Descriptor;
  };

  // Index 0 must stay the canonical defaults ("Reset to Defaults" applies it).
  AddPreset("Calm Ocean", plWaterProfilePreset::BodyType::Ocean);

  {
    plWaterProfileResourceDescriptor& profile = AddPreset("Coastal", plWaterProfilePreset::BodyType::Ocean);
    profile.m_fWindSpeed = 1.5f;
    profile.m_fWaveAmplitude = 0.42f;
    profile.m_fBaseWavelength = 25.0f;
    profile.m_fWaveSteepness = 0.46f;
    profile.m_ShallowColor = plColor(0.055f, 0.43f, 0.46f, 0.62f);
    profile.m_DeepColor = plColor(0.006f, 0.075f, 0.105f, 0.95f);
    profile.m_fFoamStrength = 0.95f;
    profile.m_fFoamShoreDepth = 1.15f;
    profile.m_fRefractionStrength = 0.015f;
  }

  {
    plWaterProfileResourceDescriptor& profile = AddPreset("Storm", plWaterProfilePreset::BodyType::Ocean);
    profile.m_fWindSpeed = 3.0f;
    profile.m_fWaveAmplitude = 1.15f;
    profile.m_fBaseWavelength = 52.0f;
    profile.m_fWaveSteepness = 0.7f;
    profile.m_ShallowColor = plColor(0.075f, 0.16f, 0.18f, 0.72f);
    profile.m_DeepColor = plColor(0.008f, 0.022f, 0.03f, 0.98f);
    profile.m_fRoughness = 0.16f;
    profile.m_fFoamStrength = 1.35f;
    profile.m_fFoamCrestThreshold = 0.58f;
    profile.m_fEnvironmentReflectionWeight = 1.0f;
    profile.m_fSunGlitterStrength = 0.32f;
  }

  {
    plWaterProfileResourceDescriptor& profile = AddPreset("Tropical Lagoon", plWaterProfilePreset::BodyType::Ocean);
    profile.m_fWindSpeed = 0.7f;
    profile.m_fWaveAmplitude = 0.14f;
    profile.m_fBaseWavelength = 12.0f;
    profile.m_fWaveSteepness = 0.24f;
    profile.m_ShallowColor = plColor(0.1f, 0.55f, 0.52f, 0.55f);
    profile.m_DeepColor = plColor(0.01f, 0.16f, 0.18f, 0.9f);
    profile.m_vAbsorption = plVec3(0.12f, 0.04f, 0.02f);
    profile.m_vScattering = plVec3(0.02f, 0.08f, 0.09f);
    profile.m_fFoamStrength = 0.4f;
    profile.m_fSunGlitterStrength = 0.5f;
    profile.m_fRefractionDepth = 4.0f;
  }

  {
    plWaterProfileResourceDescriptor& profile = AddPreset("Deep Swell", plWaterProfilePreset::BodyType::Ocean);
    profile.m_fWindSpeed = 2.0f;
    profile.m_fWaveAmplitude = 0.9f;
    profile.m_fBaseWavelength = 84.0f;
    profile.m_fWaveSteepness = 0.35f;
    profile.m_ShallowColor = plColor(0.05f, 0.2f, 0.28f, 0.7f);
    profile.m_DeepColor = plColor(0.004f, 0.02f, 0.045f, 0.98f);
    profile.m_fRoughness = 0.05f;
    profile.m_fFoamStrength = 0.5f;
    profile.m_fEnvironmentReflectionWeight = 1.1f;
  }

  {
    plWaterProfileResourceDescriptor& profile = AddPreset("River Clear", plWaterProfilePreset::BodyType::River);
    profile.m_SpectrumMode = plWaterWaveSpectrumMode::Analytic;
    profile.m_fWindSpeed = 1.0f;
    profile.m_fWaveAmplitude = 0.07f;
    profile.m_fBaseWavelength = 4.5f;
    profile.m_fWaveSteepness = 0.18f;
    profile.m_uiGpuCascadeCount = 1;
    profile.m_fFlowScale = 1.35f;
    profile.m_ShallowColor = plColor(0.07f, 0.43f, 0.39f, 0.48f);
    profile.m_DeepColor = plColor(0.012f, 0.12f, 0.13f, 0.88f);
    profile.m_vAbsorption = plVec3(0.18f, 0.045f, 0.025f);
    profile.m_fFoamStrength = 0.28f;
    profile.m_fEnvironmentReflectionWeight = 0.8f;
    profile.m_fSunGlitterStrength = 0.12f;
    profile.m_fRefractionDepth = 2.5f;
    profile.m_fFiniteMeshCellSize = 0.5f;
    profile.m_uiMaxRiverCrossSubdivisions = 32;
    profile.m_fShoreWaveFadeDistance = 0.75f;
    profile.m_fRiverCurvatureRefinement = 1.0f;
  }

  {
    plWaterProfileResourceDescriptor& profile = AddPreset("Mountain Rapids", plWaterProfilePreset::BodyType::River);
    profile.m_SpectrumMode = plWaterWaveSpectrumMode::Analytic;
    profile.m_fWindSpeed = 1.5f;
    profile.m_fWaveAmplitude = 0.12f;
    profile.m_fBaseWavelength = 3.0f;
    profile.m_fWaveSteepness = 0.55f;
    profile.m_uiGpuCascadeCount = 1;
    profile.m_fFlowScale = 2.4f;
    profile.m_ShallowColor = plColor(0.1f, 0.42f, 0.44f, 0.5f);
    profile.m_DeepColor = plColor(0.02f, 0.1f, 0.12f, 0.85f);
    profile.m_vAbsorption = plVec3(0.2f, 0.06f, 0.03f);
    profile.m_fFoamStrength = 1.2f;
    profile.m_fFoamCrestThreshold = 0.5f;
    profile.m_fRefractionDepth = 1.8f;
    profile.m_fFiniteMeshCellSize = 0.4f;
    profile.m_uiMaxRiverCrossSubdivisions = 48;
    profile.m_fShoreWaveFadeDistance = 0.5f;
    profile.m_fRiverCurvatureRefinement = 1.0f;
  }

  {
    plWaterProfileResourceDescriptor& profile = AddPreset("Lowland Muddy", plWaterProfilePreset::BodyType::River);
    profile.m_SpectrumMode = plWaterWaveSpectrumMode::Analytic;
    profile.m_fWindSpeed = 0.6f;
    profile.m_fWaveAmplitude = 0.05f;
    profile.m_fBaseWavelength = 5.0f;
    profile.m_fWaveSteepness = 0.12f;
    profile.m_uiGpuCascadeCount = 1;
    profile.m_fFlowScale = 1.0f;
    profile.m_ShallowColor = plColor(0.28f, 0.24f, 0.14f, 0.7f);
    profile.m_DeepColor = plColor(0.06f, 0.05f, 0.02f, 0.97f);
    profile.m_vAbsorption = plVec3(0.6f, 0.35f, 0.2f);
    profile.m_vScattering = plVec3(0.12f, 0.1f, 0.06f);
    profile.m_fFoamStrength = 0.2f;
    profile.m_fSunGlitterStrength = 0.08f;
    profile.m_fRefractionDepth = 1.2f;
    profile.m_fFiniteMeshCellSize = 0.75f;
    profile.m_fShoreWaveFadeDistance = 1.0f;
  }

  {
    plWaterProfileResourceDescriptor& profile = AddPreset("Alpine Lake", plWaterProfilePreset::BodyType::Lake);
    profile.m_SpectrumMode = plWaterWaveSpectrumMode::Analytic;
    profile.m_fWindSpeed = 0.8f;
    profile.m_fWaveAmplitude = 0.11f;
    profile.m_fBaseWavelength = 8.0f;
    profile.m_fWaveSteepness = 0.16f;
    profile.m_uiGpuCascadeCount = 2;
    profile.m_ShallowColor = plColor(0.055f, 0.39f, 0.48f, 0.58f);
    profile.m_DeepColor = plColor(0.008f, 0.065f, 0.12f, 0.96f);
    profile.m_vAbsorption = plVec3(0.24f, 0.055f, 0.018f);
    profile.m_fRoughness = 0.065f;
    profile.m_fFoamStrength = 0.18f;
    profile.m_fEnvironmentReflectionWeight = 1.15f;
    profile.m_fSunGlitterStrength = 0.48f;
    profile.m_fFiniteMeshCellSize = 0.75f;
    profile.m_fShoreWaveFadeDistance = 1.5f;
    profile.m_bEnableLakeSpectrum = false;
  }

  {
    plWaterProfileResourceDescriptor& profile = AddPreset("Still Pond", plWaterProfilePreset::BodyType::Lake);
    profile.m_SpectrumMode = plWaterWaveSpectrumMode::Analytic;
    profile.m_fWindSpeed = 0.4f;
    profile.m_fWaveAmplitude = 0.03f;
    profile.m_fBaseWavelength = 2.5f;
    profile.m_fWaveSteepness = 0.1f;
    profile.m_uiGpuCascadeCount = 1;
    profile.m_ShallowColor = plColor(0.09f, 0.32f, 0.3f, 0.6f);
    profile.m_DeepColor = plColor(0.015f, 0.06f, 0.05f, 0.95f);
    profile.m_vAbsorption = plVec3(0.3f, 0.1f, 0.05f);
    profile.m_fRoughness = 0.03f;
    profile.m_fFoamStrength = 0.05f;
    profile.m_fSunGlitterStrength = 0.5f;
    profile.m_fFiniteMeshCellSize = 0.5f;
    profile.m_fShoreWaveFadeDistance = 0.75f;
    profile.m_bEnableLakeSpectrum = false;
  }
}

void plWaterProfilePresetLibrary::LoadProjectPresets(plDynamicArray<plWaterProfilePreset>& out_presets)
{
  out_presets.Clear();

  plStringBuilder sDirectory;
  if (GetProjectPresetDirectory(sDirectory).Failed())
    return;

  plFileSystemIterator it;
  for (it.StartSearch(sDirectory, plFileSystemIteratorFlags::ReportFiles); it.IsValid(); it.Next())
  {
    if (!it.GetStats().m_sName.EndsWith_NoCase(".plWaterProfilePreset"))
      continue;

    plStringBuilder sFilePath;
    it.GetStats().GetFullPath(sFilePath);

    plFileReader file;
    if (file.Open(sFilePath).Failed())
      continue;

    plWaterProfilePresetFile presetFile;
    plReflectionSerializer::ReadObjectPropertiesFromDDL(file, *plGetStaticRTTI<plWaterProfilePresetFile>(), &presetFile);

    presetFile.m_Descriptor.Sanitize();

    plWaterProfilePreset& preset = out_presets.ExpandAndGetRef();
    preset.m_sName = presetFile.m_sName.IsEmpty() ? it.GetStats().m_sName.GetView().GetFileName() : presetFile.m_sName.GetView();
    preset.m_BodyType = static_cast<plWaterProfilePreset::BodyType>(plMath::Clamp(presetFile.m_iBodyType, 0, 2));
    preset.m_bProjectPreset = true;
    preset.m_sFilePath = sFilePath;
    preset.m_Descriptor = presetFile.m_Descriptor;
  }

  out_presets.Sort([](const plWaterProfilePreset& lhs, const plWaterProfilePreset& rhs)
    { return lhs.m_sName.Compare(rhs.m_sName) < 0; });
}

plResult plWaterProfilePresetLibrary::SaveProjectPreset(plStringView sName, plWaterProfilePreset::BodyType bodyType,
  const plWaterProfileResourceDescriptor& descriptor)
{
  plStringBuilder sDirectory;
  if (GetProjectPresetDirectory(sDirectory).Failed())
    return PL_FAILURE;

  // Keep the file name portable: the display name lives inside the file. Spaces and the characters a file name cannot
  // contain become '_'.
  plStringBuilder sSpacesReplaced = sName;
  sSpacesReplaced.ReplaceAll(" ", "_");

  plStringBuilder sFileName;
  plPathUtils::MakeValidFilename(sSpacesReplaced, '_', sFileName);

  plStringBuilder sFilePath = sDirectory;
  sFilePath.AppendPath(sFileName);
  sFilePath.Append(".plWaterProfilePreset");

  plWaterProfilePresetFile presetFile;
  presetFile.m_sName = sName;
  presetFile.m_iBodyType = static_cast<plInt32>(bodyType);
  presetFile.m_Descriptor = descriptor;

  plDeferredFileWriter file;
  file.SetOutput(sFilePath);
  plReflectionSerializer::WriteObjectToDDL(file, plGetStaticRTTI<plWaterProfilePresetFile>(), &presetFile);
  return file.Close();
}

void plWaterProfilePresetLibrary::DiffDescriptors(const plWaterProfileResourceDescriptor& current,
  const plWaterProfileResourceDescriptor& target, plDynamicArray<plWaterPresetDiffEntry>& out_diff)
{
  out_diff.Clear();

  const plRTTI* pRtti = plGetStaticRTTI<plWaterProfileResourceDescriptor>();
  for (const plAbstractProperty* pProp : pRtti->GetProperties())
  {
    if (pProp->GetCategory() != plPropertyCategory::Member)
      continue;

    const auto* pMember = static_cast<const plAbstractMemberProperty*>(pProp);
    const plVariant oldValue = plReflectionUtils::GetMemberPropertyValue(pMember, &current);
    const plVariant newValue = plReflectionUtils::GetMemberPropertyValue(pMember, &target);

    if (oldValue == newValue)
      continue;

    plWaterPresetDiffEntry& entry = out_diff.ExpandAndGetRef();
    entry.m_sProperty = pProp->GetPropertyName();
    entry.m_OldValue = oldValue;
    entry.m_NewValue = newValue;
  }
}

void plWaterProfilePresetLibrary::CopyColorProperties(const plWaterProfileResourceDescriptor& from, plWaterProfileResourceDescriptor& inout_to)
{
  inout_to.m_ShallowColor = from.m_ShallowColor;
  inout_to.m_DeepColor = from.m_DeepColor;
  inout_to.m_FoamColor = from.m_FoamColor;
  inout_to.m_vAbsorption = from.m_vAbsorption;
  inout_to.m_vScattering = from.m_vScattering;
}
