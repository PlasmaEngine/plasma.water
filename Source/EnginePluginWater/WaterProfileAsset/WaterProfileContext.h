#pragma once

#include <EditorEngineProcessFramework/EngineProcess/EngineProcessDocumentContext.h>
#include <EnginePluginWater/EnginePluginWaterDLL.h>

class plSplineComponent;
class plWaterBodyComponent;
class plWaterProfileViewContext;

class PL_ENGINEPLUGINWATER_DLL plWaterProfileContext : public plEngineProcessDocumentContext
{
  PL_ADD_DYNAMIC_REFLECTION(plWaterProfileContext, plEngineProcessDocumentContext);

public:
  plWaterProfileContext();

  virtual void HandleMessage(const plEditorEngineDocumentMsg* pMsg) override;

protected:
  virtual void OnInitialize() override;
  virtual plEngineProcessViewContext* CreateViewContext() override;
  virtual void DestroyViewContext(plEngineProcessViewContext* pContext) override;

private:
  void ConfigureBody(plInt32 iBodyType);
  void BuildPreviewSpline(plInt32 iBodyType);
  void ConfigureScene(plInt32 iScene);
  void SetSunElevation(plInt32 iDegrees);
  void TriggerInteraction(const plVec3& vPosition, bool bContinuous);

  plWaterBodyComponent* m_pWaterBody = nullptr;
  plSplineComponent* m_pSpline = nullptr;
  plGameObjectHandle m_hFloor;
  plGameObjectHandle m_hSun;
  plInt32 m_iCurrentBodyType = -1;
  plInt32 m_iCurrentScene = 0;
  plInt32 m_iSunElevation = 35;
  plInt32 m_iPreviewQuality = 2;
  plHybridArray<plWaterProfileViewContext*, 2> m_ProfileViews;
};
