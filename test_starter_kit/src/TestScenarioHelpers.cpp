#include "TestScenarioHelpers.hpp"

#include <algorithm>
#include <cstdint>
#include <string>

namespace peanutbutter {
namespace testkit {
namespace {

std::size_t PickNameLength(RandomTestFileNameType pType,
                           DeterministicRng& pRng) {
  switch (pType) {
    case RandomTestFileNameType::kInvalidSmall:
      return pRng.UniformSize(0u, 2u);
    case RandomTestFileNameType::kMinimum:
      return 1u;
    case RandomTestFileNameType::kSmall:
      return pRng.UniformSize(1u, 8u);
    case RandomTestFileNameType::kMedium:
      return pRng.UniformSize(9u, 64u);
    case RandomTestFileNameType::kLarge:
      return pRng.UniformSize(65u, 2048u);
    case RandomTestFileNameType::kInvalidLarge:
      return pRng.UniformSize(2049u, 2300u);
  }
  return 1u;
}

std::string BuildValidName(std::size_t pLength,
                           DeterministicRng& pRng) {
  static const char kAlphabet[] =
      "abcdefghijklmnopqrstuvwxyz"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "0123456789"
      "_-[]";

  if (pLength == 0u) {
    return std::string("a");
  }

  std::string aName;
  aName.resize(pLength, 'a');
  for (std::size_t aIndex = 0u; aIndex < pLength; ++aIndex) {
    const std::size_t aPick = pRng.UniformSize(0u, sizeof(kAlphabet) - 2u);
    aName[aIndex] = kAlphabet[aPick];
  }

  if (pLength > 6u && pRng.Chance(1u, 3u)) {
    const std::size_t aSlash = pRng.UniformSize(1u, pLength - 2u);
    aName[aSlash] = '/';
    if (aSlash > 0u && aName[aSlash - 1u] == '/') {
      aName[aSlash - 1u] = 'x';
    }
    if (aSlash + 1u < pLength && aName[aSlash + 1u] == '/') {
      aName[aSlash + 1u] = 'y';
    }
  }

  for (char& aChar : aName) {
    if (aChar == '\\') {
      aChar = '/';
    }
  }
  if (!aName.empty() && (aName.front() == '/' || aName.back() == '/')) {
    aName.front() = 'a';
    aName.back() = 'z';
  }
  return aName;
}

std::string BuildInvalidSmallName(DeterministicRng& pRng) {
  const std::size_t aCase = pRng.UniformSize(0u, 5u);
  switch (aCase) {
    case 0u:
      return std::string();
    case 1u:
      return std::string(".");
    case 2u:
      return std::string("..");
    case 3u:
      return std::string("/x");
    case 4u:
      return std::string("a/../b");
    default: {
      std::string aOut;
      aOut.push_back('a');
      aOut.push_back(static_cast<char>(1));
      aOut.push_back('b');
      return aOut;
    }
  }
}

std::size_t PickContentLength(RandomTestFileContentType pType,
                              DeterministicRng& pRng,
                              std::size_t pPayloadBytesPerBlock) {
  switch (pType) {
    case RandomTestFileContentType::kZero:
      return 0u;
    case RandomTestFileContentType::kSmall:
      return pRng.UniformSize(0u, 4u);
    case RandomTestFileContentType::kMedium:
      return pRng.UniformSize(5u, 64u);
    case RandomTestFileContentType::kLarge: {
      const std::size_t aMin = std::max<std::size_t>(1u, (3u * pPayloadBytesPerBlock + 1u) / 2u);
      const std::size_t aMax = std::max<std::size_t>(aMin, 6u * pPayloadBytesPerBlock);
      return pRng.UniformSize(aMin, aMax);
    }
    case RandomTestFileContentType::kGiant: {
      const std::size_t aMin = std::max<std::size_t>(1u, 12u * pPayloadBytesPerBlock);
      const std::size_t aMax = std::max<std::size_t>(aMin, 18u * pPayloadBytesPerBlock);
      return pRng.UniformSize(aMin, aMax);
    }
  }
  return 0u;
}

}  // namespace

TestFile GetRandomTestFile(RandomTestFileNameType pNameType,
                           RandomTestFileContentType pContentType,
                           DeterministicRng& pRng,
                           std::size_t pPayloadBytesPerBlock) {
  TestFile aOut;

  if (pNameType == RandomTestFileNameType::kInvalidSmall) {
    aOut.mRelativePath = BuildInvalidSmallName(pRng);
  } else if (pNameType == RandomTestFileNameType::kInvalidLarge) {
    aOut.mRelativePath = BuildValidName(PickNameLength(pNameType, pRng), pRng);
  } else {
    aOut.mRelativePath = BuildValidName(PickNameLength(pNameType, pRng), pRng);
  }

  aOut.mIsDirectory = false;
  const std::size_t aContentLength = PickContentLength(pContentType, pRng, pPayloadBytesPerBlock);
  aOut.mContentBytes.resize(aContentLength, 0u);
  pRng.FillBytes(aOut.mContentBytes);
  return aOut;
}

TestFile MakeDirectoryTestFile(const std::string& pRelativePath) {
  TestFile aOut;
  aOut.mRelativePath = pRelativePath;
  aOut.mIsDirectory = true;
  return aOut;
}

bool MaterializeTestFiles(const std::vector<TestFile>& pFiles,
                          const std::string& pRootDirectory,
                          FileSystem& pFileSystem,
                          std::vector<SourceEntry>& pOutSourceEntries) {
  pOutSourceEntries.clear();
  if (!pFileSystem.EnsureDirectory(pRootDirectory)) {
    return false;
  }

  for (const TestFile& aFile : pFiles) {
    SourceEntry aEntry;
    aEntry.mRelativePath = aFile.mRelativePath;
    aEntry.mIsDirectory = aFile.mIsDirectory;

    const std::string aOutputPath = pFileSystem.JoinPath(pRootDirectory, aFile.mRelativePath);
    if (aFile.mIsDirectory) {
      if (!pFileSystem.EnsureDirectory(aOutputPath)) {
        return false;
      }
      aEntry.mSourcePath.clear();
      aEntry.mFileLength = 0u;
    } else {
      const std::string aParent = pFileSystem.ParentPath(aOutputPath);
      if (!aParent.empty() && !pFileSystem.EnsureDirectory(aParent)) {
        return false;
      }
      if (!pFileSystem.WriteFile(aOutputPath, aFile.mContentBytes.data(), aFile.mContentBytes.size())) {
        return false;
      }
      aEntry.mSourcePath = aOutputPath;
      aEntry.mFileLength = static_cast<std::uint64_t>(aFile.mContentBytes.size());
    }

    pOutSourceEntries.push_back(aEntry);
  }

  std::sort(pOutSourceEntries.begin(),
            pOutSourceEntries.end(),
            [](const SourceEntry& pLeft, const SourceEntry& pRight) {
              return pLeft.mRelativePath < pRight.mRelativePath;
            });

  return true;
}

}  // namespace testkit
}  // namespace peanutbutter
