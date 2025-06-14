/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file LICENSE.rst or https://cmake.org/licensing for details.  */
#include "cmInstallFileSetGenerator.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <cm/string_view>
#include <cmext/string_view>

#include "cmFileSet.h"
#include "cmGeneratorExpression.h"
#include "cmGeneratorTarget.h"
#include "cmGlobalGenerator.h"
#include "cmInstallType.h"
#include "cmList.h"
#include "cmListFileCache.h"
#include "cmLocalGenerator.h"
#include "cmMessageType.h"
#include "cmStringAlgorithms.h"
#include "cmTarget.h"

cmInstallFileSetGenerator::cmInstallFileSetGenerator(
  std::string targetName, std::string fileSetName, cmFileSetDestinations dests,
  std::string file_permissions, std::vector<std::string> const& configurations,
  std::string const& component, MessageLevel message, bool exclude_from_all,
  bool optional, cmListFileBacktrace backtrace)
  : cmInstallGenerator("", configurations, component, message,
                       exclude_from_all, false, std::move(backtrace))
  , TargetName(std::move(targetName))
  , FileSetName(std::move(fileSetName))
  , FilePermissions(std::move(file_permissions))
  , FileSetDestinations(std::move(dests))
  , Optional(optional)
{
  this->ActionsPerConfig = true;
}

cmInstallFileSetGenerator::~cmInstallFileSetGenerator() = default;

bool cmInstallFileSetGenerator::Compute(cmLocalGenerator* lg)
{
  this->LocalGenerator = lg;

  // Lookup this target in the current directory.
  this->Target = lg->FindLocalNonAliasGeneratorTarget(this->TargetName);
  if (!this->Target) {
    // If no local target has been found, find it in the global scope.
    this->Target =
      lg->GetGlobalGenerator()->FindGeneratorTarget(this->TargetName);
  }

  auto const& target = *this->Target->Target;
  this->FileSet = target.GetFileSet(this->FileSetName);

  if (!this->FileSet) {
    // No file set of the given name was ever provided for this target, nothing
    // for this generator to do
    return true;
  }

  cmList interfaceFileSetEntries{ target.GetSafeProperty(
    cmTarget::GetInterfaceFileSetsPropertyName(this->FileSet->GetType())) };

  if (std::find(interfaceFileSetEntries.begin(), interfaceFileSetEntries.end(),
                this->FileSetName) != interfaceFileSetEntries.end()) {
    if (this->FileSet->GetType() == "HEADERS"_s) {
      this->Destination = this->FileSetDestinations.Headers;
    } else {
      this->Destination = this->FileSetDestinations.CXXModules;
    }
  } else {
    // File set of the given name was provided but it's private, so give up
    this->FileSet = nullptr;
    return true;
  }

  if (this->Destination.empty()) {
    lg->IssueMessage(MessageType::FATAL_ERROR,
                     cmStrCat("File set \"", this->FileSetName,
                              "\" installed by target \"", this->TargetName,
                              "\" has no destination."));
    return false;
  }

  return true;
}

std::string cmInstallFileSetGenerator::GetDestination(
  std::string const& config) const
{
  return cmGeneratorExpression::Evaluate(this->Destination,
                                         this->LocalGenerator, config);
}

void cmInstallFileSetGenerator::GenerateScriptForConfig(
  std::ostream& os, std::string const& config, Indent indent)
{

  if (!this->FileSet) {
    return;
  }

  for (auto const& dirEntry : this->CalculateFilesPerDir(config)) {
    std::string destSub;
    if (!dirEntry.first.empty()) {
      destSub = cmStrCat('/', dirEntry.first);
    }
    this->AddInstallRule(os, cmStrCat(this->GetDestination(config), destSub),
                         cmInstallType_FILES, dirEntry.second,
                         this->GetOptional(), this->FilePermissions.c_str(),
                         nullptr, nullptr, nullptr, indent);
  }
}

std::map<std::string, std::vector<std::string>>
cmInstallFileSetGenerator::CalculateFilesPerDir(
  std::string const& config) const
{
  std::map<std::string, std::vector<std::string>> result;

  auto dirCges = this->FileSet->CompileDirectoryEntries();
  auto dirs = this->FileSet->EvaluateDirectoryEntries(
    dirCges, this->LocalGenerator, config, this->Target);

  auto fileCges = this->FileSet->CompileFileEntries();
  for (auto const& fileCge : fileCges) {
    this->FileSet->EvaluateFileEntry(
      dirs, result, fileCge, this->LocalGenerator, config, this->Target);
  }

  return result;
}
