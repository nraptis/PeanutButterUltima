#ifndef PEANUT_BUTTER_ULTIMA_TEST_UTILS_HPP_
#define PEANUT_BUTTER_ULTIMA_TEST_UTILS_HPP_

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "AppCore.hpp"
#include "Test_Wrappers.hpp"

namespace peanutbutter::testing {

inline constexpr std::uint32_t kDemoMagicHeaderBytes = peanutbutter::MAGIC_HEADER_BYTES;
inline constexpr std::uint32_t kDemoMagicFooterBytes = peanutbutter::MAGIC_FOOTER_BYTES;
inline constexpr std::uint32_t kDemoMajorVersion = peanutbutter::MAJOR_VERSION;
inline constexpr std::uint32_t kDemoMinorVersion = peanutbutter::MINOR_VERSION;
inline constexpr std::size_t kDemoPlainTextHeaderLength = peanutbutter::SB_PLAIN_TEXT_HEADER_LENGTH;
inline constexpr std::size_t kDemoRecoveryHeaderLength = peanutbutter::SB_RECOVERY_HEADER_LENGTH;
inline constexpr std::size_t kDemoEbMaxLength = peanutbutter::EB_MAX_LENGTH;
inline constexpr std::size_t kDemoPayloadSize = peanutbutter::SB_PAYLOAD_SIZE;
inline constexpr std::size_t kDemoL1Length = peanutbutter::SB_L1_LENGTH;
inline constexpr std::size_t kDemoL2Length = peanutbutter::SB_L2_LENGTH;
inline constexpr std::size_t kDemoL3Length = peanutbutter::SB_L3_LENGTH;
inline constexpr std::size_t kDemoMaxValidFilePathLength = peanutbutter::MAX_VALID_FILE_PATH_LENGTH;

bool Fail(const std::string& pMessage, std::string* pErrorMessage);
unsigned long long ReadLe48(const unsigned char* pData);
unsigned long long ReadLe48(const std::array<unsigned char, kDemoRecoveryHeaderLength>& pBytes);
peanutbutter::ByteVector ToBytes(const std::string& pText);
std::string ToHex(const peanutbutter::ByteVector& pBytes, std::size_t pOffset, std::size_t pLength);
bool Read_ArchiveHeader(const peanutbutter::ByteVector& pArchiveBytes,
                        TestArchiveHeader& pArchiveHeader,
                        std::string* pErrorMessage);
bool Read_RecoveryHeader(const peanutbutter::ByteVector& pArchiveBytes,
                         std::size_t pOffset,
                         TestRecoveryHeader& pRecoveryHeader,
                         std::string* pErrorMessage);

}  // namespace peanutbutter::testing

#endif  // PEANUT_BUTTER_ULTIMA_TEST_UTILS_HPP_
