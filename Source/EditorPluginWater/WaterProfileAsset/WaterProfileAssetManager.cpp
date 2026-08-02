#include <EditorPluginWater/EditorPluginWaterPCH.h>

#include <EditorPluginWater/WaterProfileAsset/WaterProfileAsset.h>
#include <EditorPluginWater/WaterProfileAsset/WaterProfileAssetManager.h>
#include <EditorPluginWater/WaterProfileAsset/WaterProfileAssetWindow.moc.h>

#include <GuiFoundation/UIServices/ImageCache.moc.h>

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plWaterProfileAssetDocumentManager, 1, plRTTIDefaultAllocator<plWaterProfileAssetDocumentManager>)
PL_END_DYNAMIC_REFLECTED_TYPE;

plWaterProfileAssetDocumentManager::plWaterProfileAssetDocumentManager()
{
  plDocumentManager::s_Events.AddEventHandler(plMakeDelegate(&plWaterProfileAssetDocumentManager::OnDocumentManagerEvent, this));

  m_DocTypeDesc.m_sDocumentTypeName = "WaterProfile";
  m_DocTypeDesc.m_sFileExtension = "plWaterProfileAsset";
  m_DocTypeDesc.m_sIcon = ":/AssetIcons/WaterProfile.svg";
  m_DocTypeDesc.m_sAssetCategory = "Rendering/Water";
  m_DocTypeDesc.m_pDocumentType = plGetStaticRTTI<plWaterProfileAssetDocument>();
  m_DocTypeDesc.m_pManager = this;
  m_DocTypeDesc.m_CompatibleTypes.PushBack("CompatibleAsset_WaterProfile");
  m_DocTypeDesc.m_sResourceFileExtension = "plBinWaterProfile";
  m_DocTypeDesc.m_AssetDocumentFlags = plAssetDocumentFlags::AutoTransformOnSave | plAssetDocumentFlags::SupportsThumbnail;

  plQtImageCache::GetSingleton()->RegisterTypeImage("WaterProfile", QPixmap(":/AssetIcons/WaterProfile.svg"));
}

plWaterProfileAssetDocumentManager::~plWaterProfileAssetDocumentManager()
{
  plDocumentManager::s_Events.RemoveEventHandler(plMakeDelegate(&plWaterProfileAssetDocumentManager::OnDocumentManagerEvent, this));
}

void plWaterProfileAssetDocumentManager::OnDocumentManagerEvent(const plDocumentManager::Event& e)
{
  if (e.m_Type == plDocumentManager::Event::Type::DocumentWindowRequested &&
      e.m_pDocument->GetDynamicRTTI() == plGetStaticRTTI<plWaterProfileAssetDocument>())
  {
    new plQtWaterProfileAssetDocumentWindow(e.m_pDocument); // NOLINT: owned by Qt
  }
}

void plWaterProfileAssetDocumentManager::InternalCreateDocument(plStringView sDocumentTypeName, plStringView sPath, bool bCreateNewDocument,
  plDocument*& out_pDocument, const plDocumentObject* pOpenContext)
{
  out_pDocument = new plWaterProfileAssetDocument(sPath);
}

void plWaterProfileAssetDocumentManager::InternalGetSupportedDocumentTypes(plDynamicArray<const plDocumentTypeDescriptor*>& inout_documentTypes) const
{
  inout_documentTypes.PushBack(&m_DocTypeDesc);
}
