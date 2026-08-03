#pragma once

Texture2DArray WaterSpectrumDisplacement BIND_GROUP(BG_RENDER_PASS);
Texture2DArray WaterInteractionAtlas BIND_GROUP(BG_RENDER_PASS);

// Evaluates every source of geometric water displacement at an arbitrary base-space point. The fallback
// vertex path calls this once per authored vertex; the tessellated path calls it for every domain point.
VS_OUT EvaluateWaterSurfaceVertex(float3 baseWorldPosition, float2 uv, float waveAttenuation,
  float2 vertexFlowDirection, float profileBlend)
{
  const uint geometryType = GET_PUSH_CONSTANT(plWaterDrawConstants, PatchParams).w;
  const float4 waveParams = GET_PUSH_CONSTANT(plWaterDrawConstants, WaveParams);
  const float4 flowAndBlend = GET_PUSH_CONSTANT(plWaterDrawConstants, FlowAndJunctionBlend);
  const float simulationTime = GET_PUSH_CONSTANT(plWaterDrawConstants, SurfaceParams).x;

  float3 waveOffset;
  float3 waveNormal;
  // Rivers and junction patches carry an authored per-vertex flow direction; flowAndBlend.xy packs the
  // scalar (flowSpeed, flowScale) pair and must never be used as a direction. Lakes and oceans do not advect.
  const bool usesVertexFlow = geometryType == 1u || geometryType == 3u || geometryType == 5u || geometryType == 6u;
  const float2 authoredWindDirection = PrimaryWindDirection.xy;
  const float2 waveDirection = usesVertexFlow ? vertexFlowDirection : authoredWindDirection;
  const float2 flowVelocity = usesVertexFlow ? vertexFlowDirection * (flowAndBlend.x * flowAndBlend.y) : float2(0.0, 0.0);
  const float2 waveSamplePosition = baseWorldPosition.xy + flowVelocity * simulationTime;
  EvaluateWaterGerstner(waveSamplePosition, simulationTime, waveParams.x * waveAttenuation, waveParams.y,
    waveParams.z, waveParams.w, waveDirection, waveOffset, waveNormal);
  float spectrumFoam = 0.0;

  if (geometryType == 5u || geometryType == 6u)
  {
    const float4 secondaryWaveParams = SecondaryWaveParams;
    const float2 secondaryWind = SecondaryWindAndRefraction.xy;
    float3 secondaryOffset;
    float3 secondaryNormal;
    EvaluateWaterGerstner(baseWorldPosition.xy, simulationTime, secondaryWaveParams.x * waveAttenuation,
      secondaryWaveParams.y, secondaryWaveParams.z, secondaryWaveParams.w, secondaryWind, secondaryOffset,
      secondaryNormal);
    waveOffset = lerp(waveOffset, secondaryOffset, profileBlend);
    waveNormal = normalize(lerp(waveNormal, secondaryNormal, profileBlend));
  }

  if (waveAttenuation > 0.0)
  {
    const uint spectrumLayers = GET_PUSH_CONSTANT(plWaterDrawConstants, PatchParams).y;
    if (spectrumLayers > 0u)
    {
      const float4 surfaceParams = GET_PUSH_CONSTANT(plWaterDrawConstants, SurfaceParams);
      const uint availableCascadeCount = max(spectrumLayers / 4u, 1u);
      const uint cascadeCount = min(min((uint)surfaceParams.z, availableCascadeCount), 4u);
      const uint profileSlot = min((uint)surfaceParams.w, 3u);
      const uint firstSpectrumLayer = profileSlot * availableCascadeCount;
      float4 spectral = 0.0;
      float spectralWeight = 0.0;
      float spectralFoamWeight = 0.0;
      [unroll]
      for (uint cascade = 0u; cascade < 4u; ++cascade)
      {
        if (cascade >= cascadeCount || firstSpectrumLayer + cascade >= spectrumLayers)
          break;
        const float cascadeDomainScale = exp2(float(cascade) * 2.0);
        const float2 spectrumUV = baseWorldPosition.xy / max(waveParams.y * 4.0 * cascadeDomainScale, 1.0);
        const float4 cascadeSample = WaterSpectrumDisplacement.SampleLevel(
          LinearSampler, float3(spectrumUV, firstSpectrumLayer + cascade), 0);
        const float cascadeWeight = 1.0 / exp2(float(cascade));
        spectral += cascadeSample * cascadeWeight;
        spectralWeight += cascadeWeight;
        spectralFoamWeight += cascadeSample.w * cascadeWeight;
      }
      spectral /= max(spectralWeight, 1.0);
      spectrumFoam = spectralFoamWeight / max(spectralWeight, 1.0);
      // Spectrum XY stores slope, never horizontal position. Applying it as position folds the surface.
      waveOffset.z += spectral.z * waveAttenuation;
      const float2 analyticGradient = -waveNormal.xy / max(waveNormal.z, 0.1);
      waveNormal = normalize(float3(-(analyticGradient + spectral.xy * waveAttenuation), 1.0));
    }

    if ((geometryType == 5u || geometryType == 6u) && profileBlend > 0.0 && spectrumLayers > 0u)
    {
      const uint secondaryCascadeCount = min((uint)JunctionSpillParams.y, 4u);
      if (secondaryCascadeCount > 0u)
      {
        const uint availableCascadeCount = max(spectrumLayers / 4u, 1u);
        const uint secondarySlot = min((uint)JunctionSpillParams.x, 3u);
        const uint firstSecondaryLayer = secondarySlot * availableCascadeCount;
        float4 secondarySpectral = 0.0;
        float secondarySpectralWeight = 0.0;
        float secondaryFoamWeight = 0.0;
        [unroll]
        for (uint cascade = 0u; cascade < 4u; ++cascade)
        {
          if (cascade >= secondaryCascadeCount || firstSecondaryLayer + cascade >= spectrumLayers)
            break;
          const float cascadeDomainScale = exp2(float(cascade) * 2.0);
          const float2 spectrumUV = baseWorldPosition.xy / max(SecondaryWaveParams.y * 4.0 * cascadeDomainScale, 1.0);
          const float4 cascadeSample = WaterSpectrumDisplacement.SampleLevel(
            LinearSampler, float3(spectrumUV, firstSecondaryLayer + cascade), 0);
          const float cascadeWeight = 1.0 / exp2(float(cascade));
          secondarySpectral += cascadeSample * cascadeWeight;
          secondarySpectralWeight += cascadeWeight;
          secondaryFoamWeight += cascadeSample.w * cascadeWeight;
        }
        secondarySpectral /= max(secondarySpectralWeight, 1.0);
        const float secondarySpectrumFoam = secondaryFoamWeight / max(secondarySpectralWeight, 1.0);
        const float secondaryInfluence = waveAttenuation * profileBlend;
        waveOffset.z += secondarySpectral.z * secondaryInfluence;
        const float2 analyticGradient = -waveNormal.xy / max(waveNormal.z, 0.1);
        waveNormal = normalize(float3(-(analyticGradient + secondarySpectral.xy * secondaryInfluence), 1.0));
        spectrumFoam = lerp(spectrumFoam, secondarySpectrumFoam, profileBlend);
      }
    }

    float4 interaction = 0.0;
    if (InteractionWindowParams.w > 0.5)
      interaction = SampleWaterInteraction(WaterInteractionAtlas, baseWorldPosition.xy, InteractionWindowParams);
    if (!all(abs(interaction) < 1024.0))
      interaction = 0.0;

    const float rippleCeiling = GET_PUSH_CONSTANT(plWaterDrawConstants, SurfaceParams).y;
    float maxInteractionHeight = rippleCeiling > 0.0 ? rippleCeiling : max(waveParams.x * 2.0, 0.5);
    if (geometryType == 5u || geometryType == 6u)
    {
      const float secondaryCeiling = JunctionSpillParams.w;
      const float secondaryHeight = secondaryCeiling > 0.0
                                      ? secondaryCeiling
                                      : max(SecondaryWaveParams.x * 2.0, 0.5);
      maxInteractionHeight = lerp(maxInteractionHeight, secondaryHeight, profileBlend);
    }
    interaction.x = clamp(interaction.x, -maxInteractionHeight, maxInteractionHeight);
    const float2 interactionGradient = DecodeWaterInteractionGradient(interaction);

    // Interactions remain a height-field deformation. Shifting vertices laterally by opposing ring gradients
    // can invert adjacent triangles; the resulting vertical fans were previously mistaken for tessellation.
    waveOffset.z += interaction.x * waveAttenuation;
    waveNormal = normalize(waveNormal + float3(-interactionGradient, 0.0) * waveAttenuation);

    // Continue the signed river current across the topology bridge and onto the lake's own mesh. The field is
    // compact and zero on every boundary, so this adds real moving geometry without opening a new junction seam.
    if (geometryType == 2u || geometryType == 4u || geometryType == 5u || geometryType == 6u)
    {
      float currentHeight;
      float2 currentGradient;
      EvaluateWaterJunctionCurrentRipples(baseWorldPosition.xy, simulationTime, currentHeight, currentGradient);
      waveOffset.z += currentHeight * waveAttenuation;
      const float2 analyticGradient = -waveNormal.xy / max(waveNormal.z, 0.1);
      waveNormal = normalize(float3(-(analyticGradient + currentGradient * waveAttenuation), 1.0));
    }
  }

  VS_OUT output = (VS_OUT)0;
  output.WorldPosition = baseWorldPosition + waveOffset;
  output.Normal = waveNormal;
  output.TexCoord0 = uv;
  output.BaseWorldXY = baseWorldPosition.xy;
  output.SpectrumFoam = saturate(spectrumFoam) * waveAttenuation;
#if WATER_GEOMETRY_SOURCE == WATER_GEOMETRY_SOURCE_FINITE
  float2 outputFlowDirection = vertexFlowDirection;
  if (geometryType == 2u || geometryType == 4u || geometryType == 5u || geometryType == 6u)
  {
    const float2 junctionVelocity = EvaluateWaterJunctionFlow(baseWorldPosition.xy).xy;
    if (dot(junctionVelocity, junctionVelocity) > 1.0e-10)
      outputFlowDirection = normalize(junctionVelocity);
  }
  output.Tangent = float3(outputFlowDirection, 0.0);
  output.BiTangent = float3(-outputFlowDirection.y, outputFlowDirection.x, 0.0);
  output.TexCoord1 = float2(waveAttenuation, 0.0);
  output.Color0 = float4(profileBlend, 0.0, 0.0, 1.0);
#endif
#if WATER_RENDER_PASS == WATER_RENDER_PASS_CAUSTIC_FIELD
  const float2 fieldPosition = (output.WorldPosition.xy - WaterCausticFieldCenterAndExtent.xy) *
                               WaterCausticFieldCenterAndExtent.w;
  output.Position = float4(fieldPosition.x, -fieldPosition.y, 0.5, 1.0);
#else
  output.Position = mul(GetWorldToScreenMatrix(), float4(output.WorldPosition, 1.0));
#endif
#if WATER_GEOMETRY_SOURCE == WATER_GEOMETRY_SOURCE_FINITE && RENDER_PASS == RENDER_PASS_FORWARD
  if (GET_PUSH_CONSTANT(plWaterDrawConstants, PatchParams).z >= 2u && geometryType == 5u)
    output.Position.z = 0.0001 * output.Position.w;
#endif
#if CAMERA_MODE == CAMERA_MODE_STEREO
  output.RenderTargetArrayIndex = s_ActiveCameraEyeIndex;
#endif
  return output;
}
