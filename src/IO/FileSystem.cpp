#include "IO/FileSystem.hpp"

namespace peanutbutter {

bool FileSystem::ReadFile(const std::string& pPath, ByteVector& pContents) const {
  pContents.clear();
  std::unique_ptr<FileReadStream> aStream = OpenReadStream(pPath);
  if (aStream == nullptr || !aStream->IsReady()) {
    return false;
  }
  pContents.resize(aStream->GetLength());
  return aStream->Read(0, pContents.data(), pContents.size());
}

bool FileSystem::ReadTextFile(const std::string& pPath, std::string& pContents) const {
  ByteVector aBytes;
  if (!ReadFile(pPath, aBytes)) {
    pContents.clear();
    return false;
  }
  pContents.assign(reinterpret_cast<const char*>(aBytes.data()), aBytes.size());
  return true;
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

bool FileSystem::WriteTextFile(const std::string& pPath, const std::string& pContents) {
  return WriteFile(pPath,
                   reinterpret_cast<const unsigned char*>(pContents.data()),
                   pContents.size());
}

bool FileSystem::AppendTextFile(const std::string& pPath, const std::string& pContents) {
  return AppendFile(pPath,
                    reinterpret_cast<const unsigned char*>(pContents.data()),
                    pContents.size());
}

}  // namespace peanutbutter
