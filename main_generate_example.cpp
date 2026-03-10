#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "AppCore.hpp"
#include "AppCore_Helpers.hpp"
#include "Encryption/Crypt.hpp"
#include "IO/FileSystem.hpp"
#include "IO/LocalFileSystem.hpp"

namespace {

using peanutbutter::detail::ArchiveHeader;
using peanutbutter::detail::SourceFileEntry;

constexpr std::size_t kLargeReadBufferLength = 64 * 1024;

struct ArchiveReportSource {
  std::string mPath;
  std::string mName;
  ArchiveHeader mHeader{};
  std::size_t mFileLength = 0;
  std::size_t mPayloadLength = 0;
};

struct ReportCell {
  std::string mField;
  std::string mValue;
  std::string mNote;
};

std::string FormatHexByte(unsigned char pByte) {
  std::ostringstream aStream;
  aStream << std::hex << std::nouppercase << std::setfill('0') << std::setw(2)
          << static_cast<unsigned int>(pByte);
  return aStream.str();
}

std::string FormatHexOffset(std::size_t pOffset) {
  std::ostringstream aStream;
  aStream << "0x" << std::hex << std::nouppercase << std::setfill('0') << std::setw(8)
          << pOffset;
  return aStream.str();
}

std::string FormatHex32(std::uint32_t pValue) {
  std::ostringstream aStream;
  aStream << "0x" << std::hex << std::nouppercase << std::setfill('0') << std::setw(8)
          << pValue;
  return aStream.str();
}

std::string FormatHex64(std::uint64_t pValue) {
  std::ostringstream aStream;
  aStream << "0x" << std::hex << std::nouppercase << std::setfill('0') << std::setw(16)
          << pValue;
  return aStream.str();
}

std::string BuildOutputFileName() {
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

bool WriteText(peanutbutter::FileWriteStream& pStream, const std::string& pText) {
  return pStream.Write(reinterpret_cast<const unsigned char*>(pText.data()), pText.size());
}

std::uint64_t ToLe64(const unsigned char* pBytes) {
  return peanutbutter::detail::ReadLeFromBytes(pBytes, 8);
}

std::string ZoneForOffset(std::size_t pFileOffset) {
  if (pFileOffset < peanutbutter::detail::kArchiveHeaderLength) {
    return "[AR-HEAD]";
  }
  const std::size_t aPayloadOffset = pFileOffset - peanutbutter::detail::kArchiveHeaderLength;
  const std::size_t aInBlockOffset = aPayloadOffset % peanutbutter::detail::kBlockLength;
  if (aInBlockOffset < peanutbutter::detail::kRecoveryHeaderLength) {
    return "[RC-HEAD]";
  }
  return "[PAYLOAD]";
}

std::string BuildRow(const std::string& pZone,
                     const std::string& pArchiveName,
                     const std::string& pOffsetHex,
                     const std::string& pByteHex,
                     const ReportCell& pCell) {
  std::ostringstream aOut;
  aOut << std::left
       << std::setw(10) << pZone << " | "
       << std::setw(24) << pArchiveName << " | "
       << std::setw(10) << pOffsetHex << " | "
       << std::setw(6) << pByteHex << " | "
       << std::setw(38) << pCell.mField << " | "
       << std::setw(20) << pCell.mValue << " | "
       << pCell.mNote
       << "\n";
  return aOut.str();
}

ReportCell HeaderByteCell(const ArchiveHeader& pHeader, std::size_t pOffset) {
  ReportCell aCell;
  if (pOffset < 4) {
    aCell.mField = "ah_magic_b" + std::to_string(pOffset + 1) + "of4";
    aCell.mValue = FormatHex32(pHeader.mMagic);
    return aCell;
  }
  if (pOffset < 6) {
    aCell.mField = "ah_ver_major_b" + std::to_string(pOffset - 3) + "of2";
    aCell.mValue = std::to_string(pHeader.mVersionMajor);
    return aCell;
  }
  if (pOffset < 8) {
    aCell.mField = "ah_ver_minor_b" + std::to_string(pOffset - 5) + "of2";
    aCell.mValue = std::to_string(pHeader.mVersionMinor);
    return aCell;
  }
  if (pOffset < 16) {
    aCell.mField = "ah_id_b" + std::to_string(pOffset - 7) + "of8";
    aCell.mValue = FormatHex64(pHeader.mIdentifier);
    return aCell;
  }
  if (pOffset < 20) {
    aCell.mField = "ah_index_b" + std::to_string(pOffset - 15) + "of4";
    aCell.mValue = std::to_string(pHeader.mArchiveIndex);
    return aCell;
  }
  if (pOffset < 24) {
    aCell.mField = "ah_count_b" + std::to_string(pOffset - 19) + "of4";
    aCell.mValue = std::to_string(pHeader.mArchiveCount);
    return aCell;
  }
  if (pOffset < 28) {
    aCell.mField = "ah_payload_len_b" + std::to_string(pOffset - 23) + "of4";
    aCell.mValue = std::to_string(pHeader.mPayloadLength);
    return aCell;
  }
  if (pOffset == 28) {
    aCell.mField = "ah_file_count_mod256";
    aCell.mValue = std::to_string(static_cast<unsigned int>(pHeader.mRecordCountMod256));
    return aCell;
  }
  if (pOffset == 29) {
    aCell.mField = "ah_dir_count_mod256";
    aCell.mValue = std::to_string(static_cast<unsigned int>(pHeader.mFolderCountMod256));
    return aCell;
  }
  if (pOffset < 32) {
    aCell.mField = "ah_pad_b" + std::to_string(pOffset - 29) + "of2";
    aCell.mValue = "-";
    return aCell;
  }
  aCell.mField = "ah_reserved_b" + std::to_string(pOffset - 31) + "of8";
  aCell.mValue = std::to_string(pHeader.mReserved);
  return aCell;
}

class LogicalAnnotationStream {
 public:
  LogicalAnnotationStream(const std::vector<SourceFileEntry>& pFiles,
                          const std::vector<std::string>& pEmptyDirectories)
      : mFiles(pFiles),
        mEmptyDirectories(pEmptyDirectories) {}

  bool Next(ReportCell& pCell) {
    pCell = {};
    while (true) {
      if (mPhase == Phase::kPrepareRecord) {
        if (!PrepareNextRecord()) {
          return false;
        }
        continue;
      }

      if (mPhase == Phase::kDone) {
        return false;
      }

      if (mPhase == Phase::kPathLengthBytes) {
        pCell.mField = "rec_path_len_b" + std::to_string(mPhaseOffset + 1) + "of2";
        pCell.mValue = std::to_string(mCurrentPath.size());
        pCell.mNote = (mCurrentKind == RecordKind::kEndMarker) ? "end_of_stream" : "record_header";
        ++mPhaseOffset;
        if (mPhaseOffset == 2) {
          mPhaseOffset = 0;
          if (mCurrentKind == RecordKind::kEndMarker) {
            mPhase = Phase::kDone;
          } else {
            mPhase = Phase::kPathBytes;
          }
        }
        return true;
      }

      if (mPhase == Phase::kPathBytes) {
        pCell.mField = "rec_path_b" + std::to_string(mPhaseOffset + 1) + "of" +
                       std::to_string(mCurrentPath.size());
        pCell.mValue = std::to_string(mCurrentPath.size());
        pCell.mNote = (mCurrentKind == RecordKind::kDirectory) ? "directory_path" : "file_path";
        ++mPhaseOffset;
        if (mPhaseOffset == mCurrentPath.size()) {
          mPhaseOffset = 0;
          mPhase = Phase::kContentLengthBytes;
        }
        return true;
      }

      if (mPhase == Phase::kContentLengthBytes) {
        pCell.mField = "rec_content_len_b" + std::to_string(mPhaseOffset + 1) + "of6";
        if (mCurrentKind == RecordKind::kDirectory) {
          pCell.mValue = "0xffffffffffff";
          pCell.mNote = "directory_marker";
        } else {
          pCell.mValue = std::to_string(mCurrentContentLength);
          pCell.mNote = "file_length";
        }
        ++mPhaseOffset;
        if (mPhaseOffset == 6) {
          mPhaseOffset = 0;
          if (mCurrentKind == RecordKind::kFile) {
            mPhase = Phase::kContentBytes;
          } else {
            if (mCurrentKind == RecordKind::kDirectory) {
              ++mDirectoryIndex;
            }
            mPhase = Phase::kPrepareRecord;
          }
        }
        return true;
      }

      if (mPhase == Phase::kContentBytes) {
        pCell.mField = "rec_file_bytes_b" + std::to_string(mPhaseOffset + 1);
        pCell.mValue = std::to_string(mCurrentContentLength);
        pCell.mNote = "file_bytes";
        ++mPhaseOffset;
        if (mPhaseOffset == mCurrentContentLength) {
          mPhaseOffset = 0;
          ++mFileIndex;
          mPhase = Phase::kPrepareRecord;
        }
        return true;
      }
    }
  }

 private:
  enum class RecordKind {
    kNone,
    kFile,
    kDirectory,
    kEndMarker,
  };

  enum class Phase {
    kPrepareRecord,
    kPathLengthBytes,
    kPathBytes,
    kContentLengthBytes,
    kContentBytes,
    kDone,
  };

  bool PrepareNextRecord() {
    mCurrentKind = RecordKind::kNone;
    mCurrentPath.clear();
    mCurrentContentLength = 0;
    mPhaseOffset = 0;

    if (mFileIndex < mFiles.size()) {
      const SourceFileEntry& aFile = mFiles[mFileIndex];
      mCurrentKind = RecordKind::kFile;
      mCurrentPath = aFile.mRelativePath;
      mCurrentContentLength = static_cast<std::size_t>(aFile.mContentLength);
      mPhase = Phase::kPathLengthBytes;
      return true;
    }

    if (mDirectoryIndex < mEmptyDirectories.size()) {
      mCurrentKind = RecordKind::kDirectory;
      mCurrentPath = mEmptyDirectories[mDirectoryIndex];
      mCurrentContentLength = 0;
      mPhase = Phase::kPathLengthBytes;
      return true;
    }

    if (!mEndMarkerWritten) {
      mCurrentKind = RecordKind::kEndMarker;
      mCurrentPath.clear();
      mCurrentContentLength = 0;
      mEndMarkerWritten = true;
      mPhase = Phase::kPathLengthBytes;
      return true;
    }

    mPhase = Phase::kDone;
    return false;
  }

  const std::vector<SourceFileEntry>& mFiles;
  const std::vector<std::string>& mEmptyDirectories;
  std::size_t mFileIndex = 0;
  std::size_t mDirectoryIndex = 0;
  std::size_t mPhaseOffset = 0;
  bool mEndMarkerWritten = false;
  RecordKind mCurrentKind = RecordKind::kNone;
  Phase mPhase = Phase::kPrepareRecord;
  std::string mCurrentPath;
  std::size_t mCurrentContentLength = 0;
};

bool DescribePayloadByte(peanutbutter::FileReadStream& pStream,
                         std::size_t pPayloadOffset,
                         std::size_t pPayloadLength,
                         std::size_t& pCachedBlockStart,
                         unsigned char* pCachedRecoveryHeader,
                         std::uint64_t& pCachedDistance,
                         LogicalAnnotationStream& pLogicalAnnotations,
                         ReportCell& pCell) {
  pCell = {};
  if (pPayloadOffset >= pPayloadLength) {
    pCell.mField = "payload_outside_declared";
    pCell.mValue = "-";
    pCell.mNote = "beyond_payload";
    return true;
  }

  const std::size_t aOffsetInBlock = pPayloadOffset % peanutbutter::detail::kBlockLength;
  const std::size_t aBlockStart = pPayloadOffset - aOffsetInBlock;

  if (pCachedBlockStart != aBlockStart) {
    if (aBlockStart + peanutbutter::detail::kRecoveryHeaderLength > pPayloadLength) {
      pCell.mField = "payload_partial_tail";
      pCell.mValue = "-";
      pCell.mNote = "partial_block";
      return true;
    }
    if (!pStream.Read(peanutbutter::detail::kArchiveHeaderLength + aBlockStart,
                      pCachedRecoveryHeader,
                      peanutbutter::detail::kRecoveryHeaderLength)) {
      return false;
    }
    pCachedBlockStart = aBlockStart;
    pCachedDistance = ToLe64(pCachedRecoveryHeader + peanutbutter::SB_RECOVERY_CHECKSUM_LENGTH);
  }

  if (aOffsetInBlock < peanutbutter::SB_RECOVERY_CHECKSUM_LENGTH) {
    pCell.mField = "rh_checksum_b" + std::to_string(aOffsetInBlock + 1) + "of" +
                   std::to_string(peanutbutter::SB_RECOVERY_CHECKSUM_LENGTH);
    pCell.mValue = "-";
    pCell.mNote = "recovery_header";
    return true;
  }
  if (aOffsetInBlock < peanutbutter::detail::kRecoveryHeaderLength) {
    const std::size_t aDistanceByteIndex =
        aOffsetInBlock - peanutbutter::SB_RECOVERY_CHECKSUM_LENGTH + 1;
    pCell.mField = "rh_distance_b" + std::to_string(aDistanceByteIndex) + "of" +
                   std::to_string(peanutbutter::SB_RECOVERY_STRIDE_LENGTH);
    pCell.mValue = std::to_string(pCachedDistance);
    pCell.mNote = "distance_to_next_record";
    return true;
  }

  if (!pLogicalAnnotations.Next(pCell)) {
    pCell.mField = "payload_pad_after_eos";
    pCell.mValue = "-";
    pCell.mNote = "padding";
  }
  return true;
}

bool WriteExampleReport(peanutbutter::FileSystem& pFileSystem,
                        const std::string& pOutputPath,
                        const std::string& pInputPath,
                        const std::string& pArchivePath,
                        const std::vector<SourceFileEntry>& pFiles,
                        const std::vector<std::string>& pEmptyDirectories,
                        const std::vector<ArchiveReportSource>& pArchives,
                        const peanutbutter::CapturingLogger& pBundleLog,
                        std::string& pErrorMessage) {
  std::unique_ptr<peanutbutter::FileWriteStream> aOutput = pFileSystem.OpenWriteStream(pOutputPath);
  if (aOutput == nullptr || !aOutput->IsReady()) {
    pErrorMessage = "Failed to open output report: " + pOutputPath;
    return false;
  }

  std::ostringstream aHeader;
  aHeader << "format_example\n\n";
  aHeader << "Input directory: " << pInputPath << "\n";
  aHeader << "Archive directory: " << pArchivePath << "\n";
  aHeader << "Archive header length: " << peanutbutter::detail::kArchiveHeaderLength << "\n";
  aHeader << "Recovery header length: " << peanutbutter::detail::kRecoveryHeaderLength << "\n";
  aHeader << "L1 block length: " << peanutbutter::detail::kBlockLength << "\n";
  aHeader << "L1 payload bytes: " << peanutbutter::detail::kPayloadBytesPerBlock << "\n";
  aHeader << "L3 page length: " << peanutbutter::detail::kPageLength << "\n\n";

  aHeader << "Bundle log:\n";
  for (const std::string& aLogLine : pBundleLog.StatusMessages()) {
    aHeader << "  " << aLogLine << "\n";
  }
  for (const std::string& aLogLine : pBundleLog.ErrorMessages()) {
    aHeader << "  [error] " << aLogLine << "\n";
  }
  aHeader << "\n";

  aHeader << "Input files (ordered):\n";
  for (const SourceFileEntry& aFile : pFiles) {
    aHeader << "  FILE " << aFile.mRelativePath << " (" << aFile.mContentLength << " bytes)\n";
  }
  for (const std::string& aDirectory : pEmptyDirectories) {
    aHeader << "  DIR  " << aDirectory << "\n";
  }
  aHeader << "\n";

  if (!WriteText(*aOutput, aHeader.str())) {
    pErrorMessage = "Failed to write report header.";
    return false;
  }

  LogicalAnnotationStream aLogicalAnnotations(pFiles, pEmptyDirectories);
  unsigned char aChunk[kLargeReadBufferLength] = {};
  unsigned char aCachedRecoveryHeader[peanutbutter::detail::kRecoveryHeaderLength] = {};

  for (const ArchiveReportSource& aArchive : pArchives) {
    std::ostringstream aArchiveStart;
    aArchiveStart << "================================================================================\n";
    aArchiveStart << "ARCHIVE_FILE_BEGIN | " << aArchive.mName
                  << " | archive_index=" << aArchive.mHeader.mArchiveIndex
                  << " | archive_count=" << aArchive.mHeader.mArchiveCount
                  << " | payload_length=" << aArchive.mPayloadLength
                  << " | file_length=" << aArchive.mFileLength << "\n";
    aArchiveStart << "================================================================================\n";
    aArchiveStart << std::left
                  << std::setw(10) << "ZONE" << " | "
                  << std::setw(24) << "ARCHIVE_FILE" << " | "
                  << std::setw(10) << "OFFSET" << " | "
                  << std::setw(6) << "BYTE" << " | "
                  << std::setw(38) << "FIELD" << " | "
                  << std::setw(20) << "VALUE" << " | "
                  << "NOTE\n";
    aArchiveStart << "--------------------------------------------------------------------------------\n";
    if (!WriteText(*aOutput, aArchiveStart.str())) {
      pErrorMessage = "Failed to write archive heading.";
      return false;
    }

    std::unique_ptr<peanutbutter::FileReadStream> aStream = pFileSystem.OpenReadStream(aArchive.mPath);
    if (aStream == nullptr || !aStream->IsReady() || aStream->GetLength() < aArchive.mFileLength) {
      pErrorMessage = "Failed to open archive for report: " + aArchive.mPath;
      return false;
    }

    std::size_t aCachedBlockStart = std::numeric_limits<std::size_t>::max();
    std::uint64_t aCachedDistance = 0;
    std::string aPreviousZone;

    for (std::size_t aOffset = 0; aOffset < aArchive.mFileLength; aOffset += kLargeReadBufferLength) {
      const std::size_t aSpan = std::min(kLargeReadBufferLength, aArchive.mFileLength - aOffset);
      if (!aStream->Read(aOffset, aChunk, aSpan)) {
        pErrorMessage = "Failed to read archive bytes while writing report: " + aArchive.mPath;
        return false;
      }

      for (std::size_t aIndex = 0; aIndex < aSpan; ++aIndex) {
        const std::size_t aFileOffset = aOffset + aIndex;
        const std::string aZone = ZoneForOffset(aFileOffset);
        ReportCell aCell;
        if (aFileOffset < peanutbutter::detail::kArchiveHeaderLength) {
          aCell = HeaderByteCell(aArchive.mHeader, aFileOffset);
        } else {
          const std::size_t aPayloadOffset = aFileOffset - peanutbutter::detail::kArchiveHeaderLength;
          if (!DescribePayloadByte(*aStream,
                                   aPayloadOffset,
                                   aArchive.mPayloadLength,
                                   aCachedBlockStart,
                                   aCachedRecoveryHeader,
                                   aCachedDistance,
                                   aLogicalAnnotations,
                                   aCell)) {
            pErrorMessage = "Failed to decode block metadata for report: " + aArchive.mPath;
            return false;
          }
        }

        if (!aPreviousZone.empty() && aZone != aPreviousZone) {
          if (!WriteText(*aOutput, "\n")) {
            pErrorMessage = "Failed to write concern spacer.";
            return false;
          }
        }
        aPreviousZone = aZone;

        const std::string aLine = BuildRow(aZone,
                                           aArchive.mName,
                                           FormatHexOffset(aFileOffset),
                                           "0x" + FormatHexByte(aChunk[aIndex]),
                                           aCell);
        if (!WriteText(*aOutput, aLine)) {
          pErrorMessage = "Failed to write archive line to report.";
          return false;
        }
      }
    }

    if (!WriteText(*aOutput, "\nARCHIVE_FILE_END | " + aArchive.mName + "\n\n")) {
      pErrorMessage = "Failed to write archive separator.";
      return false;
    }
  }

  if (!aOutput->Close()) {
    pErrorMessage = "Failed to close report file.";
    return false;
  }
  return true;
}

bool CollectArchives(peanutbutter::FileSystem& pFileSystem,
                     const std::string& pArchiveDirectory,
                     std::vector<ArchiveReportSource>& pArchives,
                     std::string& pErrorMessage) {
  pArchives.clear();
  const auto aSelection = peanutbutter::detail::ResolveArchiveInputSelection(pFileSystem, pArchiveDirectory);
  if (!aSelection.has_value()) {
    pErrorMessage = "Archive directory does not exist: " + pArchiveDirectory;
    return false;
  }

  const std::vector<peanutbutter::DirectoryEntry> aFiles =
      peanutbutter::detail::CollectArchiveFilesByHeaderScan(pFileSystem, aSelection.value());
  if (aFiles.empty()) {
    pErrorMessage = "No archive files found in: " + pArchiveDirectory;
    return false;
  }

  for (const peanutbutter::DirectoryEntry& aEntry : aFiles) {
    ArchiveReportSource aSource;
    aSource.mPath = aEntry.mPath;
    aSource.mName = pFileSystem.FileName(aEntry.mPath);
    if (!peanutbutter::detail::TryReadArchiveHeader(
            pFileSystem, aEntry.mPath, aSource.mHeader, aSource.mFileLength)) {
      continue;
    }
    aSource.mPayloadLength = static_cast<std::size_t>(aSource.mHeader.mPayloadLength);
    if (aSource.mPayloadLength == 0 && aSource.mFileLength >= peanutbutter::detail::kArchiveHeaderLength) {
      aSource.mPayloadLength = aSource.mFileLength - peanutbutter::detail::kArchiveHeaderLength;
    }
    pArchives.push_back(aSource);
  }

  if (pArchives.empty()) {
    pErrorMessage = "No valid archive headers found in: " + pArchiveDirectory;
    return false;
  }

  std::sort(pArchives.begin(), pArchives.end(), [](const ArchiveReportSource& pLeft, const ArchiveReportSource& pRight) {
    if (pLeft.mHeader.mArchiveIndex != pRight.mHeader.mArchiveIndex) {
      return pLeft.mHeader.mArchiveIndex < pRight.mHeader.mArchiveIndex;
    }
    return pLeft.mName < pRight.mName;
  });

  return true;
}

bool GenerateExample(std::string& pErrorMessage, std::string& pOutputPath) {
  peanutbutter::LocalFileSystem aFileSystem;
  peanutbutter::PassthroughCrypt aCrypt;
  peanutbutter::CapturingLogger aLogger;
  peanutbutter::RuntimeSettings aSettings;
  peanutbutter::ApplicationCore aCore(aFileSystem, aCrypt, aLogger, aSettings);

  const std::string aCwd = aFileSystem.CurrentWorkingDirectory();
  const std::string aInputPath = aFileSystem.JoinPath(aCwd, "input");
  const std::string aArchivePath = aFileSystem.JoinPath(aCwd, "archive_example");

  if (!aFileSystem.IsDirectory(aInputPath)) {
    pErrorMessage = "Input directory not found: " + aInputPath;
    return false;
  }

  peanutbutter::BundleRequest aRequest;
  aRequest.mSourceDirectory = aInputPath;
  aRequest.mDestinationDirectory = aArchivePath;
  aRequest.mArchivePrefix = "archive_";
  aRequest.mArchiveSuffix = "xyz";
  aRequest.mArchiveBlockCount = 2;
  aRequest.mUseEncryption = false;

  const peanutbutter::OperationResult aBundleResult =
      aCore.RunBundle(aRequest, peanutbutter::DestinationAction::Clear);
  if (!aBundleResult.mSucceeded) {
    pErrorMessage = aBundleResult.mMessage;
    return false;
  }

  const std::vector<SourceFileEntry> aFiles =
      peanutbutter::detail::CollectSourceEntries(aFileSystem, aInputPath);
  if (aFiles.empty()) {
    pErrorMessage = "No readable source files found after bundle run.";
    return false;
  }
  const std::vector<std::string> aEmptyDirectories =
      peanutbutter::detail::CollectEmptyDirectoryEntries(aFileSystem, aInputPath);

  std::vector<ArchiveReportSource> aArchives;
  if (!CollectArchives(aFileSystem, aArchivePath, aArchives, pErrorMessage)) {
    return false;
  }

  pOutputPath = aFileSystem.JoinPath(aCwd, BuildOutputFileName());
  return WriteExampleReport(aFileSystem,
                            pOutputPath,
                            aInputPath,
                            aArchivePath,
                            aFiles,
                            aEmptyDirectories,
                            aArchives,
                            aLogger,
                            pErrorMessage);
}

}  // namespace

int main() {
  std::string aErrorMessage;
  std::string aOutputPath;
  if (!GenerateExample(aErrorMessage, aOutputPath)) {
    std::cerr << "Generate example failed: " << aErrorMessage << "\n";
    return 1;
  }

  std::cout << "Wrote " << aOutputPath << "\n";
  return 0;
}
