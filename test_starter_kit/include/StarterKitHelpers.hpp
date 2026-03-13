#ifndef PEANUT_BUTTER_ULTIMA_TEST_STARTER_KIT_STARTER_KIT_HELPERS_HPP_
#define PEANUT_BUTTER_ULTIMA_TEST_STARTER_KIT_STARTER_KIT_HELPERS_HPP_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "AppShell_ArchiveFormat.hpp"
#include "AppShell_Types.hpp"
#include "IO/FileSystem.hpp"

namespace peanutbutter {
namespace testkit {

struct TreeSnapshotEntry {
  std::string mRelativePath;
  bool mIsDirectory = false;
  std::vector<unsigned char> mContents;
};

struct ArchiveBlockInspection {
  std::size_t mBlockIndex = 0u;
  RecoveryHeader mRecoveryHeader{};
  Checksum mExpectedChecksum{};
  bool mRecoveryHeaderReadable = false;
  bool mChecksumMatches = false;
};

struct ArchiveInspection {
  std::string mArchivePath;
  std::size_t mArchiveLength = 0u;
  ArchiveHeader mArchiveHeader{};
  bool mArchiveHeaderReadable = false;
  std::vector<ArchiveBlockInspection> mBlocks;
};

bool BuildSourceEntriesFromDirectory(FileSystem& pFileSystem,
                                     const std::string& pSourceDirectory,
                                     std::vector<SourceEntry>& pOutEntries);

std::vector<std::string> ListSortedArchiveFiles(FileSystem& pFileSystem,
                                                const std::string& pArchiveDirectory);

bool SnapshotTree(FileSystem& pFileSystem,
                  const std::string& pRootDirectory,
                  std::vector<TreeSnapshotEntry>& pOutEntries);

bool CompareSnapshots(const std::vector<TreeSnapshotEntry>& pExpected,
                     const std::vector<TreeSnapshotEntry>& pActual,
                     std::string& pOutError);

bool InspectArchive(FileSystem& pFileSystem,
                    const std::string& pArchivePath,
                    ArchiveInspection& pOutInspection);

bool InspectArchiveSet(FileSystem& pFileSystem,
                       const std::vector<std::string>& pArchivePaths,
                       std::vector<ArchiveInspection>& pOutInspections);

void PrintOperationResult(const std::string& pLabel,
                          const OperationResult& pResult);

void PrintArchiveInspectionSummary(const std::vector<ArchiveInspection>& pInspections);

}  // namespace testkit
}  // namespace peanutbutter

#endif  // PEANUT_BUTTER_ULTIMA_TEST_STARTER_KIT_STARTER_KIT_HELPERS_HPP_
