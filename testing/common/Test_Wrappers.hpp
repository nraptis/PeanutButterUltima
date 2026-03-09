#ifndef PEANUT_BUTTER_ULTIMA_TEST_WRAPPERS_HPP_
#define PEANUT_BUTTER_ULTIMA_TEST_WRAPPERS_HPP_

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "AppCore.hpp"

namespace peanutbutter::ultima::testing {

struct TestArchiveHeader {
  std::uint32_t mMagicHeaderBytes = 0;
  unsigned long long mRecoveryFlag = 0;
  std::uint32_t mMajorVersion = 0;
  std::uint32_t mMinorVersion = 0;
  unsigned long long mArchiveIndex = 0;
  std::array<unsigned char, 8> mArchiveIdentifierBytes = {0, 0, 0, 0, 0, 0, 0, 0};
  unsigned long long mArchiveIdentifier = 0;
  std::uint32_t mReservedBytes = 0;
  std::uint32_t mMagicFooterBytes = 0;

  bool Equals(const TestArchiveHeader& pOther, std::string* pErrorMessage) const;
  bool Equals(const peanutbutter::ultima::ArchiveHeader& pOther, std::string* pErrorMessage) const;
};

struct TestRecoveryHeader {
  std::array<unsigned char, peanutbutter::SB_RECOVERY_HEADER_LENGTH> mRawBytes = {0, 0, 0, 0, 0, 0};
  unsigned long long mStride = 0;

  bool Equals(const TestRecoveryHeader& pOther, std::string* pErrorMessage) const;
};

struct TestFile {
  std::string mPath;
  peanutbutter::ultima::ByteVector mBytes;

  TestFile() = default;
  TestFile(std::string pPath, peanutbutter::ultima::ByteVector pBytes);

  bool Equals(const TestFile& pOther, std::string* pErrorMessage) const;
};

struct TestBlockL1 {
  std::array<unsigned char, peanutbutter::SB_RECOVERY_HEADER_LENGTH> mRecoveryHeaderBytes = {0, 0, 0, 0, 0, 0};
  peanutbutter::ultima::ByteVector mPayloadBytes;

  TestBlockL1() = default;
  TestBlockL1(std::array<unsigned char, peanutbutter::SB_RECOVERY_HEADER_LENGTH> pRecoveryHeaderBytes,
              peanutbutter::ultima::ByteVector pPayloadBytes);

  bool Equals(const TestBlockL1& pOther, std::string* pErrorMessage) const;
};

struct TestBlockL3 {
  std::vector<TestBlockL1> mBlockList;

  TestBlockL3() = default;
  explicit TestBlockL3(std::vector<TestBlockL1> pBlockList);

  bool Equals(const TestBlockL3& pOther, std::string* pErrorMessage) const;
};

struct TestArchive {
  std::string mPath;
  TestArchiveHeader mHeader;
  std::vector<TestBlockL3> mBlockList;

  TestArchive() = default;
  explicit TestArchive(TestArchiveHeader pHeader);
  TestArchive(std::string pPath, TestArchiveHeader pHeader);

  bool Load(const std::string& pFilePath,
            const peanutbutter::ultima::FileSystem& pFileSystem,
            std::string* pErrorMessage);
  bool Load(const peanutbutter::ultima::ByteVector& pBytes, std::string* pErrorMessage);
  bool Equals(const TestArchive& pOther, std::string* pErrorMessage) const;
};

peanutbutter::ultima::ByteVector ToBytes(const TestBlockL1& pBlock);
peanutbutter::ultima::ByteVector ToBytes(const TestBlockL3& pBlock);
peanutbutter::ultima::ByteVector ToBytes(const TestArchive& pArchive);

}  // namespace peanutbutter::ultima::testing

#endif  // PEANUT_BUTTER_ULTIMA_TEST_WRAPPERS_HPP_
