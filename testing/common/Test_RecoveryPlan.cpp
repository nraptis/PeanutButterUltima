#include "Test_RecoveryPlan.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "Test_Utils.hpp"
#include "Test_Wrappers.hpp"

namespace peanutbutter::ultima::testing {

namespace {

struct ArchiveBytes {
  std::string mPath;
  std::string mName;
  unsigned long long mArchiveIndex = 0;
  ByteVector mBytes;
};

struct FileStart {
  std::size_t mArchiveListIndex = 0;
  std::size_t mPayloadOffset = 0;
};

bool IsRecoveryHeaderOffset(std::size_t pPayloadOffset) {
  return (pPayloadOffset % kDemoL1Length) < kDemoRecoveryHeaderLength;
}

bool LoadArchivesSorted(const FileSystem& pFileSystem,
                        const std::string& pArchiveDirectory,
                        const Crypt* pCrypt,
                        bool pUseEncryption,
                        std::vector<ArchiveBytes>& pArchives,
                        std::string* pErrorMessage) {
  pArchives.clear();
  const std::vector<DirectoryEntry> aEntries = pFileSystem.ListFiles(pArchiveDirectory);
  for (const DirectoryEntry& aEntry : aEntries) {
    if (aEntry.mIsDirectory) {
      continue;
    }

    ByteVector aBytes;
    if (!pFileSystem.ReadFile(aEntry.mPath, aBytes)) {
      return Fail("Could not read archive file: " + aEntry.mPath, pErrorMessage);
    }

    TestArchiveHeader aHeader;
    std::string aHeaderError;
    if (!Read_ArchiveHeader(aBytes, aHeader, &aHeaderError)) {
      return Fail("Could not read archive header from " + aEntry.mPath + ": " + aHeaderError, pErrorMessage);
    }

    if (pUseEncryption) {
      if (pCrypt == nullptr) {
        return Fail("Could not decrypt archive payloads: no crypt was supplied.", pErrorMessage);
      }
      ByteVector aDecryptedBytes;
      aDecryptedBytes.reserve(aBytes.size());
      aDecryptedBytes.insert(aDecryptedBytes.end(),
                             aBytes.begin(),
                             aBytes.begin() + static_cast<std::ptrdiff_t>(kDemoPlainTextHeaderLength));

      for (std::size_t aOffset = kDemoPlainTextHeaderLength; aOffset < aBytes.size(); aOffset += kDemoL3Length) {
        const std::size_t aChunkLength = std::min(kDemoL3Length, aBytes.size() - aOffset);
        unsigned char aSource[kDemoL3Length] = {};
        unsigned char aWorker[kDemoL3Length] = {};
        unsigned char aDestination[kDemoL3Length] = {};
        std::copy_n(aBytes.data() + aOffset, aChunkLength, aSource);
        std::string aCryptError;
        if (!pCrypt->UnsealData(aSource, aWorker, aDestination, aChunkLength, &aCryptError, CryptMode::kNormal)) {
          return Fail("Could not decrypt archive payload from " + aEntry.mPath +
                          (aCryptError.empty() ? "" : ": " + aCryptError),
                      pErrorMessage);
        }
        aDecryptedBytes.insert(aDecryptedBytes.end(), aDestination, aDestination + aChunkLength);
      }
      aBytes = std::move(aDecryptedBytes);
    }

    pArchives.push_back({aEntry.mPath, pFileSystem.FileName(aEntry.mPath), aHeader.mArchiveIndex, std::move(aBytes)});
  }

  std::sort(pArchives.begin(), pArchives.end(),
            [](const ArchiveBytes& pLeft, const ArchiveBytes& pRight) {
              if (pLeft.mArchiveIndex != pRight.mArchiveIndex) {
                return pLeft.mArchiveIndex < pRight.mArchiveIndex;
              }
              return pLeft.mName < pRight.mName;
            });
  return !pArchives.empty();
}

bool AdvanceToNextPayloadByte(const std::vector<ArchiveBytes>& pArchives,
                              std::size_t& pArchiveListIndex,
                              std::size_t& pPayloadOffset) {
  while (pArchiveListIndex < pArchives.size()) {
    const ByteVector& aBytes = pArchives[pArchiveListIndex].mBytes;
    while (kDemoPlainTextHeaderLength + pPayloadOffset < aBytes.size()) {
      if (!IsRecoveryHeaderOffset(pPayloadOffset)) {
        return true;
      }
      ++pPayloadOffset;
    }
    ++pArchiveListIndex;
    pPayloadOffset = 0;
  }
  return false;
}

bool ReadNextLogicalByte(const std::vector<ArchiveBytes>& pArchives,
                         std::size_t& pArchiveListIndex,
                         std::size_t& pPayloadOffset,
                         unsigned char& pByte) {
  if (!AdvanceToNextPayloadByte(pArchives, pArchiveListIndex, pPayloadOffset)) {
    return false;
  }

  const ByteVector& aBytes = pArchives[pArchiveListIndex].mBytes;
  pByte = aBytes[kDemoPlainTextHeaderLength + pPayloadOffset];
  ++pPayloadOffset;
  return true;
}

bool ReadLogicalBytes(const std::vector<ArchiveBytes>& pArchives,
                      std::size_t& pArchiveListIndex,
                      std::size_t& pPayloadOffset,
                      std::size_t pLength,
                      ByteVector& pBytes) {
  pBytes.clear();
  pBytes.reserve(pLength);
  for (std::size_t aIndex = 0; aIndex < pLength; ++aIndex) {
    unsigned char aByte = 0;
    if (!ReadNextLogicalByte(pArchives, pArchiveListIndex, pPayloadOffset, aByte)) {
      return false;
    }
    pBytes.push_back(aByte);
  }
  return true;
}

bool WalkFileStarts(const std::vector<ArchiveBytes>& pArchives,
                    std::vector<FileStart>& pFileStarts,
                    std::string* pErrorMessage) {
  pFileStarts.clear();
  std::size_t aArchiveListIndex = 0;
  std::size_t aPayloadOffset = 0;

  while (AdvanceToNextPayloadByte(pArchives, aArchiveListIndex, aPayloadOffset)) {
    const std::size_t aFileStartArchiveIndex = aArchiveListIndex;
    const std::size_t aFileStartPayloadOffset = aPayloadOffset;

    ByteVector aPathLengthBytes;
    if (!ReadLogicalBytes(pArchives, aArchiveListIndex, aPayloadOffset, 2, aPathLengthBytes)) {
      break;
    }
    const std::size_t aPathLength =
        static_cast<std::size_t>(static_cast<std::uint16_t>(aPathLengthBytes[0]) |
                                 (static_cast<std::uint16_t>(aPathLengthBytes[1]) << 8));
    if (aPathLength == 0) {
      break;
    }
    if (aPathLength > kDemoMaxValidFilePathLength) {
      return Fail("GenerateAllRecoveryHeaders failed: path length exceeded max valid path length.", pErrorMessage);
    }

    ByteVector aPathBytes;
    if (!ReadLogicalBytes(pArchives, aArchiveListIndex, aPayloadOffset, aPathLength, aPathBytes)) {
      return Fail("GenerateAllRecoveryHeaders failed while reading archive path bytes.", pErrorMessage);
    }

    ByteVector aContentLengthBytes;
    if (!ReadLogicalBytes(pArchives, aArchiveListIndex, aPayloadOffset, kDemoRecoveryHeaderLength, aContentLengthBytes)) {
      return Fail("GenerateAllRecoveryHeaders failed while reading content length bytes.", pErrorMessage);
    }
    const std::size_t aContentLength = static_cast<std::size_t>(ReadLe48(aContentLengthBytes.data()));

    ByteVector aContentBytes;
    if (!ReadLogicalBytes(pArchives, aArchiveListIndex, aPayloadOffset, aContentLength, aContentBytes)) {
      return Fail("GenerateAllRecoveryHeaders failed while reading content bytes.", pErrorMessage);
    }

    pFileStarts.push_back({aFileStartArchiveIndex, aFileStartPayloadOffset});
  }

  return true;
}

}  // namespace

bool GenerateAllRecoveryHeaders(const FileSystem& pFileSystem,
                                const std::string& pArchiveDirectory,
                                std::vector<unsigned long long>& pRecoveryHeaders,
                                const Crypt* pCrypt,
                                bool pUseEncryption,
                                std::string* pErrorMessage) {
  std::vector<ArchiveBytes> aArchives;
  if (!LoadArchivesSorted(pFileSystem, pArchiveDirectory, pCrypt, pUseEncryption, aArchives, pErrorMessage)) {
    return Fail("GenerateAllRecoveryHeaders failed: no readable archives were found.", pErrorMessage);
  }

  std::vector<FileStart> aFileStarts;
  if (!WalkFileStarts(aArchives, aFileStarts, pErrorMessage)) {
    return false;
  }

  pRecoveryHeaders.clear();
  for (std::size_t aArchiveListIndex = 0; aArchiveListIndex < aArchives.size(); ++aArchiveListIndex) {
    const std::size_t aPayloadLength = aArchives[aArchiveListIndex].mBytes.size() - kDemoPlainTextHeaderLength;
    for (std::size_t aRecoveryOffset = 0; aRecoveryOffset + kDemoRecoveryHeaderLength <= aPayloadLength;
         aRecoveryOffset += kDemoL1Length) {
      if (aArchiveListIndex == 0 && aRecoveryOffset == 0) {
        pRecoveryHeaders.push_back(0);
        continue;
      }

      unsigned long long aStride = 0;
      const std::size_t aRecoveryEnd = aRecoveryOffset + kDemoRecoveryHeaderLength;
      for (const FileStart& aFileStart : aFileStarts) {
        if (aFileStart.mArchiveListIndex != aArchiveListIndex) {
          continue;
        }
        if (aFileStart.mPayloadOffset >= aRecoveryEnd) {
          aStride = static_cast<unsigned long long>(aFileStart.mPayloadOffset - aRecoveryEnd);
          break;
        }
      }
      pRecoveryHeaders.push_back(aStride);
    }
  }

  return true;
}

bool CollectAllRecoveryHeaders(const FileSystem& pFileSystem,
                               const std::string& pArchiveDirectory,
                               std::vector<unsigned long long>& pRecoveryHeaders,
                               const Crypt* pCrypt,
                               bool pUseEncryption,
                               std::string* pErrorMessage) {
  std::vector<ArchiveBytes> aArchives;
  if (!LoadArchivesSorted(pFileSystem, pArchiveDirectory, pCrypt, pUseEncryption, aArchives, pErrorMessage)) {
    return Fail("CollectAllRecoveryHeaders failed: no readable archives were found.", pErrorMessage);
  }

  pRecoveryHeaders.clear();
  for (std::size_t aArchiveListIndex = 0; aArchiveListIndex < aArchives.size(); ++aArchiveListIndex) {
    const std::size_t aPayloadLength = aArchives[aArchiveListIndex].mBytes.size() - kDemoPlainTextHeaderLength;
    for (std::size_t aRecoveryOffset = 0; aRecoveryOffset + kDemoRecoveryHeaderLength <= aPayloadLength;
         aRecoveryOffset += kDemoL1Length) {
      pRecoveryHeaders.push_back(
          ReadLe48(aArchives[aArchiveListIndex].mBytes.data() + kDemoPlainTextHeaderLength + aRecoveryOffset));
    }
  }

  return true;
}

bool GenerateAllRecoveryHeaders(const FileSystem& pFileSystem,
                                const std::string& pArchiveDirectory,
                                std::vector<unsigned long long>& pRecoveryHeaders,
                                std::string* pErrorMessage) {
  return GenerateAllRecoveryHeaders(pFileSystem,
                                    pArchiveDirectory,
                                    pRecoveryHeaders,
                                    nullptr,
                                    false,
                                    pErrorMessage);
}

bool CollectAllRecoveryHeaders(const FileSystem& pFileSystem,
                               const std::string& pArchiveDirectory,
                               std::vector<unsigned long long>& pRecoveryHeaders,
                               std::string* pErrorMessage) {
  return CollectAllRecoveryHeaders(pFileSystem,
                                   pArchiveDirectory,
                                   pRecoveryHeaders,
                                   nullptr,
                                   false,
                                   pErrorMessage);
}

}  // namespace peanutbutter::ultima::testing
