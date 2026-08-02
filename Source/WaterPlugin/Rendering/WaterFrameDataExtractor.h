#pragma once

#include <Foundation/Threading/Mutex.h>
#include <RendererCore/Pipeline/Extractor.h>
#include <WaterPlugin/WaterPluginDLL.h>

/// \brief Produces exactly one immutable water snapshot per view after visible object extraction.
class PL_WATER_DLL plWaterFrameDataExtractor : public plExtractor
{
  PL_ADD_DYNAMIC_REFLECTION(plWaterFrameDataExtractor, plExtractor);

public:
  plWaterFrameDataExtractor(const char* szName = "WaterFrameDataExtractor");
  ~plWaterFrameDataExtractor();

  virtual void Extract(const plView& view, const plDynamicArray<const plGameObject*>& visibleObjects, plExtractedRenderData& ref_extractedRenderData) override;
  virtual void PostSortAndBatch(const plView& view, const plDynamicArray<const plGameObject*>& visibleObjects, plExtractedRenderData& ref_extractedRenderData) override;
  virtual plResult Serialize(plStreamWriter& inout_stream) const override;
  virtual plResult Deserialize(plStreamReader& inout_stream) override;

private:
  plMutex m_InteractionMutex;
  const plWorld* m_pInteractionWorld = nullptr;
  plUInt64 m_uiLastInteractionSequence = 0;
  plUInt64 m_uiLastInteractionSimulationTick = 0;
};
