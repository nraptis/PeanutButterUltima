#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "AppShell_Bundle.hpp"
#include "AppShell_Sanity.hpp"
#include "AppShell_Unbundle.hpp"
#include "Encryption/Crypt.hpp"
#include "IO/LocalFileSystem.hpp"

namespace {

using peanutbutter::CapturingLogger;
using peanutbutter::DirectoryEntry;
using peanutbutter::FileSystem;
using peanutbutter::LocalFileSystem;
using peanutbutter::OperationResult;
using peanutbutter::SourceEntry;

void PrintResult(const std::string& pLabel,
                 const OperationResult& pResult,
                 const CapturingLogger& pLogger) {
  std::cout << "[" << pLabel << "] succeeded=" << (pResult.mSucceeded ? "true" : "false")
            << " canceled=" << (pResult.mCanceled ? "true" : "false")
            << " code=" << peanutbutter::ErrorCodeToString(pResult.mErrorCode)
            << " message=" << pResult.mFailureMessage << "\n";
  if (!pResult.mSucceeded) {
    for (const std::string& aError : pLogger.ErrorMessages()) {
      std::cout << "  error: " << aError << "\n";
    }
  }
}

bool CollectBundleSourceEntries(const FileSystem& pFileSystem,
                                const std::string& pSourcePath,
                                std::vector<SourceEntry>& pEntries,
                                std::string& pErrorMessage) {
  pEntries.clear();
  if (pFileSystem.IsFile(pSourcePath)) {
    SourceEntry aEntry;
    aEntry.mSourcePath = pSourcePath;
    aEntry.mRelativePath = pFileSystem.FileName(pSourcePath);
    aEntry.mIsDirectory = false;
    pEntries.push_back(aEntry);
    return true;
  }
  if (!pFileSystem.IsDirectory(pSourcePath)) {
    pErrorMessage = "bundle source is not a readable file or folder.";
    return false;
  }

  std::vector<DirectoryEntry> aFiles = pFileSystem.ListFilesRecursive(pSourcePath);
  std::sort(aFiles.begin(), aFiles.end(), [](const DirectoryEntry& pLeft, const DirectoryEntry& pRight) {
    return pLeft.mRelativePath < pRight.mRelativePath;
  });
  for (const DirectoryEntry& aEntry : aFiles) {
    if (aEntry.mIsDirectory) {
      continue;
    }
    SourceEntry aSourceEntry;
    aSourceEntry.mSourcePath = aEntry.mPath;
    aSourceEntry.mRelativePath = aEntry.mRelativePath;
    aSourceEntry.mIsDirectory = false;
    pEntries.push_back(aSourceEntry);
  }

  std::vector<DirectoryEntry> aDirs = pFileSystem.ListDirectoriesRecursive(pSourcePath);
  std::sort(aDirs.begin(), aDirs.end(), [](const DirectoryEntry& pLeft, const DirectoryEntry& pRight) {
    return pLeft.mRelativePath < pRight.mRelativePath;
  });
  for (const DirectoryEntry& aEntry : aDirs) {
    if (aEntry.mRelativePath.empty()) {
      continue;
    }
    if (!pFileSystem.DirectoryHasEntries(aEntry.mPath)) {
      SourceEntry aSourceEntry;
      aSourceEntry.mSourcePath.clear();
      aSourceEntry.mRelativePath = aEntry.mRelativePath;
      aSourceEntry.mIsDirectory = true;
      pEntries.push_back(aSourceEntry);
    }
  }

  if (pEntries.empty()) {
    pErrorMessage = "bundle source did not produce entries.";
    return false;
  }
  return true;
}

std::vector<std::string> CollectArchiveFileList(const FileSystem& pFileSystem, const std::string& pArchiveDir) {
  std::vector<std::string> aArchiveFiles;
  if (pFileSystem.IsFile(pArchiveDir)) {
    aArchiveFiles.push_back(pArchiveDir);
    return aArchiveFiles;
  }
  std::vector<DirectoryEntry> aFiles = pFileSystem.ListFilesRecursive(pArchiveDir);
  for (const DirectoryEntry& aEntry : aFiles) {
    if (!aEntry.mIsDirectory) {
      aArchiveFiles.push_back(aEntry.mPath);
    }
  }
  std::sort(aArchiveFiles.begin(), aArchiveFiles.end());
  return aArchiveFiles;
}

bool WritePatternFile(FileSystem& pFileSystem, const std::string& pPath, std::size_t pBytes) {
  peanutbutter::ByteBuffer aBuffer;
  if (!aBuffer.Resize(pBytes)) {
    return false;
  }
  for (std::size_t aIndex = 0; aIndex < pBytes; ++aIndex) {
    aBuffer.Data()[aIndex] = static_cast<unsigned char>((aIndex * 131u) & 0xFFu);
  }
  return pFileSystem.WriteFile(pPath, aBuffer);
}

}  // namespace

int main() {
  LocalFileSystem aFileSystem;
  peanutbutter::PassthroughCrypt aCrypt;
  const std::string aRoot = "/tmp/pb_codex_sandbox";
  const std::string aInputDir = aFileSystem.JoinPath(aRoot, "input");
  const std::string aArchiveDir = aFileSystem.JoinPath(aRoot, "archive");
  const std::string aUnbundleDir = aFileSystem.JoinPath(aRoot, "unbundle_out");
  const std::string aRecoverDir = aFileSystem.JoinPath(aRoot, "recover_out");

  (void)aFileSystem.ClearDirectory(aRoot);
  if (!aFileSystem.EnsureDirectory(aInputDir) ||
      !aFileSystem.EnsureDirectory(aArchiveDir) ||
      !aFileSystem.EnsureDirectory(aUnbundleDir) ||
      !aFileSystem.EnsureDirectory(aRecoverDir)) {
    std::cerr << "failed to create smoke directories\n";
    return 1;
  }

  if (!WritePatternFile(aFileSystem, aFileSystem.JoinPath(aInputDir, "a.bin"), 410000u) ||
      !WritePatternFile(aFileSystem, aFileSystem.JoinPath(aInputDir, "b.bin"), 270000u) ||
      !aFileSystem.EnsureDirectory(aFileSystem.JoinPath(aInputDir, "empty_dir"))) {
    std::cerr << "failed to seed smoke input\n";
    return 1;
  }

  std::vector<SourceEntry> aEntries;
  std::string aCollectError;
  if (!CollectBundleSourceEntries(aFileSystem, aInputDir, aEntries, aCollectError)) {
    std::cerr << "collect source entries failed: " << aCollectError << "\n";
    return 1;
  }

  peanutbutter::BundleRequest aBundleRequest;
  aBundleRequest.mDestinationDirectory = aArchiveDir;
  aBundleRequest.mSourceStem = "bundle_input";
  aBundleRequest.mArchivePrefix.clear();
  aBundleRequest.mArchiveSuffix = ".PBTR";
  aBundleRequest.mArchiveBlockCount = 1u;
  aBundleRequest.mUseEncryption = false;

  CapturingLogger aBundleLogger;
  const OperationResult aBundleResult =
      peanutbutter::Bundle(aBundleRequest, aEntries, aFileSystem, aCrypt, aBundleLogger, nullptr);
  PrintResult("bundle", aBundleResult, aBundleLogger);
  if (!aBundleResult.mSucceeded) {
    return 2;
  }

  const std::vector<std::string> aArchiveFiles = CollectArchiveFileList(aFileSystem, aArchiveDir);
  if (aArchiveFiles.empty()) {
    std::cerr << "no archive files discovered after bundle\n";
    return 3;
  }

  peanutbutter::UnbundleRequest aUnbundleRequest;
  aUnbundleRequest.mDestinationDirectory = aUnbundleDir;
  aUnbundleRequest.mUseEncryption = false;

  CapturingLogger aUnbundleLogger;
  const OperationResult aUnbundleResult =
      peanutbutter::Unbundle(aUnbundleRequest, aArchiveFiles, aFileSystem, aCrypt, aUnbundleLogger, nullptr);
  PrintResult("unbundle", aUnbundleResult, aUnbundleLogger);
  if (!aUnbundleResult.mSucceeded) {
    return 4;
  }

  peanutbutter::ValidateRequest aValidateUnbundle;
  aValidateUnbundle.mLeftDirectory = aInputDir;
  aValidateUnbundle.mRightDirectory = aUnbundleDir;
  CapturingLogger aValidateUnbundleLogger;
  const OperationResult aValidateUnbundleResult =
      peanutbutter::RunSanity(aValidateUnbundle, aFileSystem, aValidateUnbundleLogger, nullptr);
  PrintResult("validate_unbundle", aValidateUnbundleResult, aValidateUnbundleLogger);
  if (!aValidateUnbundleResult.mSucceeded) {
    return 5;
  }

  peanutbutter::UnbundleRequest aRecoverRequest;
  aRecoverRequest.mDestinationDirectory = aRecoverDir;
  aRecoverRequest.mUseEncryption = false;
  aRecoverRequest.mRecoverMode = true;

  CapturingLogger aRecoverLogger;
  const OperationResult aRecoverResult =
      peanutbutter::Recover(aRecoverRequest, aArchiveFiles, aFileSystem, aCrypt, aRecoverLogger, nullptr);
  PrintResult("recover", aRecoverResult, aRecoverLogger);
  if (!aRecoverResult.mSucceeded) {
    return 6;
  }

  peanutbutter::ValidateRequest aValidateRecover;
  aValidateRecover.mLeftDirectory = aInputDir;
  aValidateRecover.mRightDirectory = aRecoverDir;
  CapturingLogger aValidateRecoverLogger;
  const OperationResult aValidateRecoverResult =
      peanutbutter::RunSanity(aValidateRecover, aFileSystem, aValidateRecoverLogger, nullptr);
  PrintResult("validate_recover", aValidateRecoverResult, aValidateRecoverLogger);
  if (!aValidateRecoverResult.mSucceeded) {
    return 7;
  }

  std::cout << "SMOKE_OK\n";
  return 0;
}
