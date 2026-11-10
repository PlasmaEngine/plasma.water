#pragma once

#include <Shaders/Common/ConstantBufferMacros.h>

// The profile table: one entry per water body, uploaded once per frame and view by plWaterSurfacePass. A draw names
// the entries it uses by index, a finite mesh names them per vertex, so one mesh can carry any number of profiles.
// The index of an entry is the index of its body in plWaterWorldModule.
#define WATER_MAX_PROFILES 64

// A vertex or a draw that leaves a profile index at this value takes the draw's own profile.
#define WATER_PROFILE_FROM_DRAW 255

struct PL_SHADER_STRUCT plWaterGpuProfile
{
  FLOAT4(WaveParams);             // x: amplitude (m), y: base wavelength (m), z: wind speed, w: steepness
  FLOAT4(WindAndFlow);            // xy: wind direction, z: flow speed, w: flow scale
  FLOAT4(SpectrumParams);         // x: spectrum slot, y: cascades in use, z: ripple ceiling (m), w: 1 where waves and detail follow the mesh's flow direction (rivers)
  FLOAT4(AbsorptionAndRoughness); // xyz: absorption per metre, w: roughness
  FLOAT4(ScatteringAndIor);       // xyz: scattering per metre, w: index of refraction
  FLOAT4(ShallowColor);
  FLOAT4(DeepColor);
  FLOAT4(OpticalParams);          // x: reflection strength, y: environment reflection weight, z: refraction depth (m), w: shore fade depth (m)
  FLOAT4(FoamParams);             // x: strength, y: crest threshold, z: shore depth (m), w: refraction strength
  FLOAT4(SurfaceDetailParams);    // x: micro normal scale, y: micro normal strength, z: sun glitter strength, w: refraction edge rejection
  FLOAT4(FoamMaterialParams);     // x: roughness, y: breakup scale, z: 1 with a foam texture, w: 1 with a normal texture
  FLOAT4(FoamColor);
  FLOAT4(CausticParams);          // x: transmitted strength, y: reflected strength, zw: unused
};
