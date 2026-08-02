#pragma once

#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/Containers/StaticArray.h>
#include <Foundation/Time/Time.h>
#include <Foundation/Types/Delegate.h>
#include <GameEngine/Interfaces/WaterWorldModule.h>
#include <RendererCore/RenderGraph/Declarations.h>
#include <RendererCore/Shader/ConstantBufferStorage.h>
#include <RendererCore/Shader/ShaderResource.h>
#include <WaterPlugin/Rendering/WaterQuality.h>
#include <WaterPlugin/Resources/WaterProfileResource.h>
#include <WaterPlugin/WaterPluginDLL.h>

class plRenderGraph;
class plRenderPipeline;

/// \brief Owns bounded water simulation resources. A world module should own one instance and call AddRenderPasses once per frame.
///
/// The class deliberately has no global callbacks and therefore cannot outlive WaterPlugin on unload.
class PL_WATER_DLL plWaterGpuSimulation
{
public:
  static constexpr plUInt32 MaxSpectrumProfiles = 4;

  plWaterGpuSimulation();
  ~plWaterGpuSimulation();

  bool Initialize(const plWaterQualitySettings& quality);
  void Deinitialize();

  /// Returns a stable [0,3] slot, or plInvalidIndex. A fifth profile is downgraded to analytic Gerstner waves.
  plUInt32 RegisterSpectrumProfile(const plWaterProfileResourceDescriptor& profile, plUInt32 uiPreferredSlot = plInvalidIndex);
  void ClearSpectrumProfiles();
  void UnregisterSpectrumProfile(plUInt32 uiSlot);

  void QueueInteractions(plArrayPtr<const plWaterInteraction> interactions);
  void AddRenderPasses(plRenderGraph& ref_graph, plRenderPipeline* pPipeline, plDelegate<void()> profileRefreshCallback = {});

  plGALTextureHandle GetSpectrumDisplacement() const;
  plGALTextureHandle GetInteractionAtlas() const;
  bool HasValidSpectrum() const { return m_bSpectrumValid; }
  bool HasInteractionData() const { return m_bInteractionAtlasContainsData; }
  bool GetActiveInteractionBounds(plVec4& out_bounds) const
  {
    out_bounds = m_vActiveInteractionBounds;
    return m_bActiveInteractionBoundsValid;
  }
  plUInt32 GetDroppedInteractionCount() const { return m_uiDroppedInteractions; }

private:
  void DestroyResources();

  plWaterQualitySettings m_Quality;
  plShaderResourceHandle m_hSpectrumShader;
  plShaderResourceHandle m_hInteractionShader;
  plConstantBufferStorageHandle m_hConstants; ///< Spectrum pass constants.
  plGALTextureHandle m_hSpectrum;
  plGALTextureHandle m_hInteraction;
  plGALBufferHandle m_hProfileBuffer;
  plGALBufferHandle m_hInteractionBuffer;
  plGALBufferHandle m_hInteractionTileBuffer;

  struct ProfileSlot
  {
    plWaterProfileResourceDescriptor m_Profile;
    bool m_bInUse = false;
  };
  plStaticArray<ProfileSlot, MaxSpectrumProfiles> m_Profiles;

  /// An admitted interaction: one analytic expanding ring, alive until its lifetime expires. The atlas is a
  /// pure function of this list (redrawn every frame) - there is no integrated feedback state.
  struct LiveInteraction
  {
    plVec3 m_vPosition;
    plVec3 m_vDirection;
    float m_fRadius = 0.5f;
    float m_fStrength = 1.0f; ///< Raw event strength; gain cvars apply at upload so they stay live-tunable.
    plUInt32 m_uiType = 0;
    double m_fBirthSeconds = 0.0;
  };
  plDynamicArray<LiveInteraction> m_LiveInteractions;

  plTime m_TotalTime;
  plTime m_LastExecuteWorldTime;
  plTime m_LastDropLogTime;
  plVec2 m_vInteractionAnchor = plVec2::MakeZero();
  /// World XY min/max of the live crest/trough annuli, used to refine only procedural patches they touch.
  plVec4 m_vActiveInteractionBounds = plVec4::MakeZero();
  plUInt64 m_uiLastAppliedSequence = 0; ///< Re-delivery guard: highest event sequence already admitted.
  plUInt32 m_uiDroppedInteractions = 0;
  plUInt32 m_uiCurrentEventCount = 0;
  bool m_bSpectrumValid = false;
  bool m_bInteractionAtlasContainsData = false;
  bool m_bActiveInteractionBoundsValid = false;
  bool m_bWarnedProfileOverflow = false;
  bool m_bHaveSimulationMode = false;
  bool m_bLastSimulationMode = false;
  bool m_bInitialized = false;
};
