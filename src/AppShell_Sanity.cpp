#include "AppShell_Sanity.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "AppShell_Common.hpp"

namespace peanutbutter {
namespace {

inline constexpr std::size_t kValidateCompareChunkBytes = 64u * 1024u;

OperationResult MakeSuccess() {
  OperationResult aResult;
  aResult.mSucceeded = true;
  aResult.mErrorCode = ErrorCode::kNone;
  return aResult;
}

OperationResult MakeCanceled() {
  OperationResult aResult;
  aResult.mSucceeded = false;
  aResult.mCanceled = true;
  aResult.mErrorCode = ErrorCode::kCanceled;
  return aResult;
}

OperationResult MakeFailure(const std::string& pMessage,
                            ErrorCode pCode,
                            Logger& pLogger) {
  OperationResult aResult;
  aResult.mSucceeded = false;
  aResult.mErrorCode = pCode;
  aResult.mFailureMessage = pMessage;
  pLogger.LogError(std::string(ErrorCodeToString(pCode)) + ": " + pMessage);
  return aResult;
}

bool IsHiddenRelativePath(const std::string& pRelativePath) {
  if (pRelativePath.empty()) {
    return false;
  }
  std::size_t aStart = 0u;
  while (aStart < pRelativePath.size()) {
    std::size_t aEnd = pRelativePath.find('/', aStart);
    if (aEnd == std::string::npos) {
      aEnd = pRelativePath.size();
    }
    if (aEnd > aStart && pRelativePath[aStart] == '.') {
      return true;
    }
    aStart = aEnd + 1u;
  }
  return false;
}

bool IsFolderWithNoFiles(const std::string& pFolderRelativePath,
                         const std::vector<DirectoryEntry>& pFilesInRoot) {
  const std::string aPrefix = pFolderRelativePath + "/";
  for (const DirectoryEntry& aFile : pFilesInRoot) {
    if (aFile.mRelativePath == pFolderRelativePath ||
        (aFile.mRelativePath.size() > aPrefix.size() &&
         aFile.mRelativePath.compare(0u, aPrefix.size(), aPrefix) == 0)) {
      return false;
    }
  }
  return true;
}

std::vector<DirectoryEntry> CollectFoldersWithNoFiles(const std::vector<DirectoryEntry>& pDirectoriesInRoot,
                                                      const std::vector<DirectoryEntry>& pFilesInRoot) {
  std::vector<DirectoryEntry> aOut;
  aOut.reserve(pDirectoriesInRoot.size());
  for (const DirectoryEntry& aDirectory : pDirectoriesInRoot) {
    if (IsFolderWithNoFiles(aDirectory.mRelativePath, pFilesInRoot)) {
      aOut.push_back(aDirectory);
    }
  }
  return aOut;
}

void LogCappedPathList(Logger& pLogger,
                       const std::vector<std::string>& pRelativePaths,
                       std::size_t pCap,
                       bool pIsFolder,
                       bool pIsWarn,
                       const std::string& pFromLabel,
                       const std::string& pToLabel) {
  const std::size_t aLimit = std::min(pRelativePaths.size(), pCap);
  const std::string aLevel = pIsWarn ? "Warn" : "Fail";
  for (std::size_t aIndex = 0u; aIndex < aLimit; ++aIndex) {
    std::string aPath = pFromLabel + "/" + pRelativePaths[aIndex];
    if (pIsFolder) {
      aPath += "/";
    }
    pLogger.LogStatus("[Validate][Summary] " + aLevel + ": " + aPath +
                      " was not found in " + pToLabel + ".");
  }
}

void LogCappedInequalFiles(Logger& pLogger,
                           const std::vector<std::string>& pRelativePaths,
                           std::size_t pCap,
                           const std::string& pLeftLabel,
                           const std::string& pRightLabel) {
  const std::size_t aLimit = std::min(pRelativePaths.size(), pCap);
  for (std::size_t aIndex = 0u; aIndex < aLimit; ++aIndex) {
    pLogger.LogStatus("[Validate][Summary] Warn: " + pLeftLabel + "/" + pRelativePaths[aIndex] +
                      " has inequal data in " + pRightLabel + "/" + pRelativePaths[aIndex] + ".");
  }
}

}  // namespace

OperationResult ValidateSanityInputs(const ValidateRequest& pRequest) {
  if (pRequest.mLeftDirectory.empty() || pRequest.mRightDirectory.empty()) {
    OperationResult aResult;
    aResult.mErrorCode = ErrorCode::kInvalidRequest;
    aResult.mFailureMessage = "validate inputs are invalid.";
    return aResult;
  }
  return MakeSuccess();
}

OperationResult RunSanity(const ValidateRequest& pRequest,
                          FileSystem& pFileSystem,
                          Logger& pLogger,
                          CancelCoordinator* pCancelCoordinator) {
  if (pCancelCoordinator != nullptr && pCancelCoordinator->ShouldCancelNow()) {
    return MakeCanceled();
  }
  const auto aStart = std::chrono::steady_clock::now();
  const OperationResult aValidation = ValidateSanityInputs(pRequest);
  if (!aValidation.mSucceeded) {
    pLogger.LogError(std::string(ErrorCodeToString(aValidation.mErrorCode)) + ": " +
                     (aValidation.mFailureMessage.empty() ? "validate inputs are invalid." : aValidation.mFailureMessage));
    return aValidation;
  }

  if (!pFileSystem.IsDirectory(pRequest.mLeftDirectory) || !pFileSystem.IsDirectory(pRequest.mRightDirectory)) {
    return MakeFailure("both paths must be existing directories.", ErrorCode::kInvalidRequest, pLogger);
  }

  const std::string aD1Label = "d1";
  const std::string aD2Label = "d2";
  pLogger.LogStatus("[Validate][Discovery] Collecting comparison file list START.");
  pLogger.LogStatus("[Validate][Discovery] d1: " + pRequest.mLeftDirectory);
  pLogger.LogStatus("[Validate][Discovery] d2: " + pRequest.mRightDirectory);
  std::size_t aDiscoveryScannedD1 = 0u;
  std::size_t aDiscoveryScannedD2 = 0u;
  std::size_t aNextDiscoveryProgressCount =
      static_cast<std::size_t>(std::max<std::uint32_t>(1u, kProgressCountLogIntervalDefault));
  const auto MaybeLogDiscoveryProgress = [&](bool pForce) {
    const std::size_t aTotalScanned = aDiscoveryScannedD1 + aDiscoveryScannedD2;
    if (!pForce) {
      if (aTotalScanned < aNextDiscoveryProgressCount) {
        return;
      }
    } else if (aTotalScanned == 0u) {
      return;
    }
    pLogger.LogStatus("[Validate][Discovery] Scanned " + std::to_string(aDiscoveryScannedD1) +
                      " items in d1 and " + std::to_string(aDiscoveryScannedD2) + " items in d2.");
    while (aNextDiscoveryProgressCount <= aTotalScanned) {
      aNextDiscoveryProgressCount +=
          static_cast<std::size_t>(std::max<std::uint32_t>(1u, kProgressCountLogIntervalDefault));
    }
  };

  const std::size_t aD1FilesBase = aDiscoveryScannedD1;
  std::vector<DirectoryEntry> aLeft = pFileSystem.ListFilesRecursive(
      pRequest.mLeftDirectory,
      [&](std::size_t pCount) {
        aDiscoveryScannedD1 = aD1FilesBase + pCount;
        MaybeLogDiscoveryProgress(false);
        if (pCancelCoordinator != nullptr && pCancelCoordinator->ShouldCancelNow()) {
          return false;
        }
        return true;
      });
  if (pCancelCoordinator != nullptr && pCancelCoordinator->ShouldCancelNow()) {
    return MakeCanceled();
  }
  aDiscoveryScannedD1 = aD1FilesBase + aLeft.size();
  MaybeLogDiscoveryProgress(false);

  const std::size_t aD2FilesBase = aDiscoveryScannedD2;
  std::vector<DirectoryEntry> aRight = pFileSystem.ListFilesRecursive(
      pRequest.mRightDirectory,
      [&](std::size_t pCount) {
        aDiscoveryScannedD2 = aD2FilesBase + pCount;
        MaybeLogDiscoveryProgress(false);
        if (pCancelCoordinator != nullptr && pCancelCoordinator->ShouldCancelNow()) {
          return false;
        }
        return true;
      });
  if (pCancelCoordinator != nullptr && pCancelCoordinator->ShouldCancelNow()) {
    return MakeCanceled();
  }
  aDiscoveryScannedD2 = aD2FilesBase + aRight.size();
  MaybeLogDiscoveryProgress(false);

  const std::size_t aD1FoldersBase = aDiscoveryScannedD1;
  const std::vector<DirectoryEntry> aLeftDirectories = pFileSystem.ListDirectoriesRecursive(
      pRequest.mLeftDirectory,
      [&](std::size_t pCount) {
        aDiscoveryScannedD1 = aD1FoldersBase + pCount;
        MaybeLogDiscoveryProgress(false);
        if (pCancelCoordinator != nullptr && pCancelCoordinator->ShouldCancelNow()) {
          return false;
        }
        return true;
      });
  if (pCancelCoordinator != nullptr && pCancelCoordinator->ShouldCancelNow()) {
    return MakeCanceled();
  }
  aDiscoveryScannedD1 = aD1FoldersBase + aLeftDirectories.size();
  MaybeLogDiscoveryProgress(false);

  const std::size_t aD2FoldersBase = aDiscoveryScannedD2;
  const std::vector<DirectoryEntry> aRightDirectories = pFileSystem.ListDirectoriesRecursive(
      pRequest.mRightDirectory,
      [&](std::size_t pCount) {
        aDiscoveryScannedD2 = aD2FoldersBase + pCount;
        MaybeLogDiscoveryProgress(false);
        if (pCancelCoordinator != nullptr && pCancelCoordinator->ShouldCancelNow()) {
          return false;
        }
        return true;
      });
  if (pCancelCoordinator != nullptr && pCancelCoordinator->ShouldCancelNow()) {
    return MakeCanceled();
  }
  aDiscoveryScannedD2 = aD2FoldersBase + aRightDirectories.size();
  MaybeLogDiscoveryProgress(false);
  const std::vector<DirectoryEntry> aLeftFoldersWithNoFiles = CollectFoldersWithNoFiles(aLeftDirectories, aLeft);
  const std::vector<DirectoryEntry> aRightFoldersWithNoFiles = CollectFoldersWithNoFiles(aRightDirectories, aRight);
  MaybeLogDiscoveryProgress(true);
  pLogger.LogStatus("[Validate][Discovery] Found " + std::to_string(aLeft.size()) +
                    " left files and " + std::to_string(aRight.size()) + " right files.");
  pLogger.LogStatus("[Validate][Discovery] Comparison file list prepared DONE.");

  ElapsedTimeLogGate aElapsedLog("Validate", pLogger);
  std::uint64_t aFilesCompared = 0u;
  std::uint64_t aBytesCompared = 0u;
  std::uint64_t aFoldersCompared = 0u;
  const std::uint64_t aCompareProgressIntervalBytes = std::max<std::uint64_t>(
      static_cast<std::uint64_t>(kValidateCompareChunkBytes),
      static_cast<std::uint64_t>(kProgressByteLogIntervalDefault));
  std::uint64_t aNextCompareProgressBytes = aCompareProgressIntervalBytes;
  const auto MaybeLogCompareProgress = [&]() {
    while (aBytesCompared >= aNextCompareProgressBytes) {
      pLogger.LogStatus("[Validate] Compared " + std::to_string(aFilesCompared) +
                        " files (" + FormatHumanBytes(aBytesCompared) + ").");
      aNextCompareProgressBytes += aCompareProgressIntervalBytes;
    }
  };
  pLogger.LogStatus("[Validate] Compared 0 files (0B) START.");

  ByteBuffer aLeftCompareBuffer;
  ByteBuffer aRightCompareBuffer;
  if (!aLeftCompareBuffer.Resize(kValidateCompareChunkBytes) ||
      !aRightCompareBuffer.Resize(kValidateCompareChunkBytes)) {
    return MakeFailure("could not allocate compare buffers.", ErrorCode::kInternal, pLogger);
  }

  std::unordered_map<std::string, DirectoryEntry> aLeftFilesByRelative;
  std::unordered_map<std::string, DirectoryEntry> aRightFilesByRelative;
  aLeftFilesByRelative.reserve(aLeft.size());
  aRightFilesByRelative.reserve(aRight.size());
  for (const DirectoryEntry& aFile : aLeft) {
    aLeftFilesByRelative[aFile.mRelativePath] = aFile;
  }
  for (const DirectoryEntry& aFile : aRight) {
    aRightFilesByRelative[aFile.mRelativePath] = aFile;
  }

  std::unordered_set<std::string> aLeftFolderSet;
  std::unordered_set<std::string> aRightFolderSet;
  aLeftFolderSet.reserve(aLeftFoldersWithNoFiles.size());
  aRightFolderSet.reserve(aRightFoldersWithNoFiles.size());
  for (const DirectoryEntry& aFolder : aLeftFoldersWithNoFiles) {
    aLeftFolderSet.insert(aFolder.mRelativePath);
  }
  for (const DirectoryEntry& aFolder : aRightFoldersWithNoFiles) {
    aRightFolderSet.insert(aFolder.mRelativePath);
  }

  std::vector<std::string> aHiddenFoldersMissingFromD2;
  std::vector<std::string> aHiddenFilesMissingFromD2;
  std::vector<std::string> aNormalFoldersMissingFromD2;
  std::vector<std::string> aNormalFilesMissingFromD2;
  std::vector<std::string> aHiddenFoldersMissingFromD1;
  std::vector<std::string> aHiddenFilesMissingFromD1;
  std::vector<std::string> aNormalFoldersMissingFromD1;
  std::vector<std::string> aNormalFilesMissingFromD1;
  std::vector<std::string> aHiddenInequalFiles;
  std::vector<std::string> aNormalInequalFiles;

  for (const auto& aPair : aLeftFolderSet) {
    if (pCancelCoordinator != nullptr && pCancelCoordinator->ShouldCancelNow()) {
      return MakeCanceled();
    }
    if (aRightFolderSet.find(aPair) != aRightFolderSet.end()) {
      ++aFoldersCompared;
      continue;
    }
    if (IsHiddenRelativePath(aPair)) {
      aHiddenFoldersMissingFromD2.push_back(aPair);
    } else {
      aNormalFoldersMissingFromD2.push_back(aPair);
    }
  }
  for (const auto& aPair : aRightFolderSet) {
    if (pCancelCoordinator != nullptr && pCancelCoordinator->ShouldCancelNow()) {
      return MakeCanceled();
    }
    if (aLeftFolderSet.find(aPair) != aLeftFolderSet.end()) {
      continue;
    }
    if (IsHiddenRelativePath(aPair)) {
      aHiddenFoldersMissingFromD1.push_back(aPair);
    } else {
      aNormalFoldersMissingFromD1.push_back(aPair);
    }
  }

  for (const auto& aPair : aLeftFilesByRelative) {
    if (pCancelCoordinator != nullptr && pCancelCoordinator->ShouldCancelNow()) {
      return MakeCanceled();
    }
    aElapsedLog.MaybeLog();
    const std::string& aRelativePath = aPair.first;
    auto aRightIt = aRightFilesByRelative.find(aRelativePath);
    if (aRightIt == aRightFilesByRelative.end()) {
      if (IsHiddenRelativePath(aRelativePath)) {
        aHiddenFilesMissingFromD2.push_back(aRelativePath);
      } else {
        aNormalFilesMissingFromD2.push_back(aRelativePath);
      }
      continue;
    }

    std::unique_ptr<FileReadStream> aLeftRead = pFileSystem.OpenReadStream(aPair.second.mPath);
    std::unique_ptr<FileReadStream> aRightRead = pFileSystem.OpenReadStream(aRightIt->second.mPath);
    if (aLeftRead == nullptr || !aLeftRead->IsReady() ||
        aRightRead == nullptr || !aRightRead->IsReady()) {
      return MakeFailure("could not open files for byte comparison.", ErrorCode::kFileSystem, pLogger);
    }

    const std::size_t aLeftLength = aLeftRead->GetLength();
    const std::size_t aRightLength = aRightRead->GetLength();
    bool aIsInequal = false;
    if (aLeftLength != aRightLength) {
      aIsInequal = true;
    } else {
      std::size_t aOffset = 0u;
      while (aOffset < aLeftLength) {
        if (pCancelCoordinator != nullptr && pCancelCoordinator->ShouldCancelNow()) {
          return MakeCanceled();
        }

        const std::size_t aChunkSize = std::min<std::size_t>(kValidateCompareChunkBytes, aLeftLength - aOffset);
        if (!aLeftRead->Read(aOffset, aLeftCompareBuffer.Data(), aChunkSize) ||
            !aRightRead->Read(aOffset, aRightCompareBuffer.Data(), aChunkSize)) {
          return MakeFailure("could not read files for byte comparison.", ErrorCode::kFileSystem, pLogger);
        }

        std::size_t aMatchedBytes = aChunkSize;
        if (std::memcmp(aLeftCompareBuffer.Data(), aRightCompareBuffer.Data(), aChunkSize) != 0) {
          aMatchedBytes = 0u;
          while (aMatchedBytes < aChunkSize &&
                 aLeftCompareBuffer.Data()[aMatchedBytes] == aRightCompareBuffer.Data()[aMatchedBytes]) {
            ++aMatchedBytes;
          }
        }

        aBytesCompared += static_cast<std::uint64_t>(aMatchedBytes);
        aElapsedLog.MaybeLog();
        MaybeLogCompareProgress();

        if (aMatchedBytes != aChunkSize) {
          aIsInequal = true;
          break;
        }
        aOffset += aChunkSize;
      }
    }
    if (aIsInequal) {
      if (IsHiddenRelativePath(aRelativePath)) {
        aHiddenInequalFiles.push_back(aRelativePath);
      } else {
        aNormalInequalFiles.push_back(aRelativePath);
      }
    }
    ++aFilesCompared;
  }

  for (const auto& aPair : aRightFilesByRelative) {
    if (pCancelCoordinator != nullptr && pCancelCoordinator->ShouldCancelNow()) {
      return MakeCanceled();
    }
    if (aLeftFilesByRelative.find(aPair.first) != aLeftFilesByRelative.end()) {
      continue;
    }
    if (IsHiddenRelativePath(aPair.first)) {
      aHiddenFilesMissingFromD1.push_back(aPair.first);
    } else {
      aNormalFilesMissingFromD1.push_back(aPair.first);
    }
  }

  std::sort(aHiddenFoldersMissingFromD2.begin(), aHiddenFoldersMissingFromD2.end());
  std::sort(aHiddenFilesMissingFromD2.begin(), aHiddenFilesMissingFromD2.end());
  std::sort(aNormalFoldersMissingFromD2.begin(), aNormalFoldersMissingFromD2.end());
  std::sort(aNormalFilesMissingFromD2.begin(), aNormalFilesMissingFromD2.end());
  std::sort(aHiddenFoldersMissingFromD1.begin(), aHiddenFoldersMissingFromD1.end());
  std::sort(aHiddenFilesMissingFromD1.begin(), aHiddenFilesMissingFromD1.end());
  std::sort(aNormalFoldersMissingFromD1.begin(), aNormalFoldersMissingFromD1.end());
  std::sort(aNormalFilesMissingFromD1.begin(), aNormalFilesMissingFromD1.end());
  std::sort(aHiddenInequalFiles.begin(), aHiddenInequalFiles.end());
  std::sort(aNormalInequalFiles.begin(), aNormalInequalFiles.end());

  LogCappedPathList(pLogger, aHiddenFoldersMissingFromD2, kValidationLogCapFolders, true, true, aD1Label, aD2Label);
  LogCappedPathList(pLogger, aHiddenFoldersMissingFromD1, kValidationLogCapFolders, true, true, aD2Label, aD1Label);
  LogCappedPathList(pLogger, aHiddenFilesMissingFromD2, kValidationLogCapFiles, false, true, aD1Label, aD2Label);
  LogCappedPathList(pLogger, aHiddenFilesMissingFromD1, kValidationLogCapFiles, false, true, aD2Label, aD1Label);
  LogCappedPathList(pLogger, aNormalFoldersMissingFromD2, kValidationLogCapFolders, true, false, aD1Label, aD2Label);
  LogCappedPathList(pLogger, aNormalFoldersMissingFromD1, kValidationLogCapFolders, true, false, aD2Label, aD1Label);
  LogCappedPathList(pLogger, aNormalFilesMissingFromD2, kValidationLogCapFiles, false, false, aD1Label, aD2Label);
  LogCappedPathList(pLogger, aNormalFilesMissingFromD1, kValidationLogCapFiles, false, false, aD2Label, aD1Label);
  LogCappedInequalFiles(pLogger, aHiddenInequalFiles, kValidationLogCapFiles, aD1Label, aD2Label);
  LogCappedInequalFiles(pLogger, aNormalInequalFiles, kValidationLogCapFiles, aD1Label, aD2Label);

  pLogger.LogStatus("[Validate][Summary] d1: " + pRequest.mLeftDirectory);
  pLogger.LogStatus("[Validate][Summary] d2: " + pRequest.mRightDirectory);

  if (!aHiddenFoldersMissingFromD2.empty()) {
    pLogger.LogStatus("[Validate][Summary] Warn: Found " + std::to_string(aHiddenFoldersMissingFromD2.size()) +
                      " hidden folders in d1 missing in d2.");
  }
  if (!aHiddenFilesMissingFromD2.empty()) {
    pLogger.LogStatus("[Validate][Summary] Warn: Found " + std::to_string(aHiddenFilesMissingFromD2.size()) +
                      " hidden files in d1 missing in d2.");
  }
  if (!aHiddenFoldersMissingFromD1.empty()) {
    pLogger.LogStatus("[Validate][Summary] Warn: Found " + std::to_string(aHiddenFoldersMissingFromD1.size()) +
                      " hidden folders in d2 missing in d1.");
  }
  if (!aHiddenFilesMissingFromD1.empty()) {
    pLogger.LogStatus("[Validate][Summary] Warn: Found " + std::to_string(aHiddenFilesMissingFromD1.size()) +
                      " hidden files in d2 missing in d1.");
  }
  if (!aHiddenInequalFiles.empty()) {
    pLogger.LogStatus("[Validate][Summary] Warn: Found " + std::to_string(aHiddenInequalFiles.size()) +
                      " hidden files with inequal data.");
  }
  if (!aNormalFoldersMissingFromD2.empty()) {
    pLogger.LogStatus("[Validate][Summary] Fail: Found " + std::to_string(aNormalFoldersMissingFromD2.size()) +
                      " normal folders in d1 missing in d2.");
  }
  if (!aNormalFilesMissingFromD2.empty()) {
    pLogger.LogStatus("[Validate][Summary] Fail: Found " + std::to_string(aNormalFilesMissingFromD2.size()) +
                      " normal files in d1 missing in d2.");
  }
  if (!aNormalFoldersMissingFromD1.empty()) {
    pLogger.LogStatus("[Validate][Summary] Fail: Found " + std::to_string(aNormalFoldersMissingFromD1.size()) +
                      " normal folders in d2 missing in d1.");
  }
  if (!aNormalFilesMissingFromD1.empty()) {
    pLogger.LogStatus("[Validate][Summary] Fail: Found " + std::to_string(aNormalFilesMissingFromD1.size()) +
                      " normal files in d2 missing in d1.");
  }
  if (!aNormalInequalFiles.empty()) {
    pLogger.LogStatus("[Validate][Summary] Warn: Found " + std::to_string(aNormalInequalFiles.size()) +
                      " normal files with inequal data.");
  }

  pLogger.LogStatus("[Validate][Summary] Compared " + std::to_string(aFilesCompared) +
                    " files and " + std::to_string(aFoldersCompared) + " folders (" +
                    FormatHumanBytes(aBytesCompared) + ").");

  const std::size_t aHiddenMismatches =
      aHiddenFoldersMissingFromD2.size() + aHiddenFilesMissingFromD2.size() +
      aHiddenFoldersMissingFromD1.size() + aHiddenFilesMissingFromD1.size() +
      aHiddenInequalFiles.size();
  const std::size_t aNormalMismatches =
      aNormalFoldersMissingFromD2.size() + aNormalFilesMissingFromD2.size() +
      aNormalFoldersMissingFromD1.size() + aNormalFilesMissingFromD1.size() +
      aNormalInequalFiles.size();

  if (aHiddenMismatches > 0u && aNormalMismatches > 0u) {
    pLogger.LogStatus("[Validate][Summary] Warn: Found " + std::to_string(aHiddenMismatches) +
                      " mismatches in hidden files and folders.");
    pLogger.LogStatus("[Validate][Summary] Fail: Found " + std::to_string(aNormalMismatches) +
                      " mismatches in normal files and folders.");
  } else if (aHiddenMismatches > 0u) {
    pLogger.LogStatus("[Validate][Summary] Warn: Found " + std::to_string(aHiddenMismatches) +
                      " mismatches in hidden files and folders.");
  } else if (aNormalMismatches > 0u) {
    pLogger.LogStatus("[Validate][Summary] Fail: Found " + std::to_string(aNormalMismatches) +
                      " mismatches in normal files and folders.");
  } else {
    pLogger.LogStatus("[Validate][Summary] Good: This was a healthy execution.");
  }

  const auto aElapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - aStart);
  pLogger.LogStatus("[Validate][Summary] Total time was " +
                    FormatHumanDurationSeconds(static_cast<std::uint64_t>(aElapsed.count())) + ".");

  if (aNormalMismatches == 0u) {
    if (aHiddenMismatches == 0u) {
      return MakeSuccess();
    }
    return MakeSuccess();
  }

  return MakeFailure("normal-file or normal-folder mismatches were found.", ErrorCode::kRecordParse, pLogger);
}

}  // namespace peanutbutter
