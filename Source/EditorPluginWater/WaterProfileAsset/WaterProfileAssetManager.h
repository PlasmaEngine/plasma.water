#pragma once

#include <EditorFramework/Assets/AssetDocumentManager.h>

class plWaterProfileAssetDocumentManager : public plAssetDocumentManager
{
  PL_ADD_DYNAMIC_REFLECTION(plWaterProfileAssetDocumentManager, plAssetDocumentManager);

public:
  plWaterProfileAssetDocumentManager();
  ~plWaterProfileAssetDocumentManager();

  virtual OutputReliability GetAssetTypeOutputReliability() const override { return OutputReliability::Perfect; }

private:
  void OnDocumentManagerEvent(const plDocumentManager::Event& e);

  virtual void InternalCreateDocument(plStringView sDocumentTypeName, plStringView sPath, bool bCreateNewDocument, plDocument*& out_pDocument,
    const plDocumentObject* pOpenContext) override;
  virtual void InternalGetSupportedDocumentTypes(plDynamicArray<const plDocumentTypeDescriptor*>& inout_documentTypes) const override;
  virtual bool GeneratesProfileSpecificAssets() const override { return false; }

  plAssetDocumentTypeDescriptor m_DocTypeDesc;
};
