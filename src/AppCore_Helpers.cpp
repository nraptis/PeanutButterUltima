#include "AppCore_Helpers.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstring>
#include <limits>
#include <random>
#include <sstream>
#include <utility>

namespace peanutbutter {

FunctionLogger::FunctionLogger(std::function<void(const std::string&, bool)> pSink) : mSink(std::move(pSink)) {}

void FunctionLogger::LogStatus(const std::string& pMessage) {
  if (mSink) {
    mSink(pMessage, false);
  }
}

void FunctionLogger::LogError(const std::string& pMessage) {
  if (mSink) {
    mSink(pMessage, true);
  }
}

void CapturingLogger::LogStatus(const std::string& pMessage) {
  mStatusMessages.push_back(pMessage);
}

void CapturingLogger::LogError(const std::string& pMessage) {
  mErrorMessages.push_back(pMessage);
}

const std::vector<std::string>& CapturingLogger::StatusMessages() const {
  return mStatusMessages;
}

const std::vector<std::string>& CapturingLogger::ErrorMessages() const {
  return mErrorMessages;
}

SessionLogger::SessionLogger(std::function<void(const std::string&, bool)> pSink) : mSink(std::move(pSink)) {}

void SessionLogger::SetFileSystem(FileSystem* pFileSystem) {
  std::lock_guard<std::mutex> aLock(mMutex);
  mFileSystem = pFileSystem;
}

void SessionLogger::BeginSession(const std::string& pFilePath) {
  std::lock_guard<std::mutex> aLock(mMutex);
  mFilePath = pFilePath;
  if (mFileSystem != nullptr && !mFilePath.empty()) {
    mFileSystem->WriteTextFile(mFilePath, std::string());
  }
}

void SessionLogger::EndSession() {
  std::lock_guard<std::mutex> aLock(mMutex);
  mFilePath.clear();
}

void SessionLogger::LogStatus(const std::string& pMessage) {
  LogLine(pMessage, false);
}

void SessionLogger::LogError(const std::string& pMessage) {
  LogLine(pMessage, true);
}

void SessionLogger::LogLine(const std::string& pMessage, bool pIsError) {
  std::function<void(const std::string&, bool)> aSink;
  std::string aFilePath;
  FileSystem* aFileSystem = nullptr;
  {
    std::lock_guard<std::mutex> aLock(mMutex);
    aSink = mSink;
    aFilePath = mFilePath;
    aFileSystem = mFileSystem;
  }

  if (aSink) {
    aSink(pMessage, pIsError);
  }

  if (aFileSystem == nullptr || aFilePath.empty()) {
    return;
  }

  std::string aLine;
  if (pIsError) {
    aLine += "[error] ";
  }
  aLine += pMessage;
  aLine += "\n";
  (void)aFileSystem->AppendTextFile(aFilePath, aLine);
}

namespace detail {
namespace {

bool EndsWithCaseInsensitive(std::string_view pText, std::string_view pSuffix) {
  if (pSuffix.size() > pText.size()) {
    return false;
  }
  const std::size_t aStart = pText.size() - pSuffix.size();
  for (std::size_t aIndex = 0; aIndex < pSuffix.size(); ++aIndex) {
    const char aLeft = static_cast<char>(std::tolower(static_cast<unsigned char>(pText[aStart + aIndex])));
    const char aRight = static_cast<char>(std::tolower(static_cast<unsigned char>(pSuffix[aIndex])));
    if (aLeft != aRight) {
      return false;
    }
  }
  return true;
}

}  // namespace

PreflightResult MakeInvalid(const std::string& pTitle, const std::string& pMessage) {
  return {PreflightSignal::RedLight, pTitle, pMessage};
}

PreflightResult MakeNeedsDestination(const std::string& pTitle, const std::string& pMessage) {
  return {PreflightSignal::YellowLight, pTitle, pMessage};
}

OperationResult MakeFailure(Logger& pLogger, const std::string& pTitle, const std::string& pMessage) {
  pLogger.LogError(pMessage);
  return {false, pTitle, pMessage};
}

std::string FormatBytes(std::uint64_t pBytes) {
  std::ostringstream aStream;
  constexpr std::uint64_t kKiB = 1024;
  constexpr std::uint64_t kMiB = 1024 * 1024;
  if (pBytes >= kMiB) {
    aStream << (pBytes / kMiB) << " MiB";
  } else if (pBytes >= kKiB) {
    aStream << (pBytes / kKiB) << " KiB";
  } else {
    aStream << pBytes << " B";
  }
  return aStream.str();
}

std::optional<ArchiveInputSelection> ResolveArchiveInputSelection(const FileSystem& pFileSystem,
                                                                  const std::string& pArchivePathOrDirectory) {
  ArchiveInputSelection aSelection;
  if (pFileSystem.IsDirectory(pArchivePathOrDirectory)) {
    aSelection.mSearchDirectory = pArchivePathOrDirectory;
    return aSelection;
  }
  if (pFileSystem.IsFile(pArchivePathOrDirectory)) {
    aSelection.mSearchDirectory = pFileSystem.ParentPath(pArchivePathOrDirectory);
    aSelection.mSelectedFilePath = pArchivePathOrDirectory;
    return aSelection;
  }
  return std::nullopt;
}

std::optional<ArchiveInputSelection> ResolveArchiveInputSelection(const FileSystem& pFileSystem,
                                                                  const std::string& pArchivePathOrDirectory,
                                                                  const std::string& pSelectedFilePath) {
  if (!pSelectedFilePath.empty()) {
    if (!pFileSystem.IsFile(pSelectedFilePath)) {
      return std::nullopt;
    }
    ArchiveInputSelection aSelection;
    aSelection.mSearchDirectory = pFileSystem.ParentPath(pSelectedFilePath);
    aSelection.mSelectedFilePath = pSelectedFilePath;
    return aSelection;
  }
  return ResolveArchiveInputSelection(pFileSystem, pArchivePathOrDirectory);
}

std::vector<DirectoryEntry> CollectArchiveFilesByHeaderScan(const FileSystem& pFileSystem,
                                                            const ArchiveInputSelection& pSelection) {
  std::vector<DirectoryEntry> aFiltered;
  const std::vector<DirectoryEntry> aFiles = pFileSystem.ListFiles(pSelection.mSearchDirectory);
  for (const DirectoryEntry& aEntry : aFiles) {
    if (!aEntry.mIsDirectory) {
      aFiltered.push_back(aEntry);
    }
  }
  std::sort(aFiltered.begin(), aFiltered.end(), [&pFileSystem](const DirectoryEntry& pLeft, const DirectoryEntry& pRight) {
    return pFileSystem.FileName(pLeft.mPath) < pFileSystem.FileName(pRight.mPath);
  });
  return aFiltered;
}

std::vector<SourceFileEntry> CollectSourceEntries(const FileSystem& pFileSystem,
                                                  const std::string& pSourceDirectory) {
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

std::vector<std::string> CollectEmptyDirectoryEntries(const FileSystem& pFileSystem,
                                                      const std::string& pSourceDirectory) {
  std::vector<DirectoryEntry> aDirectories = pFileSystem.ListDirectoriesRecursive(pSourceDirectory);
  std::sort(aDirectories.begin(), aDirectories.end(),
            [](const DirectoryEntry& pLeft, const DirectoryEntry& pRight) {
              return pLeft.mRelativePath < pRight.mRelativePath;
            });

  std::vector<std::string> aEmptyDirectories;
  for (const DirectoryEntry& aDirectory : aDirectories) {
    if (aDirectory.mRelativePath.empty()) {
      continue;
    }
    if (!pFileSystem.DirectoryHasEntries(aDirectory.mPath)) {
      aEmptyDirectories.push_back(aDirectory.mRelativePath);
    }
  }
  return aEmptyDirectories;
}

std::size_t LogicalCapacityForPhysicalLength(std::size_t pPhysicalLength) {
  const std::size_t aFullBlocks = pPhysicalLength / kBlockLength;
  const std::size_t aRemainder = pPhysicalLength % kBlockLength;
  const std::size_t aPartialPayloadLength =
      aRemainder > kRecoveryHeaderLength ? aRemainder - kRecoveryHeaderLength : 0;
  return (aFullBlocks * kPayloadBytesPerBlock) + aPartialPayloadLength;
}

std::size_t EffectiveArchivePhysicalPayloadLength(const RuntimeSettings& pSettings) {
  if (pSettings.mArchiveFileLength <= kArchiveHeaderLength) {
    return 0;
  }
  const std::size_t aRawPayloadLength = pSettings.mArchiveFileLength - kArchiveHeaderLength;
  return (aRawPayloadLength / kPageLength) * kPageLength;
}

std::size_t EffectiveArchiveLogicalPayloadLength(const RuntimeSettings& pSettings) {
  return LogicalCapacityForPhysicalLength(EffectiveArchivePhysicalPayloadLength(pSettings));
}

std::size_t PhysicalOffsetForLogicalOffset(std::size_t pLogicalOffset) {
  const std::size_t aBlockIndex = pLogicalOffset / kPayloadBytesPerBlock;
  const std::size_t aOffsetInBlock = pLogicalOffset % kPayloadBytesPerBlock;
  return (aBlockIndex * kBlockLength) + kRecoveryHeaderLength + aOffsetInBlock;
}

std::size_t PhysicalLengthForLogicalLength(std::size_t pLogicalLength) {
  if (pLogicalLength == 0) {
    return 0;
  }
  const std::size_t aLastPhysicalOffset = PhysicalOffsetForLogicalOffset(pLogicalLength - 1);
  return ((aLastPhysicalOffset + 1 + kPageLength - 1) / kPageLength) * kPageLength;
}

bool TryBuildBundleSettings(const RuntimeSettings& pBaseSettings,
                            const BundleRequest& pRequest,
                            RuntimeSettings& pBundleSettings,
                            std::string* pErrorMessage) {
  if (pRequest.mArchiveBlockCount < 1) {
    if (pErrorMessage != nullptr) {
      *pErrorMessage = "Bundle failed: archive block count must be at least 1.";
    }
    return false;
  }

  if (pRequest.mArchiveBlockCount > (std::numeric_limits<std::size_t>::max() / peanutbutter::SB_L3_LENGTH)) {
    if (pErrorMessage != nullptr) {
      *pErrorMessage = "Bundle failed: archive block count is too large.";
    }
    return false;
  }

  const std::size_t aPayloadLength = peanutbutter::SB_L3_LENGTH * pRequest.mArchiveBlockCount;
  if (aPayloadLength > (std::numeric_limits<std::size_t>::max() - kArchiveHeaderLength)) {
    if (pErrorMessage != nullptr) {
      *pErrorMessage = "Bundle failed: archive file length overflowed.";
    }
    return false;
  }

  pBundleSettings = pBaseSettings;
  pBundleSettings.mArchiveFileLength = kArchiveHeaderLength + aPayloadLength;
  return true;
}

void GenerateChecksum(const unsigned char* pBlockData, unsigned char* pDestination) {
  if (pBlockData == nullptr || pDestination == nullptr) {
    return;
  }

  std::memset(pDestination, 0, peanutbutter::SB_RECOVERY_CHECKSUM_LENGTH);
  std::uint32_t aTwiddle = 5381u;
  std::size_t aDestinationIndex = 0;
  for (std::size_t aDataIndex = 0; aDataIndex < peanutbutter::SB_PAYLOAD_SIZE; ++aDataIndex) {
    const unsigned char aByte = pBlockData[kRecoveryHeaderLength + aDataIndex];
    aTwiddle = static_cast<std::uint32_t>(aByte) + (aTwiddle << 6) + (aTwiddle << 16) - aTwiddle;
    pDestination[aDestinationIndex] ^= static_cast<unsigned char>(aTwiddle & 0xFFu);
    ++aDestinationIndex;
    if (aDestinationIndex >= peanutbutter::SB_RECOVERY_CHECKSUM_LENGTH) {
      aDestinationIndex = 0;
    }
  }
}

std::uint64_t ReadLeFromBytes(const unsigned char* pBytes, std::size_t pWidth) {
  std::uint64_t aValue = 0;
  for (std::size_t aIndex = 0; aIndex < pWidth; ++aIndex) {
    aValue |= static_cast<std::uint64_t>(pBytes[aIndex]) << (8 * aIndex);
  }
  return aValue;
}

void WriteLeToBytes(unsigned char* pBytes, std::uint64_t pValue, std::size_t pWidth) {
  for (std::size_t aIndex = 0; aIndex < pWidth; ++aIndex) {
    pBytes[aIndex] = static_cast<unsigned char>((pValue >> (8 * aIndex)) & 0xFFu);
  }
}

bool TryReadArchiveHeader(const FileSystem& pFileSystem,
                          const std::string& pPath,
                          ArchiveHeader& pHeader,
                          std::size_t& pFileLength) {
  pFileLength = 0;
  std::unique_ptr<FileReadStream> aStream = pFileSystem.OpenReadStream(pPath);
  if (aStream == nullptr || !aStream->IsReady() || aStream->GetLength() < kArchiveHeaderLength) {
    return false;
  }

  unsigned char aHeaderBytes[kArchiveHeaderLength] = {};
  if (!aStream->Read(0, aHeaderBytes, kArchiveHeaderLength)) {
    return false;
  }

  std::memcpy(&pHeader, aHeaderBytes, sizeof(ArchiveHeader));
  if (pHeader.mMagic != kMagicHeaderBytes) {
    return false;
  }

  pFileLength = aStream->GetLength();
  return true;
}

void WriteArchiveHeaderBytes(const ArchiveHeader& pHeader, unsigned char* pBytes) {
  std::memset(pBytes, 0, kArchiveHeaderLength);
  std::memcpy(pBytes, &pHeader, sizeof(ArchiveHeader));
}

std::uint64_t GenerateArchiveIdentifier() {
#ifdef PEANUT_BUTTER_ULTIMA_TEST_BUILD
  return 0x1fd423f6e2995a96ULL;
#else
  const std::uint64_t aNow =
      static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
  const std::uint64_t aRandom = (static_cast<std::uint64_t>(std::random_device{}()) << 32) ^
                                static_cast<std::uint64_t>(std::random_device{}());
  return aNow ^ aRandom;
#endif
}

bool ApplyDestinationAction(FileSystem& pFileSystem,
                            const std::string& pDestinationDirectory,
                            DestinationAction pAction) {
  if (pAction == DestinationAction::Cancel) {
    return false;
  }
  if (pAction == DestinationAction::Clear) {
    return pFileSystem.ClearDirectory(pDestinationDirectory);
  }
  return pFileSystem.EnsureDirectory(pDestinationDirectory);
}

std::string MakeArchiveName(const std::string& pSourceStem,
                            const std::string& pPrefix,
                            const std::string& pSuffix,
                            std::size_t pSequenceOneBased,
                            std::size_t pArchiveCount) {
  const std::size_t aWidth = std::max<std::size_t>(1, std::to_string(pArchiveCount).size());
  std::ostringstream aStream;
  aStream << pPrefix << pSourceStem << "_";
  aStream.width(static_cast<std::streamsize>(aWidth));
  aStream.fill('0');
  aStream << pSequenceOneBased;
  if (!pSuffix.empty() && pSuffix.front() != '.') {
    aStream << '.';
  }
  aStream << pSuffix;
  return aStream.str();
}

void LogMissingArchiveRanges(Logger& pLogger,
                             const std::vector<std::pair<std::uint32_t, std::uint32_t>>& pMissingRanges) {
  for (const auto& aRange : pMissingRanges) {
    if (aRange.first == aRange.second) {
      pLogger.LogStatus("Discovery: Missing archive " + std::to_string(aRange.first));
    } else {
      pLogger.LogStatus("Discovery: Missing archives " + std::to_string(aRange.first) + "-" +
                        std::to_string(aRange.second));
    }
  }
}

}  // namespace detail
}  // namespace peanutbutter
