#pragma once

#include <Shaders/Common/ConstantBufferMacros.h>

// Shared by the half-resolution caustic trace and the edge-aware additive resolve.
CONSTANT_BUFFER(plWaterCausticsConstants, 5)
{
  // xyz: normalized direction from the receiver towards the dominant directional light, w: light is valid.
  FLOAT4(WaterCausticLightDirectionAndValidity);
  // rgb: physical light color * intensity, w: global intensity normalization.
  FLOAT4(WaterCausticLightColorAndScale);
  // xy: inverse full-resolution size, zw: inverse caustic-resolution size.
  FLOAT4(WaterCausticTextureParams);
  // x: maximum world-space search distance in meters, y: base hit tolerance, z: any source is valid, w: contact shadow layers per eye (the sun is slot 0).
  FLOAT4(WaterCausticTraceParams);
  // xy: world-space field center, z: half extent, w: reciprocal half extent.
  FLOAT4(WaterCausticFieldCenterAndExtent);
  // min.xy, max.zw. The analytic fallback is only valid inside this conservative footprint.
  FLOAT4(WaterCausticFallbackBounds);
  // xy: anchor used to keep the fallback wave height continuous, z: sampled surface height, w: validity.
  FLOAT4(WaterCausticFallbackCenterHeightValidity);
  // x: amplitude, y: wavelength, z: speed, w: choppiness.
  FLOAT4(WaterCausticFallbackWaveParams);
  // xy: transmitted/reflected strengths, zw: reserved.
  FLOAT4(WaterCausticFallbackStrengths);
  // xy: wind direction, z: simulation time, w: world-space field texel size.
  FLOAT4(WaterCausticFallbackWindTimeTexel);
  // x: GBuffer1 available, y: sun contact shadow available, z/w: transmitted/reflected enabled.
  FLOAT4(WaterCausticFeatureParams);
  // x/y: caustic output size, z: view-array size, w: ray-march step count.
  UINT4(WaterCausticDispatchParams);
};
