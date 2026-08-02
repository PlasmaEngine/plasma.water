#include <EditorPluginWater/EditorPluginWaterPCH.h>

#include <EditorPluginWater/Validation/WaterValidation.h>

#include <Foundation/Utilities/ConversionUtils.h>
#include <ToolsFoundation/Document/Document.h>
#include <ToolsFoundation/Object/DocumentObjectManager.h>

namespace
{
  static bool IsType(const plDocumentObject* pObject, plStringView sTypeName)
  {
    return pObject != nullptr && pObject->GetType()->GetTypeName() == sTypeName;
  }

  static plString GetObjectName(const plDocumentObject* pObject)
  {
    if (pObject == nullptr)
      return "<missing>";

    const plDocumentObject* pNamedObject = pObject;
    while (pNamedObject != nullptr && !IsType(pNamedObject, "plGameObject"))
      pNamedObject = pNamedObject->GetParent();

    if (pNamedObject != nullptr)
    {
      plVariant value = pNamedObject->GetTypeAccessor().GetValue("Name");
      if (value.IsA<plString>() && !value.Get<plString>().IsEmpty())
        return value.Get<plString>();
    }

    return pObject->GetType()->GetTypeName();
  }

  static const plDocumentObject* FindOwner(const plDocumentObject* pObject)
  {
    while (pObject != nullptr && !IsType(pObject, "plGameObject"))
      pObject = pObject->GetParent();
    return pObject;
  }

  static const plDocumentObject* FindComponent(const plDocumentObject* pOwner, plStringView sTypeName)
  {
    if (pOwner == nullptr)
      return nullptr;

    plVariantArray components;
    if (!pOwner->GetTypeAccessor().GetValues("Components", components))
      return nullptr;

    for (const plVariant& value : components)
    {
      if (!value.IsA<plUuid>())
        continue;

      const plDocumentObject* pComponent = pOwner->GetDocumentObjectManager()->GetObject(value.Get<plUuid>());
      if (IsType(pComponent, sTypeName))
        return pComponent;
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

      plVariant value = pChild->GetTypeAccessor().GetValue("Name");
      if (value.IsA<plString>() && value.Get<plString>() == sName)
        return pChild;
    }

    return nullptr;
  }

  static void AddIssue(plDynamicArray<plWaterValidationIssue>& out_issues, plWaterValidationIssue::Severity severity, const plDocumentObject* pObject, const plFormatString& message)
  {
    auto& issue = out_issues.ExpandAndGetRef();
    issue.m_Severity = severity;
    issue.m_Object = pObject != nullptr ? pObject->GetGuid() : plUuid();
    plStringBuilder sStorage;
    issue.m_sMessage = message.GetText(sStorage);
  }

  static bool IsReferenceEmpty(const plVariant& value)
  {
    if (!value.IsValid())
      return true;
    if (value.IsA<plString>())
      return value.Get<plString>().IsEmpty();
    if (value.IsA<plUuid>())
      return !value.Get<plUuid>().IsValid();
    return false;
  }

  static plUuid GetReferenceGuid(const plVariant& value)
  {
    if (value.IsA<plUuid>())
      return value.Get<plUuid>();

    if (value.IsA<plString>() && plConversionUtils::IsStringUuid(value.Get<plString>()))
      return plConversionUtils::ConvertStringToUuid(value.Get<plString>());

    return plUuid();
  }

  static float Cross2D(const plVec3& a, const plVec3& b, const plVec3& c)
  {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
  }

  static bool SegmentsIntersect2D(const plVec3& a, const plVec3& b, const plVec3& c, const plVec3& d)
  {
    const float abC = Cross2D(a, b, c);
    const float abD = Cross2D(a, b, d);
    const float cdA = Cross2D(c, d, a);
    const float cdB = Cross2D(c, d, b);

    return ((abC > 0.0f && abD < 0.0f) || (abC < 0.0f && abD > 0.0f)) &&
           ((cdA > 0.0f && cdB < 0.0f) || (cdA < 0.0f && cdB > 0.0f));
  }

  static void CollectObjects(const plDocumentObject* pObject, plDynamicArray<const plDocumentObject*>& out_objects)
  {
    if (pObject == nullptr)
      return;

    out_objects.PushBack(pObject);
    for (const plDocumentObject* pChild : pObject->GetChildren())
      CollectObjects(pChild, out_objects);
  }

  static void ValidateBody(const plDocumentObject* pBody, plDynamicArray<plWaterValidationIssue>& out_issues)
  {
    const plDocumentObject* pOwner = FindOwner(pBody);
    const plString sName = GetObjectName(pBody);

    const plVariant profile = pBody->GetTypeAccessor().GetValue("Profile");
    if (IsReferenceEmpty(profile))
      AddIssue(out_issues, plWaterValidationIssue::Severity::Warning, pBody, plFmt("Water body '{}' has no WaterProfile assigned.", sName));

    const plInt64 iWaterType = pBody->GetTypeAccessor().GetValue("WaterType").ConvertTo<plInt64>();
    if (iWaterType == 0)
      return; // Ocean does not require a spline.

    const plDocumentObject* pSpline = FindComponent(pOwner, "plSplineComponent");
    if (pSpline == nullptr)
    {
      AddIssue(out_issues, plWaterValidationIssue::Severity::Error, pBody, plFmt("Water body '{}' requires a sibling plSplineComponent.", sName));
      return;
    }

    const bool bClosed = pSpline->GetTypeAccessor().GetValue("Closed").ConvertTo<bool>();
    if (iWaterType == 1 && bClosed)
      AddIssue(out_issues, plWaterValidationIssue::Severity::Error, pSpline, plFmt("River '{}' must use an open spline.", sName));
    if (iWaterType == 2 && !bClosed)
      AddIssue(out_issues, plWaterValidationIssue::Severity::Error, pSpline, plFmt("Lake '{}' must use a closed spline.", sName));

    plVariantArray nodeNames;
    pSpline->GetTypeAccessor().GetValues("Nodes", nodeNames);
    const plUInt32 uiMinimumNodes = iWaterType == 2 ? 3u : 2u;
    if (nodeNames.GetCount() < uiMinimumNodes)
    {
      AddIssue(out_issues, plWaterValidationIssue::Severity::Error, pSpline,
        plFmt("Water body '{}' needs at least {} spline nodes.", sName, uiMinimumNodes));
      return;
    }

    plDynamicArray<plVec3> positions;
    plDynamicArray<float> riverHalfWidths;
    positions.Reserve(nodeNames.GetCount());
    riverHalfWidths.Reserve(nodeNames.GetCount());

    for (const plVariant& value : nodeNames)
    {
      plStringBuilder sNodeName;
      if (value.IsA<plHashedString>())
        sNodeName = value.Get<plHashedString>().GetString();
      else if (value.IsA<plString>())
        sNodeName = value.Get<plString>();
      else
        continue;

      const plDocumentObject* pNode = FindChildByName(pOwner, sNodeName);
      if (pNode == nullptr)
      {
        AddIssue(out_issues, plWaterValidationIssue::Severity::Error, pSpline,
          plFmt("Water body '{}' references missing spline node '{}'.", sName, sNodeName));
        continue;
      }

      const plVariant position = pNode->GetTypeAccessor().GetValue("LocalPosition");
      if (position.IsA<plVec3>())
      {
        positions.PushBack(position.Get<plVec3>());
        riverHalfWidths.PushBack(plMath::Max(pBody->GetTypeAccessor().GetValue("DefaultWidth").ConvertTo<float>() * 0.5f, 0.01f));
      }

      if (iWaterType == 1)
      {
        const plDocumentObject* pPoint = FindComponent(pNode, "plWaterSplinePointComponent");
        if (pPoint == nullptr)
        {
          AddIssue(out_issues, plWaterValidationIssue::Severity::Error, pNode,
            plFmt("River '{}' node '{}' has no plWaterSplinePointComponent. Use Repair River Metadata.", sName, sNodeName));
          continue;
        }

        const char* properties[] = {"LeftWidth", "RightWidth", "Depth"};
        for (const char* szProperty : properties)
        {
          const float fValue = pPoint->GetTypeAccessor().GetValue(szProperty).ConvertTo<float>();
          if (!plMath::IsFinite(fValue) || fValue <= 0.0f)
          {
            AddIssue(out_issues, plWaterValidationIssue::Severity::Error, pPoint,
              plFmt("River '{}' node '{}' has invalid {} ({}).", sName, sNodeName, szProperty, fValue));
          }
        }

        if (!riverHalfWidths.IsEmpty())
        {
          const float fLeftWidth = pPoint->GetTypeAccessor().GetValue("LeftWidth").ConvertTo<float>();
          const float fRightWidth = pPoint->GetTypeAccessor().GetValue("RightWidth").ConvertTo<float>();
          if (plMath::IsFinite(fLeftWidth) && plMath::IsFinite(fRightWidth))
            riverHalfWidths.PeekBack() = plMath::Max(fLeftWidth, fRightWidth);
        }

        const float fFlowScale = pPoint->GetTypeAccessor().GetValue("FlowScale").ConvertTo<float>();
        if (!plMath::IsFinite(fFlowScale))
        {
          AddIssue(out_issues, plWaterValidationIssue::Severity::Error, pPoint,
            plFmt("River '{}' node '{}' has a non-finite FlowScale.", sName, sNodeName));
        }
      }
    }

    for (plUInt32 i = 1; i < positions.GetCount(); ++i)
    {
      if ((positions[i] - positions[i - 1]).GetLengthSquared() < 0.0001f)
      {
        AddIssue(out_issues, plWaterValidationIssue::Severity::Error, pSpline,
          plFmt("Water body '{}' contains coincident consecutive spline nodes.", sName));
        break;
      }
    }

    if (iWaterType == 1 && positions.GetCount() == riverHalfWidths.GetCount())
    {
      for (plUInt32 i = 1; i + 1 < positions.GetCount(); ++i)
      {
        plVec2 incoming = (positions[i] - positions[i - 1]).GetAsVec2();
        plVec2 outgoing = (positions[i + 1] - positions[i]).GetAsVec2();
        const float fIncomingLength = incoming.GetLength();
        const float fOutgoingLength = outgoing.GetLength();
        if (fIncomingLength <= 0.001f || fOutgoingLength <= 0.001f)
          continue;
        incoming /= fIncomingLength;
        outgoing /= fOutgoingLength;
        const float fTurnDot = plMath::Clamp(incoming.Dot(outgoing), -1.0f, 1.0f);
        const float fMiterScale = 1.0f / plMath::Max(plMath::Sqrt(plMath::Max((1.0f + fTurnDot) * 0.5f, 0.0f)), 0.001f);
        if (fMiterScale > 1.75f && riverHalfWidths[i] > plMath::Min(fIncomingLength, fOutgoingLength) * 0.25f)
        {
          AddIssue(out_issues, plWaterValidationIssue::Severity::Warning, pSpline,
            plFmt("River '{}' has a sharp, wide turn at node {} that may be rejected as a ribbon foldover.", sName, i));
          break;
        }
      }
    }

    if (iWaterType != 2 || positions.GetCount() < 3)
      return;

    float fMinZ = positions[0].z;
    float fMaxZ = positions[0].z;
    for (const plVec3& position : positions)
    {
      fMinZ = plMath::Min(fMinZ, position.z);
      fMaxZ = plMath::Max(fMaxZ, position.z);
    }

    if (fMaxZ - fMinZ > 0.05f)
    {
      AddIssue(out_issues, plWaterValidationIssue::Severity::Error, pSpline,
        plFmt("Lake '{}' is not planar (vertical spread is {} m; maximum is 0.05 m).", sName, fMaxZ - fMinZ));
    }

    const plUInt32 uiCount = positions.GetCount();
    for (plUInt32 i = 0; i < uiCount; ++i)
    {
      const plUInt32 iNext = (i + 1) % uiCount;
      for (plUInt32 j = i + 1; j < uiCount; ++j)
      {
        const plUInt32 jNext = (j + 1) % uiCount;
        if (i == j || iNext == j || jNext == i)
          continue;

        if (SegmentsIntersect2D(positions[i], positions[iNext], positions[j], positions[jNext]))
        {
          AddIssue(out_issues, plWaterValidationIssue::Severity::Error, pSpline,
            plFmt("Lake '{}' has a self-intersecting outline.", sName));
          return;
        }
      }
    }
  }

  static void ValidateJunction(const plDocumentObject* pJunction, plDynamicArray<plWaterValidationIssue>& out_issues)
  {
    const plVariant bodyA = pJunction->GetTypeAccessor().GetValue("WaterBodyA");
    const plVariant bodyB = pJunction->GetTypeAccessor().GetValue("WaterBodyB");
    const plUuid bodyAGuid = GetReferenceGuid(bodyA);
    const plUuid bodyBGuid = GetReferenceGuid(bodyB);

    if (!bodyAGuid.IsValid() || !bodyBGuid.IsValid())
      AddIssue(out_issues, plWaterValidationIssue::Severity::Error, pJunction, "Water junction must reference two water bodies.");
    else if (bodyAGuid == bodyBGuid)
      AddIssue(out_issues, plWaterValidationIssue::Severity::Error, pJunction, "Water junction cannot connect a body to itself.");
    else
    {
      const auto* pManager = pJunction->GetDocumentObjectManager();
      const plDocumentObject* pBodyAOwner = pManager->GetObject(bodyAGuid);
      const plDocumentObject* pBodyBOwner = pManager->GetObject(bodyBGuid);
      if (FindComponent(pBodyAOwner, "plWaterBodyComponent") == nullptr || FindComponent(pBodyBOwner, "plWaterBodyComponent") == nullptr)
        AddIssue(out_issues, plWaterValidationIssue::Severity::Error, pJunction, "Water junction contains a broken or non-water body reference.");
    }

    const float fSnapRadius = pJunction->GetTypeAccessor().GetValue("SnapRadius").ConvertTo<float>();
    const float fElevationTolerance = pJunction->GetTypeAccessor().GetValue("ElevationTolerance").ConvertTo<float>();
    if (!plMath::IsFinite(fSnapRadius) || fSnapRadius <= 0.0f)
      AddIssue(out_issues, plWaterValidationIssue::Severity::Error, pJunction, "Water junction SnapRadius must be positive.");
    if (!plMath::IsFinite(fElevationTolerance) || fElevationTolerance < 0.0f)
      AddIssue(out_issues, plWaterValidationIssue::Severity::Error, pJunction, "Water junction ElevationTolerance cannot be negative.");
  }
} // namespace

void plWaterEditorValidation::ValidateDocument(const plDocument* pDocument, plDynamicArray<plWaterValidationIssue>& out_issues)
{
  out_issues.Clear();
  if (pDocument == nullptr || pDocument->GetObjectManager() == nullptr)
    return;

  plDynamicArray<const plDocumentObject*> objects;
  CollectObjects(pDocument->GetObjectManager()->GetRootObject(), objects);

  for (const plDocumentObject* pObject : objects)
  {
    if (IsType(pObject, "plWaterBodyComponent"))
    {
      ValidateBody(pObject, out_issues);
    }
    else if (IsType(pObject, "plWaterJunctionComponent"))
    {
      ValidateJunction(pObject, out_issues);
    }
    else if (IsType(pObject, "plWaterExclusionVolumeComponent"))
    {
      const plVariant extents = pObject->GetTypeAccessor().GetValue("Extents");
      if (!extents.IsA<plVec3>() || extents.Get<plVec3>().x <= 0.0f || extents.Get<plVec3>().y <= 0.0f || extents.Get<plVec3>().z <= 0.0f)
        AddIssue(out_issues, plWaterValidationIssue::Severity::Error, pObject, "Water exclusion volume Extents must be positive.");
    }
    else if (IsType(pObject, "plOceanComponent"))
    {
      AddIssue(out_issues, plWaterValidationIssue::Severity::Warning, pObject,
        plFmt("Legacy ocean '{}' is still present. Use Convert Legacy Ocean to Water Body.", GetObjectName(pObject)));
    }
  }
}

void plWaterEditorValidation::BuildReport(plArrayPtr<const plWaterValidationIssue> issues, plStringBuilder& out_sReport)
{
  plUInt32 uiErrors = 0;
  plUInt32 uiWarnings = 0;
  for (const auto& issue : issues)
  {
    if (issue.m_Severity == plWaterValidationIssue::Severity::Error)
      ++uiErrors;
    else
      ++uiWarnings;
  }

  out_sReport.SetFormat("Water validation: {} error(s), {} warning(s).", uiErrors, uiWarnings);
  for (const auto& issue : issues)
  {
    out_sReport.AppendFormat("\n{} {}", issue.m_Severity == plWaterValidationIssue::Severity::Error ? "[Error]" : "[Warning]", issue.m_sMessage);
  }
}
