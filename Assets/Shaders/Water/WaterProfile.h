#pragma once

#include <Shaders/Water/WaterDrawConstants.h>
#include <Shaders/Water/WaterProfileData.h>

StructuredBuffer<plWaterGpuProfile> WaterProfiles BIND_GROUP(BG_RENDER_PASS);

plWaterGpuProfile LoadWaterProfile(uint index)
{
  const uint count = max(GET_PUSH_CONSTANT(plWaterDrawConstants, ProfileParams).z, 1u);
  return WaterProfiles[min(index, count - 1u)];
}

// The two profiles of the draw itself. Procedural ocean patches have no vertices to carry a pair.
void GetWaterDrawProfilePair(out uint profileA, out uint profileB)
{
  const uint4 params = GET_PUSH_CONSTANT(plWaterDrawConstants, ProfileParams);
  profileA = params.x;
  profileB = params.y;
}

// A finite mesh carries its profile pair per vertex: Color0 is (weight of B, index of A, index of B) with the indices
// stored as n / 255. All three vertices of a triangle hold the same pair, so the interpolated indices only need rounding.
void DecodeWaterProfilePair(float4 vertexColor, out uint profileA, out uint profileB, out float weightB)
{
  uint drawA;
  uint drawB;
  GetWaterDrawProfilePair(drawA, drawB);
  const uint encodedA = (uint)(saturate(vertexColor.g) * 255.0 + 0.5);
  const uint encodedB = (uint)(saturate(vertexColor.b) * 255.0 + 0.5);
  profileA = encodedA >= WATER_PROFILE_FROM_DRAW ? drawA : encodedA;
  profileB = encodedB >= WATER_PROFILE_FROM_DRAW ? drawB : encodedB;
  weightB = saturate(vertexColor.r);
}
