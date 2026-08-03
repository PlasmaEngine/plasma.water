#pragma once

// Four deterministic low-frequency Gerstner components. The GPU spectrum path adds displacement before this
// fallback in a later integration step; keeping this function deterministic also gives gameplay a matching basis.
void EvaluateWaterGerstner(float2 position, float time, float amplitude, float wavelength, float speed, float choppiness, float2 windDirection, out float3 offset, out float3 normal)
{
  static const float2 directions[4] =
  {
    float2(0.9701, 0.2425),
    float2(-0.5145, 0.8575),
    float2(0.1961, -0.9806),
    float2(0.7809, 0.6247),
  };
  static const float wavelengthScale[4] = {1.0, 0.53, 0.27, 0.13};
  static const float amplitudeScale[4] = {0.52, 0.25, 0.15, 0.08};

  offset = 0.0;
  float2 gradient = 0.0;
  const float2 wind = dot(windDirection, windDirection) > 1.0e-10 ? normalize(windDirection) : float2(1.0, 0.0);
  const float2 windPerpendicular = float2(-wind.y, wind.x);

  [unroll]
  for (uint i = 0; i < 4; ++i)
  {
    const float componentWavelength = max(0.05, wavelength * wavelengthScale[i]);
    const float waveNumber = 6.28318530718 / componentWavelength;
    const float componentAmplitude = amplitude * amplitudeScale[i];
    const float2 direction = normalize(wind * directions[i].x + windPerpendicular * directions[i].y);
    const float phase = dot(position, direction) * waveNumber + time * speed * sqrt(9.81 * waveNumber) + float(i) * 1.731;
    const float s = sin(phase);
    const float c = cos(phase);

    offset.xy += direction * (componentAmplitude * choppiness * c);
    offset.z += componentAmplitude * s;
    gradient += direction * (componentAmplitude * waveNumber * c);
  }

  normal = normalize(float3(-gradient.x, -gradient.y, 1.0));
}

// Evaluates an unpolarized dielectric interface without the grazing-angle error of the usual Schlick
// approximation. The returned transmission cosine is zero when total internal reflection occurs.
float EvaluateWaterDielectricFresnel(float cosIncident, float incidentIor, float transmittedIor, out float cosTransmitted)
{
  cosIncident = saturate(cosIncident);
  incidentIor = max(incidentIor, 1.0e-4);
  transmittedIor = max(transmittedIor, 1.0e-4);
  const float eta = incidentIor / transmittedIor;
  const float sinTransmittedSquared = eta * eta * max(1.0 - cosIncident * cosIncident, 0.0);
  if (sinTransmittedSquared >= 1.0)
  {
    cosTransmitted = 0.0;
    return 1.0;
  }

  cosTransmitted = sqrt(max(1.0 - sinTransmittedSquared, 0.0));
  const float rsNumerator = incidentIor * cosIncident - transmittedIor * cosTransmitted;
  const float rsDenominator = incidentIor * cosIncident + transmittedIor * cosTransmitted;
  const float rpNumerator = transmittedIor * cosIncident - incidentIor * cosTransmitted;
  const float rpDenominator = transmittedIor * cosIncident + incidentIor * cosTransmitted;
  const float rs = rsNumerator / max(abs(rsDenominator), 1.0e-5);
  const float rp = rpNumerator / max(abs(rpDenominator), 1.0e-5);
  return saturate(0.5 * (rs * rs + rp * rp));
}

float3 EvaluateWaterRefractionDirection(float3 incidentDirection, float3 interfaceNormal, float incidentIor, float transmittedIor,
  float cosIncident, float cosTransmitted)
{
  const float eta = incidentIor / max(transmittedIor, 1.0e-4);
  if (cosTransmitted <= 0.0)
    return reflect(incidentDirection, interfaceNormal);
  return normalize(eta * incidentDirection + (eta * cosIncident - cosTransmitted) * interfaceNormal);
}

// Interaction slopes are stored as height change per texel in a resolution-independent 512-texel convention.
// Convert them back to the physical world-space height gradient before perturbing a normal or estimating foam.
float2 DecodeWaterInteractionGradient(float4 interactionSample)
{
  return interactionSample.zw * (512.0 / 100.0);
}

// The interaction atlas wraps world space with a 100m period, so every texel keeps a fixed world identity
// (the write side in WaterInteractionUpdate.plShader uses the same frac mapping). window.xy anchors the
// currently valid 100m region (the camera), window.z is its inner half extent (one texel short of 50m so
// bilinear reads cannot bleed across the wrap seam). Outside the window there is no valid data - texels
// there may belong to a different 100m tile, so the sample must read as 'no interaction'. Texels re-entering
// the window after a long camera round trip can briefly carry decayed stale data; the planned paged atlas
// allocator replaces this scheme.
float4 SampleWaterInteraction(Texture2DArray atlas, float2 worldXY, float4 window)
{
  const float2 rel = worldXY - window.xy;
  if (any(abs(rel) > window.z))
    return 0.0;

  const float2 uv = frac(worldXY * 0.01 + 0.5);
  // Channels: x = ripple height (m), y = unused, zw = slope in the 'height per 512-atlas-texel' convention.
  // DecodeWaterInteractionGradient converts zw back to a physical world-space height gradient.
  return atlas.SampleLevel(LinearSampler, float3(uv, 0), 0);
}
