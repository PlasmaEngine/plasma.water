#pragma once

#include <Shaders/Common/ConstantBufferMacros.h>

#define WATER_SIMULATION_THREAD_GROUP_SIZE 8
#define WATER_MAX_SPECTRUM_PROFILES 4
#define WATER_INTERACTION_TILE_COUNT 32
#define WATER_MAX_INTERACTIONS_PER_TILE 16
#define WATER_INTERACTION_TILE_EVENT_RECORDS ((WATER_MAX_INTERACTIONS_PER_TILE + 3) / 4)
#define WATER_INTERACTION_TILE_RECORDS (1 + WATER_INTERACTION_TILE_EVENT_RECORDS)

struct PL_SHADER_STRUCT plWaterGpuSpectrumProfile
{
  FLOAT4(WindDirectionSpeedAmplitude);
  FLOAT4(WaveParams);
  UINT4(ProfileParams);
};

struct PL_SHADER_STRUCT plWaterGpuInteraction
{
  FLOAT4(PositionRadius);   // xyz world position, w contact radius (m)
  FLOAT4(DirectionStrength); // xyz direction, w strength with gain applied
  UINT4(InteractionParams);  // x unused, y plWaterInteractionType, z age in seconds as float bits, w unused
};

CONSTANT_BUFFER(plWaterSimulationConstants, 5)
{
  UINT4(WaterSimulationSize);
  FLOAT4(WaterSimulationTime);
};
