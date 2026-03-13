#include "AppShell_Types.hpp"

namespace peanutbutter {

void CapturingLogger::LogStatus(const std::string& pMessage) {
  mStatusMessages.push_back(pMessage);
}

void CapturingLogger::LogError(const std::string& pMessage) {
  mErrorMessages.push_back(pMessage);
}

const std::vector<std::string>& CapturingLogger::StatusMessages() const {
  return mStatusMessages;
}

const std::vector<std::string>& CapturingLogger::ErrorMessages() const {
  return mErrorMessages;
}

const char* ErrorCodeToString(ErrorCode pCode) {
  switch (pCode) {
    case ErrorCode::kNone:
      return "NONE";
    case ErrorCode::kCanceled:
      return "CANCELED";
    case ErrorCode::kInvalidRequest:
      return "INVALID_REQUEST";
    case ErrorCode::kFileSystem:
      return "FILE_SYSTEM";
    case ErrorCode::kCrypt:
      return "CRYPT";
    case ErrorCode::kArchiveHeader:
      return "ARCHIVE_HEADER";
    case ErrorCode::kGap001:
      return "GAP_001";
    case ErrorCode::kBlockChecksum:
      return "BLOCK_CHECKSUM";
    case ErrorCode::kRecordParse:
      return "RECORD_PARSE";
    case ErrorCode::kRecoverExhausted:
      return "RECOVER_EXHAUSTED";
    case ErrorCode::kInternal:
      return "INTERNAL";
  }
  return "INTERNAL";
}

std::uint32_t ErrorCodeToUInt(ErrorCode pCode) {
  return static_cast<std::uint32_t>(pCode);
}

}  // namespace peanutbutter
