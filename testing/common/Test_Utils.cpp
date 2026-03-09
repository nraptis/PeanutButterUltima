#include "Test_Utils.hpp"

#include <iomanip>
#include <sstream>

namespace peanutbutter::testing {

namespace {

std::uint32_t ReadLe32(const unsigned char* pData) {
  return static_cast<std::uint32_t>(pData[0]) |
         (static_cast<std::uint32_t>(pData[1]) << 8) |
         (static_cast<std::uint32_t>(pData[2]) << 16) |
         (static_cast<std::uint32_t>(pData[3]) << 24);
}

unsigned long long ReadLe64Prefix(const std::array<unsigned char, 8>& pBytes) {
  unsigned long long aValue = 0;
  for (std::size_t aIndex = 0; aIndex < 8; ++aIndex) {
    aValue |= static_cast<unsigned long long>(pBytes[aIndex]) << (8 * aIndex);
  }
  return aValue;
}

}  // namespace

bool Fail(const std::string& pMessage, std::string* pErrorMessage) {
  if (pErrorMessage != nullptr) {
    *pErrorMessage = pMessage;
  }
  return false;
}

unsigned long long ReadLe48(const unsigned char* pData) {
  return static_cast<unsigned long long>(pData[0]) |
         (static_cast<unsigned long long>(pData[1]) << 8) |
         (static_cast<unsigned long long>(pData[2]) << 16) |
         (static_cast<unsigned long long>(pData[3]) << 24) |
         (static_cast<unsigned long long>(pData[4]) << 32) |
         (static_cast<unsigned long long>(pData[5]) << 40);
}

unsigned long long ReadLe48(const std::array<unsigned char, kDemoRecoveryHeaderLength>& pBytes) {
  return ReadLe48(pBytes.data());
}

peanutbutter::ByteVector ToBytes(const std::string& pText) {
  return peanutbutter::ByteVector(pText.begin(), pText.end());
}

std::string ToHex(const peanutbutter::ByteVector& pBytes, std::size_t pOffset, std::size_t pLength) {
  std::ostringstream aStream;
  aStream << std::hex << std::setfill('0');
  for (std::size_t aIndex = 0; aIndex < pLength; ++aIndex) {
    if (aIndex > 0) {
      aStream << ' ';
    }
    aStream << std::setw(2) << static_cast<int>(pBytes[pOffset + aIndex]);
  }
  return aStream.str();
}

bool Read_ArchiveHeader(const peanutbutter::ByteVector& pArchiveBytes,
                        TestArchiveHeader& pArchiveHeader,
                        std::string* pErrorMessage) {
  if (pArchiveBytes.size() < kDemoPlainTextHeaderLength) {
    return Fail("Archive header read failed: archive is smaller than the 36-byte demo header.", pErrorMessage);
  }

  const unsigned char* const aData = pArchiveBytes.data();
  pArchiveHeader.mMagicHeaderBytes = ReadLe32(aData + 0);
  pArchiveHeader.mRecoveryFlag = static_cast<unsigned long long>(aData[4]);
  pArchiveHeader.mMajorVersion = ReadLe32(aData + 6);
  pArchiveHeader.mMinorVersion = ReadLe32(aData + 10);
  pArchiveHeader.mArchiveIndex = ReadLe48(aData + 14);
  for (std::size_t aIndex = 0; aIndex < pArchiveHeader.mArchiveIdentifierBytes.size(); ++aIndex) {
    pArchiveHeader.mArchiveIdentifierBytes[aIndex] = aData[20 + aIndex];
  }
  pArchiveHeader.mArchiveIdentifier = ReadLe64Prefix(pArchiveHeader.mArchiveIdentifierBytes);
  pArchiveHeader.mReservedBytes = ReadLe32(aData + 28);
  pArchiveHeader.mMagicFooterBytes = ReadLe32(aData + 32);
  return true;
}

bool Read_RecoveryHeader(const peanutbutter::ByteVector& pArchiveBytes,
                         std::size_t pOffset,
                         TestRecoveryHeader& pRecoveryHeader,
                         std::string* pErrorMessage) {
  if (pOffset + kDemoRecoveryHeaderLength > pArchiveBytes.size()) {
    return Fail("Recovery header read failed: requested offset is outside the archive bytes.", pErrorMessage);
  }

  for (std::size_t aIndex = 0; aIndex < kDemoRecoveryHeaderLength; ++aIndex) {
    pRecoveryHeader.mRawBytes[aIndex] = pArchiveBytes[pOffset + aIndex];
  }
  pRecoveryHeader.mStride = ReadLe48(pRecoveryHeader.mRawBytes);
  return true;
}

}  // namespace peanutbutter::testing
