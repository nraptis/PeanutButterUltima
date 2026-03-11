#include "AppCore_Helpers.hpp"

namespace peanutbutter::detail {
namespace {

PreflightResult CheckUnbundleDecodeJob(FileSystem& pFileSystem,
                                       const std::string& pArchivePathOrDirectory,
                                       const std::string& pDestinationDirectory) {
  const std::optional<ArchiveInputSelection> aInputSelection =
      ResolveArchiveInputSelection(pFileSystem, pArchivePathOrDirectory, std::string());
  if (!aInputSelection.has_value()) {
    return MakeInvalid("Unbundle Failed", "Unbundle failed: archive path does not exist.");
  }

  const std::vector<DirectoryEntry> aArchiveFiles = CollectArchiveFilesByHeaderScan(pFileSystem, aInputSelection.value());
  if (aArchiveFiles.empty()) {
    return MakeInvalid("Unbundle Failed", "Unbundle failed: no files were found to scan for archive headers.");
  }
  if (pFileSystem.DirectoryHasEntries(pDestinationDirectory)) {
    return MakeNeedsDestination("Unbundle Destination", "Unbundle destination is not empty.");
  }
  return {PreflightSignal::GreenLight, "", ""};
}

}  // namespace

OperationResult RunUnbundleDecodeJob(FileSystem& pFileSystem,
                                     const Crypt& pCrypt,
                                     Logger& pLogger,
                                     const RuntimeSettings& pSettings,
                                     const std::string& pArchivePathOrDirectory,
                                     const std::string& pDestinationPathOrDirectory,
                                     bool pUseEncryption,
                                     DestinationAction pAction) {
  const std::string aDestinationDirectory = ResolveDirectoryTargetPath(pFileSystem, pDestinationPathOrDirectory);
  const PreflightResult aPreflight = CheckUnbundleDecodeJob(pFileSystem, pArchivePathOrDirectory, aDestinationDirectory);
  if (aPreflight.mSignal == PreflightSignal::RedLight) {
    return MakeFailure(pLogger, aPreflight.mTitle, aPreflight.mMessage);
  }
  if (aPreflight.mSignal == PreflightSignal::YellowLight && pAction == DestinationAction::Cancel) {
    return {false, "Unbundle Canceled", "Unbundle canceled."};
  }
  if (!ApplyDestinationAction(pFileSystem, aDestinationDirectory, pAction)) {
    return MakeFailure(pLogger,
                       "Unbundle Failed",
                       "Unbundle failed: could not prepare destination directory.");
  }

  pLogger.LogStatus("Unbundle job starting...");
  std::vector<ArchiveHeaderRecord> aDiscoveredArchives;
  std::size_t aSelectedArchiveOffset = 0;
  bool aSelectedArchiveOffsetValid = false;
  std::string aErrorMessage;
  if (!DiscoverArchiveSetForDecode(pFileSystem,
                                   pLogger,
                                   pSettings,
                                   "Unbundle",
                                   pArchivePathOrDirectory,
                                   std::string(),
                                   aDiscoveredArchives,
                                   aSelectedArchiveOffset,
                                   aSelectedArchiveOffsetValid,
                                   aErrorMessage)) {
    return MakeFailure(pLogger, "Unbundle Failed", aErrorMessage);
  }

  const std::size_t aStartArchiveOffset = aSelectedArchiveOffsetValid ? aSelectedArchiveOffset : 0;
  if (aStartArchiveOffset >= aDiscoveredArchives.size()) {
    return MakeFailure(pLogger, "Unbundle Failed", "Unbundle failed: selected start is outside the discovered archive set.");
  }

  std::vector<ArchiveHeaderRecord> aDecodeArchives;
  aDecodeArchives.assign(aDiscoveredArchives.begin() + aStartArchiveOffset,
                         aDiscoveredArchives.end());
  if (aDecodeArchives.empty()) {
    return MakeFailure(pLogger, "Unbundle Failed", "Unbundle failed: no archives were available from the selected start.");
  }
  pLogger.LogStatus("Selected archive set start = " + std::to_string(aStartArchiveOffset) + ", count = " +
                    std::to_string(aDecodeArchives.size()) + ".");

  std::uint64_t aProcessedBytes = 0;
  std::size_t aFilesProcessed = 0;
  std::size_t aEmptyDirectoriesProcessed = 0;
  if (!DecodeArchiveSetForUnbundle(pFileSystem,
                                   pCrypt,
                                   pLogger,
                                   aDecodeArchives,
                                   0,
                                   aDestinationDirectory,
                                   true,
                                   pUseEncryption,
                                   aProcessedBytes,
                                   aFilesProcessed,
                                   aEmptyDirectoriesProcessed,
                                   aErrorMessage)) {
    return MakeFailure(pLogger, "Unbundle Failed", "Unbundle failed: " + aErrorMessage);
  }

  if (aEmptyDirectoriesProcessed > 0) {
    pLogger.LogStatus("Successfully unpacked " + std::to_string(aEmptyDirectoriesProcessed) +
                      " empty directories.");
  }

  pLogger.LogStatus("Unbundled archive " + std::to_string(aDecodeArchives.size()) + " / " +
                    std::to_string(aDecodeArchives.size()) + ", " +
                    std::to_string(aFilesProcessed) + " files written, " +
                    FormatBytes(aProcessedBytes) + " / " + FormatBytes(aProcessedBytes) + ".");
  pLogger.LogStatus("Unbundle job complete.");
  return {true, "Unbundle Complete", "Unbundle completed successfully."};
}

PreflightResult CheckUnbundleJob(FileSystem& pFileSystem, const UnbundleRequest& pRequest) {
  return CheckUnbundleDecodeJob(
      pFileSystem, pRequest.mArchiveDirectory, ResolveDirectoryTargetPath(pFileSystem, pRequest.mDestinationDirectory));
}

OperationResult RunUnbundleJob(FileSystem& pFileSystem,
                               const Crypt& pCrypt,
                               Logger& pLogger,
                               const RuntimeSettings& pSettings,
                               const UnbundleRequest& pRequest,
                               DestinationAction pAction) {
  return RunUnbundleDecodeJob(pFileSystem,
                              pCrypt,
                              pLogger,
                              pSettings,
                              pRequest.mArchiveDirectory,
                              pRequest.mDestinationDirectory,
                              pRequest.mUseEncryption,
                              pAction);
}

}  // namespace peanutbutter::detail
