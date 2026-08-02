#include <EnginePluginWater/EnginePluginWaterPCH.h>

#include <EnginePluginWater/WaterProfileAsset/WaterProfileContext.h>
#include <EnginePluginWater/WaterProfileAsset/WaterProfileView.h>
#include <RendererCore/Pipeline/View.h>
#include <RendererCore/RenderWorld/RenderWorld.h>

PL_STATICLINK_FILE(EnginePluginWater, EnginePluginWater_WaterProfileAsset_WaterProfileView);

plWaterProfileViewContext::plWaterProfileViewContext(plWaterProfileContext* pContext)
  : plEngineProcessViewContext(pContext)
{
  m_Camera.SetCameraMode(plCameraMode::PerspectiveFixedFovY, 55.0f, 0.05f, 500.0f);
  m_Camera.LookAt(plVec3(-12.0f, -12.0f, 8.0f), plVec3::MakeZero(), plVec3(0, 0, 1));
}

void plWaterProfileViewContext::SetQuality(plInt32 iQuality)
{
  m_iQuality = plMath::Clamp(iQuality, 0, 3);

  plView* pView = nullptr;
  if (!plRenderWorld::TryGetView(m_hView, pView))
    return;

  pView->SetRenderPassProperty("WaterSurfacePass", "Quality", m_iQuality);
  pView->SetRenderPassProperty("WaterUnderwaterPass", "Quality", m_iQuality);
}

plViewHandle plWaterProfileViewContext::CreateView()
{
  plView* pView = nullptr;
  plRenderWorld::CreateView("Water Profile Editor - View", pView);
  pView->SetCameraUsageHint(plCameraUsageHint::EditorView);
  pView->SetRenderPipelineResource(CreateDefaultRenderPipeline());
  pView->SetWorld(GetDocumentContext()->GetWorld());
  pView->SetCamera(&m_Camera);
  pView->SetRenderPassProperty("WaterSurfacePass", "Quality", m_iQuality);
  pView->SetRenderPassProperty("WaterUnderwaterPass", "Quality", m_iQuality);
  return pView->GetHandle();
}
