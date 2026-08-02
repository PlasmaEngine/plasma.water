#pragma once

#include <EditorPluginWater/EditorPluginWaterDLL.h>
#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/Strings/String.h>
#include <Foundation/Types/Uuid.h>

class plDocument;
class plDocumentObject;

struct PL_EDITORPLUGINWATER_DLL plWaterValidationIssue
{
  enum class Severity : plUInt8
  {
    Warning,
    Error
  };

  Severity m_Severity = Severity::Warning;
  plUuid m_Object;
  plString m_sMessage;
};

/// Reflection-based scene validation for Water authoring data.
///
/// Keeping this service independent of concrete component headers makes the
/// editor plugin resilient to runtime component versioning and also lets it
/// diagnose scenes whose runtime plugin is temporarily unavailable.
class PL_EDITORPLUGINWATER_DLL plWaterEditorValidation
{
public:
  static void ValidateDocument(const plDocument* pDocument, plDynamicArray<plWaterValidationIssue>& out_issues);
  static void BuildReport(plArrayPtr<const plWaterValidationIssue> issues, plStringBuilder& out_sReport);
};
