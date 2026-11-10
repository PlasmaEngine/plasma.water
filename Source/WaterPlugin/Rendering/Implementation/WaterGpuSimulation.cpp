#include <WaterPlugin/WaterPluginPCH.h>

#include <RendererCore/Pipeline/Declarations.h>
#include <RendererCore/Pipeline/RenderPipeline.h>
#include <RendererCore/RenderContext/BindGroupBuilder.h>
#include <RendererCore/RenderContext/RenderContext.h>
#include <RendererCore/RenderGraph/RenderGraph.h>
#include <RendererCore/RenderGraph/RenderGraphContext.h>
#include <RendererCore/RenderGraph/RenderGraphPassBuilder.h>
#include <RendererFoundation/Device/Device.h>
#include <RendererFoundation/Resources/Buffer.h>
#include <RendererFoundation/Resources/ResourceFormats.h>
#include <RendererFoundation/Resources/Texture.h>
#include <WaterPlugin/Rendering/WaterFrameData.h>
#include <WaterPlugin/Rendering/WaterGpuSimulation.h>
#include <WaterPlugin/Rendering/WaterSpectrum.h>
#include <WaterPlugin/Rendering/WaterRenderData.h>

#include <Shaders/Water/WaterSimulationConstants.h>
#include <Shaders/Water/WaterSpectrumFftConstants.h>
#include <Shaders/Water/WaterSimulationStepConstants.h>

#include <Foundation/Configuration/CVar.h>

// The interaction atlas is NOT an integrated wave field: it is redrawn every frame as a pure function of the
// live event list - each event is one analytic ring that grows at the propagation speed and weakens with age
// (wave particles). A 2D wave-equation integrator was tried first and is intrinsically unable to produce clean
// rings: 2D wave fronts leave a filled interior tail (Huygens violation) and any net-volume source grows an
// unbounded logarithmic height dish. Stateless evaluation has no feedback, so there is nothing to saturate,
// destabilize, or fail to decay.
static plCVarFloat cvar_WaterInteractionPropagationSpeed("Water.Interaction.PropagationSpeed", 3.0f, plCVarFlags::Default, "Ripple ring growth speed in m/s.");
static plCVarFloat cvar_WaterInteractionRingLifetime("Water.Interaction.RingLifetime", 3.0f, plCVarFlags::Default, "Seconds a splash ring lives while growing and fading; movement-wake ringlets live 40% of this. Clamped to [0.5, 10].");
static plCVarFloat cvar_WaterInteractionImpulseGain("Water.Interaction.ImpulseGain", 3.0f, plCVarFlags::Default, "Scales splash ring amplitude (a strength-1 splash crests ~0.15 m at gain 3). Clamped to [0, 20].");
// Wake/continuous events arrive as a ~30 Hz STREAM from every moving interactor - their per-event gain must be
// far below the splash gain or the stream's combined amplitude drowns everything.
static plCVarFloat cvar_WaterInteractionWakeGain("Water.Interaction.WakeGain", 1.0f, plCVarFlags::Default, "Scales movement-wake ringlet amplitude (per event of the ~30 Hz stream). Clamped to [0, 20].");

namespace
{
  constexpr float s_fInteractionAtlasWorldSize = 100.0f;

  float WrapInteractionCoordinate(float fWorldCoordinate)
  {
    float fWrapped = fWorldCoordinate / s_fInteractionAtlasWorldSize + 0.5f;
    fWrapped -= plMath::Floor(fWrapped);
    return fWrapped * s_fInteractionAtlasWorldSize;
  }

  void BuildInteractionTileList(plArrayPtr<const plWaterGpuInteraction> interactions, plUInt32 uiEventsPerTile,
    plUInt32 uiAtlasResolution, float fPropagationSpeed, float fSplashLifetime, plDynamicArray<plVec4U32>& out_tileRecords)
  {
    const plUInt32 uiTileCount = WATER_INTERACTION_TILE_COUNT;
    const plUInt32 uiRecordsPerTile = WATER_INTERACTION_TILE_RECORDS;
    uiEventsPerTile = plMath::Clamp<plUInt32>(uiEventsPerTile, 1u, WATER_MAX_INTERACTIONS_PER_TILE);
    out_tileRecords.SetCount(uiTileCount * uiTileCount * uiRecordsPerTile, plVec4U32::MakeZero());

    const float fTileSize = s_fInteractionAtlasWorldSize / static_cast<float>(uiTileCount);
    const float fTileHalfSize = fTileSize * 0.5f;

    // Splash impulses have the strongest visual contribution, followed by the newest wake ringlets. Binning in
    // these two reverse-order passes makes overload deterministic and preserves those contributions when a busy
    // tile reaches its quality-controlled cap.
    for (plUInt32 uiClass = 0; uiClass < 2; ++uiClass)
    {
      const bool bBinSplashes = uiClass == 0;
      for (plUInt32 uiInteraction = interactions.GetCount(); uiInteraction-- > 0;)
      {
        const plWaterGpuInteraction& interaction = interactions[uiInteraction];
        const bool bSplash = interaction.InteractionParams.y == static_cast<plUInt32>(plWaterInteractionType::Splash);
        if (bSplash != bBinSplashes)
          continue;

        float fAge = 0.0f;
        const plUInt32 uiAgeBits = interaction.InteractionParams.z;
        memcpy(&fAge, &uiAgeBits, sizeof(fAge));
        const float fLifetime = bSplash ? fSplashLifetime : fSplashLifetime * 0.4f;
        const float fContactRadius = plMath::Max(interaction.PositionRadius.w, 0.1f);
        if (!plMath::IsFinite(fAge) || !plMath::IsFinite(fContactRadius) || fAge < 0.0f || fAge > fLifetime)
          continue;

        const float fRingRadius = fContactRadius + fPropagationSpeed * fAge;
        // Match the shader's two-texel minimum so the spatial index never clips the edge of a visible annulus.
        const float fRingWidth = plMath::Max(fContactRadius * 0.5f, 200.0f / static_cast<float>(plMath::Max(uiAtlasResolution, 1u)));
        const float fInnerRadius = plMath::Max(fRingRadius - fRingWidth * 4.0f, 0.0f);
        const float fOuterRadius = fRingRadius + fRingWidth * 4.0f;
        const float fInnerRadiusSquared = fInnerRadius * fInnerRadius;
        const float fOuterRadiusSquared = fOuterRadius * fOuterRadius;
        const float fEventX = WrapInteractionCoordinate(interaction.PositionRadius.x);
        const float fEventY = WrapInteractionCoordinate(interaction.PositionRadius.y);

        for (plUInt32 uiTileY = 0; uiTileY < uiTileCount; ++uiTileY)
        {
          const float fTileCenterY = (static_cast<float>(uiTileY) + 0.5f) * fTileSize;
          float fDeltaY = plMath::Abs(fTileCenterY - fEventY);
          fDeltaY = plMath::Min(fDeltaY, s_fInteractionAtlasWorldSize - fDeltaY);
          const float fMinDeltaY = plMath::Max(fDeltaY - fTileHalfSize, 0.0f);
          const float fMaxDeltaY = plMath::Min(fDeltaY + fTileHalfSize, s_fInteractionAtlasWorldSize * 0.5f);

          for (plUInt32 uiTileX = 0; uiTileX < uiTileCount; ++uiTileX)
          {
            const float fTileCenterX = (static_cast<float>(uiTileX) + 0.5f) * fTileSize;
            float fDeltaX = plMath::Abs(fTileCenterX - fEventX);
            fDeltaX = plMath::Min(fDeltaX, s_fInteractionAtlasWorldSize - fDeltaX);
            const float fMinDeltaX = plMath::Max(fDeltaX - fTileHalfSize, 0.0f);
            const float fMaxDeltaX = plMath::Min(fDeltaX + fTileHalfSize, s_fInteractionAtlasWorldSize * 0.5f);
            const float fMinDistanceSquared = fMinDeltaX * fMinDeltaX + fMinDeltaY * fMinDeltaY;
            const float fMaxDistanceSquared = fMaxDeltaX * fMaxDeltaX + fMaxDeltaY * fMaxDeltaY;
            if (fMinDistanceSquared > fOuterRadiusSquared || fMaxDistanceSquared < fInnerRadiusSquared)
              continue;

            const plUInt32 uiTile = uiTileY * uiTileCount + uiTileX;
            const plUInt32 uiRecordBase = uiTile * uiRecordsPerTile;
            plUInt32& uiTileEventCount = out_tileRecords[uiRecordBase].x;
            if (uiTileEventCount >= uiEventsPerTile)
              continue;

            const plUInt32 uiRecord = uiRecordBase + 1u + uiTileEventCount / 4u;
            out_tileRecords[uiRecord].GetData()[uiTileEventCount & 3u] = uiInteraction;
            ++uiTileEventCount;
          }
        }
      }
    }
  }
} // namespace

plWaterGpuSimulation::plWaterGpuSimulation()
{
  m_Profiles.SetCount(MaxSpectrumProfiles);
}

plWaterGpuSimulation::~plWaterGpuSimulation()
{
  Deinitialize();
}

bool plWaterGpuSimulation::Initialize(const plWaterQualitySettings& quality)
{
  Deinitialize();
  m_Quality = quality;

  plGALDevice* pDevice = plGALDevice::GetDefaultDevice();
  if (pDevice == nullptr || !pDevice->GetCapabilities().m_bShaderStageSupported[plGALShaderStage::ComputeShader])
    return false;

  m_hSpectrumShader = plResourceManager::LoadResource<plShaderResource>("Shaders/Water/WaterSpectrumUpdate.plShader");
  m_hSpectrumFftShader = plResourceManager::LoadResource<plShaderResource>("Shaders/Water/WaterSpectrumFft.plShader");
  m_hSpectrumAssembleShader = plResourceManager::LoadResource<plShaderResource>("Shaders/Water/WaterSpectrumAssemble.plShader");
  m_hInteractionShader = plResourceManager::LoadResource<plShaderResource>("Shaders/Water/WaterInteractionUpdate.plShader");
  m_hConstants = plRenderContext::CreateConstantBufferStorage<plWaterSimulationConstants>();

  if (quality.m_bUseSpectrum && quality.m_uiSpectrumResolution > 0 && quality.m_uiSpectrumCascadeCount > 0)
  {
    // What the surface shaders read: half floats with a full mip chain, so that distant water reads the waves
    // averaged over a pixel and not one texel of them.
    plGALTextureCreationDescription desc;
    desc.m_uiWidth = quality.m_uiSpectrumResolution;
    desc.m_uiHeight = quality.m_uiSpectrumResolution;
    desc.m_uiArraySize = MaxSpectrumProfiles * quality.m_uiSpectrumCascadeCount;
    desc.m_uiMipLevelCount = plMath::Log2i(quality.m_uiSpectrumResolution) + 1;
    desc.m_Type = plGALTextureType::Texture2DArray;
    desc.m_Format = plGALResourceFormat::RGBAHalf;
    desc.m_bAllowShaderResourceView = true;
    desc.m_bAllowUAV = true;
    desc.m_bAllowRenderTargetView = true;
    desc.m_bAllowDynamicMipGeneration = true;
    desc.m_ResourceAccess.m_bImmutable = false;
    m_hSpectrum = pDevice->CreateTexture(desc);
    m_hSpectrumSlope = pDevice->CreateTexture(desc);

    // What the transform works on: full floats (it adds up thousands of waves of very different sizes), one mip.
    plGALTextureCreationDescription workDesc = desc;
    workDesc.m_uiMipLevelCount = 1;
    workDesc.m_Format = plGALResourceFormat::RGBAFloat;
    workDesc.m_bAllowRenderTargetView = false;
    workDesc.m_bAllowDynamicMipGeneration = false;
    for (plUInt32 i = 0; i < 2; ++i)
    {
      m_hSpectrumFrequency[i] = pDevice->CreateTexture(workDesc);
      m_hSpectrumScratch[i] = pDevice->CreateTexture(workDesc);
    }
    plGALTextureCreationDescription foamDesc = workDesc;
    foamDesc.m_Format = plGALResourceFormat::RFloat;
    m_hSpectrumFoam = pDevice->CreateTexture(foamDesc);
    m_bFoamHistoryValid = false;
    if (m_hSpectrumSlope.IsInvalidated() || m_hSpectrumFoam.IsInvalidated() || m_hSpectrumFrequency[0].IsInvalidated() || m_hSpectrumFrequency[1].IsInvalidated() ||
        m_hSpectrumScratch[0].IsInvalidated() || m_hSpectrumScratch[1].IsInvalidated())
    {
      // Without all of them there is no spectrum. The surface falls back to its analytic waves.
      for (plGALTextureHandle* pHandle : {&m_hSpectrum, &m_hSpectrumSlope, &m_hSpectrumFoam, &m_hSpectrumFrequency[0], &m_hSpectrumFrequency[1], &m_hSpectrumScratch[0], &m_hSpectrumScratch[1]})
      {
        if (!pHandle->IsInvalidated())
          pDevice->DestroyTexture(*pHandle);
        pHandle->Invalidate();
      }
    }
  }

  if (quality.m_uiInteractionAtlasSize > 0)
  {
    plGALTextureCreationDescription desc;
    desc.m_uiWidth = quality.m_uiInteractionAtlasSize;
    desc.m_uiHeight = quality.m_uiInteractionAtlasSize;
    desc.m_uiArraySize = 1;
    desc.m_uiMipLevelCount = 1;
    desc.m_Type = plGALTextureType::Texture2DArray;
    // Height, velocity and resolved XY slope. Pixel shading consumes slope directly so interaction detail remains
    // visible even when the procedural ocean grid is too coarse to place a vertex inside a small contact.
    desc.m_Format = plGALResourceFormat::RGBAHalf;
    desc.m_bAllowRenderTargetView = true;
    desc.m_bAllowShaderResourceView = true;
    desc.m_bAllowUAV = true;
    desc.m_ResourceAccess.m_bImmutable = false;
    m_hInteraction = pDevice->CreateTexture(desc);
  }

  plGALBufferCreationDescription profileDesc;
  profileDesc.m_uiStructSize = sizeof(plWaterGpuSpectrumProfile);
  profileDesc.m_uiTotalSize = MaxSpectrumProfiles * profileDesc.m_uiStructSize;
  profileDesc.m_BufferFlags = plGALBufferUsageFlags::StructuredBuffer | plGALBufferUsageFlags::ShaderResource;
  profileDesc.m_ResourceAccess.m_bImmutable = false;
  m_hProfileBuffer = pDevice->CreateBuffer(profileDesc);

  plGALBufferCreationDescription interactionDesc;
  interactionDesc.m_uiStructSize = sizeof(plWaterGpuInteraction);
  interactionDesc.m_uiTotalSize = plMath::Max<plUInt32>(quality.m_uiInteractionEventBudget, 1u) * interactionDesc.m_uiStructSize;
  interactionDesc.m_BufferFlags = plGALBufferUsageFlags::StructuredBuffer | plGALBufferUsageFlags::ShaderResource;
  interactionDesc.m_ResourceAccess.m_bImmutable = false;
  m_hInteractionBuffer = pDevice->CreateBuffer(interactionDesc);

  plGALBufferCreationDescription interactionTileDesc;
  interactionTileDesc.m_uiStructSize = sizeof(plVec4U32);
  interactionTileDesc.m_uiTotalSize = WATER_INTERACTION_TILE_COUNT * WATER_INTERACTION_TILE_COUNT * WATER_INTERACTION_TILE_RECORDS * interactionTileDesc.m_uiStructSize;
  interactionTileDesc.m_BufferFlags = plGALBufferUsageFlags::StructuredBuffer | plGALBufferUsageFlags::ShaderResource;
  interactionTileDesc.m_ResourceAccess.m_bImmutable = false;
  m_hInteractionTileBuffer = pDevice->CreateBuffer(interactionTileDesc);

  m_bInitialized = !m_hProfileBuffer.IsInvalidated() && !m_hInteractionBuffer.IsInvalidated() &&
                   !m_hInteractionTileBuffer.IsInvalidated() && !m_hInteraction.IsInvalidated();
  // Newly allocated textures are undefined. Force one zero-event dispatch before allowing the idle fast path.
  m_bInteractionAtlasContainsData = m_bInitialized;
  m_bActiveInteractionBoundsValid = false;
  m_bHaveSimulationMode = false;
  if (!m_bInitialized)
    DestroyResources();
  return m_bInitialized;
}

void plWaterGpuSimulation::Deinitialize()
{
  DestroyResources();
  if (!m_hConstants.IsInvalidated())
    plRenderContext::DeleteConstantBufferStorage(m_hConstants);
  m_hConstants.Invalidate();
  m_hSpectrumShader.Invalidate();
  m_hSpectrumFftShader.Invalidate();
  m_hSpectrumAssembleShader.Invalidate();
  m_hInteractionShader.Invalidate();
  for (ScaleCache& cache : m_ScaleCache)
    cache = ScaleCache();
  m_LiveInteractions.Clear();
  m_uiLastAppliedSequence = 0;
  m_TotalTime = plTime::MakeZero();
  m_LastExecuteWorldTime = plTime::MakeZero();
  m_bHaveSimulationMode = false;
  m_uiCurrentEventCount = 0;
  m_bSpectrumValid = false;
  m_bInteractionAtlasContainsData = false;
  m_bActiveInteractionBoundsValid = false;
  m_bInitialized = false;
}

void plWaterGpuSimulation::DestroyResources()
{
  plGALDevice* pDevice = plGALDevice::GetDefaultDevice();
  if (pDevice == nullptr)
    return;

  for (plGALTextureHandle* pHandle : {&m_hSpectrum, &m_hSpectrumSlope, &m_hSpectrumFoam, &m_hSpectrumFrequency[0], &m_hSpectrumFrequency[1], &m_hSpectrumScratch[0], &m_hSpectrumScratch[1]})
  {
    if (!pHandle->IsInvalidated())
      pDevice->DestroyTexture(*pHandle);
    pHandle->Invalidate();
  }
  if (!m_hInteraction.IsInvalidated())
    pDevice->DestroyTexture(m_hInteraction);
  m_hInteraction.Invalidate();
  if (!m_hProfileBuffer.IsInvalidated())
    pDevice->DestroyBuffer(m_hProfileBuffer);
  if (!m_hInteractionBuffer.IsInvalidated())
    pDevice->DestroyBuffer(m_hInteractionBuffer);
  if (!m_hInteractionTileBuffer.IsInvalidated())
    pDevice->DestroyBuffer(m_hInteractionTileBuffer);
  m_hProfileBuffer.Invalidate();
  m_hInteractionBuffer.Invalidate();
  m_hInteractionTileBuffer.Invalidate();
}

plUInt32 plWaterGpuSimulation::RegisterSpectrumProfile(const plWaterProfileResourceDescriptor& profile, plUInt32 uiPreferredSlot)
{
  if (uiPreferredSlot < MaxSpectrumProfiles && !m_Profiles[uiPreferredSlot].m_bInUse)
  {
    m_Profiles[uiPreferredSlot].m_bInUse = true;
    m_Profiles[uiPreferredSlot].m_Profile = profile;
    return uiPreferredSlot;
  }

  for (plUInt32 i = 0; i < MaxSpectrumProfiles; ++i)
  {
    if (!m_Profiles[i].m_bInUse)
    {
      m_Profiles[i].m_bInUse = true;
      m_Profiles[i].m_Profile = profile;
      return i;
    }
  }

  if (!m_bWarnedProfileOverflow)
  {
    plLog::Warning("Water GPU spectrum supports at most {} simultaneous profiles. Additional profiles use deterministic Gerstner waves.", MaxSpectrumProfiles);
    m_bWarnedProfileOverflow = true;
  }
  return plInvalidIndex;
}

void plWaterGpuSimulation::ClearSpectrumProfiles()
{
  for (ProfileSlot& profile : m_Profiles)
    profile = ProfileSlot();
}

void plWaterGpuSimulation::UnregisterSpectrumProfile(plUInt32 uiSlot)
{
  if (uiSlot >= MaxSpectrumProfiles)
    return;
  m_Profiles[uiSlot] = ProfileSlot();
}

void plWaterGpuSimulation::QueueInteractions(plArrayPtr<const plWaterInteraction> interactions)
{
  const plUInt32 uiBudget = plMath::Max<plUInt32>(m_Quality.m_uiInteractionEventBudget, 1u);
  for (const plWaterInteraction& interaction : interactions)
  {
    LiveInteraction& live = m_LiveInteractions.ExpandAndGetRef();
    live.m_vPosition = interaction.m_vPosition;
    live.m_vDirection = interaction.m_vDirection;
    live.m_fRadius = interaction.m_fRadius;
    live.m_fStrength = interaction.m_fStrength;
    live.m_uiType = static_cast<plUInt32>(interaction.m_Type.GetValue());
    live.m_fBirthSeconds = static_cast<double>(interaction.m_uiSimulationTick) / 60.0;

    // Prefer fresh impulses under overload. The execution-time path follows the same oldest-first eviction rule.
    while (m_LiveInteractions.GetCount() > uiBudget)
    {
      m_LiveInteractions.RemoveAtAndCopy(0);
      ++m_uiDroppedInteractions;
    }
  }
}

void plWaterGpuSimulation::AddRenderPasses(plRenderGraph& ref_graph, plRenderPipeline* pPipeline, plDelegate<void()> profileRefreshCallback)
{
  if (!m_bInitialized || pPipeline == nullptr)
    return;

  const plRenderGraphBufferHandle hProfileBuffer = ref_graph.ImportBuffer(m_hProfileBuffer);
  const plRenderGraphBufferHandle hInteractionBuffer = ref_graph.ImportBuffer(m_hInteractionBuffer);
  const plRenderGraphBufferHandle hInteractionTileBuffer = ref_graph.ImportBuffer(m_hInteractionTileBuffer);

  // This pass executes every frame even when the render-graph structure is cached. It derives fixed-step count and
  // consumes the current immutable frame snapshot at execution time; interactions therefore cannot get frozen into
  // the frame on which the graph happened to be compiled.
  {
    auto pass = ref_graph.AddTransferPass("Water Simulation Upload");
    pass.WriteBuffer(hProfileBuffer, plGALResourceState::CopyDestination);
    pass.WriteBuffer(hInteractionBuffer, plGALResourceState::CopyDestination);
    pass.WriteBuffer(hInteractionTileBuffer, plGALResourceState::CopyDestination);
    pass.HasSideEffects();
    pass.SetExecuteCallback([this, pPipeline, hProfileBuffer, hInteractionBuffer, hInteractionTileBuffer, profileRefreshCallback](const plRenderGraphContext& ctx) {
      // Visible water/profile sets can change while the render graph remains cached. Refresh the owner's
      // registration and profile-to-slot mapping immediately before rebuilding the upload payload.
      if (profileRefreshCallback.IsValid())
        profileRefreshCallback();

      plTime worldTime = pPipeline->GetRenderData().GetWorldTime();
      if (const plWaterFrameData* pFrameData = pPipeline->GetRenderData().GetFrameData<plWaterFrameData>())
      {
        worldTime = plTime::MakeFromSeconds(pFrameData->m_fSimulationTime);
        if (!m_bHaveSimulationMode || m_bLastSimulationMode != pFrameData->m_bWorldSimulationEnabled)
        {
          // Edit/simulate transitions are epochs: live rings belong to the previous session.
          m_bHaveSimulationMode = true;
          m_bLastSimulationMode = pFrameData->m_bWorldSimulationEnabled;
          m_LiveInteractions.Clear();
          m_LastExecuteWorldTime = plTime::MakeZero();
        }
      }
      else
      {
        const plRenderDataBatchList waterBatches = pPipeline->GetRenderDataBatchesWithCategory(plWaterRenderData::WaterSurface);
        if (waterBatches.GetBatchCount() > 0)
        {
          if (const plWaterRenderData* pWater = waterBatches.GetBatch(0).GetFirstData<plWaterRenderData>())
            worldTime = plTime::MakeFromSeconds(pWater->m_fSimulationTime);
        }
      }
      // Editor play-mode transitions replace the world module and restart its fixed clock at zero while the render
      // pipeline (and its GPU simulation object) can persist. Treat a backwards time jump as a new epoch: live
      // rings belong to the previous session's clock, so they are dropped rather than aged with garbage.
      if (!m_LastExecuteWorldTime.IsZero() && worldTime < m_LastExecuteWorldTime)
      {
        m_LiveInteractions.Clear();
      }
      m_LastExecuteWorldTime = worldTime;
      m_TotalTime = worldTime;

      const float fRingLifetime = plMath::Clamp<float>(cvar_WaterInteractionRingLifetime, 0.5f, 10.0f);
      auto GetLifetime = [&](plUInt32 uiType) { return uiType == static_cast<plUInt32>(plWaterInteractionType::Splash) ? fRingLifetime : fRingLifetime * 0.4f; };

      const plUInt32 uiEventBudget = plMath::Max<plUInt32>(static_cast<plUInt32>(m_Quality.m_uiInteractionEventBudget), 1u);

      bool bPrimaryView = false;
      if (const plWaterFrameData* pFrameData = pPipeline->GetRenderData().GetFrameData<plWaterFrameData>())
      {
        m_vInteractionAnchor = pFrameData->m_vInteractionWindowCenter;
        bPrimaryView = pFrameData->m_bPrimaryView;

        // Admission: each event becomes exactly ONE live ring. Sequences restart when the world module is
        // replaced - a batch whose newest sequence is below the guard signals a new epoch, not stale data.
        plUInt64 uiBatchMax = 0;
        for (const plWaterInteraction& interaction : pFrameData->m_Interactions)
          uiBatchMax = plMath::Max(uiBatchMax, interaction.m_uiSequence);
        if (uiBatchMax != 0 && uiBatchMax < m_uiLastAppliedSequence)
          m_uiLastAppliedSequence = 0;

        for (const plWaterInteraction& interaction : pFrameData->m_Interactions)
        {
          // Re-delivery guard (extractor cursor resets, replays): every event spawns its ring exactly once.
          if (interaction.m_uiSequence != 0 && interaction.m_uiSequence <= m_uiLastAppliedSequence)
            continue;
          m_uiLastAppliedSequence = plMath::Max(m_uiLastAppliedSequence, interaction.m_uiSequence);

          // The atlas wraps world space with a 100 m period; only the window around the camera anchor is
          // valid. Events outside it would alias phantom ripples into the visible region.
          if (plMath::Max(plMath::Abs(interaction.m_vPosition.x - m_vInteractionAnchor.x),
                plMath::Abs(interaction.m_vPosition.y - m_vInteractionAnchor.y)) > 50.0f)
          {
            ++m_uiDroppedInteractions;
            // Dropped ripples are otherwise invisible - say why, but only for the view the user watches and
            // without spamming per event. Secondary views (shadows, reflections, extra viewports) run their
            // own sim and their drops are not actionable.
            if (bPrimaryView && (m_LastDropLogTime.IsZero() || m_TotalTime - m_LastDropLogTime > plTime::MakeFromSeconds(2.0)))
            {
              m_LastDropLogTime = m_TotalTime;
              plLog::Warning("Water: interaction at ({0}, {1}) dropped - outside the 100 m interaction window around ({2}, {3}). The window follows the camera (enable Water.Debug.ShowInteractions to see it).",
                plArgF(interaction.m_vPosition.x, 1), plArgF(interaction.m_vPosition.y, 1),
                plArgF(m_vInteractionAnchor.x, 1), plArgF(m_vInteractionAnchor.y, 1));
            }
            continue;
          }

          const plUInt32 uiType = static_cast<plUInt32>(interaction.m_Type.GetValue());
          const double fBirthSeconds = static_cast<double>(interaction.m_uiSimulationTick) / 60.0;
          // Late deliveries (freshly opened views, cursor resyncs, paused sims) past the ring's lifetime
          // would spawn already-dead rings - consume them silently.
          if (m_TotalTime.GetSeconds() - fBirthSeconds > GetLifetime(uiType))
            continue;

          LiveInteraction& live = m_LiveInteractions.ExpandAndGetRef();
          live.m_vPosition = interaction.m_vPosition;
          live.m_vDirection = interaction.m_vDirection;
          live.m_fRadius = interaction.m_fRadius;
          live.m_fStrength = interaction.m_fStrength;
          live.m_uiType = uiType;
          live.m_fBirthSeconds = fBirthSeconds;
        }

      }

      // Prune expired rings; enforce the budget by dropping the OLDEST (front) so fresh events always land.
      for (plUInt32 i = m_LiveInteractions.GetCount(); i-- > 0;)
      {
        if (m_TotalTime.GetSeconds() - m_LiveInteractions[i].m_fBirthSeconds > GetLifetime(m_LiveInteractions[i].m_uiType))
          m_LiveInteractions.RemoveAtAndCopy(i);
      }
      while (m_LiveInteractions.GetCount() > uiEventBudget)
      {
        m_LiveInteractions.RemoveAtAndCopy(0);
        ++m_uiDroppedInteractions;
      }

      m_bActiveInteractionBoundsValid = false;
      const float fPropagationSpeed = plMath::Max<float>(cvar_WaterInteractionPropagationSpeed, 0.0f);
      const float fMinimumRingWidth = 200.0f / static_cast<float>(plMath::Max<plUInt16>(m_Quality.m_uiInteractionAtlasSize, 1));
      for (const LiveInteraction& live : m_LiveInteractions)
      {
        const float fAge = plMath::Max(static_cast<float>(m_TotalTime.GetSeconds() - live.m_fBirthSeconds), 0.0f);
        const float fOuterRadius = live.m_fRadius + fPropagationSpeed * fAge + plMath::Max(live.m_fRadius * 0.5f, fMinimumRingWidth) * 4.0f;
        const plVec4 bounds(live.m_vPosition.x - fOuterRadius, live.m_vPosition.y - fOuterRadius,
          live.m_vPosition.x + fOuterRadius, live.m_vPosition.y + fOuterRadius);
        if (!m_bActiveInteractionBoundsValid)
        {
          m_vActiveInteractionBounds = bounds;
          m_bActiveInteractionBoundsValid = true;
        }
        else
        {
          m_vActiveInteractionBounds.x = plMath::Min(m_vActiveInteractionBounds.x, bounds.x);
          m_vActiveInteractionBounds.y = plMath::Min(m_vActiveInteractionBounds.y, bounds.y);
          m_vActiveInteractionBounds.z = plMath::Max(m_vActiveInteractionBounds.z, bounds.z);
          m_vActiveInteractionBounds.w = plMath::Max(m_vActiveInteractionBounds.w, bounds.w);
        }
      }

      // Upload the WHOLE live list every frame with fresh ages - the atlas is a pure function of this data.
      plDynamicArray<plWaterGpuInteraction> gpuInteractions;
      gpuInteractions.Reserve(m_LiveInteractions.GetCount());
      for (const LiveInteraction& live : m_LiveInteractions)
      {
        const bool bSplash = live.m_uiType == static_cast<plUInt32>(plWaterInteractionType::Splash);
        const float fGain = plMath::Clamp<float>(bSplash ? cvar_WaterInteractionImpulseGain : cvar_WaterInteractionWakeGain, 0.0f, 20.0f);
        const float fAge = plMath::Max(static_cast<float>(m_TotalTime.GetSeconds() - live.m_fBirthSeconds), 0.0f);
        plUInt32 uiAgeBits;
        memcpy(&uiAgeBits, &fAge, sizeof(uiAgeBits));

        plWaterGpuInteraction& gpuInteraction = gpuInteractions.ExpandAndGetRef();
        gpuInteraction.PositionRadius = plVec4(live.m_vPosition.x, live.m_vPosition.y, live.m_vPosition.z, live.m_fRadius);
        gpuInteraction.DirectionStrength = plVec4(live.m_vDirection.x, live.m_vDirection.y, live.m_vDirection.z, live.m_fStrength * fGain);
        gpuInteraction.InteractionParams = plVec4U32(0u, live.m_uiType, uiAgeBits, 0u);
      }
      m_uiCurrentEventCount = gpuInteractions.GetCount();

      plDynamicArray<plVec4U32> gpuInteractionTiles;
      if (!gpuInteractions.IsEmpty())
      {
        BuildInteractionTileList(gpuInteractions, m_Quality.m_uiInteractionEventsPerTile,
          m_Quality.m_uiInteractionAtlasSize, plMath::Max<float>(cvar_WaterInteractionPropagationSpeed, 0.0f), fRingLifetime, gpuInteractionTiles);
      }

      // Rebuild the tiny profile upload from the current registration at execution time. This keeps profile
      // edits live when a caller refreshes registrations without forcing render-graph recompilation.
      plStaticArray<plWaterGpuSpectrumProfile, MaxSpectrumProfiles> gpuProfiles;
      gpuProfiles.SetCount(MaxSpectrumProfiles);
      for (plUInt32 i = 0; i < MaxSpectrumProfiles; ++i)
      {
        const plWaterProfileResourceDescriptor& profile = m_Profiles[i].m_Profile;
        gpuProfiles[i].WindDirectionSpeedAmplitude = plVec4(profile.m_vWindDirection.x, profile.m_vWindDirection.y, profile.m_fWindSpeed, profile.m_fWaveAmplitude);
        gpuProfiles[i].WaveParams = plVec4(profile.m_fBaseWavelength, profile.m_fWaveSteepness, profile.m_fFoamCrestThreshold, m_Profiles[i].m_bInUse ? GetAmplitudeScale(i) : 0.0f);
        gpuProfiles[i].ProfileParams = plVec4U32(profile.m_uiWaveSeed, static_cast<plUInt32>(profile.m_SpectrumMode.GetValue()), profile.m_uiGpuCascadeCount, m_Profiles[i].m_bInUse ? 1u : 0u);
      }

      // AheadOfTime is REQUIRED here: UpdateBuffer's default mode is TransientConstantBuffer, which is only
      // valid for transient constant buffers - on these structured buffers it silently never delivered any
      // data to the GPU, which is why interaction events never produced ripples.
      ctx.GetCommandEncoder()->UpdateBuffer(ctx.ResolveBuffer(hProfileBuffer), 0,
        plArrayPtr<const plUInt8>(reinterpret_cast<const plUInt8*>(gpuProfiles.GetData()), gpuProfiles.GetCount() * sizeof(plWaterGpuSpectrumProfile)),
        plGALUpdateMode::AheadOfTime);
      if (!gpuInteractions.IsEmpty())
      {
        ctx.GetCommandEncoder()->UpdateBuffer(ctx.ResolveBuffer(hInteractionBuffer), 0,
          plArrayPtr<const plUInt8>(reinterpret_cast<const plUInt8*>(gpuInteractions.GetData()), gpuInteractions.GetCount() * sizeof(plWaterGpuInteraction)),
          plGALUpdateMode::AheadOfTime);
        ctx.GetCommandEncoder()->UpdateBuffer(ctx.ResolveBuffer(hInteractionTileBuffer), 0,
          plArrayPtr<const plUInt8>(reinterpret_cast<const plUInt8*>(gpuInteractionTiles.GetData()), gpuInteractionTiles.GetCount() * sizeof(plVec4U32)),
          plGALUpdateMode::AheadOfTime);
      }
    });
  }

  if (m_Quality.m_bUseSpectrum && !m_hSpectrum.IsInvalidated())
  {
    // The wave field of every live cascade, four passes: the field at this moment in the frequency domain, its
    // transform along rows, its transform along columns, and the result in the form the surface shaders read.
    const plRenderGraphTextureHandle hDisplacement = ref_graph.ImportTexture(m_hSpectrum);
    const plRenderGraphTextureHandle hSlope = ref_graph.ImportTexture(m_hSpectrumSlope);
    const plRenderGraphTextureHandle hFrequencyA = ref_graph.ImportTexture(m_hSpectrumFrequency[0]);
    const plRenderGraphTextureHandle hFrequencyB = ref_graph.ImportTexture(m_hSpectrumFrequency[1]);
    const plRenderGraphTextureHandle hScratchA = ref_graph.ImportTexture(m_hSpectrumScratch[0]);
    const plRenderGraphTextureHandle hScratchB = ref_graph.ImportTexture(m_hSpectrumScratch[1]);
    const plRenderGraphTextureHandle hFoam = ref_graph.ImportTexture(m_hSpectrumFoam);

    const plUInt32 uiResolution = m_Quality.m_uiSpectrumResolution;
    const plUInt32 uiCascadeSlots = m_Quality.m_uiSpectrumCascadeCount;
    const plUInt32 uiLayerCount = MaxSpectrumProfiles * uiCascadeSlots;

    // Profiles are compacted into the lowest slots by the owner, so the layers in use are the first ones.
    auto liveLayerCount = [this, uiCascadeSlots]()
    {
      plUInt32 uiLayers = 0;
      for (plUInt32 uiProfile = 0; uiProfile < MaxSpectrumProfiles; ++uiProfile)
      {
        if (m_Profiles[uiProfile].m_bInUse)
          uiLayers = (uiProfile + 1) * uiCascadeSlots;
      }
      return uiLayers;
    };

    {
      auto pass = ref_graph.AddComputePass("Water Spectrum Update");
      pass.ReadBuffer(hProfileBuffer, plGALResourceState::ComputeShaderRead);
      pass.WriteTexture(hFrequencyA, {}, plGALResourceState::UnorderedAccessWrite);
      pass.WriteTexture(hFrequencyB, {}, plGALResourceState::UnorderedAccessWrite);
      pass.HasSideEffects();
      pass.SetExecuteCallback([=](const plRenderGraphContext& ctx) {
        const plUInt32 uiLive = liveLayerCount();
        if (uiLive == 0)
        {
          // No water with a spectrum in view: nothing is computed, and nothing may be sampled.
          m_bSpectrumValid = false;
          m_bFoamHistoryValid = false;
          return;
        }

        // Foam remembers the frames before. If there is none to remember (the first frame, or the clock was set
        // back), it starts from nothing; if the frames are far apart, what is left is what would have faded anyway.
        const double fSinceLast = (m_TotalTime - m_LastFoamTime).GetSeconds();
        const bool bStartFoam = !m_bFoamHistoryValid || fSinceLast < 0.0;
        m_LastFoamTime = m_TotalTime;
        m_bFoamHistoryValid = true;

        plWaterSimulationConstants* constants = plRenderContext::GetConstantBufferData<plWaterSimulationConstants>(m_hConstants);
        constants->WaterSimulationSize = plVec4U32(uiResolution, uiResolution, uiLayerCount, MaxSpectrumProfiles);
        constants->WaterSimulationTime = plVec4(static_cast<float>(m_TotalTime.GetSeconds()), static_cast<float>(plMath::Clamp(fSinceLast, 0.0, 30.0)), bStartFoam ? 1.0f : 0.0f, 0.0f);
        const plRenderViewContext& renderViewContext = *ctx.GetUserData<plRenderViewContext>();
        plRenderContext* pContext = renderViewContext.m_pRenderContext;
        plBindGroupBuilder& bindGroup = pContext->GetBindGroup();
        bindGroup.BindBuffer("plWaterSimulationConstants", m_hConstants);
        bindGroup.BindBuffer("WaterSpectrumProfiles", ctx.ResolveBuffer(hProfileBuffer));
        bindGroup.BindTexture("WaterSpectrumFrequencyA", ctx.ResolveTexture(hFrequencyA));
        bindGroup.BindTexture("WaterSpectrumFrequencyB", ctx.ResolveTexture(hFrequencyB));
        pContext->BindShader(m_hSpectrumShader);
        const plUInt32 uiGroups = (uiResolution + WATER_SIMULATION_THREAD_GROUP_SIZE - 1) / WATER_SIMULATION_THREAD_GROUP_SIZE;
        m_bSpectrumValid = pContext->Dispatch(uiGroups, uiGroups, uiLive).Succeeded();
      });
    }

    auto addTransformPass = [&](const char* szName, bool bColumns, plRenderGraphTextureHandle hInputA, plRenderGraphTextureHandle hInputB, plRenderGraphTextureHandle hOutputA, plRenderGraphTextureHandle hOutputB)
    {
      auto pass = ref_graph.AddComputePass(szName);
      pass.ReadTexture(hInputA, {}, plGALResourceState::ComputeShaderRead);
      pass.ReadTexture(hInputB, {}, plGALResourceState::ComputeShaderRead);
      pass.WriteTexture(hOutputA, {}, plGALResourceState::UnorderedAccessWrite);
      pass.WriteTexture(hOutputB, {}, plGALResourceState::UnorderedAccessWrite);
      pass.HasSideEffects();
      pass.SetExecuteCallback([=](const plRenderGraphContext& ctx) {
        const plUInt32 uiLive = liveLayerCount();
        if (uiLive == 0 || !m_bSpectrumValid)
          return;

        const plRenderViewContext& renderViewContext = *ctx.GetUserData<plRenderViewContext>();
        plRenderContext* pContext = renderViewContext.m_pRenderContext;
        plWaterSpectrumFftConstants constants;
        constants.FftParams = plVec4U32(uiResolution, plMath::Log2i(uiResolution), bColumns ? 1u : 0u, uiLive);
        plBindGroupBuilder& bindGroup = pContext->GetBindGroup();
        bindGroup.BindTexture("WaterFftInputA", ctx.ResolveTexture(hInputA));
        bindGroup.BindTexture("WaterFftInputB", ctx.ResolveTexture(hInputB));
        bindGroup.BindTexture("WaterFftOutputA", ctx.ResolveTexture(hOutputA));
        bindGroup.BindTexture("WaterFftOutputB", ctx.ResolveTexture(hOutputB));
        pContext->BindShader(m_hSpectrumFftShader);
        pContext->SetPushConstants("plWaterSpectrumFftConstants", constants);
        // One thread group per line of every live layer.
        m_bSpectrumValid = pContext->Dispatch(1, uiResolution, uiLive).Succeeded();
      });
    };
    addTransformPass("Water Spectrum Transform Rows", false, hFrequencyA, hFrequencyB, hScratchA, hScratchB);
    addTransformPass("Water Spectrum Transform Columns", true, hScratchA, hScratchB, hFrequencyA, hFrequencyB);

    {
      auto pass = ref_graph.AddComputePass("Water Spectrum Assemble");
      pass.ReadBuffer(hProfileBuffer, plGALResourceState::ComputeShaderRead);
      pass.ReadTexture(hFrequencyA, {}, plGALResourceState::ComputeShaderRead);
      pass.ReadTexture(hFrequencyB, {}, plGALResourceState::ComputeShaderRead);
      pass.WriteTexture(hDisplacement, {}, plGALResourceState::UnorderedAccessWrite);
      pass.WriteTexture(hSlope, {}, plGALResourceState::UnorderedAccessWrite);
      pass.WriteTexture(hFoam, {}, plGALResourceState::UnorderedAccessWrite);
      pass.HasSideEffects();
      pass.SetExecuteCallback([=](const plRenderGraphContext& ctx) {
        const plUInt32 uiLive = liveLayerCount();
        if (uiLive == 0 || !m_bSpectrumValid)
          return;

        const plRenderViewContext& renderViewContext = *ctx.GetUserData<plRenderViewContext>();
        plRenderContext* pContext = renderViewContext.m_pRenderContext;
        plBindGroupBuilder& bindGroup = pContext->GetBindGroup();
        bindGroup.BindBuffer("plWaterSimulationConstants", m_hConstants);
        bindGroup.BindBuffer("WaterSpectrumProfiles", ctx.ResolveBuffer(hProfileBuffer));
        bindGroup.BindTexture("WaterFftInputA", ctx.ResolveTexture(hFrequencyA));
        bindGroup.BindTexture("WaterFftInputB", ctx.ResolveTexture(hFrequencyB));
        // The shader writes the top mip; the chain below it is generated right after.
        bindGroup.BindTexture("WaterSpectrumDisplacementOutput", ctx.ResolveTexture(hDisplacement));
        bindGroup.BindTexture("WaterSpectrumSlopeOutput", ctx.ResolveTexture(hSlope));
        bindGroup.BindTexture("WaterSpectrumFoamHistory", ctx.ResolveTexture(hFoam));
        pContext->BindShader(m_hSpectrumAssembleShader);
        const plUInt32 uiGroups = (uiResolution + WATER_SIMULATION_THREAD_GROUP_SIZE - 1) / WATER_SIMULATION_THREAD_GROUP_SIZE;
        m_bSpectrumValid = pContext->Dispatch(uiGroups, uiGroups, uiLive).Succeeded();
        if (m_bSpectrumValid)
        {
          pContext->GetCommandEncoder()->GenerateMipMaps(ctx.ResolveTexture(hDisplacement), {});
          pContext->GetCommandEncoder()->GenerateMipMaps(ctx.ResolveTexture(hSlope), {});
        }
      });
    }
  }

  // One stateless pass redraws the atlas while rings are alive, followed by one zero-event dispatch that clears
  // stale data. The recorded graph remains identical for cache reuse even when the execute callback takes its idle path.
  {
    const plRenderGraphTextureHandle hAtlas = ref_graph.ImportTexture(m_hInteraction);
    auto pass = ref_graph.AddComputePass("Water Interaction Ripples");
    pass.ReadBuffer(hInteractionBuffer, plGALResourceState::ComputeShaderRead);
    pass.ReadBuffer(hInteractionTileBuffer, plGALResourceState::ComputeShaderRead);
    pass.WriteTexture(hAtlas, {}, plGALResourceState::UnorderedAccessWrite);
    pass.HasSideEffects();
    pass.SetExecuteCallback([this, hInteractionBuffer, hInteractionTileBuffer, hAtlas](const plRenderGraphContext& ctx) {
      // A stateless atlas only needs work while events are alive, plus one final dispatch to erase their data.
      // This also guarantees a newly allocated (undefined) atlas is cleared before its first sample.
      if (m_uiCurrentEventCount == 0 && !m_bInteractionAtlasContainsData)
        return;

      plWaterSimulationStepConstants stepConstants;
      stepConstants.WaterSimStep = plVec4U32(
        m_uiCurrentEventCount, m_Quality.m_uiInteractionAtlasSize, m_Quality.m_uiInteractionAtlasSize, WATER_INTERACTION_TILE_COUNT);
      stepConstants.WaterSimTiming = plVec4(static_cast<float>(m_TotalTime.GetSeconds()),
        plMath::Max<float>(cvar_WaterInteractionPropagationSpeed, 0.0f),
        plMath::Clamp<float>(cvar_WaterInteractionRingLifetime, 0.5f, 10.0f),
        static_cast<float>(plMath::Clamp<plUInt32>(m_Quality.m_uiInteractionEventsPerTile, 1u, WATER_MAX_INTERACTIONS_PER_TILE)));

      const plRenderViewContext& renderViewContext = *ctx.GetUserData<plRenderViewContext>();
      plRenderContext* pContext = renderViewContext.m_pRenderContext;
      pContext->SetPushConstants("plWaterSimulationStepConstants", stepConstants);
      pContext->BindShader(m_hInteractionShader);
      plBindGroupBuilder& bindGroup = pContext->GetBindGroup();
      bindGroup.BindBuffer("WaterInteractionEvents", ctx.ResolveBuffer(hInteractionBuffer));
      bindGroup.BindBuffer("WaterInteractionTileEvents", ctx.ResolveBuffer(hInteractionTileBuffer));
      bindGroup.BindTexture("WaterInteractionOutput", ctx.ResolveTexture(hAtlas));
      const plResult dispatchResult = pContext->Dispatch(
        (m_Quality.m_uiInteractionAtlasSize + WATER_SIMULATION_THREAD_GROUP_SIZE - 1) / WATER_SIMULATION_THREAD_GROUP_SIZE,
        (m_Quality.m_uiInteractionAtlasSize + WATER_SIMULATION_THREAD_GROUP_SIZE - 1) / WATER_SIMULATION_THREAD_GROUP_SIZE, 1);
      if (dispatchResult.Succeeded())
        m_bInteractionAtlasContainsData = m_uiCurrentEventCount > 0;
    });
  }
}

float plWaterGpuSimulation::GetAmplitudeScale(plUInt32 uiSlot)
{
  const plWaterProfileResourceDescriptor& profile = m_Profiles[uiSlot].m_Profile;
  const plUInt32 uiCascades = plMath::Min<plUInt32>(profile.m_uiGpuCascadeCount, m_Quality.m_uiSpectrumCascadeCount);
  const float values[] = {profile.m_fWaveAmplitude, profile.m_fBaseWavelength, profile.m_vWindDirection.x, profile.m_vWindDirection.y,
    static_cast<float>(profile.m_SpectrumMode.GetValue()), static_cast<float>(uiCascades), static_cast<float>(m_Quality.m_uiSpectrumResolution)};
  const plUInt64 uiKey = plHashingUtils::xxHash64(values, sizeof(values), 1);
  ScaleCache& cache = m_ScaleCache[uiSlot];
  if (cache.m_uiKey != uiKey)
  {
    cache.m_uiKey = uiKey;
    cache.m_fScale = plWaterSpectrum::ComputeAmplitudeScale(profile, m_Quality.m_uiSpectrumResolution, uiCascades);
  }
  return cache.m_fScale;
}

plGALTextureHandle plWaterGpuSimulation::GetSpectrumDisplacement() const
{
  return m_hSpectrum;
}

plGALTextureHandle plWaterGpuSimulation::GetInteractionAtlas() const
{
  return m_hInteraction;
}

PL_STATICLINK_FILE(WaterPlugin, WaterPlugin_Rendering_Implementation_WaterGpuSimulation);
