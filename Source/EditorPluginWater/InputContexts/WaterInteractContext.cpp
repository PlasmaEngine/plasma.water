#include <EditorPluginWater/EditorPluginWaterPCH.h>

#include <EditorPluginWater/InputContexts/WaterInteractContext.h>

#include <EditorFramework/DocumentWindow/EngineViewWidget.h>
#include <EditorFramework/InputContexts/EditorInputEvent.h>

plWaterInteractContext::plWaterInteractContext(plEngineDocumentWindow* pOwnerWindow, plEngineViewWidget* pOwnerView)
{
  SetOwner(pOwnerWindow, pOwnerView);
}

void plWaterInteractContext::SetEnabled(bool bEnabled)
{
  if (m_bEnabled == bEnabled)
    return;

  m_bEnabled = bEnabled;
  m_bDragging = false;

  if (GetOwnerView() != nullptr)
    GetOwnerView()->SetViewCursor(m_bEnabled ? plEditorPlatformCursor::Cross : plEditorPlatformCursor::Arrow);

  if (!m_bEnabled && IsActiveInputContext())
    MakeActiveInputContext(false);
}

void plWaterInteractContext::DoFocusLost(bool bCancel)
{
  m_bDragging = false;
  if (IsActiveInputContext())
    MakeActiveInputContext(false);
}

plEditorInput plWaterInteractContext::DoMousePressEvent(const plEditorInputEvent& e)
{
  if (!m_bEnabled || e.m_uiButton != plEditorInputEvent::LeftButton)
    return plEditorInput::MayBeHandledByOthers;

  plVec3 vPosition;
  if (TryPickWaterPlane(e, vPosition))
  {
    m_SendInteraction(vPosition, false);
    m_LastSendTime = plTime::Now();
  }

  m_bDragging = true;
  MakeActiveInputContext();
  return plEditorInput::WasExclusivelyHandled;
}

plEditorInput plWaterInteractContext::DoMouseMoveEvent(const plEditorInputEvent& e)
{
  if (!m_bEnabled || !m_bDragging)
    return plEditorInput::MayBeHandledByOthers;

  // Match the runtime interactor cadence (every 2nd 60 Hz tick) so dragging produces a wake, not an event flood.
  const plTime now = plTime::Now();
  if (now - m_LastSendTime >= plTime::MakeFromMilliseconds(33))
  {
    plVec3 vPosition;
    if (TryPickWaterPlane(e, vPosition))
    {
      m_SendInteraction(vPosition, true);
      m_LastSendTime = now;
    }
  }

  return plEditorInput::WasExclusivelyHandled;
}

plEditorInput plWaterInteractContext::DoMouseReleaseEvent(const plEditorInputEvent& e)
{
  if (!m_bDragging || e.m_uiButton != plEditorInputEvent::LeftButton)
    return plEditorInput::MayBeHandledByOthers;

  m_bDragging = false;
  MakeActiveInputContext(false);
  return plEditorInput::WasExclusivelyHandled;
}

bool plWaterInteractContext::TryPickWaterPlane(const plEditorInputEvent& e, plVec3& out_vPosition) const
{
  if (GetOwnerView() == nullptr || !m_SendInteraction.IsValid())
    return false;

  const plPlane waterPlane = plPlane::MakeFromNormalAndPoint(plVec3(0, 0, 1), plVec3::MakeZero());
  return GetOwnerView()
    ->PickPlane(static_cast<plUInt16>(plMath::Max(e.m_vPosition.x, 0.0f)), static_cast<plUInt16>(plMath::Max(e.m_vPosition.y, 0.0f)), waterPlane, out_vPosition)
    .Succeeded();
}
