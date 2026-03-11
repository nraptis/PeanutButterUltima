#include "AppCore_Helpers.hpp"

namespace peanutbutter::detail {
namespace {

std::string ResolveRecoverArchivePath(const RecoverRequest& pRequest) {
  if (!pRequest.mRecoveryStartFilePath.empty()) {
    return pRequest.mRecoveryStartFilePath;
  }
  return pRequest.mArchiveDirectory;
}

PreflightResult CheckRecoverDecodeJob(FileSystem& pFileSystem,
                                      const std::string& pArchivePathOrDirectory,
                                      const std::string& pDestinationDirectory) {
  const std::optional<ArchiveInputSelection> aInputSelection =
      ResolveArchiveInputSelection(pFileSystem, pArchivePathOrDirectory, std::string());
  if (!aInputSelection.has_value()) {
    return MakeInvalid("Recover Failed", "Recover failed: archive path does not exist.");
  }

  const std::vector<DirectoryEntry> aArchiveFiles = CollectArchiveFilesByHeaderScan(pFileSystem, aInputSelection.value());
  if (aArchiveFiles.empty()) {
    return MakeInvalid("Recover Failed", "Recover failed: no files were found to scan for archive headers.");
  }
  if (pFileSystem.DirectoryHasEntries(pDestinationDirectory)) {
    return MakeNeedsDestination("Recover Destination", "Recover destination is not empty.");
  }
  return {PreflightSignal::GreenLight, "", ""};
}

bool DiscoverRecoverDecodeArchives(FileSystem& pFileSystem,
                                   Logger& pLogger,
                                   const RuntimeSettings& pSettings,
                                   const std::string& pArchivePathOrDirectory,
                                   std::vector<ArchiveHeaderRecord>& pDecodeArchives,
                                   std::string& pErrorMessage) {
  std::vector<ArchiveHeaderRecord> aDiscoveredArchives;
  std::size_t aSelectedArchiveOffset = 0;
  bool aSelectedArchiveOffsetValid = false;
  if (!DiscoverArchiveSetForDecode(pFileSystem,
                                   pLogger,
                                   pSettings,
                                   "Recover",
                                   pArchivePathOrDirectory,
                                   std::string(),
                                   aDiscoveredArchives,
                                   aSelectedArchiveOffset,
                                   aSelectedArchiveOffsetValid,
                                   pErrorMessage)) {
    return false;
  }

  const std::size_t aStartArchiveOffset = aSelectedArchiveOffsetValid ? aSelectedArchiveOffset : 0;
  if (aStartArchiveOffset >= aDiscoveredArchives.size()) {
    pErrorMessage = "Recover failed: selected recovery start is outside the discovered archive set.";
    return false;
  }

  pDecodeArchives.assign(aDiscoveredArchives.begin() + aStartArchiveOffset, aDiscoveredArchives.end());
  if (pDecodeArchives.empty()) {
    pErrorMessage = "Recover failed: no archives were available from the selected recovery start.";
    return false;
  }

  pLogger.LogStatus("Selected archive set start = " + std::to_string(aStartArchiveOffset) + ", count = " +
                    std::to_string(pDecodeArchives.size()) + ".");
  return true;
}

void LogRecoverStartProbe(Logger& pLogger,
                          bool pProbeMatched,
                          std::size_t pRecoveredArchiveOffset,
                          std::size_t pResolvedPhysicalOffset,
                          const std::string& pProbeErrorMessage) {
  if (pProbeMatched) {
    pLogger.LogStatus("Recover start probe located a candidate record boundary at archive offset " +
                      std::to_string(pRecoveredArchiveOffset) + ", byte " +
                      FormatBytes(pResolvedPhysicalOffset) +
                      "; recovery walk will still process the full selected archive range.");
    return;
  }

  if (!pProbeErrorMessage.empty()) {
    pLogger.LogStatus("Recover start probe did not locate a valid boundary (" + pProbeErrorMessage +
                      "); recovery walk will continue from the selected archive start.");
  }
}

bool DecodeRecoverArchives(FileSystem& pFileSystem,
                           const Crypt& pCrypt,
                           Logger& pLogger,
                           const std::vector<ArchiveHeaderRecord>& pDecodeArchives,
                           const std::string& pDestinationDirectory,
                           bool pUseEncryption,
                           std::uint64_t& pProcessedBytes,
                           std::size_t& pFilesProcessed,
                           std::size_t& pEmptyDirectoriesProcessed,
                           std::string& pErrorMessage) {
  return DecodeArchiveSetForRecover(pFileSystem,
                                    pCrypt,
                                    pLogger,
                                    pDecodeArchives,
                                    0,
                                    pDestinationDirectory,
                                    true,
                                    pUseEncryption,
                                    pProcessedBytes,
                                    pFilesProcessed,
                                    pEmptyDirectoriesProcessed,
                                    pErrorMessage);
}

}  // namespace

OperationResult RunRecoverDecodeJob(FileSystem& pFileSystem,
                                    const Crypt& pCrypt,
                                    Logger& pLogger,
                                    const RuntimeSettings& pSettings,
                                    const std::string& pArchivePathOrDirectory,
                                    const std::string& pDestinationPathOrDirectory,
                                    bool pUseEncryption,
                                    DestinationAction pAction) {
  const std::string aDestinationDirectory = ResolveDirectoryTargetPath(pFileSystem, pDestinationPathOrDirectory);
  const PreflightResult aPreflight = CheckRecoverDecodeJob(pFileSystem, pArchivePathOrDirectory, aDestinationDirectory);
  if (aPreflight.mSignal == PreflightSignal::RedLight) {
    return MakeFailure(pLogger, aPreflight.mTitle, aPreflight.mMessage);
  }
  if (aPreflight.mSignal == PreflightSignal::YellowLight && pAction == DestinationAction::Cancel) {
    return {false, "Recover Canceled", "Recover canceled."};
  }
  if (!ApplyDestinationAction(pFileSystem, aDestinationDirectory, pAction)) {
    return MakeFailure(pLogger,
                       "Recover Failed",
                       "Recover failed: could not prepare destination directory.");
  }

  pLogger.LogStatus("Recover job starting...");

  std::string aErrorMessage;
  std::vector<ArchiveHeaderRecord> aDecodeArchives;
  if (!DiscoverRecoverDecodeArchives(pFileSystem,
                                     pLogger,
                                     pSettings,
                                     pArchivePathOrDirectory,
                                     aDecodeArchives,
                                     aErrorMessage)) {
    return MakeFailure(pLogger, "Recover Failed", aErrorMessage);
  }

  std::size_t aRecoveredArchiveOffset = 0;
  std::size_t aResolvedPhysicalOffset = 0;
  const bool aProbeMatched = ResolveRecoveryStartPositionForDecode(pFileSystem,
                                                                    pCrypt,
                                                                    aDecodeArchives,
                                                                    pUseEncryption,
                                                                    aRecoveredArchiveOffset,
                                                                    aResolvedPhysicalOffset,
                                                                    aErrorMessage);
  LogRecoverStartProbe(pLogger,
                       aProbeMatched,
                       aRecoveredArchiveOffset,
                       aResolvedPhysicalOffset,
                       aErrorMessage);

  std::uint64_t aProcessedBytes = 0;
  std::size_t aFilesProcessed = 0;
  std::size_t aEmptyDirectoriesProcessed = 0;
  if (!DecodeRecoverArchives(pFileSystem,
                             pCrypt,
                             pLogger,
                             aDecodeArchives,
                             aDestinationDirectory,
                             pUseEncryption,
                             aProcessedBytes,
                             aFilesProcessed,
                             aEmptyDirectoriesProcessed,
                             aErrorMessage)) {
    return MakeFailure(pLogger, "Recover Failed", "Recover failed: " + aErrorMessage);
  }

  if (aEmptyDirectoriesProcessed > 0) {
    pLogger.LogStatus("Successfully unpacked " + std::to_string(aEmptyDirectoriesProcessed) +
                      " empty directories.");
  }

  pLogger.LogStatus("Recovered archive " + std::to_string(aDecodeArchives.size()) + " / " +
                    std::to_string(aDecodeArchives.size()) + ", " +
                    std::to_string(aFilesProcessed) + " files written, " +
                    FormatBytes(aProcessedBytes) + " / " + FormatBytes(aProcessedBytes) + ".");
  pLogger.LogStatus("Recover job complete.");
  return {true, "Recover Complete", "Recover completed successfully."};
}

PreflightResult CheckRecoverJob(FileSystem& pFileSystem, const RecoverRequest& pRequest) {
  if (!pRequest.mRecoveryStartFilePath.empty() &&
      !pFileSystem.IsFile(pRequest.mRecoveryStartFilePath) &&
      !pFileSystem.IsDirectory(pRequest.mRecoveryStartFilePath)) {
    return MakeInvalid("Recover Failed", "Recover failed: recovery start path does not exist.");
  }
  const std::string aArchivePathOrDirectory = ResolveRecoverArchivePath(pRequest);
  return CheckRecoverDecodeJob(
      pFileSystem, aArchivePathOrDirectory, ResolveDirectoryTargetPath(pFileSystem, pRequest.mDestinationDirectory));
}

OperationResult RunRecoverJob(FileSystem& pFileSystem,
                              const Crypt& pCrypt,
                              Logger& pLogger,
                              const RuntimeSettings& pSettings,
                              const RecoverRequest& pRequest,
                              DestinationAction pAction) {
  return RunRecoverDecodeJob(pFileSystem,
                             pCrypt,
                             pLogger,
                             pSettings,
                             ResolveRecoverArchivePath(pRequest),
                             pRequest.mDestinationDirectory,
                             pRequest.mUseEncryption,
                             pAction);
}

}  // namespace peanutbutter::detail
