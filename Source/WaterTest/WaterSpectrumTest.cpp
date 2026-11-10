#include <WaterTest/WaterTestPCH.h>

#include <WaterPlugin/Rendering/WaterSpectrum.h>

PL_CREATE_SIMPLE_TEST(Water, Spectrum)
{
  auto makeProfile = [](plWaterWaveSpectrumMode::Enum mode, float fAmplitude, float fWavelength)
  {
    plWaterProfileResourceDescriptor profile;
    profile.m_SpectrumMode = mode;
    profile.m_fWaveAmplitude = fAmplitude;
    profile.m_fBaseWavelength = fWavelength;
    profile.m_vWindDirection = plVec2(0.8f, 0.6f);
    return profile;
  };

  PL_TEST_BLOCK(plTestBlock::Enabled, "The spectrum is scaled to the profile's amplitude, whatever its shape")
  {
    // The shader's field has the variance that is added up here, times the square of the scale it is given. So with
    // the scale applied, the field's standard deviation is the profile's amplitude times the spectrum's share.
    for (plWaterWaveSpectrumMode::Enum mode : {plWaterWaveSpectrumMode::Analytic, plWaterWaveSpectrumMode::Phillips, plWaterWaveSpectrumMode::JONSWAP})
    {
      for (float fAmplitude : {0.07f, 0.14f, 1.4f})
      {
        for (float fWavelength : {4.5f, 12.0f, 48.0f})
        {
          for (plUInt32 uiCascades : {1u, 2u, 3u, 4u})
          {
            for (plUInt32 uiResolution : {128u, 256u})
            {
              const plWaterProfileResourceDescriptor profile = makeProfile(mode, fAmplitude, fWavelength);
              const double fVariance = plWaterSpectrum::ComputeUnscaledVariance(profile, uiResolution, uiCascades);
              const float fScale = plWaterSpectrum::ComputeAmplitudeScale(profile, uiResolution, uiCascades);
              if (!PL_TEST_BOOL_MSG(fVariance > 0.0 && fScale > 0.0f, "Mode %d, wavelength %.1f, %u cascades at %u: no wave energy on the lattice", static_cast<int>(mode), fWavelength, uiCascades, uiResolution))
                continue;
              const double fDeviation = plMath::Sqrt(fVariance) * fScale;
              PL_TEST_DOUBLE(fDeviation, fAmplitude * plWaterSpectrum::s_fSpectrumDeviationShare, fAmplitude * 0.001);
            }
          }
        }
      }
    }
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "A profile without waves gets no spectrum")
  {
    plWaterProfileResourceDescriptor profile = makeProfile(plWaterWaveSpectrumMode::JONSWAP, 0.0f, 12.0f);
    PL_TEST_FLOAT(plWaterSpectrum::ComputeAmplitudeScale(profile, 128, 3), 0.0f, 0.0f);
    PL_TEST_FLOAT(plWaterSpectrum::ComputeAmplitudeScale(makeProfile(plWaterWaveSpectrumMode::JONSWAP, 0.5f, 12.0f), 128, 0), 0.0f, 0.0f);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Cascades: the largest tile holds the dominant wave many times, and tiles never repeat in step")
  {
    for (plUInt32 uiCascades = 1; uiCascades <= 4; ++uiCascades)
    {
      // About 25 dominant wavelengths across, for any number of cascades.
      PL_TEST_FLOAT(plWaterSpectrum::GetCascadeLength(12.0f, uiCascades - 1, uiCascades), 12.0f * 25.3f, 0.01f);
      for (plUInt32 uiCascade = 0; uiCascade + 1 < uiCascades; ++uiCascade)
      {
        const float fSmall = plWaterSpectrum::GetCascadeLength(12.0f, uiCascade, uiCascades);
        const float fLarge = plWaterSpectrum::GetCascadeLength(12.0f, uiCascade + 1, uiCascades);
        PL_TEST_BOOL(fLarge > fSmall * 4.0f);
        // Not a whole multiple: the small tile's repeat does not line up with the large one's.
        const float fRatio = fLarge / fSmall;
        PL_TEST_BOOL(plMath::Abs(fRatio - plMath::Round(fRatio)) > 0.2f);
      }
    }
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "The cosmetic layer has a stated height bound")
  {
    // Gameplay queries do not see the spectrum. Four standard deviations is the room to leave above what they return.
    const plWaterProfileResourceDescriptor profile = makeProfile(plWaterWaveSpectrumMode::JONSWAP, 1.4f, 48.0f);
    PL_TEST_FLOAT(plWaterSpectrum::GetCosmeticHeightBound(profile), 4.0f * 1.4f * plWaterSpectrum::s_fSpectrumDeviationShare, 0.0001f);
  }
}
