#pragma once

#include <Shaders/Common/ConstantBufferMacros.h>
#include <Shaders/Common/Platforms.h>

// What the draws of one water pass share besides the profile table, plus the two cutouts of an ocean patch.
// Slot 7: must not collide with any constant buffer of the include set of WaterSurfaceModern.plShader -
// notably plClusteredDataConstants on slot 3 (via Lighting.h). A collision makes the pixel shader read
// per-frame cluster lighting data instead of these constants (flickering garbage water).
CONSTANT_BUFFER2(plWaterJunctionConstants, 7, BG_DRAW_CALL)
{
  FLOAT4(JunctionCutout0);      // first cutout: center.xy, direction.xy
  FLOAT4(JunctionCutout1);      // first cutout: halfLength, startHalfWidth, endHalfWidth, enabled
  FLOAT4(JunctionCutout2);      // second cutout: center.xy, direction.xy
  FLOAT4(JunctionCutout3);      // second cutout: halfLength, startHalfWidth, endHalfWidth, enabled
  FLOAT4(MainLightDirection);   // xyz: direction from the surface towards the light, w: unused
  FLOAT4(MainLightColorIntensity); // rgb: light color * intensity (physical units), w: unused
  FLOAT4(InteractionWindowParams); // xy: world-space center of the valid interaction window (camera anchor), z: inner half extent in meters, w: atlas contains live data
  FLOAT4(WaterQualityParams);      // x: maximum inline SSR steps, yzw: reserved
  FLOAT4(TessellationParams);      // x: max factor, y: target edge pixels, z: interaction factor, w: interaction bounds valid
  FLOAT4(ActiveInteractionBounds); // world-space min.xy / max.xy of live interaction annuli
};
