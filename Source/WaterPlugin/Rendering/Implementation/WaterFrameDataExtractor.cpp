#include <WaterPlugin/WaterPluginPCH.h>

#include <Foundation/IO/TypeVersionContext.h>
#include <Foundation/Memory/FrameAllocator.h>
#include <RendererCore/Lights/DirectionalLightComponent.h>
#include <RendererCore/Pipeline/ExtractedRenderData.h>
#include <RendererCore/Pipeline/View.h>
#include <WaterPlugin/Rendering/WaterFrameData.h>
#include <WaterPlugin/Rendering/WaterFrameDataExtractor.h>
#include <WaterPlugin/Utils/WaterGeometry.h>
#include <WaterPlugin/World/WaterWorldModule.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plWaterFrameDataExtractor, 1, plRTTIDefaultAllocator<plWaterFrameDataExtractor>)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plWaterFrameDataExtractor::plWaterFrameDataExtractor(const char* szName)
  : plExtractor(szName)
{
  m_DependsOn.PushBack(plMakeHashedString("plVisibleObjectsExtractor"));
}

plWaterFrameDataExtractor::~plWaterFrameDataExtractor() = default;

void plWaterFrameDataExtractor::Extract(const plView& view, const plDynamicArray<const plGameObject*>& visibleObjects, plExtractedRenderData& ref_extractedRenderData)
{
  PL_IGNORE_UNUSED(view);
  PL_IGNORE_UNUSED(visibleObjects);
  PL_IGNORE_UNUSED(ref_extractedRenderData);
}

void plWaterFrameDataExtractor::PostSortAndBatch(const plView& view, const plDynamicArray<const plGameObject*>& visibleObjects, plExtractedRenderData& ref_extractedRenderData)
{
  PL_IGNORE_UNUSED(visibleObjects);

  plWaterFrameData* pFrameData = PL_NEW(plFrameAllocator::GetCurrentAllocator(), plWaterFrameData);
  pFrameData->m_bWorldSimulationEnabled = view.GetWorld()->GetWorldSimulationEnabled();
  {
    // PostSortAndBatch may run on an arbitrary extraction worker. Acquire the
    // world's read marker here and build the final frame payload on this worker's
    // frame allocator; never pass frame-allocated memory between extractor phases.
    PL_LOCK(view.GetWorld()->GetReadMarker());

    const plWaterWorldModule* pWaterModule = view.GetWorld()->GetModuleReadOnly<plWaterWorldModule>();
    // The RENDER camera, deliberately not the culling camera: culling can be frozen for debugging or driven
    // by a dedicated culling camera, and the interaction window has to follow what the user actually sees.
    const plCamera* pCamera = view.GetCamera();
    if (pCamera != nullptr)
    {
      // Anchor the interaction window on the camera with a modest forward bias. Ripples are small-scale
      // detail that is only perceivable near the camera, so the window must never evict the camera's own
      // surroundings - a view-target anchor did exactly that for grazing views across the water.
      plVec2 vAnchor = pCamera->GetPosition().GetAsVec2();
      plVec2 vForward = pCamera->GetCenterDirForwards().GetAsVec2();
      if (vForward.GetLengthSquared() > 0.01f)
      {
        vForward.Normalize();
        vAnchor += vForward * 20.0f;
      }
      pFrameData->m_vInteractionWindowCenter = vAnchor;

      const plEnum<plCameraUsageHint> usageHint = view.GetCameraUsageHint();
      pFrameData->m_bPrimaryView = usageHint == plCameraUsageHint::None || usageHint == plCameraUsageHint::MainView ||
                                   usageHint == plCameraUsageHint::EditorView;

      if (pFrameData->m_bPrimaryView && pWaterModule != nullptr)
        pWaterModule->SetDebugInteractionWindow(vAnchor);
    }
    if (pWaterModule != nullptr)
    {
      pFrameData->m_uiProfileCount = pWaterModule->FillGpuProfileTable(pFrameData->m_Profiles);
    }

    if (pWaterModule != nullptr && pCamera != nullptr)
    {
      pFrameData->m_fSimulationTime = static_cast<float>(pWaterModule->GetFixedSimulationStep().GetSeconds() * pWaterModule->GetSimulationTick());
      plWaterSurfaceQuery query;
      query.m_vPosition = pCamera->GetPosition();
      query.m_fMaxVerticalDistance = 10000.0f;

      plWaterSurfaceResult surface;
      if (pWaterModule->TrySampleSurface(query, surface))
      {
        pFrameData->m_bHasWaterData = true;
        plWaterBodySnapshot snapshot;
        if (pWaterModule->TryGetBodySnapshot(surface.m_BodyId, snapshot))
        {
          pFrameData->m_vAbsorption = snapshot.m_Profile.m_vAbsorption;
          pFrameData->m_vScattering = snapshot.m_Profile.m_vScattering;
          pFrameData->m_fMaxUnderwaterDistance = plMath::Max(snapshot.m_Descriptor.m_fDefaultDepth, 1.0f);
          pFrameData->m_fUnderwaterFogDensity = snapshot.m_Profile.m_fUnderwaterFogDensity;
          pFrameData->m_fUnderwaterScattering = snapshot.m_Profile.m_fUnderwaterScattering;
          pFrameData->m_fUnderwaterHaze = snapshot.m_Profile.m_fUnderwaterHaze;
          pFrameData->m_fUnderwaterDistortion = snapshot.m_Profile.m_fUnderwaterDistortion;
          pFrameData->m_fUnderwaterCaustics = snapshot.m_Profile.m_fUnderwaterCaustics;
          pFrameData->m_fReflectedCaustics = snapshot.m_Descriptor.m_Type == plWaterBodyType::Ocean
                                               ? 0.0f
                                               : snapshot.m_Profile.m_fReflectedCaustics;
          pFrameData->m_fUnderwaterLightShafts = snapshot.m_Profile.m_fUnderwaterLightShafts;
          pFrameData->m_fUnderwaterMeniscus = snapshot.m_Profile.m_fUnderwaterMeniscus;
          pFrameData->m_uiActiveBodyId = surface.m_BodyId.m_uiIndex;
          pFrameData->m_vCausticFallbackCenter = surface.m_vSurfacePosition.GetAsVec2();
          pFrameData->m_vCausticFallbackWindDirection = snapshot.m_Profile.m_vWindDirection;
          pFrameData->m_vCausticFallbackWaveParams = plVec4(snapshot.m_Profile.m_fWaveAmplitude,
            snapshot.m_Profile.m_fBaseWavelength, snapshot.m_Profile.m_fWindSpeed, snapshot.m_Profile.m_fWaveSteepness);
          pFrameData->m_fCausticFallbackSurfaceHeight = surface.m_vSurfacePosition.z;
          if (snapshot.m_Descriptor.m_Type == plWaterBodyType::Ocean)
          {
            pFrameData->m_vCausticFallbackBounds = plVec4(-1.0e9f, -1.0e9f, 1.0e9f, 1.0e9f);
            pFrameData->m_bCausticFallbackValid = true;
          }
          else if (snapshot.m_Descriptor.m_Type == plWaterBodyType::Lake && snapshot.m_Descriptor.m_ControlPoints.GetCount() >= 3)
          {
            float fDistanceToShoreSquared = plMath::HighValue<float>();
            const plVec2 vCenter = surface.m_vSurfacePosition.GetAsVec2();
            const auto& points = snapshot.m_Descriptor.m_ControlPoints;
            for (plUInt32 i = 0; i < points.GetCount(); ++i)
            {
              const plVec2 a = points[i].m_vPosition.GetAsVec2();
              const plVec2 b = points[(i + 1) % points.GetCount()].m_vPosition.GetAsVec2();
              const plVec2 edge = b - a;
              const float fEdgeLengthSquared = edge.GetLengthSquared();
              const float t = fEdgeLengthSquared > plMath::SmallEpsilon<float>()
                                ? plMath::Clamp((vCenter - a).Dot(edge) / fEdgeLengthSquared, 0.0f, 1.0f)
                                : 0.0f;
              fDistanceToShoreSquared = plMath::Min(fDistanceToShoreSquared, (vCenter - (a + edge * t)).GetLengthSquared());
            }

            const float fWaveMargin = plMath::Max(0.5f,
              snapshot.m_Profile.m_fWaveAmplitude * plMath::Abs(snapshot.m_Profile.m_fWaveSteepness) * 2.0f);
            const float fFallbackRadius = plMath::Clamp(plMath::Sqrt(fDistanceToShoreSquared) - fWaveMargin, 0.0f, 32.0f);
            if (fFallbackRadius >= 0.25f)
            {
              pFrameData->m_vCausticFallbackBounds = plVec4(vCenter.x - fFallbackRadius, vCenter.y - fFallbackRadius,
                vCenter.x + fFallbackRadius, vCenter.y + fFallbackRadius);
              pFrameData->m_bCausticFallbackValid = true;
            }
          }
          else
          {
            // Use a circle contained by the local river cross-section. This avoids filling the dry corners of
            // a curved river's rectangular bounds when its real surface was outside the render frustum.
            plWaterRiverLocation location;
            if (plWaterGeometry::FindClosestPointOnRiver(snapshot.m_Descriptor.m_ControlPoints,
                  surface.m_vSurfacePosition.GetAsVec2(), location))
            {
              const float fWaveMargin = plMath::Max(0.5f,
                snapshot.m_Profile.m_fWaveAmplitude * plMath::Abs(snapshot.m_Profile.m_fWaveSteepness) * 2.0f);
              const float fFallbackRadius = plMath::Clamp(plMath::Min(location.m_fLeftWidth, location.m_fRightWidth) -
                                                            plMath::Sqrt(location.m_fDistanceSquared) - fWaveMargin,
                0.0f, 24.0f);
              if (fFallbackRadius >= 0.25f)
              {
                const plVec2 vCenter = surface.m_vSurfacePosition.GetAsVec2();
                pFrameData->m_vCausticFallbackBounds = plVec4(vCenter.x - fFallbackRadius, vCenter.y - fFallbackRadius,
                  vCenter.x + fFallbackRadius, vCenter.y + fFallbackRadius);
                pFrameData->m_bCausticFallbackValid = true;
              }
            }
          }

          plWaterBodySnapshot secondarySnapshot;
          if (surface.m_fProfileBlend > 0.0f && pWaterModule->TryGetBodySnapshot(surface.m_SecondaryBodyId, secondarySnapshot))
          {
            const float fBlend = plMath::Saturate(surface.m_fProfileBlend);
            pFrameData->m_vAbsorption = plMath::Lerp(pFrameData->m_vAbsorption, secondarySnapshot.m_Profile.m_vAbsorption, fBlend);
            pFrameData->m_vScattering = plMath::Lerp(pFrameData->m_vScattering, secondarySnapshot.m_Profile.m_vScattering, fBlend);
            pFrameData->m_fMaxUnderwaterDistance = plMath::Lerp(pFrameData->m_fMaxUnderwaterDistance,
              plMath::Max(secondarySnapshot.m_Descriptor.m_fDefaultDepth, 1.0f), fBlend);
            pFrameData->m_fUnderwaterFogDensity = plMath::Lerp(
              pFrameData->m_fUnderwaterFogDensity, secondarySnapshot.m_Profile.m_fUnderwaterFogDensity, fBlend);
            pFrameData->m_fUnderwaterScattering = plMath::Lerp(
              pFrameData->m_fUnderwaterScattering, secondarySnapshot.m_Profile.m_fUnderwaterScattering, fBlend);
            pFrameData->m_fUnderwaterHaze = plMath::Lerp(pFrameData->m_fUnderwaterHaze, secondarySnapshot.m_Profile.m_fUnderwaterHaze, fBlend);
            pFrameData->m_fUnderwaterDistortion = plMath::Lerp(
              pFrameData->m_fUnderwaterDistortion, secondarySnapshot.m_Profile.m_fUnderwaterDistortion, fBlend);
            pFrameData->m_fUnderwaterCaustics = plMath::Lerp(
              pFrameData->m_fUnderwaterCaustics, secondarySnapshot.m_Profile.m_fUnderwaterCaustics, fBlend);
            const float fSecondaryReflectedCaustics = secondarySnapshot.m_Descriptor.m_Type == plWaterBodyType::Ocean
                                                        ? 0.0f
                                                        : secondarySnapshot.m_Profile.m_fReflectedCaustics;
            pFrameData->m_fReflectedCaustics = plMath::Lerp(
              pFrameData->m_fReflectedCaustics, fSecondaryReflectedCaustics, fBlend);
            pFrameData->m_fUnderwaterLightShafts = plMath::Lerp(
              pFrameData->m_fUnderwaterLightShafts, secondarySnapshot.m_Profile.m_fUnderwaterLightShafts, fBlend);
            pFrameData->m_fUnderwaterMeniscus = plMath::Lerp(
              pFrameData->m_fUnderwaterMeniscus, secondarySnapshot.m_Profile.m_fUnderwaterMeniscus, fBlend);
          }
        }

        pFrameData->m_fCameraWaterDepth = plMath::Max(surface.m_fDepth, 0.0f);
        pFrameData->m_bCameraUnderwater = surface.m_bContained && surface.m_fDepth > 0.0f;
      }

      plDynamicArray<plWaterInteraction> interactions;
      {
        PL_LOCK(m_InteractionMutex);
        const plUInt64 uiSimulationTick = pWaterModule->GetSimulationTick();
        // Epoch changes (world switch, editor play worlds restarting their tick/sequence while the render
        // pipeline and this extractor survive) resync the cursor to the ring's LATEST sequence WITHOUT copying:
        // the module retains a long event history and replaying it would re-apply every past impulse to the
        // wave field as one giant compound splash. Only events issued after the resync may flow.
        // On an empty ring CopyInteractionsSince echoes the passed cursor back, so the MaxValue probe must be
        // squashed to zero or it would suppress every future event.
        auto ResyncToLatest = [&]() -> plUInt64 {
          const plUInt64 uiLatest = pWaterModule->CopyInteractionsSince(plMath::MaxValue<plUInt64>(), interactions);
          interactions.Clear();
          return uiLatest == plMath::MaxValue<plUInt64>() ? 0 : uiLatest;
        };

        if (m_pInteractionWorld != view.GetWorld() || uiSimulationTick < m_uiLastInteractionSimulationTick)
        {
          m_pInteractionWorld = view.GetWorld();
          m_uiLastInteractionSequence = ResyncToLatest();
        }

        plUInt64 uiLatestSequence = pWaterModule->CopyInteractionsSince(m_uiLastInteractionSequence, interactions);
        if (uiLatestSequence < m_uiLastInteractionSequence)
        {
          // Sequence regressed without a detected world/tick change - same epoch handling, same no-replay rule.
          interactions.Clear();
          uiLatestSequence = ResyncToLatest();
        }
        m_uiLastInteractionSequence = uiLatestSequence;
        m_uiLastInteractionSimulationTick = uiSimulationTick;
      }
      const plUInt32 uiCount = plMath::Min(interactions.GetCount(), plWaterFrameData::MaxInteractions);
      const plUInt32 uiSourceOffset = interactions.GetCount() - uiCount;
      pFrameData->m_Interactions.SetCount(uiCount);
      for (plUInt32 i = 0; i < uiCount; ++i)
        pFrameData->m_Interactions[i] = interactions[uiSourceOffset + i];
    }
  }

  float fDominantLightScore = 0.0f;
  const plRenderDataBatchList lightBatches = ref_extractedRenderData.GetRenderDataBatchesWithCategory(plDefaultRenderDataCategories::Light);
  for (plUInt32 uiBatch = 0; uiBatch < lightBatches.GetBatchCount(); ++uiBatch)
  {
    const plRenderDataBatch& batch = lightBatches.GetBatch(uiBatch);
    const plRenderData* pFirst = batch.GetFirstData<plRenderData>();
    if (pFirst == nullptr || !pFirst->IsInstanceOf<plDirectionalLightRenderData>())
      continue;
    for (auto it = batch.GetIterator<plDirectionalLightRenderData>(); it.IsValid(); ++it)
    {
      // Score by plain intensity: gating on volumetric intensity made suns without a volumetric term
      // invisible to the water systems (no surface lighting, no underwater shafts direction).
      // Match clustered-light and screen-space-shadow selection so the caustic direction, radiance and
      // visibility texture always refer to the same dominant sun.
      const float fScore = it->m_fIntensity * plColor(it->m_LightColor).GetLuminance();
      if (fScore <= fDominantLightScore)
        continue;

      fDominantLightScore = fScore;
      pFrameData->m_vMainLightDirection = it->m_vDirection;
      pFrameData->m_vMainLightDirection.NormalizeIfNotZero(plVec3(0.3f, 0.2f, 0.93f)).IgnoreResult();
      // Directional-light intensity is authored in physical scene-lighting units. The underwater composite is
      // already operating on the lit HDR scene, so injecting that raw value a second time can add tens or hundreds
      // of HDR units per pixel. Normalize around the engine's default intensity and keep a conservative HDR ceiling.
      const float fUnderwaterLightScale = plMath::Clamp(it->m_fIntensity / 10.0f, 0.0f, 4.0f);
      const plColor mainLightColor = it->m_LightColor;
      pFrameData->m_MainLightColor = mainLightColor * fUnderwaterLightScale;
      // The surface tint is lit analytically and needs the unscaled physical value.
      // Render data stores linear RGB as normalized bytes, so convert it to float before applying intensity.
      // Reading the byte members directly makes a white sun 255 times too bright and sends the water into bloom.
      pFrameData->m_vMainLightColorIntensity = plVec3(mainLightColor.r, mainLightColor.g, mainLightColor.b) * it->m_fIntensity;
    }
  }

  ref_extractedRenderData.AddFrameData(pFrameData);
}

plResult plWaterFrameDataExtractor::Serialize(plStreamWriter& inout_stream) const
{
  return SUPER::Serialize(inout_stream);
}

plResult plWaterFrameDataExtractor::Deserialize(plStreamReader& inout_stream)
{
  PL_SUCCEED_OR_RETURN(SUPER::Deserialize(inout_stream));
  const plUInt32 uiVersion = plTypeVersionReadContext::GetContext()->GetTypeVersion(GetStaticRTTI());
  PL_IGNORE_UNUSED(uiVersion);
  return PL_SUCCESS;
}

PL_STATICLINK_FILE(WaterPlugin, WaterPlugin_Rendering_Implementation_WaterFrameDataExtractor);
