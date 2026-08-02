#include <WaterPlugin/WaterPluginPCH.h>

#include <Core/ResourceManager/ResourceManager.h>
#include <Foundation/Configuration/Startup.h>
#include <WaterPlugin/Resources/WaterProfileResource.h>

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
  }

  ON_CORESYSTEMS_SHUTDOWN
  {
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
