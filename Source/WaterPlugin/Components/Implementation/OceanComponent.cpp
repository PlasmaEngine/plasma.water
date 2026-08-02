#include <WaterPlugin/WaterPluginPCH.h>

#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>
#include <Foundation/Math/Frustum.h>
#include <Foundation/Serialization/AbstractObjectGraph.h>
#include <Foundation/Utilities/GraphicsUtils.h>
#include <RendererCore/Material/MaterialResource.h>
#include <RendererCore/Meshes/MeshBufferUtils.h>
#include <RendererCore/Meshes/MeshComponent.h>
#include <RendererCore/Meshes/MeshResource.h>
#include <RendererCore/Pipeline/View.h>
#include <RendererCore/Pipeline/RenderDataBatch.h>
#include <RendererCore/Pipeline/RenderDataManager.h>
#include <WaterPlugin/Components/OceanComponent.h>
#include <WaterPlugin/World/WaterWorldModule.h>

// clang-format off
PL_BEGIN_COMPONENT_TYPE(plOceanComponent, 3, plComponentMode::Static)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ACCESSOR_PROPERTY("Material", GetMaterialFile, SetMaterialFile)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_Material")),
    PL_ACCESSOR_PROPERTY("WaterLevel", GetWaterLevel, SetWaterLevel)->AddAttributes(new plDefaultValueAttribute(0.0f)),
    PL_ACCESSOR_PROPERTY("TileSize", GetTileSize, SetTileSize)->AddAttributes(new plDefaultValueAttribute(100.0f)),
    PL_ACCESSOR_PROPERTY("TileCountXZ", GetTileCountXZ, SetTileCountXZ)->AddAttributes(new plDefaultValueAttribute(5), new plClampValueAttribute(3, 64)),
    PL_ACCESSOR_PROPERTY("Tessellation", GetTessellation, SetTessellation)->AddAttributes(new plDefaultValueAttribute(plVec2U32(32))),
    PL_ACCESSOR_PROPERTY("TileTesselation", GetTileTesselation, SetTileTesselation)->AddAttributes(new plHiddenAttribute()),
    PL_ACCESSOR_PROPERTY("UpdateThreshold", GetUpdateThreshold, SetUpdateThreshold)->AddAttributes(new plDefaultValueAttribute(50.0f)),
    PL_ACCESSOR_PROPERTY("LodDivisor", GetLodDivisor, SetLodDivisor)->AddAttributes(new plDefaultValueAttribute(2), new plClampValueAttribute(1, 10)),
    PL_ACCESSOR_PROPERTY("LodDistance0", GetLodDistance0, SetLodDistance0)->AddAttributes(new plDefaultValueAttribute(100.0f), new plClampValueAttribute(10.0f, 10000.0f)),
    PL_ACCESSOR_PROPERTY("LodDistance1", GetLodDistance1, SetLodDistance1)->AddAttributes(new plDefaultValueAttribute(250.0f), new plClampValueAttribute(10.0f, 10000.0f)),
    PL_ACCESSOR_PROPERTY("LodDistance2", GetLodDistance2, SetLodDistance2)->AddAttributes(new plDefaultValueAttribute(500.0f), new plClampValueAttribute(10.0f, 10000.0f)),
    PL_ACCESSOR_PROPERTY("LodDistance3", GetLodDistance3, SetLodDistance3)->AddAttributes(new plDefaultValueAttribute(1000.0f), new plClampValueAttribute(10.0f, 10000.0f)),
    PL_ACCESSOR_PROPERTY("WaveAmplitude", GetWaveAmplitude, SetWaveAmplitude)->AddAttributes(new plDefaultValueAttribute(0.28f), new plClampValueAttribute(0.0f, 50.0f)),
  }
  PL_END_PROPERTIES;
  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("Rendering/Water"),
  }
  PL_END_ATTRIBUTES;
  PL_BEGIN_MESSAGEHANDLERS
  {
    PL_MESSAGE_HANDLER(plMsgExtractRenderData, OnMsgExtractRenderData),
  }
  PL_END_MESSAGEHANDLERS;
}
PL_END_COMPONENT_TYPE;
// clang-format on

plOceanComponent::plOceanComponent() = default;

plOceanComponent::~plOceanComponent() = default;

void plOceanComponent::SetWaterLevel(float value)
{
  m_fWaterLevel = value;
  TriggerLocalBoundsUpdate();
  InvalidateLegacyWater();
}

void plOceanComponent::SetTileSize(float value)
{
  m_fTileSize = plMath::Max(value, 10.0f);
  InvalidateMesh();
  InvalidateLegacyWater();
}

void plOceanComponent::SetTileCountXZ(plUInt32 value)
{
  m_uiTileCountXZ = plMath::Clamp(value, 3u, 100u);
  TriggerLocalBoundsUpdate();
  InvalidateLegacyWater();
}

void plOceanComponent::SetTileTesselation(plVec2U32 value)
{
  m_vTileTesselation = value;
  InvalidateMesh();
  InvalidateLegacyWater();
}

void plOceanComponent::SetUpdateThreshold(float value)
{
  m_fUpdateThreshold = plMath::Max(value, 1.0f);
}

void plOceanComponent::SetLodDivisor(plUInt32 value)
{
  m_uiLodDivisor = plMath::Clamp(value, 1u, 10u);
  InvalidateMesh();
}

void plOceanComponent::SetLodDistance0(float value)
{
  m_fLodDistances[0] = plMath::Max(value, 10.0f);
}

void plOceanComponent::SetLodDistance1(float value)
{
  m_fLodDistances[1] = plMath::Max(value, m_fLodDistances[0] + 1.0f);
}

void plOceanComponent::SetLodDistance2(float value)
{
  m_fLodDistances[2] = plMath::Max(value, m_fLodDistances[1] + 1.0f);
}

void plOceanComponent::SetLodDistance3(float value)
{
  m_fLodDistances[3] = plMath::Max(value, m_fLodDistances[2] + 1.0f);
  InvalidateLegacyWater();
}

void plOceanComponent::SetWaveAmplitude(float value)
{
  m_fWaveAmplitude = plMath::Max(value, 0.0f);
  TriggerLocalBoundsUpdate();
  InvalidateLegacyWater();
}

void plOceanComponent::SerializeComponent(plWorldWriter& stream) const
{
  SUPER::SerializeComponent(stream);
  plStreamWriter& s = stream.GetStream();

  s << m_hMaterial;
  s << m_fWaterLevel;
  s << m_fTileSize;
  s << m_uiTileCountXZ;
  s << m_vTileTesselation;
  s << m_fUpdateThreshold;
  s << m_uiLodDivisor;
  s << m_fLodDistances[0];
  s << m_fLodDistances[1];
  s << m_fLodDistances[2];
  s << m_fLodDistances[3];
  s << m_fWaveAmplitude;
}

void plOceanComponent::DeserializeComponent(plWorldReader& stream)
{
  SUPER::DeserializeComponent(stream);
  const plUInt32 uiVersion = stream.GetComponentTypeVersion(GetStaticRTTI());
  plStreamReader& s = stream.GetStream();

  s >> m_hMaterial;
  s >> m_fWaterLevel;
  s >> m_fTileSize;
  s >> m_uiTileCountXZ;
  s >> m_vTileTesselation;
  s >> m_fUpdateThreshold;
  s >> m_uiLodDivisor;
  s >> m_fLodDistances[0];
  s >> m_fLodDistances[1];
  s >> m_fLodDistances[2];
  s >> m_fLodDistances[3];

  if (uiVersion >= 2)
  {
    s >> m_fWaveAmplitude;
  }
}

void plOceanComponent::OnActivated()
{
  GenerateAllLodMeshes();

  SUPER::OnActivated();

  m_LegacyBodyId = GetWorld()->GetOrCreateModule<plWaterWorldModule>()->RegisterLegacyOcean(this);
}

void plOceanComponent::OnDeactivated()
{
  if (plWaterWorldModule* pModule = GetWorld()->GetModule<plWaterWorldModule>())
    pModule->UnregisterBody(m_LegacyBodyId);

  SUPER::OnDeactivated();
}

plResult plOceanComponent::GetLocalBounds(plBoundingBoxSphere& bounds, bool& bAlwaysVisible, plMsgUpdateLocalBounds& msg)
{
  // Ocean is infinite, always visible
  bAlwaysVisible = true;

  // Set large bounds for culling purposes
  const float halfSize = m_fTileSize * m_uiTileCountXZ * 0.5f;
  const float maxWaveHeight = 5.0f;
  bounds = plBoundingBoxSphere::MakeFromBox(plBoundingBox::MakeFromMinMax(
    plVec3(-halfSize, -halfSize, m_fWaterLevel - maxWaveHeight),
    plVec3(halfSize, halfSize, m_fWaterLevel + maxWaveHeight)));

  return PL_SUCCESS;
}

void plOceanComponent::OnMsgExtractRenderData(plMsgExtractRenderData& msg) const
{
  // Clean up instance data from previous frame to prevent memory leak
  for (auto& offset : m_PreviousFrameInstanceData)
  {
    msg.m_pRenderDataManager->DeleteInstanceData(offset);
  }
  m_PreviousFrameInstanceData.Clear();

  // Check if we have valid meshes
  if (!m_hTileMeshes[0].IsValid())
    return;

  // Get camera position for tile positioning
  plVec3 vCameraPos = msg.m_pView->GetCullingCamera()->GetPosition();

  // Compute frustum for culling
  plFrustum frustum;
  msg.m_pView->ComputeCullingFrustum(frustum);

  // Snap camera position to tile grid
  const float fTileSize = m_fTileSize;
  const plInt32 iCenterTileX = plInt32(plMath::Floor(vCameraPos.x / fTileSize));
  const plInt32 iCenterTileY = plInt32(plMath::Floor(vCameraPos.y / fTileSize));

  const plInt32 iHalfCount = plInt32(m_uiTileCountXZ / 2);
  // Use wave amplitude with some extra margin for bounds
  const float maxWaveHeight = m_fWaveAmplitude * 1.5f;

  // Render tiles in a grid around the camera
  for (plInt32 y = -iHalfCount; y <= iHalfCount; ++y)
  {
    for (plInt32 x = -iHalfCount; x <= iHalfCount; ++x)
    {
      const plInt32 iTileX = iCenterTileX + x;
      const plInt32 iTileY = iCenterTileY + y;

      // Calculate tile world bounds for frustum culling
      plVec3 vTileMin(iTileX * fTileSize, iTileY * fTileSize, m_fWaterLevel - maxWaveHeight);
      plVec3 vTileMax((iTileX + 1) * fTileSize, (iTileY + 1) * fTileSize, m_fWaterLevel + maxWaveHeight);
      plBoundingBox tileBounds = plBoundingBox::MakeFromMinMax(vTileMin, vTileMax);

      // Frustum culling - skip tiles outside the view frustum
      if (frustum.GetObjectPosition(tileBounds) == plVolumePosition::Outside)
        continue;

      // Calculate tile world position (center of tile for distance calculation)
      plVec3 vTileCenter;
      vTileCenter.x = iTileX * fTileSize + fTileSize * 0.5f;
      vTileCenter.y = iTileY * fTileSize + fTileSize * 0.5f;
      vTileCenter.z = m_fWaterLevel;

      // Calculate distance from camera to tile center
      float fDistanceToTile = (vTileCenter - vCameraPos).GetLength();

      // Select LOD based on distance
      plUInt32 uiLodLevel = GetLodLevelForDistance(fDistanceToTile);
      plMeshResourceHandle hMesh = m_hTileMeshes[uiLodLevel];

      if (!hMesh.IsValid())
        continue;

      plResourceLock<plMeshResource> pMesh(hMesh, plResourceAcquireMode::AllowLoadingFallback);
      plArrayPtr<const plMeshResourceDescriptor::SubMesh> parts = pMesh->GetSubMeshes();

      // Tile corner position (not center)
      plVec3 vTilePos;
      vTilePos.x = iTileX * fTileSize;
      vTilePos.y = iTileY * fTileSize;
      vTilePos.z = m_fWaterLevel;

      plTransform tileTransform;
      tileTransform.SetIdentity();
      tileTransform.m_vPosition = vTilePos;

      // Create render data for this tile
      for (plUInt32 uiPartIndex = 0; uiPartIndex < parts.GetCount(); ++uiPartIndex)
      {
        const plUInt32 uiMaterialIndex = parts[uiPartIndex].m_uiMaterialIndex;
        plMaterialResourceHandle hMaterial = m_hMaterial.IsValid() ? m_hMaterial : pMesh->GetMaterials()[uiMaterialIndex];

        // Create instance data for this tile (each tile is dynamic, so no caching)
        plInstanceDataOffset instanceDataOffset;
        const bool bDynamic = true; // Ocean tiles are always dynamic
        auto hInstanceDataBuffer = msg.m_pRenderDataManager->GetOrCreateInstanceDataAndFill(*this, bDynamic, tileTransform, instanceDataOffset, GetUniqueIdForRendering());

        // Track this offset so we can delete it next frame
        m_PreviousFrameInstanceData.PushBack(instanceDataOffset);

        plMeshRenderData* pRenderData = msg.m_pRenderDataManager->CreateRenderDataForThisFrame<plMeshRenderData>(GetOwner());
        pRenderData->SetFallbackGlobalBounds(plBoundingBox::MakeFromMinMax(vTileMin, vTileMax));
        pRenderData->Fill(instanceDataOffset, hInstanceDataBuffer, hMaterial, hMesh, uiMaterialIndex, uiPartIndex);

        bool bDontCacheYet = false;
        plRenderData::Category category = plMaterialResource::GetRenderDataCategory(hMaterial, &bDontCacheYet);

        // Don't cache ocean tiles (they're dynamic)
        msg.AddRenderData(pRenderData, category, plRenderData::Caching::Never);
      }
    }
  }
}

void plOceanComponent::SetMaterialFile(const char* szFile)
{
  if (!plStringUtils::IsNullOrEmpty(szFile))
  {
    m_hMaterial = plResourceManager::LoadResource<plMaterialResource>(szFile);
  }
  else
  {
    m_hMaterial.Invalidate();
  }

  InvalidateLegacyWater();
}

const char* plOceanComponent::GetMaterialFile() const
{
  if (!m_hMaterial.IsValid())
    return "";

  return m_hMaterial.GetResourceID();
}

void plOceanComponent::SetMaterial(const plMaterialResourceHandle& hMaterial)
{
  m_hMaterial = hMaterial;
  InvalidateLegacyWater();
}

void plOceanComponent::InvalidateMesh()
{
  for (plUInt32 i = 0; i < s_uiNumLodLevels; ++i)
  {
    m_hTileMeshes[i].Invalidate();
  }
  GenerateAllLodMeshes();
  TriggerLocalBoundsUpdate();
}

void plOceanComponent::InvalidateLegacyWater()
{
  if (!IsActiveAndInitialized())
    return;

  plWaterWorldModule* pModule = GetWorld()->GetOrCreateModule<plWaterWorldModule>();
  if (!m_LegacyBodyId.IsValid())
    m_LegacyBodyId = pModule->RegisterLegacyOcean(this);
  else if (!pModule->UpdateLegacyOcean(m_LegacyBodyId, this))
    m_LegacyBodyId = pModule->RegisterLegacyOcean(this);
}

plResult plOceanComponent::BuildTileMeshDescriptor(plMeshResourceDescriptor& desc, plUInt32 uiLodLevel) const
{
  PL_PROFILE_SCOPE("Ocean: GenerateTileMesh");

  // OceanSurface fallback material 
  // Data/Base/Materials/Water/OceanSurface.plMaterialAsset
  desc.SetMaterial(0, "{ 015ee5bf-cc73-44d9-a85a-896a2314bc9b }");

  // Standard precision is fine
  desc.MeshBufferDesc().AddCommonStreams();

  {
    auto& mb = desc.MeshBufferDesc();

    // Calculate tessellation based on LOD level
    // LOD 0 = full tessellation, each subsequent LOD divides by m_uiLodDivisor
    plUInt32 uiLodDivisor = 1;
    for (plUInt32 i = 0; i < uiLodLevel; ++i)
      uiLodDivisor *= m_uiLodDivisor;  // Apply divisor for each LOD level
    
    plUInt32 uiBaseTessX = plMath::Clamp(m_vTileTesselation.x, 8u, 128u);
    plUInt32 uiBaseTessY = plMath::Clamp(m_vTileTesselation.y, 8u, 128u);
    
    plUInt32 uiNumVerticesX = plMath::Max(uiBaseTessX / uiLodDivisor + 1u, 3u);
    plUInt32 uiNumVerticesY = plMath::Max(uiBaseTessY / uiLodDivisor + 1u, 3u);
    const plUInt32 uiNumTriangles = (uiNumVerticesX - 1) * (uiNumVerticesY - 1) * 2;

    mb.AllocateStreams(uiNumVerticesX * uiNumVerticesY, plGALPrimitiveTopology::Triangles, uiNumTriangles, true);

    const float fTileSize = m_fTileSize;
    const plVec2 vToNDC = plVec2(1.0f / (uiNumVerticesX - 1), 1.0f / (uiNumVerticesY - 1));

    plUInt32 uiVertexIdx = 0;

    // Generate a single tile mesh using the simple SetXXX API
    for (plUInt32 y = 0; y < uiNumVerticesY; ++y)
    {
      for (plUInt32 x = 0; x < uiNumVerticesX; ++x)
      {
        const plVec2 ndc = plVec2((float)x, (float)y).CompMul(vToNDC);
        
        // Position in tile-local space (XY plane, Z=0)
        mb.SetPosition(uiVertexIdx, plVec3(ndc.x * fTileSize, ndc.y * fTileSize, 0.0f));
        
        // Texture coordinates 0-1
        mb.SetTexCoord0(uiVertexIdx, ndc);
        
        // Z-up normal for flat water surface
        mb.SetNormal(uiVertexIdx, plVec3(0, 0, 1));
        
        // Tangent in X direction with positive bitangent sign
        mb.SetTangent(uiVertexIdx, plVec4(1, 0, 0, 1));

        ++uiVertexIdx;
      }
    }

    // Calculate bounds (Z is vertical)
    const float maxWaveHeight = 5.0f;
    plBoundingBox bbox = plBoundingBox::MakeFromMinMax(
      plVec3(0, 0, -maxWaveHeight),
      plVec3(fTileSize, fTileSize, maxWaveHeight));
    desc.SetBounds(plBoundingBoxSphere::MakeFromBox(bbox));

    plUInt32 uiTriangleIdx = 0;
    uiVertexIdx = 0;

    for (plUInt32 y = 0; y < uiNumVerticesY - 1; ++y)
    {
      for (plUInt32 x = 0; x < uiNumVerticesX - 1; ++x)
      {
        mb.SetTriangleIndices(uiTriangleIdx + 0, uiVertexIdx, uiVertexIdx + 1, uiVertexIdx + uiNumVerticesX);
        mb.SetTriangleIndices(uiTriangleIdx + 1, uiVertexIdx + 1, uiVertexIdx + uiNumVerticesX + 1, uiVertexIdx + uiNumVerticesX);
        uiTriangleIdx += 2;

        ++uiVertexIdx;
      }

      ++uiVertexIdx;
    }
  }

  desc.AddSubMesh(desc.MeshBufferDesc().GetPrimitiveCount(), 0, 0);
  return PL_SUCCESS;
}

plMeshResourceHandle plOceanComponent::GenerateTileMesh(plUInt32 uiLodLevel) const
{
  plStringBuilder sResourceName;

  {
    plUInt64 uiSettingsHash = m_uiMeshChangeCounter;
    uiSettingsHash = plHashingUtils::xxHash64(&m_fTileSize, sizeof(m_fTileSize), uiSettingsHash);
    uiSettingsHash = plHashingUtils::xxHash64(&m_vTileTesselation, sizeof(m_vTileTesselation), uiSettingsHash);
    uiSettingsHash = plHashingUtils::xxHash64(&m_uiLodDivisor, sizeof(m_uiLodDivisor), uiSettingsHash);
    uiSettingsHash = plHashingUtils::xxHash64(&uiLodLevel, sizeof(uiLodLevel), uiSettingsHash);

    sResourceName.SetFormat("OceanTile:{}:LOD{}", uiSettingsHash, uiLodLevel);

    plMeshResourceHandle hResource = plResourceManager::GetExistingResource<plMeshResource>(sResourceName);
    if (hResource.IsValid())
      return hResource;
  }

  plMeshResourceDescriptor desc;
  if (BuildTileMeshDescriptor(desc, uiLodLevel).Succeeded())
  {
    return plResourceManager::CreateResource<plMeshResource>(sResourceName, std::move(desc), sResourceName);
  }

  return plMeshResourceHandle();
}

void plOceanComponent::GenerateAllLodMeshes()
{
  for (plUInt32 i = 0; i < s_uiNumLodLevels; ++i)
  {
    if (!m_hTileMeshes[i].IsValid())
    {
      m_hTileMeshes[i] = GenerateTileMesh(i);
    }
  }
}

plUInt32 plOceanComponent::GetLodLevelForDistance(float fDistance) const
{
  // Find the appropriate LOD level based on distance
  for (plUInt32 i = 0; i < s_uiNumLodLevels; ++i)
  {
    if (fDistance < m_fLodDistances[i])
    {
      return i;
    }
  }
  return s_uiNumLodLevels - 1;  // Return lowest LOD for very distant tiles
}

//////////////////////////////////////////////////////////////////////////

plOceanComponentManager::plOceanComponentManager(plWorld* pWorld)
  : plComponentManager<ComponentType, plBlockStorageType::Compact>(pWorld)
{
}

plOceanComponentManager::~plOceanComponentManager() = default;

void plOceanComponentManager::Initialize()
{
  // No update function needed - ocean tiles are rendered in OnMsgExtractRenderData
}

PL_STATICLINK_FILE(WaterPlugin, WaterPlugin_Components_Implementation_OceanComponent);

//////////////////////////////////////////////////////////////////////////

#include <Foundation/Serialization/GraphPatch.h>

class plOceanComponentPatch_2_3 final : public plGraphPatch
{
public:
  plOceanComponentPatch_2_3()
    : plGraphPatch("plOceanComponent", 3)
  {
  }

  virtual void Patch(plGraphPatchContext& ref_context, plAbstractObjectGraph* pGraph, plAbstractObjectNode* pNode) const override
  {
    PL_IGNORE_UNUSED(ref_context);
    PL_IGNORE_UNUSED(pGraph);
    pNode->RenameProperty("TileTesselation", "Tessellation");
  }
};

plOceanComponentPatch_2_3 g_plOceanComponentPatch_2_3;
