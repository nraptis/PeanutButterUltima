#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "AppShell_ArchiveFormat.hpp"
#include "AppShell_Bundle.hpp"
#include "AppShell_Types.hpp"
#include "Encryption/Crypt.hpp"
#include "IO/LocalFileSystem.hpp"

namespace {

using peanutbutter::ArchiveHeader;
using peanutbutter::CapturingLogger;
using peanutbutter::DirectoryEntry;
using peanutbutter::FileReadStream;
using peanutbutter::FileSystem;
using peanutbutter::L3BlockBuffer;
using peanutbutter::LocalFileSystem;
using peanutbutter::OperationResult;
using peanutbutter::RecoveryHeader;
using peanutbutter::SourceEntry;

struct ReportRow {
  std::string mZone;
  std::string mArchiveName;
  std::string mOffsetHex;
  std::string mByteHex;
  std::string mField;
  std::string mValue;
  std::string mNote;
};

struct ByteField {
  std::size_t mStart = 0;
  std::size_t mLength = 0;
  std::string mName;
  std::string mValue;
};

std::string FormatHexByte(unsigned char pByte) {
  std::ostringstream aOut;
  aOut << "0x" << std::hex << std::nouppercase << std::setfill('0') << std::setw(2)
       << static_cast<unsigned int>(pByte);
  return aOut.str();
}

std::string FormatHexOffset(std::size_t pOffset) {
  std::ostringstream aOut;
  aOut << "0x" << std::hex << std::nouppercase << std::setfill('0') << std::setw(8) << pOffset;
  return aOut.str();
}

std::string FormatHex32(std::uint32_t pValue) {
  std::ostringstream aOut;
  aOut << "0x" << std::hex << std::nouppercase << std::setfill('0') << std::setw(8) << pValue;
  return aOut.str();
}

std::string FormatHex64(std::uint64_t pValue) {
  std::ostringstream aOut;
  aOut << "0x" << std::hex << std::nouppercase << std::setfill('0') << std::setw(16) << pValue;
  return aOut.str();
}

std::string BuildTimeStampFileName() {
  std::time_t aNow = std::time(nullptr);
  std::tm aLocalTime{};
#if defined(_WIN32)
  localtime_s(&aLocalTime, &aNow);
#else
  localtime_r(&aNow, &aLocalTime);
#endif
  char aBuffer[64] = {};
  std::strftime(aBuffer, sizeof(aBuffer), "example_%Y_%m_%d_%H_%M.txt", &aLocalTime);
  return std::string(aBuffer);
}

std::string ZoneForOffset(std::size_t pOffset) {
  if (pOffset < peanutbutter::kArchiveHeaderLength) {
    return "[AR-HEAD]";
  }
  const std::size_t aAfterHeader = pOffset - peanutbutter::kArchiveHeaderLength;
  const std::size_t aOffsetInBlock = aAfterHeader % peanutbutter::kBlockSizeL3;
  if (aOffsetInBlock < peanutbutter::kRecoveryHeaderLength) {
    return "[RC-HEAD]";
  }
  return "[PAYLOAD]";
}

const ByteField* FindField(const std::vector<ByteField>& pFields, std::size_t pOffset) {
  for (const ByteField& aField : pFields) {
    if (pOffset >= aField.mStart && pOffset < (aField.mStart + aField.mLength)) {
      return &aField;
    }
  }
  return nullptr;
}

std::string FieldByteLabel(const ByteField& pField, std::size_t pOffset) {
  const std::size_t aByteIndex = (pOffset - pField.mStart) + 1u;
  return pField.mName + "_b" + std::to_string(aByteIndex) + "of" + std::to_string(pField.mLength);
}

bool CollectBundleSourceEntries(const FileSystem& pFileSystem,
                                const std::string& pSourcePath,
                                std::vector<SourceEntry>& pEntries,
                                std::string& pErrorMessage) {
  pEntries.clear();
  if (pFileSystem.IsFile(pSourcePath)) {
    SourceEntry aEntry;
    aEntry.mSourcePath = pSourcePath;
    aEntry.mRelativePath = pFileSystem.FileName(pSourcePath);
    aEntry.mIsDirectory = false;
    std::unique_ptr<FileReadStream> aRead = pFileSystem.OpenReadStream(pSourcePath);
    if (aRead == nullptr || !aRead->IsReady()) {
      pErrorMessage = "failed opening source file while collecting entries.";
      return false;
    }
    aEntry.mFileLength = static_cast<std::uint64_t>(aRead->GetLength());
    pEntries.push_back(aEntry);
    return true;
  }

  if (!pFileSystem.IsDirectory(pSourcePath)) {
    pErrorMessage = "source path is not a readable file or directory.";
    return false;
  }

  std::vector<DirectoryEntry> aFiles = pFileSystem.ListFilesRecursive(pSourcePath);
  std::sort(aFiles.begin(), aFiles.end(), [](const DirectoryEntry& pLeft, const DirectoryEntry& pRight) {
    return pLeft.mRelativePath < pRight.mRelativePath;
  });
  for (const DirectoryEntry& aEntry : aFiles) {
    if (aEntry.mIsDirectory) {
      continue;
    }
    SourceEntry aSourceEntry;
    aSourceEntry.mSourcePath = aEntry.mPath;
    aSourceEntry.mRelativePath = aEntry.mRelativePath;
    aSourceEntry.mIsDirectory = false;
    std::unique_ptr<FileReadStream> aRead = pFileSystem.OpenReadStream(aEntry.mPath);
    if (aRead == nullptr || !aRead->IsReady()) {
      pErrorMessage = "failed opening source file while collecting entries.";
      return false;
    }
    aSourceEntry.mFileLength = static_cast<std::uint64_t>(aRead->GetLength());
    pEntries.push_back(aSourceEntry);
  }

  std::vector<DirectoryEntry> aDirs = pFileSystem.ListDirectoriesRecursive(pSourcePath);
  std::sort(aDirs.begin(), aDirs.end(), [](const DirectoryEntry& pLeft, const DirectoryEntry& pRight) {
    return pLeft.mRelativePath < pRight.mRelativePath;
  });
  for (const DirectoryEntry& aEntry : aDirs) {
    if (aEntry.mRelativePath.empty()) {
      continue;
    }
    if (!pFileSystem.DirectoryHasEntries(aEntry.mPath)) {
      SourceEntry aSourceEntry;
      aSourceEntry.mSourcePath.clear();
      aSourceEntry.mRelativePath = aEntry.mRelativePath;
      aSourceEntry.mIsDirectory = true;
      pEntries.push_back(aSourceEntry);
    }
  }

  if (pEntries.empty()) {
    pErrorMessage = "source directory yielded no files or empty directories.";
    return false;
  }
  return true;
}

std::vector<std::string> CollectArchiveFiles(const FileSystem& pFileSystem, const std::string& pArchiveDirectory) {
  std::vector<std::string> aFiles;
  std::vector<DirectoryEntry> aEntries = pFileSystem.ListFilesRecursive(pArchiveDirectory);
  for (const DirectoryEntry& aEntry : aEntries) {
    if (!aEntry.mIsDirectory) {
      aFiles.push_back(aEntry.mPath);
    }
  }
  std::sort(aFiles.begin(), aFiles.end());
  return aFiles;
}

bool WritePatternFile(FileSystem& pFileSystem, const std::string& pPath, std::size_t pLength) {
  peanutbutter::ByteBuffer aBuffer;
  if (!aBuffer.Resize(pLength)) {
    return false;
  }
  for (std::size_t aIndex = 0u; aIndex < pLength; ++aIndex) {
    aBuffer.Data()[aIndex] = static_cast<unsigned char>((aIndex * 17u) & 0xFFu);
  }
  return pFileSystem.WriteFile(pPath, aBuffer);
}

class PayloadLabeler final {
 public:
  explicit PayloadLabeler(std::size_t pPreviewBytes)
      : mPreviewBytes(pPreviewBytes) {}

  void NextLabel(std::size_t pPayloadOffset, unsigned char pByte, std::string& pField, std::string& pValue, std::string& pNote) {
    (void)pPayloadOffset;
    pValue.clear();
    pNote.clear();
    if (mStage == Stage::kPadding) {
      pField = "padding";
      pNote = "unused payload";
      return;
    }

    if (mStage == Stage::kPathLength) {
      mPathLengthLe[mPathLengthBytesUsed++] = pByte;
      pField = "rec" + std::to_string(mRecordIndex) + "_path_len_b" + std::to_string(mPathLengthBytesUsed) + "of2";
      pNote = "record header";
      if (mPathLengthBytesUsed == 2u) {
        mCurrentPathLength = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(mPathLengthLe[0]) |
            (static_cast<std::uint16_t>(mPathLengthLe[1]) << 8));
        pValue = std::to_string(mCurrentPathLength);
        mPathLengthBytesUsed = 0u;
        if (mCurrentPathLength == 0u) {
          mStage = Stage::kPadding;
          pNote = "terminator";
        } else {
          mStage = Stage::kPathBytes;
          mPathBytesUsed = 0u;
        }
      }
      return;
    }

    if (mStage == Stage::kPathBytes) {
      ++mPathBytesUsed;
      pField = "rec" + std::to_string(mRecordIndex) + "_path_b" + std::to_string(mPathBytesUsed) + "of" +
               std::to_string(std::max<std::uint16_t>(1u, mCurrentPathLength));
      pNote = "path bytes";
      if (mPathBytesUsed >= mCurrentPathLength) {
        mStage = Stage::kContentLength;
        mContentLengthBytesUsed = 0u;
      }
      return;
    }

    if (mStage == Stage::kContentLength) {
      mContentLengthLe[mContentLengthBytesUsed++] = pByte;
      pField = "rec" + std::to_string(mRecordIndex) + "_content_len_b" + std::to_string(mContentLengthBytesUsed) + "of8";
      pNote = "content length";
      if (mContentLengthBytesUsed == 8u) {
        mCurrentContentLength = 0u;
        for (int aByteIndex = 0; aByteIndex < 8; ++aByteIndex) {
          mCurrentContentLength |=
              static_cast<std::uint64_t>(mContentLengthLe[static_cast<std::size_t>(aByteIndex)]) << (8 * aByteIndex);
        }
        pValue = (mCurrentContentLength == peanutbutter::kDirectoryRecordContentMarker)
                     ? std::string("directory_marker")
                     : std::to_string(mCurrentContentLength);

        mContentLengthBytesUsed = 0u;
        if (mCurrentContentLength == peanutbutter::kDirectoryRecordContentMarker) {
          ++mRecordIndex;
          mStage = Stage::kPathLength;
        } else if (mCurrentContentLength == 0u) {
          ++mRecordIndex;
          mStage = Stage::kPathLength;
        } else {
          mContentBytesUsed = 0u;
          mStage = Stage::kContentBytes;
        }
      }
      return;
    }

    ++mContentBytesUsed;
    pField = "rec" + std::to_string(mRecordIndex) + "_content_b" + std::to_string(mContentBytesUsed);
    pNote = "content bytes";
    if (mContentBytesUsed >= mCurrentContentLength) {
      ++mRecordIndex;
      mStage = Stage::kPathLength;
    }
  }

 private:
  enum class Stage {
    kPathLength,
    kPathBytes,
    kContentLength,
    kContentBytes,
    kPadding,
  };

  std::size_t mPreviewBytes = 0u;
  Stage mStage = Stage::kPathLength;

  std::size_t mRecordIndex = 0u;
  unsigned char mPathLengthLe[2] = {};
  std::size_t mPathLengthBytesUsed = 0u;
  std::uint16_t mCurrentPathLength = 0u;
  std::size_t mPathBytesUsed = 0u;

  unsigned char mContentLengthLe[8] = {};
  std::size_t mContentLengthBytesUsed = 0u;
  std::uint64_t mCurrentContentLength = 0u;
  std::uint64_t mContentBytesUsed = 0u;
};

std::string BuildReport(const std::string& pArchiveName,
                        std::size_t pArchiveLength,
                        const ArchiveHeader& pHeader,
                        const RecoveryHeader& pRecovery,
                        bool pChecksumValid,
                        const std::vector<unsigned char>& pArchiveBytes,
                        const CapturingLogger& pLogger,
                        const std::vector<SourceEntry>& pEntries) {
  std::ostringstream aOut;
  aOut << "A real example (current format)\n\n";
  aOut << "Note:\n";
  aOut << "  This file is generated for the current engine build and current on-disk layout.\n";
  aOut << "  It reflects archive header + recovery header + payload byte mapping.\n\n";
  aOut << "Constants:\n";
  aOut << "  Archive header length: " << peanutbutter::kArchiveHeaderLength << "\n";
  aOut << "  Recovery header length: " << peanutbutter::kRecoveryHeaderLength << "\n";
  aOut << "  Block length: " << peanutbutter::kBlockSizeL3 << "\n";
  aOut << "  Payload bytes per block: " << peanutbutter::kPayloadBytesPerL3 << "\n";
  aOut << "  Major.Minor version: " << peanutbutter::kMajorVersion << "." << peanutbutter::kMinorVersion << "\n";
  aOut << "\n";

  aOut << "Bundle log:\n";
  for (const std::string& aStatus : pLogger.StatusMessages()) {
    aOut << "  " << aStatus << "\n";
  }
  for (const std::string& aError : pLogger.ErrorMessages()) {
    aOut << "  [error] " << aError << "\n";
  }
  aOut << "\n";

  aOut << "Input entries:\n";
  for (const SourceEntry& aEntry : pEntries) {
    aOut << "  " << (aEntry.mIsDirectory ? "DIR  " : "FILE ") << aEntry.mRelativePath;
    if (!aEntry.mIsDirectory) {
      aOut << " (" << aEntry.mFileLength << " bytes)";
    }
    aOut << "\n";
  }
  aOut << "\n";

  aOut << "Archive under inspection:\n";
  aOut << "  name: " << pArchiveName << "\n";
  aOut << "  length: " << pArchiveLength << " bytes\n";
  aOut << "  checksum valid: " << (pChecksumValid ? "yes" : "no") << "\n\n";

  std::vector<ByteField> aHeaderFields = {
      {0u, 4u, "ah_magic", FormatHex32(pHeader.mMagic)},
      {4u, 2u, "ah_ver_major", std::to_string(pHeader.mVersionMajor)},
      {6u, 2u, "ah_ver_minor", std::to_string(pHeader.mVersionMinor)},
      {8u, 4u, "ah_archive_index", std::to_string(pHeader.mArchiveIndex)},
      {12u, 4u, "ah_archive_count", std::to_string(pHeader.mArchiveCount)},
      {16u, 4u, "ah_payload_length", std::to_string(pHeader.mPayloadLength)},
      {20u, 1u, "ah_record_count_mod256", std::to_string(static_cast<unsigned int>(pHeader.mRecordCountMod256))},
      {21u, 1u, "ah_folder_count_mod256", std::to_string(static_cast<unsigned int>(pHeader.mFolderCountMod256))},
      {22u, 2u, "ah_reserved16", std::to_string(pHeader.mReserved16)},
      {24u, 8u, "ah_reserved_a", FormatHex64(pHeader.mReservedA)},
      {32u, 8u, "ah_reserved_b", FormatHex64(pHeader.mReservedB)},
  };

  std::vector<ByteField> aRecoveryFields = {
      {0u, 8u, "rh_checksum_c1", FormatHex64(pRecovery.mChecksum.mWord1)},
      {8u, 8u, "rh_checksum_c2", FormatHex64(pRecovery.mChecksum.mWord2)},
      {16u, 8u, "rh_checksum_c3", FormatHex64(pRecovery.mChecksum.mWord3)},
      {24u, 8u, "rh_checksum_c4", FormatHex64(pRecovery.mChecksum.mWord4)},
      {32u, 2u, "rh_skip_archive_distance", std::to_string(pRecovery.mSkip.mArchiveDistance)},
      {34u, 2u, "rh_skip_block_distance", std::to_string(pRecovery.mSkip.mBlockDistance)},
      {36u, 4u, "rh_skip_byte_distance", std::to_string(pRecovery.mSkip.mByteDistance)},
  };

  aOut << "================================================================================\n";
  aOut << "ZONE       | ARCHIVE_FILE             | OFFSET     | BYTE   | FIELD                                  | VALUE                | NOTE\n";
  aOut << "--------------------------------------------------------------------------------\n";

  const std::size_t aHeaderBytesToPrint = std::min<std::size_t>(peanutbutter::kArchiveHeaderLength, pArchiveBytes.size());
  for (std::size_t aOffset = 0u; aOffset < aHeaderBytesToPrint; ++aOffset) {
    const ByteField* aField = FindField(aHeaderFields, aOffset);
    const std::string aFieldLabel = (aField == nullptr) ? std::string("ah_unknown") : FieldByteLabel(*aField, aOffset);
    const std::string aValue = (aField == nullptr) ? std::string("-") : aField->mValue;
    aOut << std::left << std::setw(10) << "[AR-HEAD]" << " | "
         << std::setw(24) << pArchiveName << " | "
         << std::setw(10) << FormatHexOffset(aOffset) << " | "
         << std::setw(6) << FormatHexByte(pArchiveBytes[aOffset]) << " | "
         << std::setw(38) << aFieldLabel << " | "
         << std::setw(20) << aValue << " | "
         << "archive header\n";
  }

  const std::size_t aBlockStart = peanutbutter::kArchiveHeaderLength;
  const std::size_t aRecoveryStart = aBlockStart;
  const std::size_t aRecoveryEnd = std::min<std::size_t>(aRecoveryStart + peanutbutter::kRecoveryHeaderLength, pArchiveBytes.size());
  for (std::size_t aOffset = aRecoveryStart; aOffset < aRecoveryEnd; ++aOffset) {
    const std::size_t aRecoveryOffset = aOffset - aRecoveryStart;
    const ByteField* aField = FindField(aRecoveryFields, aRecoveryOffset);
    const std::string aFieldLabel = (aField == nullptr) ? std::string("rh_unknown") : FieldByteLabel(*aField, aRecoveryOffset);
    const std::string aValue = (aField == nullptr) ? std::string("-") : aField->mValue;
    aOut << std::left << std::setw(10) << "[RC-HEAD]" << " | "
         << std::setw(24) << pArchiveName << " | "
         << std::setw(10) << FormatHexOffset(aOffset) << " | "
         << std::setw(6) << FormatHexByte(pArchiveBytes[aOffset]) << " | "
         << std::setw(38) << aFieldLabel << " | "
         << std::setw(20) << aValue << " | "
         << "recovery header\n";
  }

  const std::size_t aPayloadStart = aRecoveryStart + peanutbutter::kRecoveryHeaderLength;
  const std::size_t aPayloadPreviewBytes = std::min<std::size_t>(
      std::min<std::size_t>(256u, peanutbutter::kPayloadBytesPerL3),
      (pArchiveBytes.size() > aPayloadStart) ? (pArchiveBytes.size() - aPayloadStart) : 0u);

  PayloadLabeler aPayloadLabeler(aPayloadPreviewBytes);
  for (std::size_t aIndex = 0u; aIndex < aPayloadPreviewBytes; ++aIndex) {
    const std::size_t aOffset = aPayloadStart + aIndex;
    std::string aField;
    std::string aValue;
    std::string aNote;
    aPayloadLabeler.NextLabel(aIndex, pArchiveBytes[aOffset], aField, aValue, aNote);
    aOut << std::left << std::setw(10) << "[PAYLOAD]" << " | "
         << std::setw(24) << pArchiveName << " | "
         << std::setw(10) << FormatHexOffset(aOffset) << " | "
         << std::setw(6) << FormatHexByte(pArchiveBytes[aOffset]) << " | "
         << std::setw(38) << aField << " | "
         << std::setw(20) << (aValue.empty() ? "-" : aValue) << " | "
         << aNote << "\n";
  }

  if (aPayloadPreviewBytes < peanutbutter::kPayloadBytesPerL3) {
    aOut << "\n[PAYLOAD] preview truncated at " << aPayloadPreviewBytes
         << " bytes (payload block has " << peanutbutter::kPayloadBytesPerL3 << " bytes).\n";
  }
  return aOut.str();
}

}  // namespace

int main() {
  LocalFileSystem aFileSystem;
  peanutbutter::PassthroughCrypt aCrypt;

  const std::string aRoot = "/tmp/pb_example_current";
  const std::string aInput = aFileSystem.JoinPath(aRoot, "input");
  const std::string aArchive = aFileSystem.JoinPath(aRoot, "archive");

  (void)aFileSystem.ClearDirectory(aRoot);
  if (!aFileSystem.EnsureDirectory(aInput) || !aFileSystem.EnsureDirectory(aArchive)) {
    std::cerr << "failed to create example directories\n";
    return 1;
  }

  if (!aFileSystem.WriteTextFile(aFileSystem.JoinPath(aInput, "alpha.txt"), "alpha\n") ||
      !aFileSystem.EnsureDirectory(aFileSystem.JoinPath(aInput, "nested")) ||
      !aFileSystem.WriteTextFile(aFileSystem.JoinPath(aInput, "nested/beta.txt"), "beta\n") ||
      !WritePatternFile(aFileSystem, aFileSystem.JoinPath(aInput, "nested/gamma.bin"), 37u) ||
      !aFileSystem.EnsureDirectory(aFileSystem.JoinPath(aInput, "empty_dir"))) {
    std::cerr << "failed to seed input files\n";
    return 1;
  }

  std::vector<SourceEntry> aEntries;
  std::string aCollectError;
  if (!CollectBundleSourceEntries(aFileSystem, aInput, aEntries, aCollectError)) {
    std::cerr << "collect source entries failed: " << aCollectError << "\n";
    return 1;
  }

  peanutbutter::BundleRequest aRequest;
  aRequest.mDestinationDirectory = aArchive;
  aRequest.mSourceStem = "example_input";
  aRequest.mArchivePrefix = "bundle_";
  aRequest.mArchiveSuffix = ".PBTR";
  aRequest.mArchiveBlockCount = 1u;
  aRequest.mUseEncryption = false;

  CapturingLogger aLogger;
  const OperationResult aBundleResult =
      peanutbutter::Bundle(aRequest, aEntries, aFileSystem, aCrypt, aLogger, nullptr);
  if (!aBundleResult.mSucceeded) {
    std::cerr << "bundle failed: " << aBundleResult.mFailureMessage << "\n";
    for (const std::string& aError : aLogger.ErrorMessages()) {
      std::cerr << "  " << aError << "\n";
    }
    return 1;
  }

  const std::vector<std::string> aArchiveFiles = CollectArchiveFiles(aFileSystem, aArchive);
  if (aArchiveFiles.empty()) {
    std::cerr << "no archive files generated\n";
    return 1;
  }

  const std::string aArchivePath = aArchiveFiles.front();
  const std::string aArchiveName = aFileSystem.FileName(aArchivePath);
  std::unique_ptr<FileReadStream> aRead = aFileSystem.OpenReadStream(aArchivePath);
  if (aRead == nullptr || !aRead->IsReady()) {
    std::cerr << "failed opening archive file for report\n";
    return 1;
  }

  const std::size_t aArchiveLength = aRead->GetLength();
  peanutbutter::ByteBuffer aArchiveBytes;
  if (!aArchiveBytes.Resize(aArchiveLength) || !aRead->Read(0u, aArchiveBytes.Data(), aArchiveLength)) {
    std::cerr << "failed reading archive bytes\n";
    return 1;
  }

  ArchiveHeader aHeader{};
  if (!ReadArchiveHeaderBytes(aArchiveBytes.Data(), aArchiveBytes.Size(), aHeader)) {
    std::cerr << "failed parsing archive header\n";
    return 1;
  }

  if (aArchiveBytes.Size() < peanutbutter::kArchiveHeaderLength + peanutbutter::kRecoveryHeaderLength) {
    std::cerr << "archive too small for recovery header\n";
    return 1;
  }

  RecoveryHeader aRecovery{};
  if (!ReadRecoveryHeaderBytes(aArchiveBytes.Data() + peanutbutter::kArchiveHeaderLength,
                               peanutbutter::kRecoveryHeaderLength,
                               aRecovery)) {
    std::cerr << "failed parsing recovery header\n";
    return 1;
  }

  L3BlockBuffer aBlock{};
  if (!aRead->Read(peanutbutter::kArchiveHeaderLength, aBlock.Data(), peanutbutter::kBlockSizeL3)) {
    std::cerr << "failed reading first block\n";
    return 1;
  }

  const peanutbutter::Checksum aExpected = ComputeRecoveryChecksum(aBlock.Data(), aRecovery.mSkip);
  const bool aChecksumValid = ChecksumsEqual(aExpected, aRecovery.mChecksum);

  const std::string aReport = BuildReport(aArchiveName,
                                          aArchiveLength,
                                          aHeader,
                                          aRecovery,
                                          aChecksumValid,
                                          std::vector<unsigned char>(aArchiveBytes.Data(),
                                                                     aArchiveBytes.Data() + aArchiveBytes.Size()),
                                          aLogger,
                                          aEntries);

  const std::string aOutputPath = "example_old/archive_format_example.txt";
  const std::string aStampedPath = "example_old/" + BuildTimeStampFileName();
  if (!aFileSystem.WriteTextFile(aOutputPath, aReport)) {
    std::cerr << "failed writing report to " << aOutputPath << "\n";
    return 1;
  }
  (void)aFileSystem.WriteTextFile(aStampedPath, aReport);

  std::cout << "Generated " << aOutputPath << "\n";
  std::cout << "Generated " << aStampedPath << "\n";
  std::cout << "Inspected archive: " << aArchiveName << "\n";
  return 0;
}
