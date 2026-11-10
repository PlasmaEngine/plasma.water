#include <WaterPlugin/WaterPluginPCH.h>

#include <Core/ResourceManager/ResourceManager.h>
#include <Foundation/Configuration/Startup.h>
#include <RendererCore/Pipeline/RenderPipelineInjection.h>
#include <WaterPlugin/Rendering/WaterFrameDataExtractor.h>
#include <WaterPlugin/Rendering/WaterSurfacePass.h>
#include <WaterPlugin/Rendering/WaterUnderwaterPass.h>
#include <WaterPlugin/Resources/WaterProfileResource.h>

namespace
{
  const char* const s_szInjectionOwner = "plasma.water";

  /// Water needs two passes in every pipeline that draws transparent objects. The engine places them and keeps the
  /// pipeline working if one of them cannot be placed or fails. A pipeline whose asset already holds a water pass
  /// keeps its own.
  void RegisterPipelineInjections()
  {
    // In front of the transparent pass: reads the opaque scene, draws the surface into colour.
    plRenderPipelineInjectionDesc surface;
    surface.m_sName.Assign("plasma.water/Surface");
    surface.m_sOwner.Assign(s_szInjectionOwner);
    surface.m_sPoint.Assign(plRenderPipelineInjectionPoints::BeforeTransparent);
    surface.m_pPassType = plGetStaticRTTI<plWaterSurfacePass>();
    surface.Bind("Color", "Color", "Color");
    surface.Bind("DepthStencil", "DepthStencil", "DepthStencil");
    surface.Read("ResolvedDepth", "ResolvedDepth");
    surface.Read("GBuffer1", "GBuffer1", false); // Normals for the caustics. Forward-only pipelines have none.
    surface.Publish("plasma.water/ExitDepth", "WaterExitDepth");
    surface.m_ExtractorTypes.PushBack(plGetStaticRTTI<plWaterFrameDataExtractor>());
    plRenderPipelineInjections::Register(surface).IgnoreResult();

    // Behind the transparent pass, on resolved colour: fog, distortion and shafts while the camera is under water.
    plRenderPipelineInjectionDesc underwater;
    underwater.m_sName.Assign("plasma.water/Underwater");
    underwater.m_sOwner.Assign(s_szInjectionOwner);
    underwater.m_sPoint.Assign(plRenderPipelineInjectionPoints::AfterTransparentResolved);
    underwater.m_pPassType = plGetStaticRTTI<plWaterUnderwaterPass>();
    underwater.Bind("Color", "Color", "Output");
    underwater.Read("ResolvedDepth", "Depth");
    underwater.Read("plasma.water/ExitDepth", "WaterExitDepth", false); // The pass falls back to scene depth.
    plRenderPipelineInjections::Register(underwater).IgnoreResult();
  }
} // namespace

// clang-format off
PL_BEGIN_SUBSYSTEM_DECLARATION(Rendering, WaterPlugin)

  BEGIN_SUBSYSTEM_DEPENDENCIES
    "Foundation",
    "Core",
    "RendererCore"
  END_SUBSYSTEM_DEPENDENCIES

  ON_CORESYSTEMS_STARTUP
  {
    plResourceManager::RegisterResourceForAssetType("WaterProfile", plGetStaticRTTI<plWaterProfileResource>());

    plWaterProfileResourceDescriptor descriptor;
    plWaterProfileResourceHandle hFallback = plResourceManager::CreateResource<plWaterProfileResource>(
      "WaterProfileMissing", std::move(descriptor), "Fallback for missing water profiles");
    plResourceManager::SetResourceTypeMissingFallback<plWaterProfileResource>(hFallback);

    RegisterPipelineInjections();
  }

  ON_CORESYSTEMS_SHUTDOWN
  {
    plRenderPipelineInjections::UnregisterOwner(s_szInjectionOwner);

    plResourceManager::SetResourceTypeMissingFallback<plWaterProfileResource>(plWaterProfileResourceHandle());
    plWaterProfileResource::CleanupDynamicPluginReferences();
  }

  ON_HIGHLEVELSYSTEMS_STARTUP
  {
  }

  ON_HIGHLEVELSYSTEMS_SHUTDOWN
  {
  }

PL_END_SUBSYSTEM_DECLARATION;
// clang-format on

PL_STATICLINK_FILE(WaterPlugin, WaterPlugin_Startup);
