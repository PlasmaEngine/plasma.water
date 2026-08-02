#pragma once

#include <EditorPluginWater/EditorPluginWaterDLL.h>

#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/Strings/String.h>
#include <Foundation/Types/Variant.h>
#include <WaterPlugin/Resources/WaterProfileResource.h>

/// \brief One entry in the preset panel: a full descriptor plus display metadata.
struct plWaterProfilePreset
{
  enum class BodyType : plUInt8
  {
    Ocean,
    River,
    Lake,
  };

  plString m_sName;
  BodyType m_BodyType = BodyType::Ocean;
  bool m_bProjectPreset = false;
  plString m_sFilePath; ///< Absolute path for project presets, empty for built-ins.
  plWaterProfileResourceDescriptor m_Descriptor;
};

/// \brief One property difference between the current profile and a preset, shown in the apply-confirmation card.
struct plWaterPresetDiffEntry
{
  plString m_sProperty;
  plVariant m_OldValue;
  plVariant m_NewValue;
};

/// \brief Built-in preset table, project preset IO, and reflection-based descriptor diffing.
class plWaterProfilePresetLibrary
{
public:
  /// The canonical built-in presets. Index 0 ("Calm Ocean") equals a default-constructed descriptor.
  static void GetBuiltInPresets(plDynamicArray<plWaterProfilePreset>& out_presets);

  /// Scans <project>/Editor/WaterPresets for saved presets.
  static void LoadProjectPresets(plDynamicArray<plWaterProfilePreset>& out_presets);

  /// Writes the descriptor as a DDL preset file under <project>/Editor/WaterPresets.
  static plResult SaveProjectPreset(plStringView sName, plWaterProfilePreset::BodyType bodyType, const plWaterProfileResourceDescriptor& descriptor);

  /// Reflection walk over all descriptor member properties; returns every property whose value differs.
  static void DiffDescriptors(const plWaterProfileResourceDescriptor& current, const plWaterProfileResourceDescriptor& target,
    plDynamicArray<plWaterPresetDiffEntry>& out_diff);

  /// Copies the color-defining properties (shallow/deep/foam colors, absorption, scattering) from one descriptor to another.
  /// Used by "keep colors" when applying a preset.
  static void CopyColorProperties(const plWaterProfileResourceDescriptor& from, plWaterProfileResourceDescriptor& inout_to);
};
