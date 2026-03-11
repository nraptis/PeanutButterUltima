#ifndef PEANUT_BUTTER_ULTIMA_FORMAT_CONSTANTS_HPP_
#define PEANUT_BUTTER_ULTIMA_FORMAT_CONSTANTS_HPP_

#include <cstddef>
#include <cstdint>

namespace peanutbutter {

inline constexpr std::uint32_t MAGIC_HEADER_BYTES = 0xF01DAB1E;
inline constexpr std::uint32_t MAGIC_FOOTER_BYTES = 0xDECAFBAD;
inline constexpr std::uint32_t MAJOR_VERSION = 1;
inline constexpr std::uint32_t MINOR_VERSION = 0;
inline constexpr std::size_t SB_PLAIN_TEXT_HEADER_LENGTH = 40;
inline constexpr std::size_t SB_RECOVERY_CHECKSUM_LENGTH = 8;
inline constexpr std::size_t SB_RECOVERY_STRIDE_LENGTH = 8;
inline constexpr std::size_t SB_RECOVERY_HEADER_LENGTH =
    SB_RECOVERY_CHECKSUM_LENGTH + SB_RECOVERY_STRIDE_LENGTH;
inline constexpr std::size_t EB_MAX_LENGTH = 16;



#ifdef PEANUT_BUTTER_ULTIMA_TEST_BUILD
inline constexpr std::size_t SB_PAYLOAD_SIZE = 48 - SB_RECOVERY_HEADER_LENGTH;  // Fixed test-target payload size.
//inline constexpr std::size_t SB_PAYLOAD_SIZE = 88;  // Fixed test-target payload size.
//inline constexpr std::size_t SB_PAYLOAD_SIZE = SB_RECOVERY_HEADER_LENGTH + 3;

#else
// inline constexpr std::size_t SB_PAYLOAD_SIZE = 12;
inline constexpr std::size_t SB_PAYLOAD_SIZE = 24;//((1048608 >> 2) - SB_RECOVERY_HEADER_LENGTH);
#endif

inline constexpr std::size_t SB_L1_LENGTH = SB_PAYLOAD_SIZE + SB_RECOVERY_HEADER_LENGTH;
inline constexpr std::size_t SB_L2_LENGTH = SB_L1_LENGTH + SB_L1_LENGTH;
inline constexpr std::size_t SB_L3_LENGTH = SB_L2_LENGTH + SB_L2_LENGTH;
inline constexpr std::size_t MAX_VALID_FILE_PATH_LENGTH = 2048;

}  // namespace peanutbutter

#endif  // PEANUT_BUTTER_ULTIMA_FORMAT_CONSTANTS_HPP_
