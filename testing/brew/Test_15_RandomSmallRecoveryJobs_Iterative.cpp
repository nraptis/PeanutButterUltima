#include <algorithm>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "AppCore.hpp"
#include "MockFileSystem.hpp"
#include "Test_Wrappers.hpp"

namespace {

using namespace peanutbutter;
using peanutbutter::testing::MockFileSystem;
using peanutbutter::testing::TestFile;

std::string Pad2(int pValue) {
  if (pValue < 10) {
    return "0" + std::to_string(pValue);
  }
  return std::to_string(pValue);
}

std::string MakeRelativePath(int pJobIndex, int pFileIndex) {
  if (pFileIndex % 3 == 0) {
    return "group_" + Pad2(pJobIndex % 7) + "/file_" + Pad2(pFileIndex) + ".txt";
  }
  return "file_" + Pad2(pFileIndex) + ".txt";
}

ByteVector RandomBytes(std::mt19937& pRng, int pLength) {
  std::uniform_int_distribution<int> aCharDistribution(0, 25);
  ByteVector aBytes;
  aBytes.reserve(static_cast<std::size_t>(pLength));
  for (int aIndex = 0; aIndex < pLength; ++aIndex) {
    aBytes.push_back(static_cast<unsigned char>('a' + aCharDistribution(pRng)));
  }
  return aBytes;
}

std::vector<TestFile> CollectFiles(const FileSystem& pFileSystem, const std::string& pRootPath) {
  std::vector<DirectoryEntry> aEntries = pFileSystem.ListFilesRecursive(pRootPath);
  std::sort(aEntries.begin(), aEntries.end(),
            [](const DirectoryEntry& pLeft, const DirectoryEntry& pRight) {
              return pLeft.mRelativePath < pRight.mRelativePath;
            });

  std::vector<TestFile> aFiles;
  for (const DirectoryEntry& aEntry : aEntries) {
    if (aEntry.mIsDirectory) {
      continue;
    }
    ByteVector aBytes;
    if (!pFileSystem.ReadFile(aEntry.mPath, aBytes)) {
      continue;
    }
    aFiles.emplace_back(aEntry.mRelativePath, std::move(aBytes));
  }
  return aFiles;
}

bool FilesExactlyMatch(const std::vector<TestFile>& pExpected,
                       const std::vector<TestFile>& pActual,
                       std::string* pErrorMessage) {
  if (pExpected.size() != pActual.size()) {
    if (pErrorMessage != nullptr) {
      *pErrorMessage = "file count mismatch expected=" + std::to_string(pExpected.size()) +
                       " actual=" + std::to_string(pActual.size());
    }
    return false;
  }
  for (std::size_t aIndex = 0; aIndex < pExpected.size(); ++aIndex) {
    if (!pExpected[aIndex].Equals(pActual[aIndex], pErrorMessage)) {
      if (pErrorMessage != nullptr && !pErrorMessage->empty()) {
        *pErrorMessage = "file mismatch at index " + std::to_string(aIndex) + ": " + *pErrorMessage;
      }
      return false;
    }
  }
  return true;
}

bool RecoveredFilesMatchSuffix(const std::vector<TestFile>& pInputFiles,
                               const std::vector<TestFile>& pRecoveredFiles,
                               std::string* pErrorMessage) {
  if (pRecoveredFiles.empty()) {
    if (pErrorMessage != nullptr) {
      *pErrorMessage = "recovered file list was empty.";
    }
    return false;
  }

  for (std::size_t aStartIndex = 0; aStartIndex < pInputFiles.size(); ++aStartIndex) {
    if (pInputFiles[aStartIndex].mPath != pRecoveredFiles.front().mPath) {
      continue;
    }
    const std::vector<TestFile> aExpectedSuffix(pInputFiles.begin() + static_cast<std::ptrdiff_t>(aStartIndex),
                                                pInputFiles.end());
    return FilesExactlyMatch(aExpectedSuffix, pRecoveredFiles, pErrorMessage);
  }

  if (pErrorMessage != nullptr) {
    *pErrorMessage = "could not match recovered first path '" + pRecoveredFiles.front().mPath + "' to input tree.";
  }
  return false;
}

bool RunIterativeRecoverySuite() {
  std::mt19937 aRng(6100);
  std::uniform_int_distribution<int> aArchiveBlockDistribution(1, 2);
  std::uniform_int_distribution<int> aFileCountDistribution(8, 20);
  std::uniform_int_distribution<int> aContentLengthDistribution(1, 40);

  for (int aJobIndex = 0; aJobIndex < 15; ++aJobIndex) {
    MockFileSystem aFileSystem;
    PassthroughCrypt aCrypt;
    CapturingLogger aLogger;
    RuntimeSettings aSettings;
    aSettings.mArchiveFileLength =
        peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH +
        (peanutbutter::SB_L3_LENGTH * static_cast<std::size_t>(aArchiveBlockDistribution(aRng)));
    ApplicationCore aCore(aFileSystem, aCrypt, aLogger, aSettings);

    const int aFileCount = aFileCountDistribution(aRng);
    for (int aFileIndex = 0; aFileIndex < aFileCount; ++aFileIndex) {
      const std::string aRelativePath = MakeRelativePath(aJobIndex, aFileIndex);
      const ByteVector aBytes = RandomBytes(aRng, aContentLengthDistribution(aRng));
      aFileSystem.AddFile("/input/" + aRelativePath, aBytes);
    }

    const std::vector<TestFile> aInputFiles = CollectFiles(aFileSystem, "/input");
    if (aInputFiles.empty()) {
      std::cerr << "Iterative recovery job " << aJobIndex << " produced no input files.\n";
      return false;
    }

    BundleRequest aBundleRequest;
    aBundleRequest.mSourceDirectory = "/input";
    aBundleRequest.mDestinationDirectory = "/archives";
    aBundleRequest.mArchivePrefix = "bundle_";
    aBundleRequest.mArchiveSuffix = ".PBTR";
    const OperationResult aBundleResult = aCore.RunBundle(aBundleRequest, DestinationAction::Clear);
    if (!aBundleResult.mSucceeded) {
      std::cerr << "Iterative recovery bundle failed on job " << aJobIndex
                << ": " << aBundleResult.mMessage << "\n";
      return false;
    }

    std::vector<DirectoryEntry> aArchives = aFileSystem.ListFiles("/archives");
    std::sort(aArchives.begin(), aArchives.end(),
              [](const DirectoryEntry& pLeft, const DirectoryEntry& pRight) { return pLeft.mPath < pRight.mPath; });
    if (aArchives.empty()) {
      std::cerr << "Iterative recovery job " << aJobIndex << " produced no archives.\n";
      return false;
    }

    bool aObservedSuccess = false;
    for (const DirectoryEntry& aArchive : aArchives) {
      RecoverRequest aRecoverRequest;
      aRecoverRequest.mArchiveDirectory = "/archives";
      aRecoverRequest.mRecoveryStartFilePath = aArchive.mPath;
      aRecoverRequest.mDestinationDirectory = "/recovered";

      const OperationResult aRecoverResult = aCore.RunRecover(aRecoverRequest, DestinationAction::Clear);
      if (!aRecoverResult.mSucceeded) {
        continue;
      }

      aObservedSuccess = true;
      const std::vector<TestFile> aRecoveredFiles = CollectFiles(aFileSystem, "/recovered");
      std::string aComparisonError;
      if (!RecoveredFilesMatchSuffix(aInputFiles, aRecoveredFiles, &aComparisonError)) {
        std::cerr << "Iterative recovery mismatch on job " << aJobIndex
                  << " from archive " << aArchive.mPath << ": " << aComparisonError << "\n";
        return false;
      }
    }

    if (!aObservedSuccess) {
      std::cerr << "Iterative recovery job " << aJobIndex << " had no successful recovery start.\n";
      return false;
    }
  }

  return true;
}

}  // namespace

int main() {
  if (!RunIterativeRecoverySuite()) {
    return 1;
  }

  std::cout << "Test_15_RandomSmallRecoveryJobs_Iterative passed\n";
  return 0;
}
