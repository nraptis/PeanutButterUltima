#ifndef PEANUT_BUTTER_ULTIMA_APP_SHELL_ARCHIVE_FORMAT_HPP_
#define PEANUT_BUTTER_ULTIMA_APP_SHELL_ARCHIVE_FORMAT_HPP_

#include <cstddef>
#include <cstdint>
#include <string>

#include "PeanutButter.hpp"

namespace peanutbutter {

inline constexpr std::uint64_t kDirectoryRecordContentMarker = 0xFFFFFFFFFFFFFFFFull;

Checksum ComputeRecoveryChecksum(const unsigned char* pPlainBlockData,
                                 const SkipRecord& pSkip);

bool ChecksumsEqual(const Checksum& pLeft, const Checksum& pRight);

bool ReadArchiveHeaderBytes(const unsigned char* pBuffer,
                            std::size_t pBufferLength,
                            ArchiveHeader& pOutHeader);
bool WriteArchiveHeaderBytes(const ArchiveHeader& pHeader,
                             unsigned char* pBuffer,
                             std::size_t pBufferLength);

bool ReadRecoveryHeaderBytes(const unsigned char* pBuffer,
                             std::size_t pBufferLength,
                             RecoveryHeader& pOutHeader);
bool WriteRecoveryHeaderBytes(const RecoveryHeader& pHeader,
                              unsigned char* pBuffer,
                              std::size_t pBufferLength);

std::string MakeArchiveFileName(const std::string& pPrefix,
                                const std::string& pSourceStem,
                                const std::string& pSuffix,
                                std::size_t pArchiveOrdinal,
                                std::size_t pArchiveCount);

bool ParseArchiveFileTemplate(const std::string& pFileName,
                              std::string& pOutPrefix,
                              std::uint32_t& pOutIndex,
                              std::string& pOutSuffix,
                              std::size_t& pOutDigits);

}  // namespace peanutbutter

#endif  // PEANUT_BUTTER_ULTIMA_APP_SHELL_ARCHIVE_FORMAT_HPP_
