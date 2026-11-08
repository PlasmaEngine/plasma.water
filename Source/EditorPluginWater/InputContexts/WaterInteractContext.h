#pragma once

#include <EditorFramework/InputContexts/EditorInputContext.h>
#include <EditorPluginWater/EditorPluginWaterDLL.h>
#include <Foundation/Time/Time.h>
#include <Foundation/Types/Delegate.h>

/// \brief Click/drag input mode for the Water Profile preview: intersects the mouse ray with the water
/// plane (z = 0) entirely editor-side and reports world positions through a callback. While disabled it
/// passes all input through, so the orbit camera behaves as usual.
class plWaterInteractContext : public plEditorInputContext
{
public:
  plWaterInteractContext(plEngineDocumentWindow* pOwnerWindow, plEngineViewWidget* pOwnerView);

  /// Callback receives the picked water-plane position and whether this is a continuous (drag) interaction.
  plDelegate<void(const plVec3&, bool)> m_SendInteraction;

  void SetEnabled(bool bEnabled);
  bool IsEnabled() const { return m_bEnabled; }

protected:
  virtual void OnSetOwner(plEngineDocumentWindow* pOwnerWindow, plEngineViewWidget* pOwnerView) override {}
  virtual void DoFocusLost(bool bCancel) override;

  virtual plEditorInput DoMousePressEvent(const plEditorInputEvent& e) override;
  virtual plEditorInput DoMouseMoveEvent(const plEditorInputEvent& e) override;
  virtual plEditorInput DoMouseReleaseEvent(const plEditorInputEvent& e) override;

private:
  bool TryPickWaterPlane(const plEditorInputEvent& e, plVec3& out_vPosition) const;

  bool m_bEnabled = false;
  bool m_bDragging = false;
  plTime m_LastSendTime;
};
