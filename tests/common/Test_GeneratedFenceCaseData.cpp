#include "Test_GeneratedFenceCaseData.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace peanutbutter::testing {
namespace {

constexpr std::array<unsigned char, 8> kCaseDataMagic = {'P', 'B', 'F', 'C', 'A', 'S', 'E', '1'};
constexpr std::uint32_t kCaseDataVersion = 1u;
constexpr std::uint32_t kMaxCaseCount = 100000u;
constexpr std::uint32_t kMaxVectorLength = 200000u;
constexpr std::uint32_t kMaxStringLength = 16u * 1024u * 1024u;
constexpr std::uint32_t kMaxMutationBytes = 1024u * 1024u;

bool ReadExact(std::ifstream& pStream, unsigned char* pData, std::size_t pSize) {
  if (pSize == 0u) {
    return true;
  }
  pStream.read(reinterpret_cast<char*>(pData), static_cast<std::streamsize>(pSize));
  return static_cast<std::size_t>(pStream.gcount()) == pSize;
}

bool ReadU32(std::ifstream& pStream, std::uint32_t& pValue) {
  unsigned char aRaw[4] = {};
  if (!ReadExact(pStream, aRaw, sizeof(aRaw))) {
    return false;
  }
  pValue = static_cast<std::uint32_t>(aRaw[0]) |
           (static_cast<std::uint32_t>(aRaw[1]) << 8u) |
           (static_cast<std::uint32_t>(aRaw[2]) << 16u) |
           (static_cast<std::uint32_t>(aRaw[3]) << 24u);
  return true;
}

bool ReadU64(std::ifstream& pStream, std::uint64_t& pValue) {
  unsigned char aRaw[8] = {};
  if (!ReadExact(pStream, aRaw, sizeof(aRaw))) {
    return false;
  }
  pValue = static_cast<std::uint64_t>(aRaw[0]) |
           (static_cast<std::uint64_t>(aRaw[1]) << 8u) |
           (static_cast<std::uint64_t>(aRaw[2]) << 16u) |
           (static_cast<std::uint64_t>(aRaw[3]) << 24u) |
           (static_cast<std::uint64_t>(aRaw[4]) << 32u) |
           (static_cast<std::uint64_t>(aRaw[5]) << 40u) |
           (static_cast<std::uint64_t>(aRaw[6]) << 48u) |
           (static_cast<std::uint64_t>(aRaw[7]) << 56u);
  return true;
}

bool ReadI64(std::ifstream& pStream, std::int64_t& pValue) {
  std::uint64_t aRaw = 0u;
  if (!ReadU64(pStream, aRaw)) {
    return false;
  }
  std::memcpy(&pValue, &aRaw, sizeof(aRaw));
  return true;
}

bool ReadString(std::ifstream& pStream, std::string& pValue, std::string& pError) {
  std::uint32_t aLength = 0u;
  if (!ReadU32(pStream, aLength)) {
    pError = "unexpected EOF while reading string length.";
    return false;
  }
  if (aLength > kMaxStringLength) {
    pError = "string length exceeds maximum allowed size.";
    return false;
  }
  pValue.assign(static_cast<std::size_t>(aLength), '\0');
  if (aLength == 0u) {
    return true;
  }
  if (!ReadExact(pStream, reinterpret_cast<unsigned char*>(&pValue[0]), static_cast<std::size_t>(aLength))) {
    pError = "unexpected EOF while reading string payload.";
    return false;
  }
  return true;
}

bool ReadStringVector(std::ifstream& pStream,
                      std::vector<std::string>& pValues,
                      std::string& pError,
                      const char* pFieldName) {
  std::uint32_t aCount = 0u;
  if (!ReadU32(pStream, aCount)) {
    pError = std::string("unexpected EOF while reading ") + pFieldName + " count.";
    return false;
  }
  if (aCount > kMaxVectorLength) {
    pError = std::string(pFieldName) + " count exceeds maximum allowed size.";
    return false;
  }
  pValues.clear();
  pValues.reserve(aCount);
  for (std::uint32_t aIndex = 0u; aIndex < aCount; ++aIndex) {
    std::string aValue;
    if (!ReadString(pStream, aValue, pError)) {
      pError = std::string("failed to read ") + pFieldName + " item: " + pError;
      return false;
    }
    pValues.push_back(std::move(aValue));
  }
  return true;
}

bool ReadSeedFiles(std::ifstream& pStream,
                   std::vector<TestSeedFile>& pFiles,
                   std::string& pError,
                   const char* pFieldName) {
  std::uint32_t aCount = 0u;
  if (!ReadU32(pStream, aCount)) {
    pError = std::string("unexpected EOF while reading ") + pFieldName + " count.";
    return false;
  }
  if (aCount > kMaxVectorLength) {
    pError = std::string(pFieldName) + " count exceeds maximum allowed size.";
    return false;
  }
  pFiles.clear();
  pFiles.reserve(aCount);
  for (std::uint32_t aIndex = 0u; aIndex < aCount; ++aIndex) {
    TestSeedFile aFile;
    if (!ReadString(pStream, aFile.mRelativePath, pError)) {
      pError = std::string("failed to read ") + pFieldName + " path: " + pError;
      return false;
    }
    if (!ReadString(pStream, aFile.mContents, pError)) {
      pError = std::string("failed to read ") + pFieldName + " content: " + pError;
      return false;
    }
    pFiles.push_back(std::move(aFile));
  }
  return true;
}

bool ReadBytes(std::ifstream& pStream, std::vector<unsigned char>& pBytes, std::string& pError) {
  std::uint32_t aCount = 0u;
  if (!ReadU32(pStream, aCount)) {
    pError = "unexpected EOF while reading mutation byte count.";
    return false;
  }
  if (aCount > kMaxMutationBytes) {
    pError = "mutation byte count exceeds maximum allowed size.";
    return false;
  }
  pBytes.assign(static_cast<std::size_t>(aCount), 0u);
  if (aCount == 0u) {
    return true;
  }
  if (!ReadExact(pStream, pBytes.data(), static_cast<std::size_t>(aCount))) {
    pError = "unexpected EOF while reading mutation bytes.";
    return false;
  }
  return true;
}

bool ReadU32Vector(std::ifstream& pStream,
                   std::vector<std::uint32_t>& pValues,
                   std::string& pError,
                   const char* pFieldName) {
  std::uint32_t aCount = 0u;
  if (!ReadU32(pStream, aCount)) {
    pError = std::string("unexpected EOF while reading ") + pFieldName + " count.";
    return false;
  }
  if (aCount > kMaxVectorLength) {
    pError = std::string(pFieldName) + " count exceeds maximum allowed size.";
    return false;
  }
  pValues.clear();
  pValues.reserve(aCount);
  for (std::uint32_t aIndex = 0u; aIndex < aCount; ++aIndex) {
    std::uint32_t aValue = 0u;
    if (!ReadU32(pStream, aValue)) {
      pError = std::string("unexpected EOF while reading ") + pFieldName + " item.";
      return false;
    }
    pValues.push_back(aValue);
  }
  return true;
}

}  // namespace

bool LoadGeneratedFenceCaseDataFile(const std::string& pPath,
                                    std::vector<GeneratedFenceCaseData>& pCases,
                                    std::string& pError) {
  pCases.clear();
  pError.clear();

  std::ifstream aStream(pPath, std::ios::binary);
  if (!aStream.is_open()) {
    pError = "could not open generated case data file: " + pPath;
    return false;
  }

  std::array<unsigned char, kCaseDataMagic.size()> aMagic = {};
  if (!ReadExact(aStream, aMagic.data(), aMagic.size())) {
    pError = "could not read generated case data magic header.";
    return false;
  }
  if (aMagic != kCaseDataMagic) {
    pError = "invalid generated case data magic header.";
    return false;
  }

  std::uint32_t aVersion = 0u;
  if (!ReadU32(aStream, aVersion)) {
    pError = "missing generated case data version.";
    return false;
  }
  if (aVersion != kCaseDataVersion) {
    pError = "unsupported generated case data version: " + std::to_string(aVersion);
    return false;
  }

  std::uint32_t aCaseCount = 0u;
  if (!ReadU32(aStream, aCaseCount)) {
    pError = "missing generated case count.";
    return false;
  }
  if (aCaseCount > kMaxCaseCount) {
    pError = "generated case count exceeds maximum allowed size.";
    return false;
  }

  pCases.reserve(aCaseCount);
  for (std::uint32_t aCaseIndex = 0u; aCaseIndex < aCaseCount; ++aCaseIndex) {
    GeneratedFenceCaseData aCase;
    std::string aReadError;
    if (!ReadString(aStream, aCase.mCaseId, aReadError) ||
        !ReadString(aStream, aCase.mFlow, aReadError) ||
        !ReadString(aStream, aCase.mFieldKind, aReadError) ||
        !ReadString(aStream, aCase.mExpectedErrorCode, aReadError) ||
        !ReadString(aStream, aCase.mExpectedFenceFlag, aReadError) ||
        !ReadStringVector(aStream, aCase.mForbiddenFenceFlags, aReadError, "forbidden fence flags")) {
      pError = "failed to read case[" + std::to_string(aCaseIndex) + "]: " + aReadError;
      return false;
    }

    std::uint32_t aArchiveBlockCount = 0u;
    if (!ReadU32(aStream, aArchiveBlockCount)) {
      pError = "failed to read case[" + std::to_string(aCaseIndex) + "] archive block count.";
      return false;
    }
    aCase.mArchiveBlockCount = static_cast<std::size_t>(aArchiveBlockCount);

    if (!ReadSeedFiles(aStream, aCase.mInputFiles, aReadError, "input files") ||
        !ReadStringVector(aStream, aCase.mInputEmptyDirs, aReadError, "input empty directories") ||
        !ReadSeedFiles(aStream, aCase.mRecoverableFiles, aReadError, "recoverable files")) {
      pError = "failed to read case[" + std::to_string(aCaseIndex) + "]: " + aReadError;
      return false;
    }

    if (!ReadU32(aStream, aCase.mArchiveIndex)) {
      pError = "failed to read case[" + std::to_string(aCaseIndex) + "] archive index.";
      return false;
    }

    std::uint64_t aMutationFileOffset = 0u;
    if (!ReadU64(aStream, aMutationFileOffset)) {
      pError = "failed to read case[" + std::to_string(aCaseIndex) + "] mutation file offset.";
      return false;
    }
    if (aMutationFileOffset > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
      pError = "case[" + std::to_string(aCaseIndex) + "] mutation file offset exceeds size_t range.";
      return false;
    }
    aCase.mMutationFileOffset = static_cast<std::size_t>(aMutationFileOffset);

    if (!ReadI64(aStream, aCase.mMutationPayloadLogicalOffset)) {
      pError = "failed to read case[" + std::to_string(aCaseIndex) + "] mutation payload logical offset.";
      return false;
    }

    if (!ReadBytes(aStream, aCase.mMutationBytes, aReadError) ||
        !ReadU32Vector(aStream, aCase.mCreateArchiveIndices, aReadError, "create archive indices") ||
        !ReadU32Vector(aStream, aCase.mRemoveArchiveIndices, aReadError, "remove archive indices") ||
        !ReadString(aStream, aCase.mFailurePointComment, aReadError)) {
      pError = "failed to read case[" + std::to_string(aCaseIndex) + "]: " + aReadError;
      return false;
    }

    pCases.push_back(std::move(aCase));
  }

  return true;
}

}  // namespace peanutbutter::testing
