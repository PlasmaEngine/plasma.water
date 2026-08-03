#pragma once

#include <Shaders/Common/ConstantBufferMacros.h>

// Kept separate from the compute constants because WaterSurfaceModern also includes the renderer's frame
// constant buffers. This render-pass slot is updated immediately before the world-space field draw.
CONSTANT_BUFFER2(plWaterCausticFieldConstants, 6, BG_RENDER_PASS)
{
  // xy: snapped world-space center, z: half extent in meters, w: reciprocal half extent.
  FLOAT4(WaterCausticFieldCenterAndExtent);
};
