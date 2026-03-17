#include "IO/FileSystem.hpp"

#include <algorithm>

#include "PeanutButter.hpp"

namespace peanutbutter {

bool FileSystem::ReadFile(const std::string& pPath, ByteBuffer& pContents) const {
  pContents.Clear();
  std::unique_ptr<FileReadStream> aStream = OpenReadStream(pPath);
  if (aStream == nullptr || !aStream->IsReady()) {
    return false;
  }
  const std::size_t aLength = aStream->GetLength();
  if (!pContents.Resize(aLength)) {
    return false;
  }
  return aStream->Read(0, pContents.Data(), pContents.Size());
}

bool FileSystem::ReadTextFile(const std::string& pPath, std::string& pContents) const {
  std::unique_ptr<FileReadStream> aStream = OpenReadStream(pPath);
  if (aStream == nullptr || !aStream->IsReady()) {
    pContents.clear();
    return false;
  }

  pContents.clear();
  const std::size_t aLength = aStream->GetLength();
  pContents.reserve(aLength);

  BlockBuffer aBuffer;
  std::size_t aOffset = 0;
  while (aOffset < aLength) {
    const std::size_t aChunkLength = std::min(kBlockSizeL3, aLength - aOffset);
    if (!aStream->Read(aOffset, aBuffer.Data(), aChunkLength)) {
      pContents.clear();
      return false;
    }
    pContents.append(reinterpret_cast<const char*>(aBuffer.Data()), aChunkLength);
    aOffset += aChunkLength;
  }
  return true;
}

bool FileSystem::WriteFile(const std::string& pPath, const ByteBuffer& pContents) {
  return WriteFile(pPath, pContents.Data(), pContents.Size());
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
