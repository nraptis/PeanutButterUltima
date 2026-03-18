#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "AppShell_ArchiveFormat.hpp"
#include "AppShell_Bundle.hpp"
#include "AppShell_Types.hpp"
#include "AppShell_Unbundle.hpp"
#include "IO/LocalFileSystem.hpp"

namespace peanutbutter {
namespace {

namespace fs = std::filesystem;

struct TestFailure : public std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct TestWorkspace {
  std::string mRoot;
  std::string mSource;
  std::string mArchive;
  std::string mOutput;
};

void Expect(bool pCondition, const std::string& pMessage) {
  if (!pCondition) {
    throw TestFailure(pMessage);
  }
}

bool HasStatusSubstring(const CapturingLogger& pLogger,
                        const std::string& pNeedle) {
  for (const std::string& aMessage : pLogger.StatusMessages()) {
    if (aMessage.find(pNeedle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

bool HasErrorSubstring(const CapturingLogger& pLogger,
                       const std::string& pNeedle) {
  for (const std::string& aMessage : pLogger.ErrorMessages()) {
    if (aMessage.find(pNeedle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

std::string UniqueSuffix() {
  const auto aNow = std::chrono::steady_clock::now().time_since_epoch();
  return std::to_string(
      static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(aNow).count()));
}

TestWorkspace MakeWorkspace(LocalFileSystem& pFileSystem,
                            const std::string& pName) {
  const std::string aRoot =
      (fs::temp_directory_path() /
       fs::path("pbultima_tests_" + pName + "_" + UniqueSuffix()))
          .lexically_normal()
          .generic_string();
  TestWorkspace aWorkspace;
  aWorkspace.mRoot = aRoot;
  aWorkspace.mSource = pFileSystem.JoinPath(aRoot, "source");
  aWorkspace.mArchive = pFileSystem.JoinPath(aRoot, "archive");
  aWorkspace.mOutput = pFileSystem.JoinPath(aRoot, "output");
  Expect(pFileSystem.EnsureDirectory(aWorkspace.mSource),
         "failed creating source directory");
  Expect(pFileSystem.EnsureDirectory(aWorkspace.mArchive),
         "failed creating archive directory");
  Expect(pFileSystem.EnsureDirectory(aWorkspace.mOutput),
         "failed creating output directory");
  return aWorkspace;
}

void CleanupWorkspace(const TestWorkspace& pWorkspace) {
  std::error_code aError;
  fs::remove_all(fs::path(pWorkspace.mRoot), aError);
}

std::vector<unsigned char> MakePatternBytes(std::size_t pLength,
                                            unsigned char pSeed = 0x41u) {
  std::vector<unsigned char> aBytes(pLength, 0u);
  for (std::size_t aIndex = 0u; aIndex < pLength; ++aIndex) {
    aBytes[aIndex] = static_cast<unsigned char>(
        pSeed + static_cast<unsigned char>(aIndex % 23u));
  }
  return aBytes;
}

void WriteBytes(const std::string& pPath,
                const std::vector<unsigned char>& pBytes) {
  std::ofstream aOut(fs::path(pPath), std::ios::binary);
  Expect(static_cast<bool>(aOut), "failed opening file for write: " + pPath);
  if (!pBytes.empty()) {
    aOut.write(reinterpret_cast<const char*>(pBytes.data()),
               static_cast<std::streamsize>(pBytes.size()));
  }
  Expect(static_cast<bool>(aOut), "failed writing file bytes: " + pPath);
}

void WriteText(const std::string& pPath, const std::string& pText) {
  std::ofstream aOut(fs::path(pPath), std::ios::binary);
  Expect(static_cast<bool>(aOut), "failed opening text file for write: " + pPath);
  aOut.write(pText.data(), static_cast<std::streamsize>(pText.size()));
  Expect(static_cast<bool>(aOut), "failed writing text file: " + pPath);
}

std::vector<unsigned char> ReadBytes(const std::string& pPath,
                                     LocalFileSystem& pFileSystem) {
  ByteBuffer aBuffer;
  Expect(pFileSystem.ReadFile(pPath, aBuffer), "failed reading file: " + pPath);
  std::vector<unsigned char> aBytes(aBuffer.Size(), 0u);
  if (!aBytes.empty()) {
    std::memcpy(aBytes.data(), aBuffer.Data(), aBuffer.Size());
  }
  return aBytes;
}

std::string ReadText(const std::string& pPath, LocalFileSystem& pFileSystem) {
  std::string aContents;
  Expect(pFileSystem.ReadTextFile(pPath, aContents),
         "failed reading text file: " + pPath);
  return aContents;
}

std::vector<std::string> CollectArchiveFiles(const std::string& pArchiveDirectory) {
  std::vector<std::string> aPaths;
  for (const auto& aEntry : fs::directory_iterator(fs::path(pArchiveDirectory))) {
    if (!aEntry.is_regular_file()) {
      continue;
    }
    aPaths.push_back(aEntry.path().lexically_normal().generic_string());
  }
  std::sort(aPaths.begin(), aPaths.end());
  return aPaths;
}

SourceEntry MakeFileEntry(const std::string& pPath, const std::string& pRelativePath) {
  SourceEntry aEntry;
  aEntry.mSourcePath = pPath;
  aEntry.mRelativePath = pRelativePath;
  aEntry.mIsDirectory = false;
  return aEntry;
}

SourceEntry MakeEmptyDirectoryEntry(const std::string& pRelativePath) {
  SourceEntry aEntry;
  aEntry.mSourcePath.clear();
  aEntry.mRelativePath = pRelativePath;
  aEntry.mIsDirectory = true;
  return aEntry;
}

BundleRequest MakeBundleRequest(const TestWorkspace& pWorkspace,
                                std::uint32_t pArchiveBlockCount = 1u) {
  BundleRequest aRequest;
  aRequest.mDestinationDirectory = pWorkspace.mArchive;
  aRequest.mSourceStem = "bundle_input";
  aRequest.mArchiveSuffix = ".PBTR";
  aRequest.mArchiveBlockCount = pArchiveBlockCount;
  aRequest.mUseEncryption = false;
  return aRequest;
}

UnbundleRequest MakeUnbundleRequest(const TestWorkspace& pWorkspace) {
  UnbundleRequest aRequest;
  aRequest.mDestinationDirectory = pWorkspace.mOutput;
  aRequest.mUseEncryption = false;
  return aRequest;
}

ArchiveHeader ReadHeader(const std::string& pPath, LocalFileSystem& pFileSystem) {
  ByteBuffer aBuffer;
  Expect(pFileSystem.ReadFile(pPath, aBuffer), "failed reading archive: " + pPath);
  Expect(aBuffer.Size() >= kArchiveHeaderLength,
         "archive shorter than header: " + pPath);
  ArchiveHeader aHeader;
  Expect(ReadArchiveHeaderBytes(aBuffer.Data(), kArchiveHeaderLength, aHeader),
         "failed parsing archive header: " + pPath);
  return aHeader;
}

void WriteHeader(const std::string& pPath,
                 const ArchiveHeader& pHeader,
                 LocalFileSystem& pFileSystem) {
  unsigned char aHeaderBytes[kArchiveHeaderLength] = {};
  Expect(WriteArchiveHeaderBytes(pHeader, aHeaderBytes, sizeof(aHeaderBytes)),
         "failed serializing archive header");
  Expect(pFileSystem.OverwriteFileRegion(
             pPath, 0u, aHeaderBytes, sizeof(aHeaderBytes)),
         "failed overwriting archive header: " + pPath);
}

void TestRoundtripSingleFile() {
  LocalFileSystem aFileSystem;
  const TestWorkspace aWorkspace = MakeWorkspace(aFileSystem, "roundtrip_single");
  try {
    const std::string aSourceFile = aFileSystem.JoinPath(aWorkspace.mSource, "hello.txt");
    WriteText(aSourceFile, "hello peanut butter");

    std::vector<SourceEntry> aEntries = {
        MakeFileEntry(aSourceFile, "hello.txt"),
    };

    CapturingLogger aBundleLogger;
    const OperationResult aBundleResult =
        Bundle(MakeBundleRequest(aWorkspace), aEntries, aFileSystem, aBundleLogger);
    Expect(aBundleResult.mSucceeded, "bundle failed for single-file smoke test");

    const std::vector<std::string> aArchives = CollectArchiveFiles(aWorkspace.mArchive);
    Expect(aArchives.size() == 1u, "expected exactly one archive");

    CapturingLogger aUnbundleLogger;
    const OperationResult aUnbundleResult =
        Unbundle(MakeUnbundleRequest(aWorkspace), aArchives, aFileSystem, aUnbundleLogger);
    Expect(aUnbundleResult.mSucceeded, "unbundle failed for single-file smoke test");
    Expect(ReadText(aFileSystem.JoinPath(aWorkspace.mOutput, "hello.txt"), aFileSystem) ==
               "hello peanut butter",
           "unbundled text mismatch");
  } catch (...) {
    CleanupWorkspace(aWorkspace);
    throw;
  }
  CleanupWorkspace(aWorkspace);
}

void TestRoundtripEmptyDirectoryRecord() {
  LocalFileSystem aFileSystem;
  const TestWorkspace aWorkspace = MakeWorkspace(aFileSystem, "empty_dir");
  try {
    std::vector<SourceEntry> aEntries = {
        MakeEmptyDirectoryEntry("empty_dir"),
    };

    CapturingLogger aBundleLogger;
    const OperationResult aBundleResult =
        Bundle(MakeBundleRequest(aWorkspace), aEntries, aFileSystem, aBundleLogger);
    Expect(aBundleResult.mSucceeded, "bundle failed for empty-directory smoke test");
    Expect(!HasStatusSubstring(aBundleLogger, "Missing source entry, skipping: ''"),
           "empty directory record was incorrectly treated as missing");

    const std::vector<std::string> aArchives = CollectArchiveFiles(aWorkspace.mArchive);
    CapturingLogger aUnbundleLogger;
    const OperationResult aUnbundleResult =
        Unbundle(MakeUnbundleRequest(aWorkspace), aArchives, aFileSystem, aUnbundleLogger);
    Expect(aUnbundleResult.mSucceeded, "unbundle failed for empty-directory smoke test");
    Expect(aFileSystem.IsDirectory(aFileSystem.JoinPath(aWorkspace.mOutput, "empty_dir")),
           "empty directory record was not restored");
  } catch (...) {
    CleanupWorkspace(aWorkspace);
    throw;
  }
  CleanupWorkspace(aWorkspace);
}

void TestMissingSourceEntriesAreSkipped() {
  LocalFileSystem aFileSystem;
  const TestWorkspace aWorkspace = MakeWorkspace(aFileSystem, "missing_source");
  try {
    const std::string aGoodFile = aFileSystem.JoinPath(aWorkspace.mSource, "keep.txt");
    const std::string aMissingFile = aFileSystem.JoinPath(aWorkspace.mSource, "gone.txt");
    WriteText(aGoodFile, "keep me");

    std::vector<SourceEntry> aEntries = {
        MakeFileEntry(aMissingFile, "gone.txt"),
        MakeFileEntry(aGoodFile, "keep.txt"),
    };

    CapturingLogger aBundleLogger;
    const OperationResult aBundleResult =
        Bundle(MakeBundleRequest(aWorkspace), aEntries, aFileSystem, aBundleLogger);
    Expect(aBundleResult.mSucceeded, "bundle failed while skipping missing source files");
    Expect(HasStatusSubstring(aBundleLogger, "Missing source entry, skipping: '"),
           "missing source entry log was not emitted");

    const std::vector<std::string> aArchives = CollectArchiveFiles(aWorkspace.mArchive);
    CapturingLogger aUnbundleLogger;
    const OperationResult aUnbundleResult =
        Unbundle(MakeUnbundleRequest(aWorkspace), aArchives, aFileSystem, aUnbundleLogger);
    Expect(aUnbundleResult.mSucceeded, "unbundle failed after skipping missing source files");
    Expect(aFileSystem.IsFile(aFileSystem.JoinPath(aWorkspace.mOutput, "keep.txt")),
           "kept file missing after unbundle");
    Expect(!aFileSystem.Exists(aFileSystem.JoinPath(aWorkspace.mOutput, "gone.txt")),
           "missing file unexpectedly appeared after unbundle");
  } catch (...) {
    CleanupWorkspace(aWorkspace);
    throw;
  }
  CleanupWorkspace(aWorkspace);
}

void TestInvalidDirtyTypeRequiresRecover() {
  LocalFileSystem aFileSystem;
  const TestWorkspace aWorkspace = MakeWorkspace(aFileSystem, "invalid_dirty");
  try {
    const std::string aSourceFile = aFileSystem.JoinPath(aWorkspace.mSource, "alpha.txt");
    WriteText(aSourceFile, "alpha");

    std::vector<SourceEntry> aEntries = {
        MakeFileEntry(aSourceFile, "alpha.txt"),
    };

    CapturingLogger aBundleLogger;
    Expect(Bundle(MakeBundleRequest(aWorkspace), aEntries, aFileSystem, aBundleLogger).mSucceeded,
           "bundle failed for invalid-dirty regression");

    const std::vector<std::string> aArchives = CollectArchiveFiles(aWorkspace.mArchive);
    Expect(aArchives.size() == 1u, "expected one archive for invalid-dirty regression");

    ArchiveHeader aHeader = ReadHeader(aArchives.front(), aFileSystem);
    aHeader.mDirtyType = DirtyType::kInvalid;
    WriteHeader(aArchives.front(), aHeader, aFileSystem);

    CapturingLogger aUnbundleLogger;
    const OperationResult aUnbundleResult =
        Unbundle(MakeUnbundleRequest(aWorkspace), aArchives, aFileSystem, aUnbundleLogger);
    Expect(!aUnbundleResult.mSucceeded && aUnbundleResult.mErrorCode == ErrorCode::kArchiveHeader,
           "normal unbundle should reject invalid dirty type");

    CapturingLogger aRecoverLogger;
    const OperationResult aRecoverResult =
        Recover(MakeUnbundleRequest(aWorkspace), aArchives, aFileSystem, aRecoverLogger);
    Expect(aRecoverResult.mSucceeded, "recover should succeed on invalid dirty type");
    Expect(ReadText(aFileSystem.JoinPath(aWorkspace.mOutput, "alpha.txt"), aFileSystem) == "alpha",
           "recovered contents mismatch");
  } catch (...) {
    CleanupWorkspace(aWorkspace);
    throw;
  }
  CleanupWorkspace(aWorkspace);
}

void TestIncompatibleComponentVersionsOnlyWarn() {
  LocalFileSystem aFileSystem;
  const TestWorkspace aWorkspace = MakeWorkspace(aFileSystem, "version_warning");
  try {
    const std::string aSourceFile = aFileSystem.JoinPath(aWorkspace.mSource, "beta.txt");
    WriteText(aSourceFile, "beta");

    std::vector<SourceEntry> aEntries = {
        MakeFileEntry(aSourceFile, "beta.txt"),
    };

    CapturingLogger aBundleLogger;
    Expect(Bundle(MakeBundleRequest(aWorkspace), aEntries, aFileSystem, aBundleLogger).mSucceeded,
           "bundle failed for version-warning regression");

    const std::vector<std::string> aArchives = CollectArchiveFiles(aWorkspace.mArchive);
    ArchiveHeader aHeader = ReadHeader(aArchives.front(), aFileSystem);
    aHeader.mArchiverVersion = static_cast<std::uint8_t>(kArchiverVersion + 7u);
    aHeader.mPasswordExpanderVersion =
        static_cast<std::uint8_t>(kPasswordExpanderVersion + 7u);
    aHeader.mCipherStackVersion = static_cast<std::uint8_t>(kCipherStackVersion + 7u);
    WriteHeader(aArchives.front(), aHeader, aFileSystem);

    CapturingLogger aUnbundleLogger;
    const OperationResult aUnbundleResult =
        Unbundle(MakeUnbundleRequest(aWorkspace), aArchives, aFileSystem, aUnbundleLogger);
    Expect(aUnbundleResult.mSucceeded, "unbundle should still attempt decode on version mismatch");
    Expect(HasStatusSubstring(
               aUnbundleLogger,
               "incompatible archiver library version"),
           "archiver version warning missing");
    Expect(HasStatusSubstring(
               aUnbundleLogger,
               "incompatible expander library version"),
           "expander version warning missing");
    Expect(HasStatusSubstring(
               aUnbundleLogger,
               "incompatible cipher library version"),
           "cipher version warning missing");
  } catch (...) {
    CleanupWorkspace(aWorkspace);
    throw;
  }
  CleanupWorkspace(aWorkspace);
}

void TestArchiveFamilyMismatchFailsFastButRecoverIgnoresIt() {
  LocalFileSystem aFileSystem;
  const TestWorkspace aWorkspace = MakeWorkspace(aFileSystem, "family_mismatch");
  try {
    const std::string aSourceFile = aFileSystem.JoinPath(aWorkspace.mSource, "big.bin");
    const std::vector<unsigned char> aBytes =
        MakePatternBytes((kPayloadBytesPerL3 * 2u) + 8192u, 0x31u);
    WriteBytes(aSourceFile, aBytes);

    std::vector<SourceEntry> aEntries = {
        MakeFileEntry(aSourceFile, "big.bin"),
    };

    CapturingLogger aBundleLogger;
    Expect(Bundle(MakeBundleRequest(aWorkspace), aEntries, aFileSystem, aBundleLogger).mSucceeded,
           "bundle failed for family-mismatch regression");

    const std::vector<std::string> aArchives = CollectArchiveFiles(aWorkspace.mArchive);
    Expect(aArchives.size() >= 3u, "expected at least three archives for family mismatch regression");

    ArchiveHeader aHeader = ReadHeader(aArchives[1], aFileSystem);
    aHeader.mArchiveFamilyId += 1u;
    WriteHeader(aArchives[1], aHeader, aFileSystem);

    CapturingLogger aUnbundleLogger;
    const OperationResult aUnbundleResult =
        Unbundle(MakeUnbundleRequest(aWorkspace), aArchives, aFileSystem, aUnbundleLogger);
    Expect(!aUnbundleResult.mSucceeded &&
               aUnbundleResult.mErrorCode == ErrorCode::kArchiveHeader,
           "normal unbundle should fail on family mismatch");
    Expect(aUnbundleResult.mFailureMessage.find("archive family id mismatch") != std::string::npos,
           "family mismatch failure message missing");

    const std::string aRecoverOutput =
        aFileSystem.JoinPath(aWorkspace.mRoot, "recover_output");
    Expect(aFileSystem.EnsureDirectory(aRecoverOutput),
           "failed creating recover output directory");
    UnbundleRequest aRecoverRequest;
    aRecoverRequest.mDestinationDirectory = aRecoverOutput;
    aRecoverRequest.mUseEncryption = false;
    CapturingLogger aRecoverLogger;
    const OperationResult aRecoverResult =
        Recover(aRecoverRequest, aArchives, aFileSystem, aRecoverLogger);
    Expect(aRecoverResult.mSucceeded, "recover should ignore family mismatch");
    Expect(ReadBytes(aFileSystem.JoinPath(aRecoverOutput, "big.bin"), aFileSystem) ==
               aBytes,
           "recovered bytes mismatch after family mismatch");
  } catch (...) {
    CleanupWorkspace(aWorkspace);
    throw;
  }
  CleanupWorkspace(aWorkspace);
}

void TestMissingTailIsOnlyLoggedOnExhaustion() {
  LocalFileSystem aFileSystem;
  const TestWorkspace aWorkspace = MakeWorkspace(aFileSystem, "missing_tail");
  try {
    const std::string aSourceFile = aFileSystem.JoinPath(aWorkspace.mSource, "tail.bin");
    WriteBytes(aSourceFile, MakePatternBytes(kPayloadBytesPerL3 + 8192u, 0x52u));

    std::vector<SourceEntry> aEntries = {
        MakeFileEntry(aSourceFile, "tail.bin"),
    };

    CapturingLogger aBundleLogger;
    Expect(Bundle(MakeBundleRequest(aWorkspace), aEntries, aFileSystem, aBundleLogger).mSucceeded,
           "bundle failed for missing-tail regression");

    std::vector<std::string> aArchives = CollectArchiveFiles(aWorkspace.mArchive);
    Expect(aArchives.size() >= 2u, "expected at least two archives for missing-tail regression");
    for (std::size_t aIndex = 1u; aIndex < aArchives.size(); ++aIndex) {
      std::error_code aError;
      fs::remove(fs::path(aArchives[aIndex]), aError);
      Expect(!aError, "failed deleting tail archive for regression");
    }
    aArchives.resize(1u);

    CapturingLogger aUnbundleLogger;
    const OperationResult aUnbundleResult =
        Unbundle(MakeUnbundleRequest(aWorkspace), aArchives, aFileSystem, aUnbundleLogger);
    Expect(!aUnbundleResult.mSucceeded, "unbundle should fail when planned tail is missing");
    Expect(HasStatusSubstring(aUnbundleLogger, "missing tail archive indices"),
           "missing tail log was not emitted on exhaustion");
  } catch (...) {
    CleanupWorkspace(aWorkspace);
    throw;
  }
  CleanupWorkspace(aWorkspace);
}

void TestDiscoveryDirtyStateWarnings() {
  const std::vector<std::pair<DirtyType, std::string>> aCases = {
      {DirtyType::kFinishedWithError,
       "this archive was finalized with errors"},
      {DirtyType::kFinishedWithCancel,
       "this archive was finalized with cancellation"},
      {DirtyType::kFinishedWithCancelAndError,
       "this archive was finalized with errors and cancellation"},
      {DirtyType::kFinished,
       "this archive is from a completed pack-job"},
  };

  LocalFileSystem aFileSystem;
  for (const auto& aCase : aCases) {
    const TestWorkspace aWorkspace = MakeWorkspace(aFileSystem, "dirty_warning");
    try {
      const std::string aSourceFile = aFileSystem.JoinPath(aWorkspace.mSource, "gamma.txt");
      WriteText(aSourceFile, "gamma");

      std::vector<SourceEntry> aEntries = {
          MakeFileEntry(aSourceFile, "gamma.txt"),
      };

      CapturingLogger aBundleLogger;
      Expect(Bundle(MakeBundleRequest(aWorkspace), aEntries, aFileSystem, aBundleLogger).mSucceeded,
             "bundle failed for dirty-warning regression");

      const std::vector<std::string> aArchives = CollectArchiveFiles(aWorkspace.mArchive);
      Expect(!aArchives.empty(), "expected at least one archive for dirty-warning regression");

      ArchiveHeader aHeader = ReadHeader(aArchives.front(), aFileSystem);
      aHeader.mDirtyType = aCase.first;
      WriteHeader(aArchives.front(), aHeader, aFileSystem);

      CapturingLogger aUnbundleLogger;
      const OperationResult aUnbundleResult =
          Unbundle(MakeUnbundleRequest(aWorkspace), aArchives, aFileSystem, aUnbundleLogger);
      Expect(aUnbundleResult.mSucceeded,
             "unbundle should still run for non-invalid dirty states");
      Expect(HasStatusSubstring(aUnbundleLogger, aCase.second),
             "expected discovery dirty-state warning was missing");
    } catch (...) {
      CleanupWorkspace(aWorkspace);
      throw;
    }
    CleanupWorkspace(aWorkspace);
  }
}

}  // namespace

int RunAllTests() {
  const std::vector<std::pair<std::string, std::function<void()>>> aTests = {
      {"Smoke_RoundtripSingleFile", TestRoundtripSingleFile},
      {"Smoke_RoundtripEmptyDirectoryRecord", TestRoundtripEmptyDirectoryRecord},
      {"Regression_SkipMissingSourceEntries", TestMissingSourceEntriesAreSkipped},
      {"Regression_InvalidDirtyRequiresRecover", TestInvalidDirtyTypeRequiresRecover},
      {"Regression_IncompatibleComponentVersionsOnlyWarn", TestIncompatibleComponentVersionsOnlyWarn},
      {"Regression_ArchiveFamilyMismatch", TestArchiveFamilyMismatchFailsFastButRecoverIgnoresIt},
      {"Regression_MissingTailLoggedOnExhaustion", TestMissingTailIsOnlyLoggedOnExhaustion},
      {"Regression_DiscoveryDirtyWarnings", TestDiscoveryDirtyStateWarnings},
  };

  std::size_t aFailures = 0u;
  for (const auto& aTest : aTests) {
    try {
      aTest.second();
      std::cout << "[PASS] " << aTest.first << "\n";
    } catch (const std::exception& aException) {
      ++aFailures;
      std::cerr << "[FAIL] " << aTest.first << ": " << aException.what() << "\n";
    } catch (...) {
      ++aFailures;
      std::cerr << "[FAIL] " << aTest.first << ": unknown exception\n";
    }
  }

  if (aFailures == 0u) {
    std::cout << "All tests passed.\n";
    return 0;
  }
  std::cerr << aFailures << " test(s) failed.\n";
  return 1;
}

}  // namespace peanutbutter

int main() {
  return peanutbutter::RunAllTests();
}
