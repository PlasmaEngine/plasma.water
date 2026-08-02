#pragma once

#include <EditorFramework/Assets/SimpleAssetDocument.h>
#include <WaterPlugin/Resources/WaterProfileResource.h>

class plWaterProfileAssetDocument : public plSimpleAssetDocument<plWaterProfileResourceDescriptor>
{
  PL_ADD_DYNAMIC_REFLECTION(plWaterProfileAssetDocument, plSimpleAssetDocument<plWaterProfileResourceDescriptor>);

public:
  plWaterProfileAssetDocument(plStringView sDocumentPath);

protected:
  virtual void InitializeAfterLoading(bool bFirstTimeCreation) override;
  virtual plTransformStatus InternalTransformAsset(plStreamWriter& stream, plStringView sOutputTag, const plPlatformProfile* pAssetProfile,
    const plAssetFileHeader& assetHeader, plBitflags<plTransformFlags> transformFlags) override;
  virtual plTransformStatus InternalCreateThumbnail(const ThumbnailInfo& thumbnailInfo) override;

private:
  bool m_bDescriptorAbiCompatible = true;
};
