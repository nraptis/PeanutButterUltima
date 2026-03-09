#include "Test_Wrappers.hpp"

#include <algorithm>

#include "Test_Utils.hpp"

namespace peanutbutter::testing {

TestFile::TestFile(std::string pPath, peanutbutter::ByteVector pBytes)
    : mPath(std::move(pPath)), mBytes(std::move(pBytes)) {}

TestBlockL1::TestBlockL1(std::array<unsigned char, peanutbutter::SB_RECOVERY_HEADER_LENGTH> pRecoveryHeaderBytes,
                         peanutbutter::ByteVector pPayloadBytes)
    : mRecoveryHeaderBytes(std::move(pRecoveryHeaderBytes)), mPayloadBytes(std::move(pPayloadBytes)) {}

TestBlockL3::TestBlockL3(std::vector<TestBlockL1> pBlockList) : mBlockList(std::move(pBlockList)) {}

TestArchive::TestArchive(TestArchiveHeader pHeader) : mHeader(std::move(pHeader)) {}

TestArchive::TestArchive(std::string pPath, TestArchiveHeader pHeader)
    : mPath(std::move(pPath)), mHeader(std::move(pHeader)) {}

bool TestArchive::Load(const std::string& pFilePath,
                       const peanutbutter::FileSystem& pFileSystem,
                       std::string* pErrorMessage) {
  peanutbutter::ByteVector aBytes;
  if (!pFileSystem.ReadFile(pFilePath, aBytes)) {
    return Fail("TestArchive.Load failed: could not read archive bytes from path '" + pFilePath + "'.",
                pErrorMessage);
  }
  mPath = pFilePath;
  return Load(aBytes, pErrorMessage);
}

bool TestArchive::Load(const peanutbutter::ByteVector& pBytes, std::string* pErrorMessage) {
  TestArchiveHeader aHeader;
  if (!Read_ArchiveHeader(pBytes, aHeader, pErrorMessage)) {
    if (pErrorMessage != nullptr && !pErrorMessage->empty()) {
      *pErrorMessage = "TestArchive.Load failed: " + *pErrorMessage;
    }
    return false;
  }

  mHeader = aHeader;
  mBlockList.clear();

  std::size_t aOffset = kDemoPlainTextHeaderLength;
  while (aOffset < pBytes.size()) {
    TestBlockL3 aBlockL3;

    for (std::size_t aL1Index = 0; aL1Index < 4 && aOffset < pBytes.size(); ++aL1Index) {
      const std::size_t aRemainingBytes = pBytes.size() - aOffset;
      if (aRemainingBytes < kDemoRecoveryHeaderLength) {
        return Fail("TestArchive.Load failed: trailing bytes are smaller than a recovery header.",
                    pErrorMessage);
      }

      const std::size_t aBlockLength = std::min<std::size_t>(kDemoL1Length, aRemainingBytes);
      TestBlockL1 aBlockL1;
      for (std::size_t aRecoveryIndex = 0; aRecoveryIndex < kDemoRecoveryHeaderLength; ++aRecoveryIndex) {
        aBlockL1.mRecoveryHeaderBytes[aRecoveryIndex] = pBytes[aOffset + aRecoveryIndex];
      }
      aBlockL1.mPayloadBytes.insert(aBlockL1.mPayloadBytes.end(),
                                    pBytes.begin() + static_cast<std::ptrdiff_t>(aOffset + kDemoRecoveryHeaderLength),
                                    pBytes.begin() + static_cast<std::ptrdiff_t>(aOffset + aBlockLength));
      aBlockL3.mBlockList.push_back(std::move(aBlockL1));
      aOffset += aBlockLength;
    }

    mBlockList.push_back(std::move(aBlockL3));
  }

  return true;
}

bool TestArchiveHeader::Equals(const TestArchiveHeader& pOther, std::string* pErrorMessage) const {
  if (mMagicHeaderBytes != pOther.mMagicHeaderBytes) {
    return Fail("TestArchiveHeader.Equals failed: magic header mismatch.", pErrorMessage);
  }
  if (mRecoveryFlag != pOther.mRecoveryFlag) {
    return Fail("TestArchiveHeader.Equals failed: recovery flag mismatch.", pErrorMessage);
  }
  if (mMajorVersion != pOther.mMajorVersion) {
    return Fail("TestArchiveHeader.Equals failed: major version mismatch.", pErrorMessage);
  }
  if (mMinorVersion != pOther.mMinorVersion) {
    return Fail("TestArchiveHeader.Equals failed: minor version mismatch.", pErrorMessage);
  }
  if (mArchiveIndex != pOther.mArchiveIndex) {
    return Fail("TestArchiveHeader.Equals failed: archive index mismatch.", pErrorMessage);
  }
  if (mArchiveIdentifierBytes != pOther.mArchiveIdentifierBytes) {
    return Fail("TestArchiveHeader.Equals failed: archive identifier bytes mismatch.", pErrorMessage);
  }
  if (mArchiveIdentifier != pOther.mArchiveIdentifier) {
    return Fail("TestArchiveHeader.Equals failed: archive identifier mismatch.", pErrorMessage);
  }
  if (mReservedBytes != pOther.mReservedBytes) {
    return Fail("TestArchiveHeader.Equals failed: reserved bytes mismatch.", pErrorMessage);
  }
  if (mMagicFooterBytes != pOther.mMagicFooterBytes) {
    return Fail("TestArchiveHeader.Equals failed: magic footer mismatch.", pErrorMessage);
  }
  return true;
}

bool TestArchiveHeader::Equals(const peanutbutter::ArchiveHeader& pOther,
                               std::string* pErrorMessage) const {
  if (mRecoveryFlag != static_cast<unsigned long long>(pOther.mRecoveryEnabled ? 1 : 0)) {
    return Fail("TestArchiveHeader.Equals(ArchiveHeader) failed: recovery flag mismatch.", pErrorMessage);
  }
  if (mArchiveIndex != pOther.mSequence) {
    return Fail("TestArchiveHeader.Equals(ArchiveHeader) failed: archive index mismatch.", pErrorMessage);
  }
  if (mArchiveIdentifierBytes != pOther.mArchiveIdentifier) {
    return Fail("TestArchiveHeader.Equals(ArchiveHeader) failed: archive identifier bytes mismatch.", pErrorMessage);
  }
  return true;
}

bool TestRecoveryHeader::Equals(const TestRecoveryHeader& pOther, std::string* pErrorMessage) const {
  if (mRawBytes != pOther.mRawBytes) {
    return Fail("TestRecoveryHeader.Equals failed: raw bytes mismatch.", pErrorMessage);
  }
  if (mStride != pOther.mStride) {
    return Fail("TestRecoveryHeader.Equals failed: stride mismatch.", pErrorMessage);
  }
  return true;
}

bool TestFile::Equals(const TestFile& pOther, std::string* pErrorMessage) const {
  if (mPath != pOther.mPath) {
    return Fail("TestFile.Equals failed: path mismatch.", pErrorMessage);
  }
  if (mBytes != pOther.mBytes) {
    return Fail("TestFile.Equals failed: byte contents mismatch.", pErrorMessage);
  }
  return true;
}

bool TestBlockL1::Equals(const TestBlockL1& pOther, std::string* pErrorMessage) const {
  if (mRecoveryHeaderBytes != pOther.mRecoveryHeaderBytes) {
    return Fail("TestBlockL1.Equals failed: recovery header bytes mismatch.", pErrorMessage);
  }
  if (mPayloadBytes != pOther.mPayloadBytes) {
    return Fail("TestBlockL1.Equals failed: payload bytes mismatch.", pErrorMessage);
  }
  return true;
}

bool TestBlockL3::Equals(const TestBlockL3& pOther, std::string* pErrorMessage) const {
  if (mBlockList.size() != pOther.mBlockList.size()) {
    return Fail("TestBlockL3.Equals failed: L1 block count mismatch.", pErrorMessage);
  }
  for (std::size_t aIndex = 0; aIndex < mBlockList.size(); ++aIndex) {
    if (!mBlockList[aIndex].Equals(pOther.mBlockList[aIndex], pErrorMessage)) {
      if (pErrorMessage != nullptr && !pErrorMessage->empty()) {
        *pErrorMessage = "TestBlockL3.Equals failed at L1 index " + std::to_string(aIndex) + ": " + *pErrorMessage;
      }
      return false;
    }
  }
  return true;
}

bool TestArchive::Equals(const TestArchive& pOther, std::string* pErrorMessage) const {
  if (mPath != pOther.mPath) {
    return Fail("TestArchive.Equals failed: path mismatch.", pErrorMessage);
  }
  if (!mHeader.Equals(pOther.mHeader, pErrorMessage)) {
    if (pErrorMessage != nullptr && !pErrorMessage->empty()) {
      *pErrorMessage = "TestArchive.Equals failed: " + *pErrorMessage;
    }
    return false;
  }
  if (mBlockList.size() != pOther.mBlockList.size()) {
    return Fail("TestArchive.Equals failed: L3 block count mismatch.", pErrorMessage);
  }
  for (std::size_t aIndex = 0; aIndex < mBlockList.size(); ++aIndex) {
    if (!mBlockList[aIndex].Equals(pOther.mBlockList[aIndex], pErrorMessage)) {
      if (pErrorMessage != nullptr && !pErrorMessage->empty()) {
        *pErrorMessage = "TestArchive.Equals failed at L3 index " + std::to_string(aIndex) + ": " + *pErrorMessage;
      }
      return false;
    }
  }
  return true;
}

peanutbutter::ByteVector ToBytes(const TestBlockL1& pBlock) {
  peanutbutter::ByteVector aBytes;
  aBytes.reserve(kDemoRecoveryHeaderLength + pBlock.mPayloadBytes.size());
  aBytes.insert(aBytes.end(), pBlock.mRecoveryHeaderBytes.begin(), pBlock.mRecoveryHeaderBytes.end());
  aBytes.insert(aBytes.end(), pBlock.mPayloadBytes.begin(), pBlock.mPayloadBytes.end());
  return aBytes;
}

peanutbutter::ByteVector ToBytes(const TestBlockL3& pBlock) {
  peanutbutter::ByteVector aBytes;
  for (const TestBlockL1& aBlockL1 : pBlock.mBlockList) {
    const peanutbutter::ByteVector aBlockBytes = ToBytes(aBlockL1);
    aBytes.insert(aBytes.end(), aBlockBytes.begin(), aBlockBytes.end());
  }
  return aBytes;
}

peanutbutter::ByteVector ToBytes(const TestArchive& pArchive) {
  peanutbutter::ByteVector aBytes;
  aBytes.reserve(kDemoPlainTextHeaderLength);

  auto aWriteLe = [&](unsigned long long pValue, std::size_t pWidth) {
    for (std::size_t aIndex = 0; aIndex < pWidth; ++aIndex) {
      aBytes.push_back(static_cast<unsigned char>((pValue >> (8 * aIndex)) & 0xFFU));
    }
  };

  aWriteLe(pArchive.mHeader.mMagicHeaderBytes, 4);
  aBytes.push_back(static_cast<unsigned char>(pArchive.mHeader.mRecoveryFlag & 0xFFU));
  aBytes.push_back(0);
  aWriteLe(pArchive.mHeader.mMajorVersion, 4);
  aWriteLe(pArchive.mHeader.mMinorVersion, 4);
  aWriteLe(pArchive.mHeader.mArchiveIndex, 6);
  aBytes.insert(aBytes.end(),
                pArchive.mHeader.mArchiveIdentifierBytes.begin(),
                pArchive.mHeader.mArchiveIdentifierBytes.end());
  aWriteLe(pArchive.mHeader.mReservedBytes, 4);
  aWriteLe(pArchive.mHeader.mMagicFooterBytes, 4);

  for (const TestBlockL3& aBlockL3 : pArchive.mBlockList) {
    const peanutbutter::ByteVector aBlockBytes = ToBytes(aBlockL3);
    aBytes.insert(aBytes.end(), aBlockBytes.begin(), aBlockBytes.end());
  }

  return aBytes;
}

}  // namespace peanutbutter::testing
