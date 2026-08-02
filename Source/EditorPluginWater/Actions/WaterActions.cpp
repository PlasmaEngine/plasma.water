#include <EditorPluginWater/EditorPluginWaterPCH.h>

#include <EditorPluginWater/Actions/WaterActions.h>
#include <EditorPluginWater/Preferences/WaterEditorPreferences.h>
#include <EditorPluginWater/Validation/WaterValidation.h>

#include <EditorFramework/Document/GameObjectDocument.h>
#include <EditorFramework/Assets/AssetCurator.h>
#include <EditorFramework/Preferences/Preferences.h>
#include <Foundation/IO/FileSystem/FileReader.h>
#include <Foundation/Reflection/ReflectionUtils.h>
#include <Foundation/Utilities/ConversionUtils.h>
#include <GameEngine/Configuration/RendererProfileConfigs.h>
#include <GuiFoundation/Action/ActionMapManager.h>
#include <GuiFoundation/UIServices/UIServices.moc.h>
#include <ToolsFoundation/Object/ObjectAccessorBase.h>
#include <ToolsFoundation/Selection/SelectionManager.h>

namespace
{
  enum class WaterType : plInt64
  {
    Ocean = 0,
    River = 1,
    Lake = 2
  };

  static bool IsType(const plDocumentObject* pObject, plStringView sTypeName)
  {
    return pObject != nullptr && pObject->GetType()->GetTypeName() == sTypeName;
  }

  static const plDocumentObject* FindOwner(const plDocumentObject* pObject)
  {
    while (pObject != nullptr && !IsType(pObject, "plGameObject"))
      pObject = pObject->GetParent();
    return pObject;
  }

  static void GetComponents(const plDocumentObject* pOwner, plDynamicArray<const plDocumentObject*>& out_components)
  {
    out_components.Clear();
    if (pOwner == nullptr)
      return;

    plVariantArray componentGuids;
    if (!pOwner->GetTypeAccessor().GetValues("Components", componentGuids))
      return;

    for (const plVariant& value : componentGuids)
    {
      if (!value.IsA<plUuid>())
        continue;

      if (const plDocumentObject* pComponent = pOwner->GetDocumentObjectManager()->GetObject(value.Get<plUuid>()))
        out_components.PushBack(pComponent);
    }
  }

  static const plDocumentObject* FindComponent(const plDocumentObject* pOwner, plStringView sTypeName)
  {
    plDynamicArray<const plDocumentObject*> components;
    GetComponents(pOwner, components);
    for (const plDocumentObject* pComponent : components)
    {
      if (IsType(pComponent, sTypeName))
        return pComponent;
    }
    return nullptr;
  }

  static const plDocumentObject* FindWaterBodyOwner(const plDocumentObject* pObject)
  {
    for (const plDocumentObject* pCurrent = pObject; pCurrent != nullptr; pCurrent = pCurrent->GetParent())
    {
      if (IsType(pCurrent, "plWaterBodyComponent"))
        return FindOwner(pCurrent);

      if (IsType(pCurrent, "plGameObject") && FindComponent(pCurrent, "plWaterBodyComponent") != nullptr)
        return pCurrent;
    }
    return nullptr;
  }

  static const plDocumentObject* FindChildByName(const plDocumentObject* pOwner, plStringView sName)
  {
    if (pOwner == nullptr)
      return nullptr;

    for (const plDocumentObject* pChild : pOwner->GetChildren())
    {
      if (!IsType(pChild, "plGameObject"))
        continue;

      const plVariant name = pChild->GetTypeAccessor().GetValue("Name");
      if (name.IsA<plString>() && name.Get<plString>() == sName)
        return pChild;
    }
    return nullptr;
  }

  static bool SetValueIfPresent(plObjectAccessorBase* pAccessor, const plDocumentObject* pObject, plStringView sProperty, const plVariant& value)
  {
    if (pObject == nullptr || pObject->GetType()->FindPropertyByName(sProperty) == nullptr)
      return true;
    return pAccessor->SetValueByName(pObject, sProperty, value).Succeeded();
  }

  static plStatus AddComponent(plObjectAccessorBase* pAccessor, const plDocumentObject* pOwner, const char* szTypeName, plUuid& out_guid)
  {
    const plRTTI* pType = plRTTI::FindTypeByName(szTypeName);
    if (pType == nullptr)
      return plStatus(PL_FAILURE);
    return pAccessor->AddObjectByName(pOwner, "Components", -1, pType, out_guid);
  }

  static plStatus CreateGameObject(plObjectAccessorBase* pAccessor, const plDocumentObject* pParent, plStringView sName, const plVec3& vLocalPosition, plUuid& out_guid)
  {
    const plRTTI* pGameObjectType = plRTTI::FindTypeByName("plGameObject");
    if (pGameObjectType == nullptr)
      return plStatus(PL_FAILURE);

    if (pAccessor->AddObjectByName(pParent, "Children", -1, pGameObjectType, out_guid).Failed())
      return plStatus(PL_FAILURE);

    const plDocumentObject* pObject = pAccessor->GetObject(out_guid);
    if (pObject == nullptr || pAccessor->SetValueByName(pObject, "Name", plVariant(sName)).Failed() ||
        pAccessor->SetValueByName(pObject, "LocalPosition", vLocalPosition).Failed())
      return plStatus(PL_FAILURE);

    return plStatus(PL_SUCCESS);
  }

  static plVec3 GetPlacementPosition(plGameObjectDocument* pDocument)
  {
    if (pDocument == nullptr)
      return plVec3::MakeZero();

    const auto& selection = pDocument->GetSelectionManager()->GetSelection();
    if (!selection.IsEmpty())
    {
      if (const plDocumentObject* pOwner = FindOwner(selection.PeekBack()))
        return pDocument->GetGlobalTransform(pOwner).m_vPosition;
    }
    return plVec3::MakeZero();
  }

  static plStatus AddSplineNode(plObjectAccessorBase* pAccessor, const plDocumentObject* pBodyOwner, const plDocumentObject* pSpline,
    plUInt32 uiIndex, const plVec3& vPosition, bool bAddWaterMetadata)
  {
    plStringBuilder sNodeName;
    sNodeName.SetFormat("Water Point {}", uiIndex + 1);

    plUuid nodeGuid;
    if (CreateGameObject(pAccessor, pBodyOwner, sNodeName, vPosition, nodeGuid).Failed())
      return plStatus(PL_FAILURE);

    const plDocumentObject* pNode = pAccessor->GetObject(nodeGuid);
    plUuid splineNodeGuid;
    if (AddComponent(pAccessor, pNode, "plSplineNodeComponent", splineNodeGuid).Failed())
      return plStatus(PL_FAILURE);

    if (bAddWaterMetadata)
    {
      plUuid pointGuid;
      if (AddComponent(pAccessor, pNode, "plWaterSplinePointComponent", pointGuid).Failed())
        return plStatus(PL_FAILURE);

      const plDocumentObject* pPoint = pAccessor->GetObject(pointGuid);
      if (!SetValueIfPresent(pAccessor, pPoint, "Order", uiIndex) || !SetValueIfPresent(pAccessor, pPoint, "LeftWidth", 5.0f) ||
          !SetValueIfPresent(pAccessor, pPoint, "RightWidth", 5.0f) ||
          !SetValueIfPresent(pAccessor, pPoint, "Depth", 2.0f) || !SetValueIfPresent(pAccessor, pPoint, "FlowScale", 1.0f))
        return plStatus(PL_FAILURE);
    }

    plHashedString nodeName;
    nodeName.Assign(sNodeName);
    return pAccessor->InsertValueByName(pSpline, "Nodes", nodeName, uiIndex);
  }

  static bool EnsureRuntimeTypes(bool bNeedsSpline)
  {
    if (plRTTI::FindTypeByName("plWaterBodyComponent") == nullptr)
    {
      plQtUiServices::GetSingleton()->MessageBoxWarning("The Water runtime plugin is not loaded, so water components cannot be created.");
      return false;
    }
    if (bNeedsSpline && (plRTTI::FindTypeByName("plSplineComponent") == nullptr || plRTTI::FindTypeByName("plSplineNodeComponent") == nullptr ||
                           plRTTI::FindTypeByName("plWaterSplinePointComponent") == nullptr))
    {
      plQtUiServices::GetSingleton()->MessageBoxWarning("The Water spline authoring types are not loaded.");
      return false;
    }
    return true;
  }

  static void CreateWaterBody(plGameObjectDocument* pDocument, WaterType type)
  {
    const bool bNeedsSpline = type != WaterType::Ocean;
    if (pDocument == nullptr || !EnsureRuntimeTypes(bNeedsSpline))
      return;

    const char* szName = type == WaterType::Ocean ? "Ocean" : (type == WaterType::River ? "River" : "Lake");
    plObjectAccessorBase* pAccessor = pDocument->GetObjectAccessor();
    plStringBuilder sTransactionName;
    sTransactionName.SetFormat("Create {} Water Body", szName);
    pAccessor->StartTransaction(sTransactionName);

    plUuid objectGuid;
    if (CreateGameObject(pAccessor, pDocument->GetObjectManager()->GetRootObject(), szName, GetPlacementPosition(pDocument), objectGuid).Failed())
    {
      pAccessor->CancelTransaction();
      return;
    }

    const plDocumentObject* pOwner = pAccessor->GetObject(objectGuid);
    plUuid bodyGuid;
    if (AddComponent(pAccessor, pOwner, "plWaterBodyComponent", bodyGuid).Failed() ||
        !SetValueIfPresent(pAccessor, pAccessor->GetObject(bodyGuid), "WaterType", static_cast<plInt64>(type)))
    {
      pAccessor->CancelTransaction();
      return;
    }

    if (bNeedsSpline)
    {
      plUuid splineGuid;
      if (AddComponent(pAccessor, pOwner, "plSplineComponent", splineGuid).Failed())
      {
        pAccessor->CancelTransaction();
        return;
      }

      const plDocumentObject* pSpline = pAccessor->GetObject(splineGuid);
      if (!SetValueIfPresent(pAccessor, pSpline, "Closed", type == WaterType::Lake))
      {
        pAccessor->CancelTransaction();
        return;
      }

      if (type == WaterType::River)
      {
        const plVec3 points[] = {plVec3(-10, 0, 0), plVec3(0, 0, 0), plVec3(10, 0, 0)};
        for (plUInt32 i = 0; i < PL_ARRAY_SIZE(points); ++i)
        {
          if (AddSplineNode(pAccessor, pOwner, pSpline, i, points[i], true).Failed())
          {
            pAccessor->CancelTransaction();
            return;
          }
        }
      }
      else
      {
        const plVec3 points[] = {plVec3(-10, -10, 0), plVec3(10, -10, 0), plVec3(10, 10, 0), plVec3(-10, 10, 0)};
        for (plUInt32 i = 0; i < PL_ARRAY_SIZE(points); ++i)
        {
          if (AddSplineNode(pAccessor, pOwner, pSpline, i, points[i], false).Failed())
          {
            pAccessor->CancelTransaction();
            return;
          }
        }
      }
    }

    pAccessor->FinishTransaction();
    pDocument->GetSelectionManager()->SetSelection(pAccessor->GetObject(objectGuid));
    pDocument->ShowDocumentStatus(plFmt("Created {}. Assign a WaterProfile before export.", szName));
  }

  static void GatherSelectedWaterBodies(plGameObjectDocument* pDocument, plDynamicArray<const plDocumentObject*>& out_bodies)
  {
    out_bodies.Clear();
    if (pDocument == nullptr)
      return;

    for (const plDocumentObject* pSelected : pDocument->GetSelectionManager()->GetSelection())
    {
      const plDocumentObject* pBody = FindWaterBodyOwner(pSelected);
      if (pBody != nullptr && !out_bodies.Contains(pBody))
        out_bodies.PushBack(pBody);
    }
  }

  static plString MakeObjectReference(const plUuid& guid)
  {
    plStringBuilder sGuid;
    plConversionUtils::ToString(guid, sGuid);
    return sGuid;
  }

  static plUuid GetObjectReference(const plVariant& value)
  {
    if (value.IsA<plUuid>())
      return value.Get<plUuid>();
    if (value.IsA<plString>() && plConversionUtils::IsStringUuid(value.Get<plString>()))
      return plConversionUtils::ConvertStringToUuid(value.Get<plString>());
    return plUuid();
  }

  static void CreateJunction(plGameObjectDocument* pDocument)
  {
    if (pDocument == nullptr || plRTTI::FindTypeByName("plWaterJunctionComponent") == nullptr)
      return;

    plDynamicArray<const plDocumentObject*> bodies;
    GatherSelectedWaterBodies(pDocument, bodies);
    if (bodies.GetCount() != 2)
    {
      plQtUiServices::GetSingleton()->MessageBoxInformation("Select exactly two water bodies before creating a junction.");
      return;
    }

    const plVec3 vA = pDocument->GetGlobalTransform(bodies[0]).m_vPosition;
    const plVec3 vB = pDocument->GetGlobalTransform(bodies[1]).m_vPosition;

    plObjectAccessorBase* pAccessor = pDocument->GetObjectAccessor();
    pAccessor->StartTransaction("Create Water Junction");

    plUuid objectGuid;
    if (CreateGameObject(pAccessor, pDocument->GetObjectManager()->GetRootObject(), "Water Junction", (vA + vB) * 0.5f, objectGuid).Failed())
    {
      pAccessor->CancelTransaction();
      return;
    }

    plUuid componentGuid;
    const plDocumentObject* pOwner = pAccessor->GetObject(objectGuid);
    if (AddComponent(pAccessor, pOwner, "plWaterJunctionComponent", componentGuid).Failed())
    {
      pAccessor->CancelTransaction();
      return;
    }

    const plDocumentObject* pJunction = pAccessor->GetObject(componentGuid);
    if (!SetValueIfPresent(pAccessor, pJunction, "WaterBodyA", MakeObjectReference(bodies[0]->GetGuid())) ||
        !SetValueIfPresent(pAccessor, pJunction, "WaterBodyB", MakeObjectReference(bodies[1]->GetGuid())) ||
        !SetValueIfPresent(pAccessor, pJunction, "SnapRadius", 5.0f) ||
        !SetValueIfPresent(pAccessor, pJunction, "ElevationTolerance", 0.25f))
    {
      pAccessor->CancelTransaction();
      return;
    }

    pAccessor->FinishTransaction();
    pDocument->GetSelectionManager()->SetSelection(pAccessor->GetObject(objectGuid));
    pDocument->ShowDocumentStatus("Created an explicit two-body water junction. Use Re-snap after changing either shoreline.");
  }

  struct AnchorResult
  {
    plVec3 m_vPosition = plVec3::MakeZero();
    plUInt32 m_uiNodeIndex = 0;
    plUInt32 m_uiNodeCount = 1;
    plInt64 m_iWaterType = static_cast<plInt64>(WaterType::Ocean);
  };

  static AnchorResult FindClosestBodyAnchor(plGameObjectDocument* pDocument, const plDocumentObject* pBodyOwner, const plVec3& vTarget)
  {
    AnchorResult result;
    result.m_vPosition = pDocument->GetGlobalTransform(pBodyOwner).m_vPosition;

    const plDocumentObject* pBody = FindComponent(pBodyOwner, "plWaterBodyComponent");
    const plDocumentObject* pSpline = FindComponent(pBodyOwner, "plSplineComponent");
    if (pBody == nullptr)
      return result;

    const plInt64 iWaterType = pBody->GetTypeAccessor().GetValue("WaterType").ConvertTo<plInt64>();
    result.m_iWaterType = iWaterType;
    if (pSpline == nullptr)
      return result;
    plVariantArray nodeNames;
    if (!pSpline->GetTypeAccessor().GetValues("Nodes", nodeNames) || nodeNames.IsEmpty())
      return result;

    result.m_uiNodeCount = nodeNames.GetCount();
    float fBestDistance = plMath::HighValue<float>();
    for (plUInt32 i = 0; i < nodeNames.GetCount(); ++i)
    {
      if (iWaterType == static_cast<plInt64>(WaterType::River) && i != 0 && i + 1 != nodeNames.GetCount())
        continue;

      plStringView sNodeName;
      if (nodeNames[i].IsA<plHashedString>())
        sNodeName = nodeNames[i].Get<plHashedString>().GetString();
      else if (nodeNames[i].IsA<plString>())
        sNodeName = nodeNames[i].Get<plString>();
      else
        continue;

      const plDocumentObject* pNode = FindChildByName(pBodyOwner, sNodeName);
      if (pNode == nullptr)
        continue;

      const plVec3 vPosition = pDocument->GetGlobalTransform(pNode).m_vPosition;
      const float fDistance = (vPosition - vTarget).GetLengthSquared();
      if (fDistance < fBestDistance)
      {
        fBestDistance = fDistance;
        result.m_vPosition = vPosition;
        result.m_uiNodeIndex = i;
      }
    }
    return result;
  }

  static void SetAnchorValue(plObjectAccessorBase* pAccessor, const plDocumentObject* pJunction, plStringView sProperty,
    const AnchorResult& anchor, const plTransform& junctionTransform)
  {
    if (pJunction->GetType()->FindPropertyByName(sProperty) == nullptr)
      return;

    const plVariant current = pJunction->GetTypeAccessor().GetValue(sProperty);
    if (current.IsA<plVec3>())
    {
      const plVec3 vLocalAnchor = junctionTransform.GetInverse().TransformPosition(anchor.m_vPosition);
      pAccessor->SetValueByName(pJunction, sProperty, vLocalAnchor).IgnoreResult();
    }
    else if (current.IsA<float>())
    {
      const float fNormalized = anchor.m_uiNodeCount > 1 ? static_cast<float>(anchor.m_uiNodeIndex) / static_cast<float>(anchor.m_uiNodeCount - 1) : 0.0f;
      pAccessor->SetValueByName(pJunction, sProperty, fNormalized).IgnoreResult();
    }
    else if (current.IsNumber())
    {
      plInt64 iAttachment = 0; // Auto
      if (anchor.m_iWaterType == static_cast<plInt64>(WaterType::River))
        iAttachment = anchor.m_uiNodeIndex == 0 ? 1 : 2; // RiverStart / RiverEnd
      else if (anchor.m_iWaterType == static_cast<plInt64>(WaterType::Lake))
        iAttachment = 3; // LakeBoundary
      else
        iAttachment = 4; // OceanBoundary
      pAccessor->SetValueByName(pJunction, sProperty, iAttachment).IgnoreResult();
    }
  }

  static void GatherSelectedComponents(plGameObjectDocument* pDocument, plStringView sTypeName, plDynamicArray<const plDocumentObject*>& out_components)
  {
    out_components.Clear();
    for (const plDocumentObject* pSelected : pDocument->GetSelectionManager()->GetSelection())
    {
      if (IsType(pSelected, sTypeName) && !out_components.Contains(pSelected))
      {
        out_components.PushBack(pSelected);
        continue;
      }

      if (const plDocumentObject* pOwner = FindOwner(pSelected))
      {
        if (const plDocumentObject* pComponent = FindComponent(pOwner, sTypeName); pComponent != nullptr && !out_components.Contains(pComponent))
          out_components.PushBack(pComponent);
      }
    }
  }

  static void ResnapJunctions(plGameObjectDocument* pDocument)
  {
    if (pDocument == nullptr)
      return;

    plDynamicArray<const plDocumentObject*> junctions;
    GatherSelectedComponents(pDocument, "plWaterJunctionComponent", junctions);
    if (junctions.IsEmpty())
    {
      plQtUiServices::GetSingleton()->MessageBoxInformation("Select one or more water junctions to re-snap.");
      return;
    }

    plObjectAccessorBase* pAccessor = pDocument->GetObjectAccessor();
    pAccessor->StartTransaction("Re-snap Water Junctions");
    plUInt32 uiSnapped = 0;

    for (const plDocumentObject* pJunction : junctions)
    {
      const plUuid bodyAGuid = GetObjectReference(pJunction->GetTypeAccessor().GetValue("WaterBodyA"));
      const plUuid bodyBGuid = GetObjectReference(pJunction->GetTypeAccessor().GetValue("WaterBodyB"));
      const plDocumentObject* pBodyA = pDocument->GetObjectManager()->GetObject(bodyAGuid);
      const plDocumentObject* pBodyB = pDocument->GetObjectManager()->GetObject(bodyBGuid);
      if (FindComponent(pBodyA, "plWaterBodyComponent") == nullptr || FindComponent(pBodyB, "plWaterBodyComponent") == nullptr)
        continue;

      AnchorResult anchorA = FindClosestBodyAnchor(pDocument, pBodyA, pDocument->GetGlobalTransform(pBodyB).m_vPosition);
      AnchorResult anchorB = FindClosestBodyAnchor(pDocument, pBodyB, anchorA.m_vPosition);
      anchorA = FindClosestBodyAnchor(pDocument, pBodyA, anchorB.m_vPosition);

      const plDocumentObject* pOwner = FindOwner(pJunction);
      plTransform transform = pDocument->GetGlobalTransform(pOwner);
      transform.m_vPosition = (anchorA.m_vPosition + anchorB.m_vPosition) * 0.5f;
      pDocument->SetGlobalTransform(pOwner, transform, TransformationChanges::Translation);
      SetAnchorValue(pAccessor, pJunction, "AnchorA", anchorA, transform);
      SetAnchorValue(pAccessor, pJunction, "AnchorB", anchorB, transform);
      ++uiSnapped;
    }

    pAccessor->FinishTransaction();
    pDocument->ShowDocumentStatus(plFmt("Re-snapped {} water junction(s).", uiSnapped));
  }

  static void CreateExclusion(plGameObjectDocument* pDocument)
  {
    if (pDocument == nullptr || plRTTI::FindTypeByName("plWaterExclusionVolumeComponent") == nullptr)
      return;

    plObjectAccessorBase* pAccessor = pDocument->GetObjectAccessor();
    pAccessor->StartTransaction("Create Water Exclusion Volume");

    plUuid objectGuid;
    if (CreateGameObject(pAccessor, pDocument->GetObjectManager()->GetRootObject(), "Water Exclusion", GetPlacementPosition(pDocument), objectGuid).Failed())
    {
      pAccessor->CancelTransaction();
      return;
    }

    plUuid componentGuid;
    if (AddComponent(pAccessor, pAccessor->GetObject(objectGuid), "plWaterExclusionVolumeComponent", componentGuid).Failed() ||
        !SetValueIfPresent(pAccessor, pAccessor->GetObject(componentGuid), "Extents", plVec3(10.0f, 10.0f, 5.0f)))
    {
      pAccessor->CancelTransaction();
      return;
    }

    pAccessor->FinishTransaction();
    pDocument->GetSelectionManager()->SetSelection(pAccessor->GetObject(objectGuid));
  }

  static void RepairRiverMetadata(plGameObjectDocument* pDocument)
  {
    if (pDocument == nullptr || plRTTI::FindTypeByName("plWaterSplinePointComponent") == nullptr)
      return;

    plDynamicArray<const plDocumentObject*> bodies;
    GatherSelectedWaterBodies(pDocument, bodies);
    if (bodies.IsEmpty())
    {
      plQtUiServices::GetSingleton()->MessageBoxInformation("Select one or more rivers to repair their node metadata.");
      return;
    }

    plObjectAccessorBase* pAccessor = pDocument->GetObjectAccessor();
    pAccessor->StartTransaction("Repair River Metadata");
    plUInt32 uiAdded = 0;

    for (const plDocumentObject* pOwner : bodies)
    {
      const plDocumentObject* pBody = FindComponent(pOwner, "plWaterBodyComponent");
      if (pBody == nullptr || pBody->GetTypeAccessor().GetValue("WaterType").ConvertTo<plInt64>() != static_cast<plInt64>(WaterType::River))
        continue;

      const plDocumentObject* pSpline = FindComponent(pOwner, "plSplineComponent");
      if (pSpline == nullptr)
        continue;

      plVariantArray nodeNames;
      pSpline->GetTypeAccessor().GetValues("Nodes", nodeNames);
      for (plUInt32 uiNodeIndex = 0; uiNodeIndex < nodeNames.GetCount(); ++uiNodeIndex)
      {
        const plVariant& nodeName = nodeNames[uiNodeIndex];
        plStringView sName;
        if (nodeName.IsA<plHashedString>())
          sName = nodeName.Get<plHashedString>().GetString();
        else if (nodeName.IsA<plString>())
          sName = nodeName.Get<plString>();
        else
          continue;

        const plDocumentObject* pNode = FindChildByName(pOwner, sName);
        if (pNode == nullptr || FindComponent(pNode, "plWaterSplinePointComponent") != nullptr)
          continue;

        plUuid pointGuid;
        if (AddComponent(pAccessor, pNode, "plWaterSplinePointComponent", pointGuid).Succeeded())
        {
          const plDocumentObject* pPoint = pAccessor->GetObject(pointGuid);
          SetValueIfPresent(pAccessor, pPoint, "Order", uiNodeIndex);
          SetValueIfPresent(pAccessor, pPoint, "LeftWidth", 5.0f);
          SetValueIfPresent(pAccessor, pPoint, "RightWidth", 5.0f);
          SetValueIfPresent(pAccessor, pPoint, "Depth", 2.0f);
          SetValueIfPresent(pAccessor, pPoint, "FlowScale", 1.0f);
          ++uiAdded;
        }
      }
    }

    pAccessor->FinishTransaction();
    pDocument->ShowDocumentStatus(plFmt("Added metadata to {} river node(s).", uiAdded));
  }

  static void ConvertLegacyOceans(plGameObjectDocument* pDocument)
  {
    if (pDocument == nullptr || plRTTI::FindTypeByName("plWaterBodyComponent") == nullptr)
      return;

    plDynamicArray<const plDocumentObject*> legacyComponents;
    GatherSelectedComponents(pDocument, "plOceanComponent", legacyComponents);
    if (legacyComponents.IsEmpty())
    {
      plQtUiServices::GetSingleton()->MessageBoxInformation("Select one or more legacy plOceanComponent objects to convert.");
      return;
    }

    plObjectAccessorBase* pAccessor = pDocument->GetObjectAccessor();
    pAccessor->StartTransaction("Convert Legacy Oceans");
    plUInt32 uiConverted = 0;

    for (const plDocumentObject* pLegacy : legacyComponents)
    {
      const plDocumentObject* pOwner = FindOwner(pLegacy);
      const plDocumentObject* pBody = FindComponent(pOwner, "plWaterBodyComponent");
      if (pBody == nullptr)
      {
        plUuid bodyGuid;
        if (AddComponent(pAccessor, pOwner, "plWaterBodyComponent", bodyGuid).Failed())
          continue;
        pBody = pAccessor->GetObject(bodyGuid);
      }

      SetValueIfPresent(pAccessor, pBody, "WaterType", static_cast<plInt64>(WaterType::Ocean));

      for (const plAbstractProperty* pProperty : pBody->GetType()->GetProperties())
      {
        const plStringView sPropertyName = pProperty->GetPropertyName();
        if (sPropertyName == "WaterType" || sPropertyName == "Profile" || pLegacy->GetType()->FindPropertyByName(sPropertyName) == nullptr)
          continue;

        const plVariant value = pLegacy->GetTypeAccessor().GetValue(sPropertyName);
        if (value.IsValid())
          pAccessor->SetValueByName(pBody, sPropertyName, value).IgnoreResult();
      }

      const plVariant tileSize = pLegacy->GetTypeAccessor().GetValue("TileSize");
      if (tileSize.IsNumber())
        SetValueIfPresent(pAccessor, pBody, "OceanPatchSize", tileSize.ConvertTo<float>());

      plVariant tessellation = pLegacy->GetTypeAccessor().GetValue("TileTesselation");
      if (!tessellation.IsValid())
        tessellation = pLegacy->GetTypeAccessor().GetValue("Tessellation");
      if (tessellation.IsA<plVec2U32>())
        SetValueIfPresent(pAccessor, pBody, "PatchResolution", plMath::Max(tessellation.Get<plVec2U32>().x, tessellation.Get<plVec2U32>().y));

      // A graph patch cannot manufacture a WaterProfile asset for the legacy
      // inline wave/material settings. Keep the old component disabled as an
      // auditable source of truth until the user assigns and verifies a profile.
      SetValueIfPresent(pAccessor, pLegacy, "Active", false);
      ++uiConverted;
    }

    pAccessor->FinishTransaction();
    pDocument->ShowDocumentStatus(plFmt(
      "Converted {} legacy ocean component(s). Legacy components were retained disabled; assign and verify WaterProfiles before removing them.", uiConverted));
  }

  static void ValidateWater(plGameObjectDocument* pDocument)
  {
    if (pDocument == nullptr)
      return;

    plDynamicArray<plWaterValidationIssue> issues;
    plWaterEditorValidation::ValidateDocument(pDocument, issues);

    plStringBuilder sReport;
    plWaterEditorValidation::BuildReport(issues, sReport);
    if (issues.IsEmpty())
      plQtUiServices::GetSingleton()->MessageBoxInformation(sReport);
    else
      plQtUiServices::GetSingleton()->MessageBoxWarning(sReport);

    pDocument->ShowDocumentStatus(sReport.GetView());
  }

  static void InspectWaterPipeline(plDocument* pDocument)
  {
    const plPlatformProfile* pProfile = plAssetCurator::GetSingleton()->GetActiveAssetProfile();
    if (pProfile == nullptr)
    {
      plQtUiServices::GetSingleton()->MessageBoxWarning("There is no active asset profile to inspect.");
      return;
    }

    const plRenderPipelineProfileConfig* pPipelineConfig = pProfile->GetTypeConfig<plRenderPipelineProfileConfig>();
    if (pPipelineConfig == nullptr || pPipelineConfig->m_sMainRenderPipeline.IsEmpty())
    {
      plQtUiServices::GetSingleton()->MessageBoxWarning("The active asset profile has no main render pipeline assigned. No changes were made.");
      return;
    }

    auto pipelineAsset = plAssetCurator::GetSingleton()->FindSubAsset(pPipelineConfig->m_sMainRenderPipeline);
    if (!pipelineAsset.isValid() || pipelineAsset->m_pAssetInfo == nullptr)
    {
      plQtUiServices::GetSingleton()->MessageBoxWarning("The active main render pipeline asset could not be resolved. No changes were made.");
      return;
    }

    const plString sPipelinePath = pipelineAsset->m_pAssetInfo->m_Path.GetAbsolutePath();
    plFileReader reader;
    if (reader.Open(sPipelinePath).Failed())
    {
      plQtUiServices::GetSingleton()->MessageBoxWarning(plFmt("Could not read render pipeline '{}'. No changes were made.", sPipelinePath));
      return;
    }

    plStringBuilder sSource;
    sSource.ReadAll(reader);
    const bool bHasSurface = sSource.FindSubString("plWaterSurfacePass") != nullptr;
    const bool bHasUnderwater = sSource.FindSubString("plWaterUnderwaterPass") != nullptr;

    if (bHasSurface && bHasUnderwater)
    {
      plQtUiServices::GetSingleton()->MessageBoxInformation(
        plFmt("Water pipeline inspection passed for '{}'. Water surface and underwater composition passes are present. No changes were needed.", sPipelinePath));
      return;
    }

    plStringBuilder sMissing;
    if (!bHasSurface)
      sMissing.Append("plWaterSurfacePass");
    if (!bHasUnderwater)
    {
      if (!sMissing.IsEmpty())
        sMissing.Append(", ");
      sMissing.Append("plWaterUnderwaterPass");
    }

    plQtUiServices::GetSingleton()->MessageBoxWarning(plFmt(
      "The active pipeline is missing {} or uses an unrecognized custom topology. The conservative assistant did not duplicate, edit, transform, or save the asset. "
      "The pipeline will be opened so the connections can be reviewed explicitly.",
      sMissing));
    plQtEditorApp::GetSingleton()->OpenDocumentQueued(sPipelinePath);

    if (pDocument != nullptr)
      pDocument->ShowDocumentStatus("Water pipeline requires manual review; no asset changes were made.");
  }
} // namespace

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plWaterAction, 1, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;

plActionDescriptorHandle plWaterActions::s_hCategory;
plActionDescriptorHandle plWaterActions::s_hMenu;
plActionDescriptorHandle plWaterActions::s_hCreateCategory;
plActionDescriptorHandle plWaterActions::s_hToolsCategory;
plActionDescriptorHandle plWaterActions::s_hDebugMenu;
plActionDescriptorHandle plWaterActions::s_hCreateOcean;
plActionDescriptorHandle plWaterActions::s_hCreateRiver;
plActionDescriptorHandle plWaterActions::s_hCreateLake;
plActionDescriptorHandle plWaterActions::s_hCreateJunction;
plActionDescriptorHandle plWaterActions::s_hCreateExclusion;
plActionDescriptorHandle plWaterActions::s_hResnapJunction;
plActionDescriptorHandle plWaterActions::s_hRepairRiverMetadata;
plActionDescriptorHandle plWaterActions::s_hConvertLegacy;
plActionDescriptorHandle plWaterActions::s_hConvertPipeline;
plActionDescriptorHandle plWaterActions::s_hValidate;
plActionDescriptorHandle plWaterActions::s_hDebugBodies;
plActionDescriptorHandle plWaterActions::s_hDebugLod;
plActionDescriptorHandle plWaterActions::s_hDebugFlow;
plActionDescriptorHandle plWaterActions::s_hDebugJunctions;
plActionDescriptorHandle plWaterActions::s_hDebugInteractions;
plActionDescriptorHandle plWaterActions::s_hDebugUnderwater;

void plWaterActions::RegisterActions()
{
  s_hCategory = PL_REGISTER_CATEGORY("Water");
  // One "Water" entry in the menu bar and one toolbar drop-down; everything below lives inside it.
  s_hMenu = PL_REGISTER_MENU_WITH_ICON("Water.Menu", ":/WaterPlugin/Water.svg");
  s_hCreateCategory = PL_REGISTER_CATEGORY("Water.Create");
  s_hToolsCategory = PL_REGISTER_CATEGORY("Water.Tools");
  s_hDebugMenu = PL_REGISTER_MENU_WITH_ICON("Water.Debug.Menu", ":/WaterPlugin/Water.svg");
  s_hCreateOcean = PL_REGISTER_ACTION_1("Water.CreateOcean", plActionScope::Document, "Water", "", plWaterAction, plWaterAction::ActionType::CreateOcean);
  s_hCreateRiver = PL_REGISTER_ACTION_1("Water.CreateRiver", plActionScope::Document, "Water", "", plWaterAction, plWaterAction::ActionType::CreateRiver);
  s_hCreateLake = PL_REGISTER_ACTION_1("Water.CreateLake", plActionScope::Document, "Water", "", plWaterAction, plWaterAction::ActionType::CreateLake);
  s_hCreateJunction = PL_REGISTER_ACTION_1("Water.CreateJunction", plActionScope::Document, "Water", "", plWaterAction, plWaterAction::ActionType::CreateJunction);
  s_hCreateExclusion = PL_REGISTER_ACTION_1("Water.CreateExclusion", plActionScope::Document, "Water", "", plWaterAction, plWaterAction::ActionType::CreateExclusion);
  s_hResnapJunction = PL_REGISTER_ACTION_1("Water.ResnapJunction", plActionScope::Document, "Water", "", plWaterAction, plWaterAction::ActionType::ResnapJunction);
  s_hRepairRiverMetadata = PL_REGISTER_ACTION_1("Water.RepairRiverMetadata", plActionScope::Document, "Water", "", plWaterAction, plWaterAction::ActionType::RepairRiverMetadata);
  s_hConvertLegacy = PL_REGISTER_ACTION_1("Water.ConvertLegacy", plActionScope::Document, "Water", "", plWaterAction, plWaterAction::ActionType::ConvertLegacy);
  s_hConvertPipeline = PL_REGISTER_ACTION_1("Water.ConvertPipeline", plActionScope::Document, "Water", "", plWaterAction, plWaterAction::ActionType::ConvertPipeline);
  s_hValidate = PL_REGISTER_ACTION_1("Water.Validate", plActionScope::Document, "Water", "", plWaterAction, plWaterAction::ActionType::Validate);
  s_hDebugBodies = PL_REGISTER_ACTION_1("Water.Debug.Bodies", plActionScope::Document, "Water", "", plWaterAction, plWaterAction::ActionType::DebugBodies);
  s_hDebugLod = PL_REGISTER_ACTION_1("Water.Debug.Lod", plActionScope::Document, "Water", "", plWaterAction, plWaterAction::ActionType::DebugLod);
  s_hDebugFlow = PL_REGISTER_ACTION_1("Water.Debug.Flow", plActionScope::Document, "Water", "", plWaterAction, plWaterAction::ActionType::DebugFlow);
  s_hDebugJunctions = PL_REGISTER_ACTION_1("Water.Debug.Junctions", plActionScope::Document, "Water", "", plWaterAction, plWaterAction::ActionType::DebugJunctions);
  s_hDebugInteractions = PL_REGISTER_ACTION_1("Water.Debug.Interactions", plActionScope::Document, "Water", "", plWaterAction, plWaterAction::ActionType::DebugInteractions);
  s_hDebugUnderwater = PL_REGISTER_ACTION_1("Water.Debug.Underwater", plActionScope::Document, "Water", "", plWaterAction, plWaterAction::ActionType::DebugUnderwater);
}

void plWaterActions::UnregisterActions()
{
  plActionDescriptorHandle handles[] = {s_hDebugUnderwater, s_hDebugInteractions, s_hDebugJunctions, s_hDebugFlow, s_hDebugLod, s_hDebugBodies,
    s_hValidate, s_hConvertPipeline, s_hConvertLegacy, s_hRepairRiverMetadata, s_hResnapJunction, s_hCreateExclusion, s_hCreateJunction, s_hCreateLake, s_hCreateRiver,
    s_hCreateOcean, s_hDebugMenu, s_hToolsCategory, s_hCreateCategory, s_hMenu, s_hCategory};
  for (plActionDescriptorHandle& handle : handles)
    plActionManager::UnregisterAction(handle);
}

void plWaterActions::MapMenuActions(plStringView sMapping)
{
  plActionMap* pMap = plActionMapManager::GetActionMap(sMapping);
  if (pMap == nullptr)
    return;

  // Everything hangs off a single "Water" submenu of the Scene menu. Categories inside it are separator
  // groups, not nested menus; only the debug toggles get their own submenu.
  pMap->MapAction(s_hMenu, "G.Scene", 20.0f);
  const char* szMenu = "G.Scene/Water.Menu";
  const char* szCreate = "G.Scene/Water.Menu/Water.Create";
  const char* szTools = "G.Scene/Water.Menu/Water.Tools";
  const char* szDebug = "G.Scene/Water.Menu/Water.Debug.Menu";

  pMap->MapAction(s_hCreateCategory, szMenu, 1.0f);
  pMap->MapAction(s_hCreateOcean, szCreate, 1.0f);
  pMap->MapAction(s_hCreateRiver, szCreate, 2.0f);
  pMap->MapAction(s_hCreateLake, szCreate, 3.0f);
  pMap->MapAction(s_hCreateJunction, szCreate, 4.0f);
  pMap->MapAction(s_hCreateExclusion, szCreate, 5.0f);

  pMap->MapAction(s_hToolsCategory, szMenu, 2.0f);
  pMap->MapAction(s_hValidate, szTools, 1.0f);
  pMap->MapAction(s_hResnapJunction, szTools, 2.0f);
  pMap->MapAction(s_hRepairRiverMetadata, szTools, 3.0f);
  pMap->MapAction(s_hConvertLegacy, szTools, 4.0f);
  pMap->MapAction(s_hConvertPipeline, szTools, 5.0f);

  pMap->MapAction(s_hDebugMenu, szMenu, 3.0f);
  pMap->MapAction(s_hDebugBodies, szDebug, 1.0f);
  pMap->MapAction(s_hDebugLod, szDebug, 2.0f);
  pMap->MapAction(s_hDebugFlow, szDebug, 3.0f);
  pMap->MapAction(s_hDebugJunctions, szDebug, 4.0f);
  pMap->MapAction(s_hDebugInteractions, szDebug, 5.0f);
  pMap->MapAction(s_hDebugUnderwater, szDebug, 6.0f);
}

void plWaterActions::MapToolbarActions(plStringView sMapping)
{
  plActionMap* pMap = plActionMapManager::GetActionMap(sMapping);
  if (pMap == nullptr)
    return;

  // A single toolbar button that opens the full water menu, instead of five separate buttons.
  pMap->MapAction(s_hCategory, "", 20.0f);
  pMap->MapAction(s_hMenu, "Water", 1.0f);
  const char* szMenu = "Water/Water.Menu";
  const char* szCreate = "Water/Water.Menu/Water.Create";
  const char* szTools = "Water/Water.Menu/Water.Tools";
  const char* szDebug = "Water/Water.Menu/Water.Debug.Menu";

  pMap->MapAction(s_hCreateCategory, szMenu, 1.0f);
  pMap->MapAction(s_hCreateOcean, szCreate, 1.0f);
  pMap->MapAction(s_hCreateRiver, szCreate, 2.0f);
  pMap->MapAction(s_hCreateLake, szCreate, 3.0f);
  pMap->MapAction(s_hCreateJunction, szCreate, 4.0f);
  pMap->MapAction(s_hCreateExclusion, szCreate, 5.0f);

  pMap->MapAction(s_hToolsCategory, szMenu, 2.0f);
  pMap->MapAction(s_hValidate, szTools, 1.0f);
  pMap->MapAction(s_hResnapJunction, szTools, 2.0f);
  pMap->MapAction(s_hRepairRiverMetadata, szTools, 3.0f);
  pMap->MapAction(s_hConvertLegacy, szTools, 4.0f);
  pMap->MapAction(s_hConvertPipeline, szTools, 5.0f);

  pMap->MapAction(s_hDebugMenu, szMenu, 3.0f);
  pMap->MapAction(s_hDebugBodies, szDebug, 1.0f);
  pMap->MapAction(s_hDebugLod, szDebug, 2.0f);
  pMap->MapAction(s_hDebugFlow, szDebug, 3.0f);
  pMap->MapAction(s_hDebugJunctions, szDebug, 4.0f);
  pMap->MapAction(s_hDebugInteractions, szDebug, 5.0f);
  pMap->MapAction(s_hDebugUnderwater, szDebug, 6.0f);
}

plWaterAction::plWaterAction(const plActionContext& context, const char* szName, ActionType type)
  : plButtonAction(context, szName, false, ":/WaterPlugin/Water.svg")
  , m_Type(type)
{
  if (IsDebugAction())
  {
    SetCheckable(true);
    plWaterEditorPreferences* pPreferences = plPreferences::QueryPreferences<plWaterEditorPreferences>();
    pPreferences->m_ChangedEvent.AddEventHandler(plMakeDelegate(&plWaterAction::OnPreferenceChange, this));
    UpdateDebugState();
  }
}

plWaterAction::~plWaterAction()
{
  if (IsDebugAction())
  {
    plWaterEditorPreferences* pPreferences = plPreferences::QueryPreferences<plWaterEditorPreferences>();
    pPreferences->m_ChangedEvent.RemoveEventHandler(plMakeDelegate(&plWaterAction::OnPreferenceChange, this));
  }
}

bool plWaterAction::IsDebugAction() const
{
  return m_Type >= ActionType::DebugBodies;
}

void plWaterAction::Execute(const plVariant& value)
{
  plGameObjectDocument* pDocument = plDynamicCast<plGameObjectDocument*>(GetContext().m_pDocument);
  switch (m_Type)
  {
    case ActionType::CreateOcean:
      CreateWaterBody(pDocument, WaterType::Ocean);
      break;
    case ActionType::CreateRiver:
      CreateWaterBody(pDocument, WaterType::River);
      break;
    case ActionType::CreateLake:
      CreateWaterBody(pDocument, WaterType::Lake);
      break;
    case ActionType::CreateJunction:
      CreateJunction(pDocument);
      break;
    case ActionType::CreateExclusion:
      CreateExclusion(pDocument);
      break;
    case ActionType::ResnapJunction:
      ResnapJunctions(pDocument);
      break;
    case ActionType::RepairRiverMetadata:
      RepairRiverMetadata(pDocument);
      break;
    case ActionType::ConvertLegacy:
      ConvertLegacyOceans(pDocument);
      break;
    case ActionType::ConvertPipeline:
      InspectWaterPipeline(GetContext().m_pDocument);
      break;
    case ActionType::Validate:
      ValidateWater(pDocument);
      break;
    default:
    {
      plWaterEditorPreferences* pPreferences = plPreferences::QueryPreferences<plWaterEditorPreferences>();
      switch (m_Type)
      {
        case ActionType::DebugBodies:
          pPreferences->SetShowBodies(!pPreferences->GetShowBodies());
          break;
        case ActionType::DebugLod:
          pPreferences->SetShowLod(!pPreferences->GetShowLod());
          break;
        case ActionType::DebugFlow:
          pPreferences->SetShowFlow(!pPreferences->GetShowFlow());
          break;
        case ActionType::DebugJunctions:
          pPreferences->SetShowJunctions(!pPreferences->GetShowJunctions());
          break;
        case ActionType::DebugInteractions:
          pPreferences->SetShowInteractions(!pPreferences->GetShowInteractions());
          break;
        case ActionType::DebugUnderwater:
          pPreferences->SetShowUnderwaterVolumes(!pPreferences->GetShowUnderwaterVolumes());
          break;
        default:
          break;
      }
      UpdateDebugState();
    }
    break;
  }
}

void plWaterAction::OnPreferenceChange(plPreferences* pPreferences)
{
  UpdateDebugState();
}

void plWaterAction::UpdateDebugState()
{
  if (!IsDebugAction())
    return;

  const plWaterEditorPreferences* pPreferences = plPreferences::QueryPreferences<plWaterEditorPreferences>();
  switch (m_Type)
  {
    case ActionType::DebugBodies:
      SetChecked(pPreferences->GetShowBodies());
      break;
    case ActionType::DebugLod:
      SetChecked(pPreferences->GetShowLod());
      break;
    case ActionType::DebugFlow:
      SetChecked(pPreferences->GetShowFlow());
      break;
    case ActionType::DebugJunctions:
      SetChecked(pPreferences->GetShowJunctions());
      break;
    case ActionType::DebugInteractions:
      SetChecked(pPreferences->GetShowInteractions());
      break;
    case ActionType::DebugUnderwater:
      SetChecked(pPreferences->GetShowUnderwaterVolumes());
      break;
    default:
      break;
  }
}
