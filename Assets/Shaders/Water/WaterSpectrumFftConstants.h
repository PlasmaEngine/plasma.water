#pragma once

#include <Shaders/Common/ConstantBufferMacros.h>
#include <Shaders/Common/Platforms.h>

// The two passes of the transform (rows, then columns) differ in nothing but this. Push constants, because the
// passes of a render graph are recorded before any of them runs: a shared constant buffer would hold the last write.
BEGIN_PUSH_CONSTANTS(plWaterSpectrumFftConstants)
{
  UINT4(FftParams); // x: size (128 or 256), y: log2 of the size, z: 0 for rows and 1 for columns, w: layers
}
END_PUSH_CONSTANTS(plWaterSpectrumFftConstants)
