#include "AppCore_Validate.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <sstream>
#include <string_view>

#include "AppCore_Helpers.hpp"

namespace peanutbutter {
namespace {

struct ComparedPath {
  std::string mPath;
  bool mIsHidden = false;
};

struct ValidationReport {
  std::vector<std::string> mOnlyInSourceVisible;
  std::vector<std::string> mOnlyInDestinationVisible;
  std::vector<std::string> mDataMismatchVisible;
  std::vector<std::string> mEmptyDirectoriesOnlyInSourceVisible;
  std::vector<std::string> mEmptyDirectoriesOnlyInDestinationVisible;
  std::vector<std::string> mOnlyInSourceHidden;
  std::vector<std::string> mOnlyInDestinationHidden;
  std::vector<std::string> mDataMismatchHidden;
  std::vector<std::string> mEmptyDirectoriesOnlyInSourceHidden;
  std::vector<std::string> mEmptyDirectoriesOnlyInDestinationHidden;
};

struct CompareProgress {
  std::uint64_t mSourceBytesProcessed = 0;
  std::uint64_t mDestinationBytesProcessed = 0;
  std::size_t mFilesScanned = 0;
};

constexpr std::size_t kValidationListLimit = 200;
constexpr std::uint64_t kValidationProgressByteStep = 500ull * 1024ull * 1024ull;

bool IsHiddenComponent(const std::string& pRelativePath) {
  std::size_t aStart = 0;
  while (aStart < pRelativePath.size()) {
    std::size_t aEnd = pRelativePath.find_first_of("/\\", aStart);
    if (aEnd == std::string::npos) {
      aEnd = pRelativePath.size();
    }
    if (aEnd > aStart) {
      const std::string_view aPart(pRelativePath.data() + aStart, aEnd - aStart);
      if (aPart != "." && aPart != ".." && !aPart.empty() && aPart.front() == '.') {
        return true;
      }
    }
    aStart = aEnd + 1;
  }
  return false;
}

void AppendGroupedLine(std::ostringstream& pReport, const std::string& pPrefix, const std::string& pPath) {
  pReport << pPrefix << pPath << "\n";
}

void WriteLimitedGroupedSection(std::ostringstream& pReport,
                                const std::string& pLabel,
                                const std::string& pPrefix,
                                const std::vector<std::string>& pPaths) {
  pReport << pLabel << " = " << pPaths.size() << "\n";
  const std::size_t aLimit = std::min(kValidationListLimit, pPaths.size());
  for (std::size_t aIndex = 0; aIndex < aLimit; ++aIndex) {
    AppendGroupedLine(pReport, pPrefix, pPaths[aIndex]);
  }
  if (pPaths.size() > aLimit) {
    pReport << "... truncated, " << (pPaths.size() - aLimit) << " more entries not shown\n";
  }
}

void ReportPathSample(Logger& pLogger,
                      const std::string& pHeader,
                      const std::vector<std::string>& pPaths) {
  if (pPaths.empty()) {
    return;
  }

  const std::size_t aLimit = std::min(kValidationListLimit, pPaths.size());
  pLogger.LogStatus(pHeader + " (" + std::to_string(aLimit) + " of " + std::to_string(pPaths.size()) + ")");
  for (std::size_t aIndex = 0; aIndex < aLimit; ++aIndex) {
    pLogger.LogStatus("  " + pPaths[aIndex]);
  }
  if (pPaths.size() > aLimit) {
    pLogger.LogStatus("  ... " + std::to_string(pPaths.size() - aLimit) + " more");
  }
}

bool FilesMatchByteForByte(const FileSystem& pFileSystem,
                           const DirectoryEntry& pLeftEntry,
                           const DirectoryEntry& pRightEntry,
                           std::uint64_t* pSourceBytesProcessed,
                           std::uint64_t* pDestinationBytesProcessed) {
  std::unique_ptr<FileReadStream> aLeftStream = pFileSystem.OpenReadStream(pLeftEntry.mPath);
  std::unique_ptr<FileReadStream> aRightStream = pFileSystem.OpenReadStream(pRightEntry.mPath);
  if (aLeftStream == nullptr || aRightStream == nullptr || !aLeftStream->IsReady() || !aRightStream->IsReady()) {
    return false;
  }
  if (aLeftStream->GetLength() != aRightStream->GetLength()) {
    return false;
  }

  unsigned char aLeftBuffer[detail::kFixedIoChunkLength] = {};
  unsigned char aRightBuffer[detail::kFixedIoChunkLength] = {};
  const std::size_t aTotalLength = aLeftStream->GetLength();
  for (std::size_t aOffset = 0; aOffset < aTotalLength; aOffset += detail::kFixedIoChunkLength) {
    const std::size_t aChunkLength = std::min(detail::kFixedIoChunkLength, aTotalLength - aOffset);
    if (!aLeftStream->Read(aOffset, aLeftBuffer, aChunkLength) ||
        !aRightStream->Read(aOffset, aRightBuffer, aChunkLength)) {
      return false;
    }
    if (pSourceBytesProcessed != nullptr) {
      *pSourceBytesProcessed += static_cast<std::uint64_t>(aChunkLength);
    }
    if (pDestinationBytesProcessed != nullptr) {
      *pDestinationBytesProcessed += static_cast<std::uint64_t>(aChunkLength);
    }
    if (std::memcmp(aLeftBuffer, aRightBuffer, aChunkLength) != 0) {
      return false;
    }
  }

  return true;
}

void LogValidationProgress(Logger& pLogger,
                           const CompareProgress& pProgress,
                           std::uint64_t& pLastLoggedBytes) {
  const std::uint64_t aTotalBytes = pProgress.mSourceBytesProcessed + pProgress.mDestinationBytesProcessed;
  if (aTotalBytes < pLastLoggedBytes + kValidationProgressByteStep) {
    return;
  }
  pLastLoggedBytes = aTotalBytes;
  const std::string aByteSummary = detail::FormatBytes(pProgress.mSourceBytesProcessed) +
                                   " source, " + detail::FormatBytes(pProgress.mDestinationBytesProcessed) +
                                   " destination";
  pLogger.LogStatus("Scanned " + aByteSummary + ", " + std::to_string(pProgress.mFilesScanned) +
                    " files checked so far.");
}

std::string BuildValidationReportText(const ValidationReport& pReport) {
  std::ostringstream aReport;
  WriteLimitedGroupedSection(aReport, "Visible-only source extras", "SRC ", pReport.mOnlyInSourceVisible);
  WriteLimitedGroupedSection(aReport, "Visible-only destination extras", "DST ", pReport.mOnlyInDestinationVisible);
  WriteLimitedGroupedSection(aReport, "Visible byte mismatches", "MISMATCH ", pReport.mDataMismatchVisible);
  WriteLimitedGroupedSection(aReport,
                             "Visible empty-directory source extras",
                             "SRC_DIR ",
                             pReport.mEmptyDirectoriesOnlyInSourceVisible);
  WriteLimitedGroupedSection(aReport,
                             "Visible empty-directory destination extras",
                             "DST_DIR ",
                             pReport.mEmptyDirectoriesOnlyInDestinationVisible);
  WriteLimitedGroupedSection(aReport, "Hidden-only source extras", "SRC_HIDDEN ", pReport.mOnlyInSourceHidden);
  WriteLimitedGroupedSection(aReport,
                             "Hidden-only destination extras",
                             "DST_HIDDEN ",
                             pReport.mOnlyInDestinationHidden);
  WriteLimitedGroupedSection(aReport, "Hidden byte mismatches", "MISMATCH_HIDDEN ", pReport.mDataMismatchHidden);
  WriteLimitedGroupedSection(aReport,
                             "Hidden empty-directory source extras",
                             "SRC_DIR_HIDDEN ",
                             pReport.mEmptyDirectoriesOnlyInSourceHidden);
  WriteLimitedGroupedSection(aReport,
                             "Hidden empty-directory destination extras",
                             "DST_DIR_HIDDEN ",
                             pReport.mEmptyDirectoriesOnlyInDestinationHidden);
  return aReport.str();
}

}  // namespace

namespace detail {

PreflightResult CheckValidateJob(const FileSystem& pFileSystem, const ValidateRequest& pRequest) {
  if (!pFileSystem.IsDirectory(pRequest.mLeftDirectory)) {
    return MakeInvalid("Sanity Failed",
                       "Sanity failed: source directory does not exist. Resolved source directory = " +
                           pRequest.mLeftDirectory);
  }
  if (!pFileSystem.IsDirectory(pRequest.mRightDirectory)) {
    return MakeInvalid("Sanity Failed",
                       "Sanity failed: destination directory does not exist. Resolved destination directory = " +
                           pRequest.mRightDirectory);
  }
  return {PreflightSignal::GreenLight, "", ""};
}

OperationResult RunValidateJob(FileSystem& pFileSystem, Logger& pLogger, const ValidateRequest& pRequest) {
  const PreflightResult aPreflight = CheckValidateJob(pFileSystem, pRequest);
  if (aPreflight.mSignal == PreflightSignal::RedLight) {
    return MakeFailure(pLogger, aPreflight.mTitle, aPreflight.mMessage);
  }

  pLogger.LogStatus("Sanity job starting...");
  const std::vector<DirectoryEntry> aLeftEntries = pFileSystem.ListFilesRecursive(pRequest.mLeftDirectory);
  const std::vector<DirectoryEntry> aRightEntries = pFileSystem.ListFilesRecursive(pRequest.mRightDirectory);
  const std::vector<DirectoryEntry> aLeftDirectories = pFileSystem.ListDirectoriesRecursive(pRequest.mLeftDirectory);
  const std::vector<DirectoryEntry> aRightDirectories = pFileSystem.ListDirectoriesRecursive(pRequest.mRightDirectory);

  std::map<std::string, ComparedPath> aLeftFiles;
  std::map<std::string, ComparedPath> aRightFiles;
  std::map<std::string, ComparedPath> aLeftEmptyDirectories;
  std::map<std::string, ComparedPath> aRightEmptyDirectories;
  for (const DirectoryEntry& aEntry : aLeftEntries) {
    if (!aEntry.mIsDirectory && !aEntry.mRelativePath.empty()) {
      aLeftFiles[aEntry.mRelativePath] = {aEntry.mPath, IsHiddenComponent(aEntry.mRelativePath)};
    }
  }
  for (const DirectoryEntry& aEntry : aRightEntries) {
    if (!aEntry.mIsDirectory && !aEntry.mRelativePath.empty()) {
      aRightFiles[aEntry.mRelativePath] = {aEntry.mPath, IsHiddenComponent(aEntry.mRelativePath)};
    }
  }
  for (const DirectoryEntry& aEntry : aLeftDirectories) {
    if (!aEntry.mRelativePath.empty() && !pFileSystem.DirectoryHasEntries(aEntry.mPath)) {
      aLeftEmptyDirectories[aEntry.mRelativePath] = {aEntry.mPath, IsHiddenComponent(aEntry.mRelativePath)};
    }
  }
  for (const DirectoryEntry& aEntry : aRightDirectories) {
    if (!aEntry.mRelativePath.empty() && !pFileSystem.DirectoryHasEntries(aEntry.mPath)) {
      aRightEmptyDirectories[aEntry.mRelativePath] = {aEntry.mPath, IsHiddenComponent(aEntry.mRelativePath)};
    }
  }

  ValidationReport aReport;
  CompareProgress aProgress;
  std::uint64_t aLastLoggedBytes = 0;

  for (const auto& aPair : aLeftFiles) {
    const auto aRightIt = aRightFiles.find(aPair.first);
    if (aRightIt == aRightFiles.end()) {
      (aPair.second.mIsHidden ? aReport.mOnlyInSourceHidden : aReport.mOnlyInSourceVisible).push_back(aPair.first);
      continue;
    }
    ++aProgress.mFilesScanned;
    if (!FilesMatchByteForByte(pFileSystem,
                               {aPair.second.mPath, aPair.first, false},
                               {aRightIt->second.mPath, aPair.first, false},
                               &aProgress.mSourceBytesProcessed,
                               &aProgress.mDestinationBytesProcessed)) {
      (aPair.second.mIsHidden ? aReport.mDataMismatchHidden : aReport.mDataMismatchVisible).push_back(aPair.first);
    }
    LogValidationProgress(pLogger, aProgress, aLastLoggedBytes);
  }

  for (const auto& aPair : aRightFiles) {
    if (aLeftFiles.find(aPair.first) == aLeftFiles.end()) {
      (aPair.second.mIsHidden ? aReport.mOnlyInDestinationHidden : aReport.mOnlyInDestinationVisible)
          .push_back(aPair.first);
    }
  }

  for (const auto& aPair : aLeftEmptyDirectories) {
    if (aRightEmptyDirectories.find(aPair.first) == aRightEmptyDirectories.end()) {
      (aPair.second.mIsHidden ? aReport.mEmptyDirectoriesOnlyInSourceHidden
                              : aReport.mEmptyDirectoriesOnlyInSourceVisible)
          .push_back(aPair.first);
    }
  }
  for (const auto& aPair : aRightEmptyDirectories) {
    if (aLeftEmptyDirectories.find(aPair.first) == aLeftEmptyDirectories.end()) {
      (aPair.second.mIsHidden ? aReport.mEmptyDirectoriesOnlyInDestinationHidden
                              : aReport.mEmptyDirectoriesOnlyInDestinationVisible)
          .push_back(aPair.first);
    }
  }

  const std::string aOutputPath =
      pFileSystem.JoinPath(pFileSystem.CurrentWorkingDirectory(), "tree_validation_report_generated.txt");
  if (pFileSystem.WriteTextFile(aOutputPath, BuildValidationReportText(aReport))) {
    pLogger.LogStatus("Generated " + aOutputPath);
  } else {
    pLogger.LogStatus("[WARN] Could not write tree_validation_report_generated.txt to " + aOutputPath + ".");
  }

  pLogger.LogStatus("Hidden-only source extras: " + std::to_string(aReport.mOnlyInSourceHidden.size()));
  pLogger.LogStatus("Hidden-only destination extras: " + std::to_string(aReport.mOnlyInDestinationHidden.size()));
  pLogger.LogStatus("Hidden byte mismatches: " + std::to_string(aReport.mDataMismatchHidden.size()));
  pLogger.LogStatus("Hidden empty-directory source extras: " +
                    std::to_string(aReport.mEmptyDirectoriesOnlyInSourceHidden.size()));
  pLogger.LogStatus("Hidden empty-directory destination extras: " +
                    std::to_string(aReport.mEmptyDirectoriesOnlyInDestinationHidden.size()));
  pLogger.LogStatus("Visible-only source extras: " + std::to_string(aReport.mOnlyInSourceVisible.size()));
  pLogger.LogStatus("Visible-only destination extras: " + std::to_string(aReport.mOnlyInDestinationVisible.size()));
  pLogger.LogStatus("Visible byte mismatches: " + std::to_string(aReport.mDataMismatchVisible.size()));
  pLogger.LogStatus("Visible empty-directory source extras: " +
                    std::to_string(aReport.mEmptyDirectoriesOnlyInSourceVisible.size()));
  pLogger.LogStatus("Visible empty-directory destination extras: " +
                    std::to_string(aReport.mEmptyDirectoriesOnlyInDestinationVisible.size()));

  ReportPathSample(pLogger, "Visible-only source sample", aReport.mOnlyInSourceVisible);
  ReportPathSample(pLogger, "Visible-only destination sample", aReport.mOnlyInDestinationVisible);
  ReportPathSample(pLogger, "Visible mismatch sample", aReport.mDataMismatchVisible);

  pLogger.LogStatus("Scanned " + std::to_string(aProgress.mFilesScanned) + " files " +
                    std::to_string(aLeftDirectories.size() + aRightDirectories.size()) + " directories.");

  const bool aSucceeded = aReport.mOnlyInSourceVisible.empty() && aReport.mOnlyInDestinationVisible.empty() &&
                          aReport.mDataMismatchVisible.empty() &&
                          aReport.mEmptyDirectoriesOnlyInSourceVisible.empty() &&
                          aReport.mEmptyDirectoriesOnlyInDestinationVisible.empty();
  pLogger.LogStatus(aSucceeded ? "[OK] source and destination trees are byte-for-byte equal."
                               : "[DIFF] source and destination trees diverged.");
  pLogger.LogStatus("Sanity job complete.");
  return {true, "Sanity Complete", "Sanity completed successfully."};
}

}  // namespace detail
}  // namespace peanutbutter
