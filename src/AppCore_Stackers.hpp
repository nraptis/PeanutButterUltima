#ifndef PEANUT_BUTTER_ULTIMA_APP_CORE_STACKERS_HPP_
#define PEANUT_BUTTER_ULTIMA_APP_CORE_STACKERS_HPP_

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "AppCore_MemoryOwners.hpp"

namespace peanutbutter::ultima {

// Archive layout planning is stack-heavy but bounded by archive count rather
// than file size. It stays separate from the owning runtime buffers.
struct PlannedArchiveLayout {
  std::size_t mLogicalStart = 0;
  std::size_t mLogicalEnd = 0;
  std::size_t mUsedPayloadLength = 0;
};

std::vector<PlannedArchiveLayout> BuildArchiveLayouts(std::size_t pLogicalByteLength,
                                                      std::size_t pLogicalCapacity,
                                                      std::size_t pArchivePayloadLength);

std::vector<std::vector<unsigned long long>> GenerateAllRecoveryHeaders(
    const std::vector<PlannedArchiveLayout>& pLayouts,
    const std::vector<std::size_t>& pFileStartLogicalOffsets);

std::vector<SourceFileEntry> CollectSourceEntries(const FileSystem& pFileSystem,
                                                  const std::string& pSourceDirectory);

std::vector<ArchiveHeaderRecord> ScanArchiveDirectory(const FileSystem& pFileSystem,
                                                      const std::string& pArchiveDirectory);

bool HasAnyReadableArchive(const FileSystem& pFileSystem, const std::string& pArchiveDirectory);

std::optional<std::size_t> FindArchiveIndex(const FileSystem& pFileSystem,
                                            const std::vector<ArchiveHeaderRecord>& pArchives,
                                            const std::string& pPath);

std::optional<std::size_t> FindArchiveHeaderIndex(const FileSystem& pFileSystem,
                                                  const std::vector<ArchiveHeaderRecord>& pArchives,
                                                  const std::string& pPath);

}  // namespace peanutbutter::ultima

#endif  // PEANUT_BUTTER_ULTIMA_APP_CORE_STACKERS_HPP_
