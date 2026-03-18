#ifndef PEANUT_BUTTER_ULTIMA_DECODE_DECODE_DISCOVERY_HPP_
#define PEANUT_BUTTER_ULTIMA_DECODE_DECODE_DISCOVERY_HPP_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "AppShell_ArchiveFormat.hpp"
#include "AppShell_Types.hpp"
#include "IO/FileSystem.hpp"

namespace peanutbutter {
namespace decode_internal {

struct ArchiveCandidate {
  std::string mPath;
  std::string mFileName;
  std::uint32_t mArchiveIndex = 0u;
  std::size_t mDigitWidth = 0u;
  std::size_t mFileLength = 0u;
  std::uint32_t mBlockCount = 0u;
  ArchiveHeader mHeader{};
  bool mHasReadableHeader = false;
};

struct DiscoverySelection {
  std::vector<ArchiveCandidate> mArchives;
  std::vector<ArchiveFileBox> mArchiveBoxes;
  std::uint32_t mMinIndex = 0u;
  std::uint32_t mMaxIndex = 0u;
  std::uint32_t mHeaderDeclaredMaxIndex = 0u;
  std::size_t mGapCount = 0u;
  std::size_t mClippedArchiveCount = 0u;
  std::uint64_t mExpectedFamilyId = 0u;
  bool mHasExpectedFamilyId = false;
  bool mHasHeaderDeclaredMaxIndex = false;
};

bool ResolveSelectionEncryptionStrength(const DiscoverySelection& pSelection,
                                        EncryptionStrength& pOutStrength,
                                        std::string& pOutErrorMessage);

bool SelectArchiveFamily(const std::vector<std::string>& pArchiveFileList,
                         const std::string& pModeName,
                         bool pRecoverMode,
                         FileSystem& pFileSystem,
                         Logger& pLogger,
                         DiscoverySelection& pOutSelection,
                         CancelCoordinator* pCancelCoordinator = nullptr);

}  // namespace decode_internal
}  // namespace peanutbutter

#endif  // PEANUT_BUTTER_ULTIMA_DECODE_DECODE_DISCOVERY_HPP_
