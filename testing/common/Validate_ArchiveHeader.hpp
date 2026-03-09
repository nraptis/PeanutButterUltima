#ifndef PEANUT_BUTTER_ULTIMA_VALIDATE_ARCHIVE_HEADER_HPP_
#define PEANUT_BUTTER_ULTIMA_VALIDATE_ARCHIVE_HEADER_HPP_

#include <cstdint>
#include <string>

#include "Test_Wrappers.hpp"

namespace peanutbutter::testing {

bool Validate_ArchiveHeader(TestArchiveHeader pArchiveHeader,
                            unsigned long long pArchiveIndex,
                            bool pArchiveIndexSkip,
                            unsigned long long pRecoveryFlag,
                            bool pRecoveryFlagSkip,
                            unsigned long long pArchiveIdentifier,
                            bool pArchiveIdentifierSkip,
                            std::string* pErrorMessage);

}  // namespace peanutbutter::testing

#endif  // PEANUT_BUTTER_ULTIMA_VALIDATE_ARCHIVE_HEADER_HPP_
