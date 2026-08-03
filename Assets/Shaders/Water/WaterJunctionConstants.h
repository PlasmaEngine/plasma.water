#pragma once

#include <Shaders/Common/ConstantBufferMacros.h>
#include <Shaders/Common/Platforms.h>

// Slot 7: must not collide with any constant buffer of the include set of WaterSurfaceModern.plShader -
// notably plClusteredDataConstants on slot 3 (via Lighting.h). A collision makes the pixel shader read
// per-frame cluster lighting data instead of these constants (flickering garbage water).
CONSTANT_BUFFER2(plWaterJunctionConstants, 7, BG_DRAW_CALL)
{
  FLOAT4(SecondaryWaveParams);
  FLOAT4(SecondaryAbsorptionAndRoughness);
  FLOAT4(SecondaryScatteringAndIor);
  FLOAT4(SecondaryShallowColor);
  FLOAT4(SecondaryDeepColor);
  FLOAT4(SecondaryOpticalParams);
  FLOAT4(SecondaryFoamParams); // xyz: strength, crest threshold, shore depth; w: reserved
  FLOAT4(SecondaryWindAndRefraction);
  FLOAT4(JunctionCutout0);
  FLOAT4(JunctionCutout1);
  FLOAT4(PrimarySurfaceDetailParams);
  FLOAT4(PrimaryFoamMaterialParams);   // x: roughness, y: breakup scale, z: has foam texture, w: has normal texture
  FLOAT4(PrimaryFoamColor);
  FLOAT4(SecondarySurfaceDetailParams);
  FLOAT4(SecondaryFoamMaterialParams); // x: roughness, y: breakup scale, z: has foam texture, w: has normal texture
  FLOAT4(SecondaryFoamColor);
  FLOAT4(PrimaryAbsorptionAndRoughness);
  FLOAT4(PrimaryScatteringAndIor);
  FLOAT4(PrimaryShallowColor);
  FLOAT4(PrimaryDeepColor);
  FLOAT4(PrimaryOpticalParams);
  FLOAT4(PrimaryFoamParams); // xyz: strength, crest threshold, shore depth; w: reserved
  FLOAT4(PrimaryWindDirection); // xy: wind direction, zw: reserved
  FLOAT4(JunctionCutout2);      // second cutout slot: center.xy, direction.xy
  FLOAT4(JunctionCutout3);      // second cutout slot: halfLength, startHalfWidth, endHalfWidth, enabled
  FLOAT4(JunctionFlow0);        // first lake current: shoreline origin.xy, inward direction.xy
  FLOAT4(JunctionFlow1);        // first lake current: length, half width, speed, direction sign
  FLOAT4(JunctionFlow2);        // second lake current: shoreline origin.xy, inward direction.xy
  FLOAT4(JunctionFlow3);        // second lake current: length, half width, speed, direction sign
  FLOAT4(MainLightDirection);   // xyz: direction from the surface towards the light, w: unused
  FLOAT4(MainLightColorIntensity); // rgb: light color * intensity (physical units), w: unused
  FLOAT4(InteractionWindowParams); // xy: world-space center of the valid interaction window (camera anchor), z: inner half extent in meters, w: atlas contains live data
  FLOAT4(JunctionSpillParams);     // x: secondary spectrum profile slot, y: secondary cascade count, z: mouth foam strength, w: secondary ripple ceiling
  FLOAT4(JunctionMaterialParams);  // xy: secondary profile flow; z: lake material-mix strength; w: matching flow slot
  FLOAT4(CausticParams);           // xy: primary transmitted/reflected strength, zw: secondary transmitted/reflected strength
  FLOAT4(WaterQualityParams);      // x: maximum inline SSR steps, yzw: reserved
  FLOAT4(TessellationParams);      // x: max factor, y: target edge pixels, z: interaction factor, w: interaction bounds valid
  FLOAT4(ActiveInteractionBounds); // world-space min.xy / max.xy of live interaction annuli
};
