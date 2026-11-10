#pragma once

#include <Shaders/Common/ConstantBufferMacros.h>
#include <Shaders/Common/Platforms.h>

// Per-draw data only. Must stay <= 128 bytes: the Vulkan spec only guarantees 128 bytes of push constants and the
// emulated push-constant fallback asserts on anything larger. Everything that belongs to a water profile (waves,
// colours, foam, ...) is in the profile table (WaterProfileData.h) and reached through ProfileParams.
BEGIN_PUSH_CONSTANTS(plWaterDrawConstants)
{
  FLOAT4(PatchOriginAndBaseHeight);
  FLOAT4(PatchAxisUAndExtent);
  FLOAT4(PatchAxisVAndExtent);
  FLOAT4(SurfaceParams); // x: simulation time (s), y: distance between the draw's vertices (m), z: texels along a spectrum cascade, w: unused
  UINT4(PatchParams); // x: cells per side, y: spectrum layer count, z: debug mode, w: geometry type
  UINT4(VolumeFilterParams); // x: primary body ID, y: junction secondary body ID, z: active body ID, w: filter enabled
  UINT4(ProfileParams); // x: profile of the draw, y: second profile of the draw (the same when there is none), z: entries in the profile table, w: ocean patch seams (bits 0-3 inner mask, 4-7 outer mask, 8-15 ring)
}
END_PUSH_CONSTANTS(plWaterDrawConstants)
