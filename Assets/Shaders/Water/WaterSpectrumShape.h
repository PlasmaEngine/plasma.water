#pragma once

// The shape of the wave spectrum and the layout of its cascades. Scalars only and nothing but sqrt, exp, pow, log, max,
// min and abs, so that the same text is HLSL in the spectrum shaders and C++ in WaterSpectrum.cpp, which adds up the
// same sum to normalise the waves to a profile's amplitude. Change one and the other follows.

// A cascade is a square tile of water that repeats. Each cascade carries one band of wavelengths, so that together
// they hold waves from the longest swell down to ripples with a fixed number of texels. Cascade 0 is the smallest
// tile. The largest tile is always about 25 dominant wavelengths across; neighbours differ by a factor that is not a
// whole number, so their repeats never line up.
float WaterCascadeLength(float baseWavelength, float cascade, float cascadeCount)
{
  return max(baseWavelength, 0.5) * 25.3 / pow(4.3, max(cascadeCount - 1.0 - cascade, 0.0));
}

// The shortest waves of a cascade: four texels per wavelength.
float WaterCascadeBandHigh(float baseWavelength, float cascade, float cascadeCount, float resolution)
{
  return 3.14159265 * resolution * 0.5 / WaterCascadeLength(baseWavelength, cascade, cascadeCount);
}

// The longest: where the next larger cascade takes over. The largest cascade has no lower limit but its own tile.
float WaterCascadeBandLow(float baseWavelength, float cascade, float cascadeCount, float resolution)
{
  return cascade + 1.0 < cascadeCount ? WaterCascadeBandHigh(baseWavelength, cascade + 1.0, cascadeCount, resolution) : 0.0;
}

// Wave energy per unit of wave-vector area, up to a constant factor: the profile's amplitude sets the total.
// mode: 0 analytic (a plain hump around the dominant wavelength), 1 Phillips, 2 JONSWAP.
float WaterSpectrumDensity(float kx, float ky, float windX, float windY, float peakWavelength, float mode)
{
  const float g = 9.81;
  const float k = sqrt(kx * kx + ky * ky);
  if (k < 1.0e-5)
    return 0.0;

  const float kp = 6.2831853 / max(peakWavelength, 0.05);
  const float cosine = (kx * windX + ky * windY) / k;
  // Most of the energy runs with the wind, some across it, nearly none against it.
  const float along = max(0.5 + 0.5 * cosine, 0.0);

  if (mode > 1.5)
  {
    const float omega = sqrt(g * k);
    const float omegaPeak = sqrt(g * kp);
    const float sigma = omega <= omegaPeak ? 0.07 : 0.09;
    const float distance = (omega - omegaPeak) / (sigma * omegaPeak);
    const float peak = pow(3.3, exp(-0.5 * distance * distance));
    const float ratio = omegaPeak / omega;
    const float frequencySpectrum = g * g / pow(omega, 5.0) * exp(-1.25 * ratio * ratio * ratio * ratio) * peak;
    // From per unit of frequency to per unit of wave-vector area: d(omega)/dk = g / (2 omega), and 1/k for the ring.
    return frequencySpectrum * g / (2.0 * omega) / k * pow(along, 6.0);
  }

  if (mode > 0.5)
  {
    // exp(-1/(kL)^2) / k^4 has its peak at kL = 0.707.
    const float L = 0.7071 / kp;
    const float kL = k * L;
    const float small = L * 0.01;
    return exp(-1.0 / (kL * kL)) / (k * k * k * k) * exp(-k * k * small * small) * pow(along, 4.0);
  }

  const float octaves = log(k / kp) / 0.55;
  return exp(-0.5 * octaves * octaves) / (k * k) * pow(along, 4.0);
}
