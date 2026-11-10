#include <WaterPlugin/WaterPluginPCH.h>

#include <WaterPlugin/Rendering/WaterSpectrum.h>

#include <cmath>

// The spectrum itself is one text for the shader and for this file. It is written with scalars and a handful of
// functions, which are given their C++ meaning here.
namespace plWaterSpectrumShader
{
  using std::exp;
  using std::log;
  using std::pow;
  using std::sqrt;

  inline float max(float a, float b) { return a > b ? a : b; }
  inline float min(float a, float b) { return a < b ? a : b; }

#include <Shaders/Water/WaterSpectrumShape.h>
} // namespace plWaterSpectrumShader

float plWaterSpectrum::GetCascadeLength(float fBaseWavelength, plUInt32 uiCascade, plUInt32 uiCascadeCount)
{
  return plWaterSpectrumShader::WaterCascadeLength(fBaseWavelength, static_cast<float>(uiCascade), static_cast<float>(uiCascadeCount));
}

double plWaterSpectrum::ComputeUnscaledVariance(const plWaterProfileResourceDescriptor& profile, plUInt32 uiResolution, plUInt32 uiCascadeCount)
{
  if (uiResolution < 2 || uiCascadeCount == 0)
    return 0.0;

  plVec2 vWind = profile.m_vWindDirection;
  if (vWind.NormalizeIfNotZero(plVec2(1.0f, 0.0f)).Failed())
    vWind = plVec2(1.0f, 0.0f);

  const float fBaseWavelength = plMath::Max(profile.m_fBaseWavelength, 0.05f);
  const float fMode = static_cast<float>(profile.m_SpectrumMode.GetValue());
  const float fResolution = static_cast<float>(uiResolution);
  const plInt32 iHalf = static_cast<plInt32>(uiResolution / 2);

  // The shader builds h(k) = h0(k) e^(iwt) + conj(h0(-k)) e^(-iwt) with E|h0(k)|^2 = density(k) dk^2 scale^2, so every
  // wave vector of the lattice adds density(k) dk^2 + density(-k) dk^2 to the variance of the height.
  double fVariance = 0.0;
  for (plUInt32 uiCascade = 0; uiCascade < uiCascadeCount; ++uiCascade)
  {
    const float fCascade = static_cast<float>(uiCascade);
    const float fCount = static_cast<float>(uiCascadeCount);
    const float fLength = plWaterSpectrumShader::WaterCascadeLength(fBaseWavelength, fCascade, fCount);
    const float fBandHigh = plWaterSpectrumShader::WaterCascadeBandHigh(fBaseWavelength, fCascade, fCount, fResolution);
    const float fBandLow = plWaterSpectrumShader::WaterCascadeBandLow(fBaseWavelength, fCascade, fCount, fResolution);
    const float fDeltaK = 6.2831853f / fLength;
    const float fLowest = plMath::Max(fBandLow, fDeltaK * 0.5f);

    for (plInt32 y = -iHalf; y < iHalf; ++y)
    {
      for (plInt32 x = -iHalf; x < iHalf; ++x)
      {
        const float kx = static_cast<float>(x) * fDeltaK;
        const float ky = static_cast<float>(y) * fDeltaK;
        const float k = plMath::Sqrt(kx * kx + ky * ky);
        if (k < fLowest || k >= fBandHigh)
          continue;
        const double fDensity = plWaterSpectrumShader::WaterSpectrumDensity(kx, ky, vWind.x, vWind.y, fBaseWavelength, fMode);
        // This wave vector as k, and again as the -k of its opposite: the lattice holds both, and the band is symmetric.
        fVariance += 2.0 * plMath::Max(fDensity, 0.0) * static_cast<double>(fDeltaK) * static_cast<double>(fDeltaK);
      }
    }
  }
  return fVariance;
}

float plWaterSpectrum::ComputeAmplitudeScale(const plWaterProfileResourceDescriptor& profile, plUInt32 uiResolution, plUInt32 uiCascadeCount)
{
  const double fVariance = ComputeUnscaledVariance(profile, uiResolution, uiCascadeCount);
  if (fVariance <= 1.0e-30 || profile.m_fWaveAmplitude <= 0.0f)
    return 0.0f;
  const double fDeviation = static_cast<double>(profile.m_fWaveAmplitude) * s_fSpectrumDeviationShare;
  return static_cast<float>(fDeviation / std::sqrt(fVariance));
}

float plWaterSpectrum::GetCosmeticHeightBound(const plWaterProfileResourceDescriptor& profile)
{
  return 4.0f * plMath::Max(profile.m_fWaveAmplitude, 0.0f) * s_fSpectrumDeviationShare;
}

PL_STATICLINK_FILE(WaterPlugin, WaterPlugin_Rendering_Implementation_WaterSpectrum);
