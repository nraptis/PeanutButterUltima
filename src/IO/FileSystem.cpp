#include "IO/FileSystem.hpp"

namespace peanutbutter::ultima {

bool FileSystem::ReadFile(const std::string& pPath, ByteVector& pContents) const {
  pContents.clear();
  std::unique_ptr<FileReadStream> aStream = OpenReadStream(pPath);
  if (aStream == nullptr || !aStream->IsReady()) {
    return false;
  }
  pContents.resize(aStream->GetLength());
  return aStream->Read(0, pContents.data(), pContents.size());
}

bool FileSystem::WriteFile(const std::string& pPath, const ByteVector& pContents) {
  return WriteFile(pPath, pContents.data(), pContents.size());
}

bool FileSystem::WriteFile(const std::string& pPath, const unsigned char* pContents, std::size_t pLength) {
  std::unique_ptr<FileWriteStream> aStream = OpenWriteStream(pPath);
  if (aStream == nullptr || !aStream->IsReady()) {
    return false;
  }
  if (!aStream->Write(pContents, pLength)) {
    return false;
  }
  return aStream->Close();
}

}  // namespace peanutbutter::ultima
