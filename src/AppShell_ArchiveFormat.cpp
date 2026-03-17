#include "AppShell_ArchiveFormat.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <limits>
#include <sstream>

namespace peanutbutter {
namespace {

constexpr std::uint64_t kFnvOffsetBasis64 = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime64 = 1099511628211ULL;

inline std::uint64_t Fnv1aUpdate(std::uint64_t pState, unsigned char pByte) {
  pState ^= static_cast<std::uint64_t>(pByte);
  pState *= kFnvPrime64;
  return pState;
}

inline std::uint64_t HashBytes(const unsigned char* pData, std::size_t pLength, std::uint64_t pSeed) {
  std::uint64_t aState = pSeed;
  if (pData == nullptr) {
    return aState;
  }
  for (std::size_t aIndex = 0; aIndex < pLength; ++aIndex) {
    aState = Fnv1aUpdate(aState, pData[aIndex]);
  }
  return aState;
}

inline std::uint64_t MixU64(std::uint64_t pValue) {
  pValue ^= (pValue >> 33);
  pValue *= 0xff51afd7ed558ccdULL;
  pValue ^= (pValue >> 33);
  pValue *= 0xc4ceb9fe1a85ec53ULL;
  pValue ^= (pValue >> 33);
  return pValue;
}

inline std::uint16_t ReadLe16(const unsigned char* pBuffer, std::size_t pOffset) {
  return static_cast<std::uint16_t>(static_cast<std::uint16_t>(pBuffer[pOffset]) |
                                    (static_cast<std::uint16_t>(pBuffer[pOffset + 1]) << 8));
}

inline std::uint32_t ReadLe32(const unsigned char* pBuffer, std::size_t pOffset) {
  return static_cast<std::uint32_t>(static_cast<std::uint32_t>(pBuffer[pOffset]) |
                                    (static_cast<std::uint32_t>(pBuffer[pOffset + 1]) << 8) |
                                    (static_cast<std::uint32_t>(pBuffer[pOffset + 2]) << 16) |
                                    (static_cast<std::uint32_t>(pBuffer[pOffset + 3]) << 24));
}

inline std::uint64_t ReadLe64(const unsigned char* pBuffer, std::size_t pOffset) {
  std::uint64_t aValue = 0;
  for (int aByte = 0; aByte < 8; ++aByte) {
    aValue |= (static_cast<std::uint64_t>(pBuffer[pOffset + static_cast<std::size_t>(aByte)]) << (8 * aByte));
  }
  return aValue;
}

inline void WriteLe16(unsigned char* pBuffer, std::size_t pOffset, std::uint16_t pValue) {
  pBuffer[pOffset] = static_cast<unsigned char>(pValue & 0xFFu);
  pBuffer[pOffset + 1] = static_cast<unsigned char>((pValue >> 8) & 0xFFu);
}

inline void WriteLe32(unsigned char* pBuffer, std::size_t pOffset, std::uint32_t pValue) {
  pBuffer[pOffset] = static_cast<unsigned char>(pValue & 0xFFu);
  pBuffer[pOffset + 1] = static_cast<unsigned char>((pValue >> 8) & 0xFFu);
  pBuffer[pOffset + 2] = static_cast<unsigned char>((pValue >> 16) & 0xFFu);
  pBuffer[pOffset + 3] = static_cast<unsigned char>((pValue >> 24) & 0xFFu);
}

inline void WriteLe64(unsigned char* pBuffer, std::size_t pOffset, std::uint64_t pValue) {
  for (int aByte = 0; aByte < 8; ++aByte) {
    pBuffer[pOffset + static_cast<std::size_t>(aByte)] =
        static_cast<unsigned char>((pValue >> (8 * aByte)) & 0xFFu);
  }
}

void WriteArchiveHeaderPrefixBytes(const ArchiveHeader& pHeader, unsigned char* pBuffer) {
  WriteLe32(pBuffer, 0, pHeader.mMagic);
  WriteLe16(pBuffer, 4, pHeader.mVersionMajor);
  WriteLe16(pBuffer, 6, pHeader.mVersionMinor);
  WriteLe32(pBuffer, 8, pHeader.mArchiveIndex);
  WriteLe32(pBuffer, 12, pHeader.mArchiveCount);
  WriteLe32(pBuffer, 16, pHeader.mPayloadLength);
  pBuffer[20] = pHeader.mRecordCountMod256;
  pBuffer[21] = pHeader.mFolderCountMod256;
  pBuffer[22] = static_cast<std::uint8_t>(pHeader.mEncryptionStrength);
  pBuffer[23] = pHeader.mReserved8;
  WriteLe64(pBuffer, 24, pHeader.mReservedA);
  WriteLe64(pBuffer, 32, pHeader.mReservedB);
}

}  // namespace

Checksum ComputeRecoveryChecksum(const unsigned char* pPlainBlockData,
                                 const SkipRecord& pSkip) {
  Checksum aOut{};
  if (pPlainBlockData == nullptr || kBlockSizeL3 <= kRecoveryHeaderLength) {
    return aOut;
  }

  const unsigned char* aPayload = pPlainBlockData + kRecoveryHeaderLength;
  const std::size_t aPayloadLength = kBlockSizeL3 - kRecoveryHeaderLength;

  std::uint64_t aState1 = kFnvOffsetBasis64 ^ 0x1122334455667788ULL;
  std::uint64_t aState2 = kFnvOffsetBasis64 ^ 0x8877665544332211ULL;
  std::uint64_t aState3 = kFnvOffsetBasis64 ^ 0xA5A5A5A5A5A5A5A5ULL;
  std::uint64_t aState4 = kFnvOffsetBasis64 ^ 0x5A5A5A5A5A5A5A5AULL;
  std::uint64_t aState5 = kFnvOffsetBasis64 ^ 0x13579BDF2468ACE0ULL;

  for (std::size_t aIndex = 0; aIndex < aPayloadLength; ++aIndex) {
    const unsigned char aByte = aPayload[aIndex];
    aState1 = Fnv1aUpdate(aState1, aByte);
    aState2 = Fnv1aUpdate(aState2, static_cast<unsigned char>(aByte ^ 0x5Au));
    aState3 = Fnv1aUpdate(aState3, static_cast<unsigned char>(aByte + static_cast<unsigned char>(aIndex & 0xFFu)));
    aState4 = Fnv1aUpdate(aState4, static_cast<unsigned char>(aByte ^ static_cast<unsigned char>((aIndex * 131u) & 0xFFu)));
    aState5 = Fnv1aUpdate(aState5,
                          static_cast<unsigned char>(aByte + static_cast<unsigned char>(((aPayloadLength - 1u - aIndex) * 17u) & 0xFFu)));
  }

  unsigned char aSkipBytes[8] = {};
  WriteLe16(aSkipBytes, 0u, pSkip.mArchiveDistance);
  WriteLe16(aSkipBytes, 2u, pSkip.mBlockDistance);
  WriteLe32(aSkipBytes, 4u, pSkip.mByteDistance);
  unsigned char aTaggedSkip[9] = {};
  for (std::size_t aIndex = 0u; aIndex < sizeof(aSkipBytes); ++aIndex) {
    aTaggedSkip[aIndex] = aSkipBytes[aIndex];
  }

  aTaggedSkip[8] = 1u;
  aState1 = HashBytes(aTaggedSkip, sizeof(aTaggedSkip), aState1);
  aTaggedSkip[8] = 2u;
  aState2 = HashBytes(aTaggedSkip, sizeof(aTaggedSkip), aState2);
  aTaggedSkip[8] = 3u;
  aState3 = HashBytes(aTaggedSkip, sizeof(aTaggedSkip), aState3);
  aTaggedSkip[8] = 4u;
  aState4 = HashBytes(aTaggedSkip, sizeof(aTaggedSkip), aState4);
  aTaggedSkip[8] = 5u;
  aState5 = HashBytes(aTaggedSkip, sizeof(aTaggedSkip), aState5);

  aOut.mWord1 = MixU64(aState1);
  aOut.mWord2 = MixU64(aState2);
  aOut.mWord3 = MixU64(aState3);
  aOut.mWord4 = MixU64(aState4);
  aOut.mWord5 = MixU64(aState5);
  return aOut;
}

bool ChecksumsEqual(const Checksum& pLeft, const Checksum& pRight) {
  return pLeft.mWord1 == pRight.mWord1 &&
         pLeft.mWord2 == pRight.mWord2 &&
         pLeft.mWord3 == pRight.mWord3 &&
         pLeft.mWord4 == pRight.mWord4 &&
         pLeft.mWord5 == pRight.mWord5;
}

bool ReadArchiveHeaderBytes(const unsigned char* pBuffer,
                            std::size_t pBufferLength,
                            ArchiveHeader& pOutHeader) {
  pOutHeader = ArchiveHeader{};
  if (pBuffer == nullptr || pBufferLength < kArchiveHeaderLength) {
    return false;
  }

  pOutHeader.mMagic = ReadLe32(pBuffer, 0);
  pOutHeader.mVersionMajor = ReadLe16(pBuffer, 4);
  pOutHeader.mVersionMinor = ReadLe16(pBuffer, 6);
  pOutHeader.mArchiveIndex = ReadLe32(pBuffer, 8);
  pOutHeader.mArchiveCount = ReadLe32(pBuffer, 12);
  pOutHeader.mPayloadLength = ReadLe32(pBuffer, 16);
  pOutHeader.mRecordCountMod256 = pBuffer[20];
  pOutHeader.mFolderCountMod256 = pBuffer[21];
  pOutHeader.mEncryptionStrength =
      static_cast<EncryptionStrength>(pBuffer[22]);
  pOutHeader.mReserved8 = pBuffer[23];
  pOutHeader.mReservedA = ReadLe64(pBuffer, 24);
  pOutHeader.mReservedB = ReadLe64(pBuffer, 32);
  pOutHeader.mArchiveFamilyId = ReadLe64(pBuffer, 40);

  if (pOutHeader.mMagic != kMagicHeaderBytes) {
    return false;
  }
  if (pOutHeader.mVersionMajor != static_cast<std::uint16_t>(kMajorVersion & 0xFFFFu)) {
    return false;
  }
  if (pOutHeader.mVersionMinor != static_cast<std::uint16_t>(kMinorVersion & 0xFFFFu)) {
    return false;
  }
  switch (pOutHeader.mEncryptionStrength) {
    case EncryptionStrength::kHigh:
    case EncryptionStrength::kMedium:
    case EncryptionStrength::kLow:
      break;
    default:
      return false;
  }
  return true;
}

bool WriteArchiveHeaderBytes(const ArchiveHeader& pHeader,
                             unsigned char* pBuffer,
                             std::size_t pBufferLength) {
  if (pBuffer == nullptr || pBufferLength < kArchiveHeaderLength) {
    return false;
  }

  WriteArchiveHeaderPrefixBytes(pHeader, pBuffer);
  WriteLe64(pBuffer, 40, pHeader.mArchiveFamilyId);
  return true;
}

bool ReadRecoveryHeaderBytes(const unsigned char* pBuffer,
                             std::size_t pBufferLength,
                             RecoveryHeader& pOutHeader) {
  pOutHeader = RecoveryHeader{};
  if (pBuffer == nullptr || pBufferLength < kRecoveryHeaderLength) {
    return false;
  }
  pOutHeader.mChecksum.mWord1 = ReadLe64(pBuffer, 0);
  pOutHeader.mChecksum.mWord2 = ReadLe64(pBuffer, 8);
  pOutHeader.mChecksum.mWord3 = ReadLe64(pBuffer, 16);
  pOutHeader.mChecksum.mWord4 = ReadLe64(pBuffer, 24);
  pOutHeader.mChecksum.mWord5 = ReadLe64(pBuffer, 32);
  pOutHeader.mSkip.mArchiveDistance = ReadLe16(pBuffer, 40);
  pOutHeader.mSkip.mBlockDistance = ReadLe16(pBuffer, 42);
  pOutHeader.mSkip.mByteDistance = ReadLe32(pBuffer, 44);
  return true;
}

bool WriteRecoveryHeaderBytes(const RecoveryHeader& pHeader,
                              unsigned char* pBuffer,
                              std::size_t pBufferLength) {
  if (pBuffer == nullptr || pBufferLength < kRecoveryHeaderLength) {
    return false;
  }
  WriteLe64(pBuffer, 0, pHeader.mChecksum.mWord1);
  WriteLe64(pBuffer, 8, pHeader.mChecksum.mWord2);
  WriteLe64(pBuffer, 16, pHeader.mChecksum.mWord3);
  WriteLe64(pBuffer, 24, pHeader.mChecksum.mWord4);
  WriteLe64(pBuffer, 32, pHeader.mChecksum.mWord5);
  WriteLe16(pBuffer, 40, pHeader.mSkip.mArchiveDistance);
  WriteLe16(pBuffer, 42, pHeader.mSkip.mBlockDistance);
  WriteLe32(pBuffer, 44, pHeader.mSkip.mByteDistance);
  return true;
}

std::string MakeArchiveFileName(const std::string& pPrefix,
                                const std::string& pSourceStem,
                                const std::string& pSuffix,
                                std::size_t pArchiveOrdinal,
                                std::size_t pArchiveCount) {
  std::size_t aDigits = 1;
  std::size_t aMax = pArchiveCount > 0 ? (pArchiveCount - 1) : 0;
  while (aMax >= 10) {
    ++aDigits;
    aMax /= 10;
  }

  std::ostringstream aStream;
  aStream << pPrefix << pSourceStem << "_" << std::setw(static_cast<int>(aDigits))
          << std::setfill('0') << pArchiveOrdinal;
  if (!pSuffix.empty()) {
    if (pSuffix[0] == '.') {
      aStream << pSuffix;
    } else {
      aStream << "." << pSuffix;
    }
  }
  return aStream.str();
}

bool ParseArchiveFileTemplate(const std::string& pFileName,
                              std::string& pOutPrefix,
                              std::uint32_t& pOutIndex,
                              std::string& pOutSuffix,
                              std::size_t& pOutDigits) {
  pOutPrefix.clear();
  pOutSuffix.clear();
  pOutIndex = 0;
  pOutDigits = 0;

  if (pFileName.empty()) {
    return false;
  }

  const std::size_t aDot = pFileName.find_last_of('.');
  const std::string aBase = (aDot == std::string::npos) ? pFileName : pFileName.substr(0u, aDot);
  pOutSuffix = (aDot == std::string::npos) ? std::string() : pFileName.substr(aDot);

  if (aBase.empty()) {
    return false;
  }

  std::size_t aDigitsStart = aBase.size();
  while (aDigitsStart > 0u && std::isdigit(static_cast<unsigned char>(aBase[aDigitsStart - 1u])) != 0) {
    --aDigitsStart;
  }
  if (aDigitsStart == aBase.size()) {
    return false;
  }

  const std::string aDigits = aBase.substr(aDigitsStart);
  if (aDigits.size() > 9u) {
    return false;
  }

  std::uint64_t aIndex = 0;
  for (char aChar : aDigits) {
    if (std::isdigit(static_cast<unsigned char>(aChar)) == 0) {
      return false;
    }
    aIndex = (aIndex * 10u) + static_cast<std::uint64_t>(aChar - '0');
    if (aIndex > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
      return false;
    }
  }

  pOutPrefix = aBase.substr(0u, aDigitsStart);
  pOutIndex = static_cast<std::uint32_t>(aIndex);
  pOutDigits = aDigits.size();
  return true;
}

}  // namespace peanutbutter
