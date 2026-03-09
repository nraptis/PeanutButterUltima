#include "AppCore_Stackers.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace peanutbutter::ultima {

namespace {

constexpr std::size_t kRecoveryHeaderLength = peanutbutter::SB_RECOVERY_HEADER_LENGTH;
constexpr std::size_t kPayloadBytesPerL1 = peanutbutter::SB_PAYLOAD_SIZE;
constexpr std::size_t kL1Length = peanutbutter::SB_L1_LENGTH;
constexpr std::size_t kL3Length = peanutbutter::SB_L3_LENGTH;

std::size_t LogicalCapacityForPhysicalLength(std::size_t pPhysicalLength) {
  const std::size_t aFullBlocks = pPhysicalLength / kL1Length;
  const std::size_t aRemainder = pPhysicalLength % kL1Length;
  const std::size_t aPartialPayloadLength =
      aRemainder > kRecoveryHeaderLength ? aRemainder - kRecoveryHeaderLength : 0;
  return (aFullBlocks * kPayloadBytesPerL1) + aPartialPayloadLength;
}

std::size_t PhysicalOffsetForLogicalOffset(std::size_t pLogicalOffset) {
  const std::size_t aBlockIndex = pLogicalOffset / kPayloadBytesPerL1;
  const std::size_t aOffsetInBlock = pLogicalOffset % kPayloadBytesPerL1;
  return (aBlockIndex * kL1Length) + kRecoveryHeaderLength + aOffsetInBlock;
}

bool ParseArchiveHeaderBytes(const unsigned char* pBytes, std::size_t pByteCount, ArchiveHeader& pHeader) {
  if (pBytes == nullptr || pByteCount < kArchiveHeaderLength) {
    return false;
  }
  auto ReadLe = [&](std::size_t pOffset, std::size_t pWidth) {
    std::uint64_t aValue = 0;
    for (std::size_t aIndex = 0; aIndex < pWidth; ++aIndex) {
      aValue |= static_cast<std::uint64_t>(pBytes[pOffset + aIndex]) << (8 * aIndex);
    }
    return aValue;
  };
  if (ReadLe(0, 4) != kMagicHeaderBytes || ReadLe(32, 4) != kMagicFooterBytes) {
    return false;
  }
  if (ReadLe(6, 4) != kMajorVersion || ReadLe(10, 4) != kMinorVersion) {
    return false;
  }
  pHeader.mRecoveryEnabled = pBytes[4] != 0;
  pHeader.mSequence = ReadLe(14, 6);
  std::copy_n(pBytes + 20, 8, pHeader.mArchiveIdentifier.begin());
  if (ReadLe(28, 4) != 0) {
    return false;
  }
  return true;
}

}  // namespace

std::vector<PlannedArchiveLayout> BuildArchiveLayouts(std::size_t pLogicalByteLength,
                                                      std::size_t pLogicalCapacity,
                                                      std::size_t pArchivePayloadLength) {
  std::vector<PlannedArchiveLayout> aLayouts;
  if (pLogicalCapacity == 0 || pLogicalByteLength == 0) {
    return aLayouts;
  }

  const std::size_t aArchiveCount = (pLogicalByteLength + pLogicalCapacity - 1) / pLogicalCapacity;
  aLayouts.reserve(aArchiveCount);
  for (std::size_t aArchiveIndex = 0; aArchiveIndex < aArchiveCount; ++aArchiveIndex) {
    PlannedArchiveLayout aLayout;
    aLayout.mLogicalStart = aArchiveIndex * pLogicalCapacity;
    aLayout.mLogicalEnd = std::min(pLogicalByteLength, aLayout.mLogicalStart + pLogicalCapacity);
    const std::size_t aLogicalLength = aLayout.mLogicalEnd - aLayout.mLogicalStart;
    if (aLogicalLength > 0) {
      const std::size_t aLastPhysicalOffset = PhysicalOffsetForLogicalOffset(aLogicalLength - 1);
      const std::size_t aRoundedUsedPayloadLength =
          ((aLastPhysicalOffset + 1 + kL3Length - 1) / kL3Length) * kL3Length;
      aLayout.mUsedPayloadLength = std::min(pArchivePayloadLength, aRoundedUsedPayloadLength);
    }
    aLayouts.push_back(aLayout);
  }

  return aLayouts;
}

std::vector<std::vector<unsigned long long>> GenerateAllRecoveryHeaders(
    const std::vector<PlannedArchiveLayout>& pLayouts,
    const std::vector<std::size_t>& pFileStartLogicalOffsets) {
  std::vector<std::vector<unsigned long long>> aAllHeaders;
  aAllHeaders.reserve(pLayouts.size());

  for (const PlannedArchiveLayout& aLayout : pLayouts) {
    std::vector<unsigned long long> aHeaders;
    for (std::size_t aRecoveryOffset = 0; aRecoveryOffset + kRecoveryHeaderLength <= aLayout.mUsedPayloadLength;
         aRecoveryOffset += kL1Length) {
      const std::size_t aRecoveryEnd = aRecoveryOffset + kRecoveryHeaderLength;
      unsigned long long aStride = 0;
      for (std::size_t aFileIndex = 0; aFileIndex < pFileStartLogicalOffsets.size(); ++aFileIndex) {
        if (pFileStartLogicalOffsets[aFileIndex] < aLayout.mLogicalStart ||
            pFileStartLogicalOffsets[aFileIndex] >= aLayout.mLogicalEnd) {
          continue;
        }
        const std::size_t aLocalLogicalOffset = pFileStartLogicalOffsets[aFileIndex] - aLayout.mLogicalStart;
        const std::size_t aFileStartPhysicalOffset = PhysicalOffsetForLogicalOffset(aLocalLogicalOffset);
        if (aFileStartPhysicalOffset >= aRecoveryEnd) {
          aStride = static_cast<unsigned long long>(aFileStartPhysicalOffset - aRecoveryEnd);
          break;
        }
      }
      aHeaders.push_back(aStride);
    }
    aAllHeaders.push_back(std::move(aHeaders));
  }

  return aAllHeaders;
}

std::vector<SourceFileEntry> CollectSourceEntries(const FileSystem& pFileSystem, const std::string& pSourceDirectory) {
  std::vector<DirectoryEntry> aEntries = pFileSystem.ListFilesRecursive(pSourceDirectory);
  std::sort(aEntries.begin(), aEntries.end(),
            [](const DirectoryEntry& pLeft, const DirectoryEntry& pRight) {
              return pLeft.mRelativePath < pRight.mRelativePath;
            });

  std::vector<SourceFileEntry> aRecords;
  for (const DirectoryEntry& aEntry : aEntries) {
    if (aEntry.mIsDirectory) {
      continue;
    }

    std::unique_ptr<FileReadStream> aStream = pFileSystem.OpenReadStream(aEntry.mPath);
    if (aStream == nullptr || !aStream->IsReady()) {
      return {};
    }
    aRecords.push_back({aEntry.mPath, aEntry.mRelativePath, static_cast<std::uint64_t>(aStream->GetLength())});
  }
  return aRecords;
}

std::vector<ArchiveHeaderRecord> ScanArchiveDirectory(const FileSystem& pFileSystem,
                                                      const std::string& pArchiveDirectory) {
  std::vector<ArchiveHeaderRecord> aArchives;
  for (const DirectoryEntry& aEntry : pFileSystem.ListFiles(pArchiveDirectory)) {
    if (aEntry.mIsDirectory) {
      continue;
    }

    std::unique_ptr<FileReadStream> aStream = pFileSystem.OpenReadStream(aEntry.mPath);
    if (aStream == nullptr || !aStream->IsReady() || aStream->GetLength() < kArchiveHeaderLength) {
      continue;
    }
    std::array<unsigned char, kArchiveHeaderLength> aBytes = {};
    if (!aStream->Read(0, aBytes.data(), aBytes.size())) {
      continue;
    }

    ArchiveHeader aHeader;
    if (!ParseArchiveHeaderBytes(aBytes.data(), aBytes.size(), aHeader)) {
      continue;
    }

    aArchives.push_back({aEntry.mPath, pFileSystem.FileName(aEntry.mPath), aHeader});
  }

  std::sort(aArchives.begin(), aArchives.end(),
            [](const ArchiveHeaderRecord& pLeft, const ArchiveHeaderRecord& pRight) {
              if (pLeft.mHeader.mArchiveIdentifier != pRight.mHeader.mArchiveIdentifier) {
                return pLeft.mHeader.mArchiveIdentifier < pRight.mHeader.mArchiveIdentifier;
              }
              if (pLeft.mHeader.mSequence != pRight.mHeader.mSequence) {
                return pLeft.mHeader.mSequence < pRight.mHeader.mSequence;
              }
              return pLeft.mName < pRight.mName;
            });
  return aArchives;
}

bool HasAnyReadableArchive(const FileSystem& pFileSystem, const std::string& pArchiveDirectory) {
  const std::vector<DirectoryEntry> aEntries = pFileSystem.ListFiles(pArchiveDirectory);
  std::array<unsigned char, kArchiveHeaderLength> aBytes = {};
  for (const DirectoryEntry& aEntry : aEntries) {
    if (aEntry.mIsDirectory) {
      continue;
    }

    std::unique_ptr<FileReadStream> aStream = pFileSystem.OpenReadStream(aEntry.mPath);
    if (aStream == nullptr || !aStream->IsReady() || aStream->GetLength() < kArchiveHeaderLength) {
      continue;
    }
    if (!aStream->Read(0, aBytes.data(), aBytes.size())) {
      continue;
    }

    ArchiveHeader aHeader;
    if (ParseArchiveHeaderBytes(aBytes.data(), aBytes.size(), aHeader)) {
      return true;
    }
  }
  return false;
}

std::optional<std::size_t> FindArchiveIndex(const FileSystem& pFileSystem,
                                            const std::vector<ArchiveHeaderRecord>& pArchives,
                                            const std::string& pPath) {
  const std::string aFileName = pFileSystem.FileName(pPath);
  for (std::size_t aIndex = 0; aIndex < pArchives.size(); ++aIndex) {
    if (pArchives[aIndex].mPath == pPath || pArchives[aIndex].mName == aFileName) {
      return aIndex;
    }
  }
  return std::nullopt;
}

std::optional<std::size_t> FindArchiveHeaderIndex(const FileSystem& pFileSystem,
                                                  const std::vector<ArchiveHeaderRecord>& pArchives,
                                                  const std::string& pPath) {
  const std::string aFileName = pFileSystem.FileName(pPath);
  for (std::size_t aIndex = 0; aIndex < pArchives.size(); ++aIndex) {
    if (pArchives[aIndex].mPath == pPath || pArchives[aIndex].mName == aFileName) {
      return aIndex;
    }
  }
  return std::nullopt;
}

}  // namespace peanutbutter::ultima
