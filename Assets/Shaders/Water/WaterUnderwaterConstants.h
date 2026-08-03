#pragma once

#include <Shaders/Common/ConstantBufferMacros.h>

CONSTANT_BUFFER(plWaterUnderwaterConstants, 5)
{
  FLOAT4(WaterAbsorptionAndDensity);
  FLOAT4(WaterScatteringAndIntensity);
  FLOAT4(WaterMainLightDirectionAndShaftIntensity);
  COLOR4F(WaterMainLightColor);
  FLOAT4(WaterCameraParams);
  FLOAT4(WaterEffectParams);
  FLOAT4(WaterProfileParams);
  UINT4(WaterUnderwaterParams);
};
