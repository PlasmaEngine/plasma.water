#pragma once

#include <Shaders/Common/ConstantBufferMacros.h>
#include <Shaders/Common/Platforms.h>

// Per-patch hot data only. Must stay <= 128 bytes: the Vulkan spec only guarantees 128 bytes of push
// constants and the emulated push-constant fallback asserts on anything larger. Per-body cold data
// (colors, absorption, foam, ...) lives in plWaterJunctionConstants, which is re-uploaded per draw.
BEGIN_PUSH_CONSTANTS(plWaterDrawConstants)
{
  FLOAT4(PatchOriginAndBaseHeight);
  FLOAT4(PatchAxisUAndExtent);
  FLOAT4(PatchAxisVAndExtent);
  FLOAT4(WaveParams);
  FLOAT4(FlowAndJunctionBlend);
  FLOAT4(SurfaceParams);
  UINT4(PatchParams); // x: cells per side, y: spectrum layer count, z: debug mode, w: geometry type
  UINT4(VolumeFilterParams); // x: primary body ID, y: junction secondary body ID, z: active body ID, w: filter enabled
}
END_PUSH_CONSTANTS(plWaterDrawConstants)
