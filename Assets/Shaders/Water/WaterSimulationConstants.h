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
  FLOAT4(WindDirectionSpeedAmplitude); // xy: wind direction, z: wind speed, w: wave amplitude (m)
  FLOAT4(WaveParams);                  // x: dominant wavelength (m), y: steepness (how far crests are pushed together), z: foam crest threshold, w: amplitude scale that normalises the spectrum
  UINT4(ProfileParams);                // x: seed, y: spectrum mode, z: cascades, w: 1 when the slot is in use
};

struct PL_SHADER_STRUCT plWaterGpuInteraction
{
  FLOAT4(PositionRadius);   // xyz world position, w contact radius (m)
  FLOAT4(DirectionStrength); // xyz direction, w strength with gain applied
  UINT4(InteractionParams);  // x unused, y plWaterInteractionType, z age in seconds as float bits, w unused
};

CONSTANT_BUFFER(plWaterSimulationConstants, 5)
{
  UINT4(WaterSimulationSize);  // xy: texels of a cascade, z: layers (profiles x cascades), w: profile slots
  FLOAT4(WaterSimulationTime); // x: time (s), y: time since the frame before (s), z: 1 when the foam history has to start from nothing, w: unused
};
