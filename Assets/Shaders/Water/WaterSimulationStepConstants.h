#pragma once

#include <Shaders/Common/ConstantBufferMacros.h>
#include <Shaders/Common/Platforms.h>

// Per-dispatch data for the stateless interaction-wave atlas. This MUST be push constants because render-graph
// passes are recorded before execution and shared mutable constant-buffer writes would become last-write-wins.
BEGIN_PUSH_CONSTANTS(plWaterSimulationStepConstants)
{
  UINT4(WaterSimStep);    // x: event count, yz: atlas dimensions, w: spatial tile count
  FLOAT4(WaterSimTiming); // x: total time (s), y: propagation speed (m/s), z: splash lifetime (s), w: events-per-tile cap
}
END_PUSH_CONSTANTS(plWaterSimulationStepConstants)
