#pragma once

#include <Shaders/Water/WaterProfile.h>

Texture2DArray WaterSpectrumDisplacement BIND_GROUP(BG_RENDER_PASS);
Texture2DArray WaterInteractionAtlas BIND_GROUP(BG_RENDER_PASS);

// What one profile's waves do to a point of the undisplaced surface.
struct WaterWaveField
{
  float3 Offset;       // displacement of the point
  float2 Gradient;     // height gradient: the normal is normalize(-Gradient, 1)
  float Foam;          // spectrum fold foam, 0..1
  float RippleCeiling; // largest interaction ripple height this profile allows (m)
};

WaterWaveField EvaluateWaterProfileWaves(plWaterGpuProfile profile, float3 baseWorldPosition, float waveAttenuation,
  float2 vertexFlowDirection, float simulationTime, uint spectrumLayers)
{
  const float4 waveParams = profile.WaveParams;
  // A river's waves and detail run along the mesh's flow direction and travel with the current. Lakes and oceans
  // run with their wind and do not advect.
  const bool usesVertexFlow = profile.SpectrumParams.w > 0.5;
  const float2 waveDirection = usesVertexFlow ? vertexFlowDirection : profile.WindAndFlow.xy;
  const float2 flowVelocity = usesVertexFlow ? vertexFlowDirection * (profile.WindAndFlow.z * profile.WindAndFlow.w) : float2(0.0, 0.0);

  float3 waveOffset;
  float3 waveNormal;
  EvaluateWaterGerstner(baseWorldPosition.xy + flowVelocity * simulationTime, simulationTime, waveParams.x * waveAttenuation,
    waveParams.y, waveParams.z, waveParams.w, waveDirection, waveOffset, waveNormal);

  WaterWaveField field;
  field.Offset = waveOffset;
  field.Gradient = -waveNormal.xy / max(waveNormal.z, 0.1);
  field.Foam = 0.0;
  field.RippleCeiling = profile.SpectrumParams.z > 0.0 ? profile.SpectrumParams.z : max(waveParams.x * 2.0, 0.5);

  const uint cascadeCount = min((uint)profile.SpectrumParams.y, 4u);
  if (waveAttenuation > 0.0 && spectrumLayers > 0u && cascadeCount > 0u)
  {
    const uint availableCascadeCount = max(spectrumLayers / 4u, 1u);
    const uint firstSpectrumLayer = min((uint)profile.SpectrumParams.x, 3u) * availableCascadeCount;
    float4 spectral = 0.0;
    float spectralWeight = 0.0;
    [unroll]
    for (uint cascade = 0u; cascade < 4u; ++cascade)
    {
      if (cascade >= cascadeCount || cascade >= availableCascadeCount || firstSpectrumLayer + cascade >= spectrumLayers)
        break;
      const float cascadeDomainScale = exp2(float(cascade) * 2.0);
      const float2 spectrumUV = baseWorldPosition.xy / max(waveParams.y * 4.0 * cascadeDomainScale, 1.0);
      const float4 cascadeSample = WaterSpectrumDisplacement.SampleLevel(
        LinearSampler, float3(spectrumUV, firstSpectrumLayer + cascade), 0);
      const float cascadeWeight = 1.0 / exp2(float(cascade));
      spectral += cascadeSample * cascadeWeight;
      spectralWeight += cascadeWeight;
    }
    spectral /= max(spectralWeight, 1.0);
    // Spectrum XY stores slope, never horizontal position. Applying it as position folds the surface.
    field.Offset.z += spectral.z * waveAttenuation;
    field.Gradient += spectral.xy * waveAttenuation;
    field.Foam = saturate(spectral.w);
  }

  return field;
}

// Evaluates every source of geometric water displacement at an arbitrary base-space point. The fallback
// vertex path calls this once per authored vertex; the tessellated path calls it for every domain point.
// profilePair is the vertex colour of a finite mesh (see DecodeWaterProfilePair); a procedural patch passes
// float4(0, 1, 1, 1), which selects the draw's own profile.
VS_OUT EvaluateWaterSurfaceVertex(float3 baseWorldPosition, float2 uv, float waveAttenuation,
  float2 vertexFlowDirection, float4 profilePair)
{
  const uint spectrumLayers = GET_PUSH_CONSTANT(plWaterDrawConstants, PatchParams).y;
  const float simulationTime = GET_PUSH_CONSTANT(plWaterDrawConstants, SurfaceParams).x;

  uint profileIndexA;
  uint profileIndexB;
  float profileBlend;
  DecodeWaterProfilePair(profilePair, profileIndexA, profileIndexB, profileBlend);

  WaterWaveField field = EvaluateWaterProfileWaves(LoadWaterProfile(profileIndexA), baseWorldPosition, waveAttenuation,
    vertexFlowDirection, simulationTime, spectrumLayers);
  [branch]
  if (profileBlend > 0.0 && profileIndexB != profileIndexA)
  {
    const WaterWaveField other = EvaluateWaterProfileWaves(LoadWaterProfile(profileIndexB), baseWorldPosition, waveAttenuation,
      vertexFlowDirection, simulationTime, spectrumLayers);
    // Two wave fields that have nothing to do with each other: a plain cross-fade would leave the water flattest
    // where they are mixed half and half. Weights whose squares add up to one keep the wave energy level across the mix.
    const float weightA = sqrt(1.0 - profileBlend);
    const float weightB = sqrt(profileBlend);
    field.Offset = field.Offset * weightA + other.Offset * weightB;
    field.Gradient = field.Gradient * weightA + other.Gradient * weightB;
    field.Foam = lerp(field.Foam, other.Foam, profileBlend);
    field.RippleCeiling = lerp(field.RippleCeiling, other.RippleCeiling, profileBlend);
  }

  float3 waveOffset = field.Offset;
  float3 waveNormal = normalize(float3(-field.Gradient, 1.0));

  if (waveAttenuation > 0.0)
  {
    float4 interaction = 0.0;
    if (InteractionWindowParams.w > 0.5)
      interaction = SampleWaterInteraction(WaterInteractionAtlas, baseWorldPosition.xy, InteractionWindowParams);
    if (!all(abs(interaction) < 1024.0))
      interaction = 0.0;

    interaction.x = clamp(interaction.x, -field.RippleCeiling, field.RippleCeiling);
    const float2 interactionGradient = DecodeWaterInteractionGradient(interaction);

    // Interactions remain a height-field deformation. Shifting vertices laterally by opposing ring gradients
    // can invert adjacent triangles; the resulting vertical fans were previously mistaken for tessellation.
    waveOffset.z += interaction.x * waveAttenuation;
    waveNormal = normalize(waveNormal + float3(-interactionGradient, 0.0) * waveAttenuation);
  }

  VS_OUT output = (VS_OUT)0;
  output.WorldPosition = baseWorldPosition + waveOffset;
  output.Normal = waveNormal;
  output.TexCoord0 = uv;
  output.BaseWorldXY = baseWorldPosition.xy;
  output.SpectrumFoam = saturate(field.Foam) * waveAttenuation;
#if WATER_GEOMETRY_SOURCE == WATER_GEOMETRY_SOURCE_FINITE
  output.Tangent = float3(vertexFlowDirection, 0.0);
  output.BiTangent = float3(-vertexFlowDirection.y, vertexFlowDirection.x, 0.0);
  output.TexCoord1 = float2(waveAttenuation, 0.0);
  output.Color0 = profilePair;
#endif
#if WATER_RENDER_PASS == WATER_RENDER_PASS_CAUSTIC_FIELD
  const float2 fieldPosition = (output.WorldPosition.xy - WaterCausticFieldCenterAndExtent.xy) *
                               WaterCausticFieldCenterAndExtent.w;
  output.Position = float4(fieldPosition.x, -fieldPosition.y, 0.5, 1.0);
#else
  output.Position = mul(GetWorldToScreenMatrix(), float4(output.WorldPosition, 1.0));
#endif
#if CAMERA_MODE == CAMERA_MODE_STEREO
  output.RenderTargetArrayIndex = s_ActiveCameraEyeIndex;
#endif
  return output;
}
