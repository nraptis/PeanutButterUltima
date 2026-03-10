#ifndef PEANUT_BUTTER_ULTIMA_TESTS_COMMON_TEST_EXECUTE_UNBUNDLE_HPP_
#define PEANUT_BUTTER_ULTIMA_TESTS_COMMON_TEST_EXECUTE_UNBUNDLE_HPP_

#include <string>

#include "AppCore.hpp"

namespace peanutbutter::testing {

struct UnbundleExecutionSpec {
  std::string mArchiveDirectory;
  std::string mDestinationDirectory;
  bool mUseEncryption = false;
  DestinationAction mAction = DestinationAction::Clear;
};

OperationResult ExecuteUnbundle(ApplicationCore& pCore, const UnbundleExecutionSpec& pSpec);

}  // namespace peanutbutter::testing

#endif  // PEANUT_BUTTER_ULTIMA_TESTS_COMMON_TEST_EXECUTE_UNBUNDLE_HPP_
