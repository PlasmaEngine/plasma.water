#pragma once

#include <WaterPlugin/Resources/WaterProfileResource.h>
#include <WaterPlugin/WaterPluginDLL.h>

/// \brief The CPU's side of the wave spectrum that WaterSpectrumUpdate.plShader evaluates.
///
/// The spectrum decides how wave energy is spread over wavelengths and directions. How tall the waves are is the
/// profile's amplitude. To bring the two together, the same sum the shader builds its wave field from is added up
/// here once per profile change, and the shader is handed the factor that makes its field come out at the wanted
/// height. Both sides include the same text for the spectrum itself (Shaders/Water/WaterSpectrumShape.h).
namespace plWaterSpectrum
{
  /// Of a profile's amplitude, the share that the spectrum's field takes as its standard deviation. The deterministic
  /// base waves (the ones gameplay queries follow) are drawn at s_fBaseWaveShare of the amplitude while a spectrum is
  /// active, so the sea is not ruled by four regular swells.
  constexpr float s_fSpectrumDeviationShare = 0.5f;
  constexpr float s_fBaseWaveShare = 0.5f;

  /// Side length in metres of a cascade's tile. Cascade 0 is the smallest.
  PL_WATER_DLL float GetCascadeLength(float fBaseWavelength, plUInt32 uiCascade, plUInt32 uiCascadeCount);

  /// The variance of the height field that the shader would produce with an amplitude scale of one.
  PL_WATER_DLL double ComputeUnscaledVariance(const plWaterProfileResourceDescriptor& profile, plUInt32 uiResolution, plUInt32 uiCascadeCount);

  /// The factor for plWaterGpuSpectrumProfile::WaveParams.w: with it, the field's standard deviation is the profile's
  /// amplitude times s_fSpectrumDeviationShare. Zero for a profile without waves.
  PL_WATER_DLL float ComputeAmplitudeScale(const plWaterProfileResourceDescriptor& profile, plUInt32 uiResolution, plUInt32 uiCascadeCount);

  /// What the spectrum's field adds to the surface at most, as far as that can be said of a random field: four
  /// standard deviations, which one point in thirty thousand exceeds. Gameplay does not see this layer, so anything
  /// that has to stay clear of the visible surface (a camera, a floating label) keeps this much room above what a
  /// query returns.
  PL_WATER_DLL float GetCosmeticHeightBound(const plWaterProfileResourceDescriptor& profile);
} // namespace plWaterSpectrum
