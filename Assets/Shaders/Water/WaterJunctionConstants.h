#pragma once

#include <Shaders/Common/ConstantBufferMacros.h>
#include <Shaders/Common/Platforms.h>

// Slot 7: must not collide with any constant buffer of the include set of WaterSurfaceModern.plShader -
// notably plClusteredDataConstants on slot 3 (via Lighting.h). A collision makes the pixel shader read
// per-frame cluster lighting data instead of these constants (flickering garbage water).
CONSTANT_BUFFER2(plWaterJunctionConstants, 7, BG_DRAW_CALL)
{
  FLOAT4(JunctionCutout0);
  FLOAT4(JunctionCutout1);
  FLOAT4(JunctionCutout2);      // second cutout slot: center.xy, direction.xy
  FLOAT4(JunctionCutout3);      // second cutout slot: halfLength, startHalfWidth, endHalfWidth, enabled
  FLOAT4(JunctionFlow0);        // first lake current: shoreline origin.xy, inward direction.xy
  FLOAT4(JunctionFlow1);        // first lake current: length, half width, speed, direction sign
  FLOAT4(JunctionFlow2);        // second lake current: shoreline origin.xy, inward direction.xy
  FLOAT4(JunctionFlow3);        // second lake current: length, half width, speed, direction sign
  FLOAT4(MainLightDirection);   // xyz: direction from the surface towards the light, w: unused
  FLOAT4(MainLightColorIntensity); // rgb: light color * intensity (physical units), w: unused
  FLOAT4(InteractionWindowParams); // xy: world-space center of the valid interaction window (camera anchor), z: inner half extent in meters, w: atlas contains live data
  FLOAT4(JunctionSpillParams);     // z: mouth foam strength, xyw: unused
  FLOAT4(JunctionMaterialParams);  // z: lake material-mix strength, w: matching flow slot, xy: unused
  FLOAT4(WaterQualityParams);      // x: maximum inline SSR steps, yzw: reserved
  FLOAT4(TessellationParams);      // x: max factor, y: target edge pixels, z: interaction factor, w: interaction bounds valid
  FLOAT4(ActiveInteractionBounds); // world-space min.xy / max.xy of live interaction annuli
};
