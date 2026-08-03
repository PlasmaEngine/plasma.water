#pragma once

// Returns xy = signed current velocity and z = smooth field influence. The basis direction always points
// from the shoreline into the lake; params.w selects inflow (+1) or outflow (-1).
float3 EvaluateSingleWaterJunctionFlow(float2 worldPosition, float4 basis, float4 params)
{
  const float fieldLength = params.x;
  const float halfWidth = params.y;
  const float speed = params.z;
  if (fieldLength <= 0.0 || halfWidth <= 0.0 || speed <= 0.0)
    return 0.0;

  const float2 inward = dot(basis.zw, basis.zw) > 1.0e-10 ? normalize(basis.zw) : float2(1.0, 0.0);
  const float2 lateralAxis = float2(-inward.y, inward.x);
  const float2 delta = worldPosition - basis.xy;
  const float longitudinal = dot(delta, inward);
  const float lateral = abs(dot(delta, lateralAxis));

  // Pin the authored shoreline, rise just inside the basin, then dissolve the plume before its end. A mild
  // lateral flare looks like mass spreading into a lake while retaining a narrow, readable stream core.
  const float normalizedDistance = saturate(longitudinal / max(fieldLength, 0.001));
  const float leadingFade = smoothstep(0.0, 0.12, normalizedDistance);
  const float trailingFade = 1.0 - smoothstep(0.58, 1.0, normalizedDistance);
  const float localHalfWidth = halfWidth * lerp(0.72, 1.45, normalizedDistance);
  const float lateralFade = 1.0 - smoothstep(localHalfWidth * 0.55, localHalfWidth, lateral);
  const float influence = leadingFade * trailingFade * lateralFade;
  const float directionSign = params.w < 0.0 ? -1.0 : 1.0;
  return float3(inward * (speed * directionSign * influence), influence);
}

float3 EvaluateWaterJunctionFlow(float2 worldPosition)
{
  const float3 flow0 = EvaluateSingleWaterJunctionFlow(worldPosition, JunctionFlow0, JunctionFlow1);
  const float3 flow1 = EvaluateSingleWaterJunctionFlow(worldPosition, JunctionFlow2, JunctionFlow3);
  return float3(flow0.xy + flow1.xy, max(flow0.z, flow1.z));
}

// Material mixing deliberately starts at full strength on the shoreline, unlike geometric current ripples,
// which are pinned there. This lets the bridge and lake evaluate complementary river/lake color weights at
// exactly the same world position while the tint plume dissolves smoothly farther into the basin.
float EvaluateSingleWaterJunctionMaterialInfluence(float2 worldPosition, float4 basis, float4 params)
{
  const float fieldLength = params.x;
  const float halfWidth = params.y;
  if (fieldLength <= 0.0 || halfWidth <= 0.0 || params.z <= 0.0)
    return 0.0;

  const float2 inward = dot(basis.zw, basis.zw) > 1.0e-10 ? normalize(basis.zw) : float2(1.0, 0.0);
  const float2 lateralAxis = float2(-inward.y, inward.x);
  const float2 delta = worldPosition - basis.xy;
  const float normalizedDistance = saturate(dot(delta, inward) / max(fieldLength, 0.001));
  const float trailingFade = 1.0 - smoothstep(0.48, 1.0, normalizedDistance);
  const float localHalfWidth = halfWidth * lerp(0.72, 1.45, normalizedDistance);
  const float lateral = abs(dot(delta, lateralAxis));
  const float lateralFade = 1.0 - smoothstep(localHalfWidth * 0.5, localHalfWidth, lateral);
  return trailingFade * lateralFade;
}

float EvaluateWaterJunctionMaterialInfluence(float2 worldPosition, float flowSlot)
{
  return flowSlot < 0.5
           ? EvaluateSingleWaterJunctionMaterialInfluence(worldPosition, JunctionFlow0, JunctionFlow1)
           : EvaluateSingleWaterJunctionMaterialInfluence(worldPosition, JunctionFlow2, JunctionFlow3);
}

// Small geometric current ridges make direction and motion legible without painting a flow texture onto the
// lake. Influence is exactly zero at every field boundary, keeping the authored shoreline and distant lake
// topology welded to their undeformed positions.
void EvaluateWaterJunctionCurrentRipples(float2 worldPosition, float simulationTime,
  out float height, out float2 gradient)
{
  height = 0.0;
  gradient = 0.0;

  const float3 field = EvaluateWaterJunctionFlow(worldPosition);
  const float speed = length(field.xy);
  if (field.z <= 1.0e-4 || speed <= 1.0e-4)
    return;

  const float2 direction = field.xy / speed;
  const float2 perpendicular = float2(-direction.y, direction.x);
  const float wavelength = clamp(1.4 + speed * 1.15, 1.4, 5.0);
  const float waveNumber = 6.28318530718 / wavelength;
  const float phase = dot(worldPosition, direction) * waveNumber - simulationTime * max(speed, 0.35) * waveNumber;
  const float secondaryPhase = phase * 1.73 + dot(worldPosition, perpendicular) * waveNumber * 0.31 + 1.2;
  const float amplitude = min(0.022 + speed * 0.014, 0.09) * field.z;

  height = amplitude * (sin(phase) + 0.28 * sin(secondaryPhase));
  gradient = amplitude * (direction * waveNumber * cos(phase) +
    (direction * (waveNumber * 1.73) + perpendicular * (waveNumber * 0.31)) * (0.28 * cos(secondaryPhase)));
}
