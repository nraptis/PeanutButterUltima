#include "Test_Execute_Bundle.hpp"

#include <algorithm>
#include <set>

#include "AppCore_Helpers.hpp"

namespace peanutbutter::testing {

namespace {

bool WriteSeedInput(FileSystem& pFileSystem,
                    const std::string& pInputDirectory,
                    const std::vector<TestSeedFile>& pSeedFiles,
                    const std::vector<std::string>& pEmptyDirectories) {
  for (const TestSeedFile& aSeedFile : pSeedFiles) {
    const std::string aAbsolutePath = pFileSystem.JoinPath(pInputDirectory, aSeedFile.mRelativePath);
    const std::string aParentPath = pFileSystem.ParentPath(aAbsolutePath);
    if (!aParentPath.empty() && aParentPath != aAbsolutePath && !pFileSystem.EnsureDirectory(aParentPath)) {
      return false;
    }
    if (!pFileSystem.WriteTextFile(aAbsolutePath, aSeedFile.mContents)) {
      return false;
    }
  }

  for (const std::string& aRelativeDirectory : pEmptyDirectories) {
    const std::string aAbsoluteDirectory = pFileSystem.JoinPath(pInputDirectory, aRelativeDirectory);
    if (!pFileSystem.EnsureDirectory(aAbsoluteDirectory)) {
      return false;
    }
  }
  return true;
}

}  // namespace

BundleExecutionResult ExecuteBundleForSeed(FileSystem& pFileSystem,
                                           ApplicationCore& pCore,
                                           const std::vector<TestSeedFile>& pSeedFiles,
                                           const BundleExecutionSpec& pSpec) {
  BundleExecutionResult aResult;
  aResult.mInputDirectory = pFileSystem.JoinPath(pSpec.mRuntimeRoot, pSpec.mInputDirectoryName);
  aResult.mArchiveDirectory = pFileSystem.JoinPath(pSpec.mRuntimeRoot, pSpec.mArchiveDirectoryName);

  if (!pFileSystem.ClearDirectory(pSpec.mRuntimeRoot) ||
      !pFileSystem.EnsureDirectory(aResult.mInputDirectory) ||
      !pFileSystem.EnsureDirectory(aResult.mArchiveDirectory)) {
    aResult.mMessage = "could not initialize bundle runtime directories.";
    return aResult;
  }

  if (!WriteSeedInput(pFileSystem, aResult.mInputDirectory, pSeedFiles, pSpec.mSeedEmptyDirectories)) {
    aResult.mMessage = "could not materialize seed input files.";
    return aResult;
  }

  BundleRequest aBundleRequest;
  aBundleRequest.mSourceDirectory = aResult.mInputDirectory;
  aBundleRequest.mDestinationDirectory = aResult.mArchiveDirectory;
  aBundleRequest.mArchivePrefix = pSpec.mArchivePrefix;
  aBundleRequest.mArchiveSuffix = pSpec.mArchiveSuffix;
  aBundleRequest.mArchiveBlockCount = pSpec.mArchiveBlockCount;
  aBundleRequest.mUseEncryption = pSpec.mUseEncryption;

  const OperationResult aBundleResult = pCore.RunBundle(aBundleRequest, DestinationAction::Clear);
  if (!aBundleResult.mSucceeded) {
    aResult.mMessage = aBundleResult.mMessage;
    return aResult;
  }

  std::vector<DirectoryEntry> aArchiveEntries = pFileSystem.ListFiles(aResult.mArchiveDirectory);
  std::sort(aArchiveEntries.begin(), aArchiveEntries.end(), [&pFileSystem](const DirectoryEntry& pLeft,
                                                                           const DirectoryEntry& pRight) {
    return pFileSystem.FileName(pLeft.mPath) < pFileSystem.FileName(pRight.mPath);
  });
  for (const DirectoryEntry& aEntry : aArchiveEntries) {
    aResult.mArchivePaths.push_back(aEntry.mPath);
  }

  aResult.mSucceeded = !aResult.mArchivePaths.empty();
  aResult.mMessage = aResult.mSucceeded ? "bundle completed." : "bundle produced no archive files.";
  return aResult;
}

BundleExecutionResult ExecuteBundleWithMutations(FileSystem& pFileSystem,
                                                 ApplicationCore& pCore,
                                                 const std::vector<TestSeedFile>& pSeedFiles,
                                                 const BundleExecutionSpec& pSpec,
                                                 const std::vector<BundlePayloadMutation>& pMutations) {
  BundleExecutionResult aResult = ExecuteBundleForSeed(pFileSystem, pCore, pSeedFiles, pSpec);
  if (!aResult.mSucceeded) {
    return aResult;
  }

  for (const BundlePayloadMutation& aMutation : pMutations) {
    if (aMutation.mArchiveOffset >= aResult.mArchivePaths.size()) {
      aResult.mSucceeded = false;
      aResult.mMessage = "mutation archive offset is outside produced archive set.";
      return aResult;
    }
    if (aMutation.mBytes.empty()) {
      continue;
    }

    const std::string& aArchivePath = aResult.mArchivePaths[aMutation.mArchiveOffset];
    ByteVector aArchiveBytes;
    if (!pFileSystem.ReadFile(aArchivePath, aArchiveBytes)) {
      aResult.mSucceeded = false;
      aResult.mMessage = "could not read archive for mutation.";
      return aResult;
    }

    const std::size_t aStartBlock = aMutation.mPayloadLogicalOffset / peanutbutter::SB_PAYLOAD_SIZE;
    const std::size_t aStartInBlockPayload = aMutation.mPayloadLogicalOffset % peanutbutter::SB_PAYLOAD_SIZE;
    const std::size_t aStartPhysicalPayloadOffset =
        (aStartBlock * peanutbutter::SB_L1_LENGTH) + peanutbutter::SB_RECOVERY_HEADER_LENGTH + aStartInBlockPayload;

    if (peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH + aStartPhysicalPayloadOffset + aMutation.mBytes.size() > aArchiveBytes.size()) {
      aResult.mSucceeded = false;
      aResult.mMessage = "mutation bytes exceed archive size.";
      return aResult;
    }

    std::set<std::size_t> aTouchedBlocks;
    for (std::size_t aByteIndex = 0; aByteIndex < aMutation.mBytes.size(); ++aByteIndex) {
      const std::size_t aLogicalOffset = aMutation.mPayloadLogicalOffset + aByteIndex;
      const std::size_t aBlockIndex = aLogicalOffset / peanutbutter::SB_PAYLOAD_SIZE;
      const std::size_t aInBlockPayload = aLogicalOffset % peanutbutter::SB_PAYLOAD_SIZE;
      const std::size_t aPhysicalPayloadOffset =
          (aBlockIndex * peanutbutter::SB_L1_LENGTH) + peanutbutter::SB_RECOVERY_HEADER_LENGTH + aInBlockPayload;
      const std::size_t aFileOffset = peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH + aPhysicalPayloadOffset;
      aArchiveBytes[aFileOffset] = aMutation.mBytes[aByteIndex];
      aTouchedBlocks.insert(aBlockIndex);
    }

    for (std::size_t aBlockIndex : aTouchedBlocks) {
      const std::size_t aBlockPayloadOffset = aBlockIndex * peanutbutter::SB_L1_LENGTH;
      const std::size_t aBlockFileOffset = peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH + aBlockPayloadOffset;
      if (aBlockFileOffset + peanutbutter::SB_L1_LENGTH > aArchiveBytes.size()) {
        continue;
      }

      unsigned char aChecksum[peanutbutter::SB_RECOVERY_CHECKSUM_LENGTH] = {};
      peanutbutter::detail::GenerateChecksum(aArchiveBytes.data() + aBlockFileOffset, aChecksum);
      std::copy(aChecksum,
                aChecksum + peanutbutter::SB_RECOVERY_CHECKSUM_LENGTH,
                aArchiveBytes.begin() + aBlockFileOffset);
    }

    if (!pFileSystem.WriteFile(aArchivePath, aArchiveBytes)) {
      aResult.mSucceeded = false;
      aResult.mMessage = "could not write mutated archive.";
      return aResult;
    }
  }

  return aResult;
}

}  // namespace peanutbutter::testing
