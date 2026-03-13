#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "AppShell_Bundle.hpp"
#include "AppShell_Extended_Bundle.hpp"
#include "AppShell_Sanity.hpp"
#include "AppShell_Unbundle.hpp"
#include "EmptyTestCrypt.hpp"
#include "MockFileSystem.hpp"
#include "PeanutButter.hpp"
#include "StarterKitHelpers.hpp"

namespace peanutbutter {
namespace testkit {
namespace {

bool CopyArchiveSet(FileSystem& pFileSystem,
                    const std::string& pFrom,
                    const std::string& pTo) {
  if (!pFileSystem.EnsureDirectory(pTo)) {
    return false;
  }

  const std::vector<DirectoryEntry> aFiles = pFileSystem.ListFiles(pFrom);
  for (const DirectoryEntry& aFile : aFiles) {
    ByteBuffer aBytes;
    if (!pFileSystem.ReadFile(aFile.mPath, aBytes)) {
      return false;
    }
    const std::string aDestinationPath = pFileSystem.JoinPath(pTo, pFileSystem.FileName(aFile.mPath));
    if (!pFileSystem.WriteFile(aDestinationPath, aBytes)) {
      return false;
    }
  }
  return true;
}

void PrintSeparator(const std::string& pTitle) {
  std::cout << "\n==== " << pTitle << " ====\n";
}

}  // namespace

int RunApiAccessExample() {
  MockFileSystem aFileSystem;
  EmptyTestCrypt aCrypt;
  CapturingLogger aLogger;

  const std::string aSourceRoot = "/input";
  const std::string aArchiveRoot = "/archives";
  const std::string aUnbundleRoot = "/unzipped";
  const std::string aRecoverRoot = "/recovered";
  const std::string aMutArchiveRoot = "/archives_mut";
  const std::string aMutUnbundleRoot = "/unzipped_mut";

  aFileSystem.EnsureDirectory(aSourceRoot);
  aFileSystem.EnsureDirectory(aFileSystem.JoinPath(aSourceRoot, "empty_dir"));

  aFileSystem.WriteTextFile(aFileSystem.JoinPath(aSourceRoot, "alpha.txt"), "alpha\n");

  std::vector<unsigned char> aLargeBytes;
  aLargeBytes.resize((kPayloadBytesPerL3 * 4u) + 17u, 0u);
  for (std::size_t aIndex = 0u; aIndex < aLargeBytes.size(); ++aIndex) {
    aLargeBytes[aIndex] = static_cast<unsigned char>((aIndex * 29u) & 0xFFu);
  }
  aFileSystem.WriteFile(aFileSystem.JoinPath(aSourceRoot, "big.bin"), aLargeBytes.data(), aLargeBytes.size());

  std::vector<SourceEntry> aSourceEntries;
  if (!BuildSourceEntriesFromDirectory(aFileSystem, aSourceRoot, aSourceEntries)) {
    std::cout << "Failed to build source entries.\n";
    return 1;
  }

  BundleRequest aBundleRequest;
  aBundleRequest.mDestinationDirectory = aArchiveRoot;
  aBundleRequest.mSourceStem = "bundle_input";
  aBundleRequest.mArchivePrefix.clear();
  aBundleRequest.mArchiveSuffix = ".PBTR";
  aBundleRequest.mArchiveBlockCount = 1u;
  aBundleRequest.mUseEncryption = false;

  PrintSeparator("Bundle");
  OperationResult aBundleResult = Bundle(aBundleRequest,
                                         aSourceEntries,
                                         aFileSystem,
                                         aCrypt,
                                         aLogger,
                                         nullptr);
  PrintOperationResult("Bundle", aBundleResult);

  const std::vector<std::string> aArchiveFiles = ListSortedArchiveFiles(aFileSystem, aArchiveRoot);
  std::vector<ArchiveInspection> aInspections;
  if (InspectArchiveSet(aFileSystem, aArchiveFiles, aInspections)) {
    PrintArchiveInspectionSummary(aInspections);
  }

  PrintSeparator("Unbundle");
  UnbundleRequest aUnbundleRequest;
  aUnbundleRequest.mDestinationDirectory = aUnbundleRoot;
  aUnbundleRequest.mUseEncryption = false;

  OperationResult aUnbundleResult = Unbundle(aUnbundleRequest,
                                             aArchiveFiles,
                                             aFileSystem,
                                             aCrypt,
                                             aLogger,
                                             nullptr);
  PrintOperationResult("Unbundle", aUnbundleResult);

  std::vector<TreeSnapshotEntry> aExpectedTree;
  std::vector<TreeSnapshotEntry> aActualTree;
  std::string aCompareError;
  const bool aSnapshotLeftOk = SnapshotTree(aFileSystem, aSourceRoot, aExpectedTree);
  const bool aSnapshotRightOk = SnapshotTree(aFileSystem, aUnbundleRoot, aActualTree);
  const bool aSnapshotMatch = aSnapshotLeftOk && aSnapshotRightOk &&
                              CompareSnapshots(aExpectedTree, aActualTree, aCompareError);
  std::cout << "Round-trip snapshot match: " << (aSnapshotMatch ? "true" : "false") << "\n";
  if (!aSnapshotMatch) {
    std::cout << "Snapshot mismatch detail: " << aCompareError << "\n";
  }

  PrintSeparator("Validate API Example");
  ValidateRequest aValidateRequest;
  aValidateRequest.mLeftDirectory = aSourceRoot;
  aValidateRequest.mRightDirectory = aUnbundleRoot;
  OperationResult aValidateResult = RunSanity(aValidateRequest, aFileSystem, aLogger, nullptr);
  PrintOperationResult("Validate", aValidateResult);

  PrintSeparator("Recover (with archive gap)");
  const std::string aGapArchiveRoot = "/archives_gap";
  CopyArchiveSet(aFileSystem, aArchiveRoot, aGapArchiveRoot);
  std::vector<std::string> aGapArchives = ListSortedArchiveFiles(aFileSystem, aGapArchiveRoot);
  if (aGapArchives.size() > 2u) {
    const std::size_t aGapIndex = aGapArchives.size() / 2u;
    aFileSystem.Drive().DeletePath(aGapArchives[aGapIndex]);
    aGapArchives = ListSortedArchiveFiles(aFileSystem, aGapArchiveRoot);
  }

  UnbundleRequest aRecoverRequest;
  aRecoverRequest.mDestinationDirectory = aRecoverRoot;
  aRecoverRequest.mUseEncryption = false;

  OperationResult aRecoverResult = Recover(aRecoverRequest,
                                           aGapArchives,
                                           aFileSystem,
                                           aCrypt,
                                           aLogger,
                                           nullptr);
  PrintOperationResult("Recover", aRecoverResult);

  PrintSeparator("BundleWithMutations");
  aFileSystem.WriteTextFile(aFileSystem.JoinPath(aSourceRoot, "added.txt"), "added\n");

  std::vector<DataMutation> aDataMutations;
  aDataMutations.push_back(DataMutation{0u, std::vector<unsigned char>{0xFFu, 0x7Fu}});

  std::vector<CreateFileMutation> aCreateMutations;
  SourceEntry aCreated;
  aCreated.mSourcePath = aFileSystem.JoinPath(aSourceRoot, "added.txt");
  aCreated.mRelativePath = "added.txt";
  aCreated.mIsDirectory = false;
  aCreated.mFileLength = 6u;
  aCreateMutations.push_back(CreateFileMutation{1u, aCreated});

  std::vector<DeleteFileMutation> aDeleteMutations;
  if (!aSourceEntries.empty()) {
    aDeleteMutations.push_back(DeleteFileMutation{0u});
  }

  BundleRequest aBundleMutRequest = aBundleRequest;
  aBundleMutRequest.mDestinationDirectory = aMutArchiveRoot;

  OperationResult aMutBundleResult = BundleWithMutations(aBundleMutRequest,
                                                         aSourceEntries,
                                                         aDataMutations,
                                                         aCreateMutations,
                                                         aDeleteMutations,
                                                         aFileSystem,
                                                         aCrypt,
                                                         nullptr);
  PrintOperationResult("BundleWithMutations", aMutBundleResult);

  std::vector<std::string> aMutArchives = ListSortedArchiveFiles(aFileSystem, aMutArchiveRoot);

  UnbundleRequest aMutUnbundleRequest;
  aMutUnbundleRequest.mDestinationDirectory = aMutUnbundleRoot;
  aMutUnbundleRequest.mUseEncryption = false;

  OperationResult aMutUnbundleResult = Unbundle(aMutUnbundleRequest,
                                                aMutArchives,
                                                aFileSystem,
                                                aCrypt,
                                                aLogger,
                                                nullptr);
  PrintOperationResult("Unbundle(mutated)", aMutUnbundleResult);

  const std::string aMutRecoverRoot = "/recovered_mut";
  UnbundleRequest aMutRecoverRequest;
  aMutRecoverRequest.mDestinationDirectory = aMutRecoverRoot;
  aMutRecoverRequest.mUseEncryption = false;

  OperationResult aMutRecoverResult = Recover(aMutRecoverRequest,
                                              aMutArchives,
                                              aFileSystem,
                                              aCrypt,
                                              aLogger,
                                              nullptr);
  PrintOperationResult("Recover(mutated)", aMutRecoverResult);

  std::cout << "\nCaptured status logs: " << aLogger.StatusMessages().size() << "\n";
  std::cout << "Captured error logs: " << aLogger.ErrorMessages().size() << "\n";

  return 0;
}

}  // namespace testkit
}  // namespace peanutbutter

int main() {
  return peanutbutter::testkit::RunApiAccessExample();
}
