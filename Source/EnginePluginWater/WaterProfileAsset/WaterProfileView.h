#pragma once

#include <EditorEngineProcessFramework/EngineProcess/EngineProcessViewContext.h>

class plWaterProfileContext;

class plWaterProfileViewContext : public plEngineProcessViewContext
{
public:
  explicit plWaterProfileViewContext(plWaterProfileContext* pContext);
  void SetQuality(plInt32 iQuality);

protected:
  virtual plViewHandle CreateView() override;

private:
  plInt32 m_iQuality = 2;
};
