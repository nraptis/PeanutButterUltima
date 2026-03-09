#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "MockHardDrive.hpp"

namespace {

bool Expect(bool pCondition, const std::string& pMessage) {
  if (!pCondition) {
    std::cerr << pMessage << "\n";
    return false;
  }
  return true;
}

bool ExpectBytes(const std::vector<unsigned char>& pExpected,
                 const std::vector<unsigned char>& pActual,
                 const std::string& pLabel) {
  if (pExpected != pActual) {
    std::cerr << pLabel << " mismatch\n";
    return false;
  }
  return true;
}

bool ReadAllBytes(const peanutbutter::testing::MockHardDrive& pHardDrive,
                  const std::string& pPath,
                  std::vector<unsigned char>& pBytes) {
  pBytes.assign(pHardDrive.GetFileLength(pPath), 0);
  return pHardDrive.ReadFileBytes(pPath, 0, pBytes.data(), pBytes.size());
}

}  // namespace

int main() {
  using peanutbutter::testing::MockHardDrive;

  MockHardDrive aHardDrive;
  bool aPassed = true;

  const std::string aProjectPath = "/project";
  const std::string aInputPath = aHardDrive.JoinPath(aProjectPath, "input");
  const std::string aPresidentsPath = aHardDrive.JoinPath(aInputPath, "presidents");
  const std::string aLovePath = aHardDrive.JoinPath(aInputPath, "love.txt");
  const std::string aLincolnPath = aHardDrive.JoinPath(aPresidentsPath, "lincoln.txt");
  const std::string aWashingtonPath = aHardDrive.JoinPath(aPresidentsPath, "washington.txt");
  const std::string aZebraPath = aHardDrive.JoinPath(aInputPath, "zebra.txt");

  aPassed = Expect(aHardDrive.EnsureDirectory(aProjectPath), "EnsureDirectory should create /project.") && aPassed;
  aPassed = Expect(aHardDrive.EnsureDirectory(aInputPath), "EnsureDirectory should create /project/input.") && aPassed;
  aPassed = Expect(aHardDrive.EnsureDirectory(aPresidentsPath),
                   "EnsureDirectory should create /project/input/presidents.") && aPassed;

  aPassed = Expect(aHardDrive.HasDirectory(aProjectPath), "Project directory should exist.") && aPassed;
  aPassed = Expect(aHardDrive.HasDirectory(aInputPath), "Input directory should exist.") && aPassed;
  aPassed = Expect(aHardDrive.HasDirectory(aPresidentsPath), "Presidents directory should exist.") && aPassed;
  aPassed = Expect(aHardDrive.ParentPath(aLincolnPath) == aPresidentsPath,
                   "ParentPath should match presidents folder.") && aPassed;
  aPassed = Expect(aHardDrive.FileName(aLincolnPath) == "lincoln.txt",
                   "FileName should return trailing name.") && aPassed;
  aPassed = Expect(aHardDrive.StemName(aWashingtonPath) == "washington",
                   "StemName should strip extension.") && aPassed;

  aPassed = Expect(aHardDrive.ClearFileBytes(aLovePath), "ClearFileBytes should initialize love.txt.") && aPassed;
  aPassed = Expect(aHardDrive.AppendFileBytes(aLovePath,
                                              reinterpret_cast<const unsigned char*>("c"), 1),
                   "AppendFileBytes should write first love.txt chunk.") && aPassed;
  aPassed = Expect(aHardDrive.AppendFileBytes(aLovePath,
                                              reinterpret_cast<const unsigned char*>("pu"), 2),
                   "AppendFileBytes should write second love.txt chunk.") && aPassed;

  aPassed = Expect(aHardDrive.ClearFileBytes(aLincolnPath), "ClearFileBytes should initialize lincoln.txt.") && aPassed;
  aPassed = Expect(aHardDrive.AppendFileBytes(aLincolnPath,
                                              reinterpret_cast<const unsigned char*>("abra"), 4),
                   "AppendFileBytes should write first lincoln.txt chunk.") && aPassed;
  aPassed = Expect(aHardDrive.AppendFileBytes(aLincolnPath,
                                              reinterpret_cast<const unsigned char*>("ham"), 3),
                   "AppendFileBytes should write second lincoln.txt chunk.") && aPassed;

  aHardDrive.PutFile(aWashingtonPath, std::vector<unsigned char>{'g', 'e', 'o', 'r', 'g', 'e'});
  aHardDrive.PutFile(aZebraPath, std::vector<unsigned char>{'a', 't', 't', 'a', 'c', 'k', ' ', 'z', 'z', 'z'});

  aPassed = Expect(aHardDrive.HasFile(aLovePath), "love.txt should exist.") && aPassed;
  aPassed = Expect(aHardDrive.HasFile(aLincolnPath), "lincoln.txt should exist.") && aPassed;
  aPassed = Expect(aHardDrive.HasFile(aWashingtonPath), "washington.txt should exist.") && aPassed;
  aPassed = Expect(aHardDrive.HasFile(aZebraPath), "zebra.txt should exist.") && aPassed;
  aPassed = Expect(aHardDrive.GetFileLength(aLovePath) == 3, "love.txt length should be 3.") && aPassed;
  aPassed = Expect(aHardDrive.GetFileLength(aLincolnPath) == 7, "lincoln.txt length should be 7.") && aPassed;
  aPassed = Expect(aHardDrive.DirectoryHasEntries(aInputPath), "input directory should report entries.") && aPassed;
  aPassed = Expect(aHardDrive.DirectoryHasEntries(aPresidentsPath),
                   "presidents directory should report entries.") && aPassed;

  std::vector<std::string> aInputFiles = aHardDrive.ListFiles(aInputPath);
  std::sort(aInputFiles.begin(), aInputFiles.end());
  aPassed = Expect(aInputFiles == std::vector<std::string>{aLovePath, aZebraPath},
                   "ListFiles should return direct child files only.") && aPassed;

  std::vector<std::string> aAllFiles = aHardDrive.ListFilesRecursive(aInputPath);
  std::sort(aAllFiles.begin(), aAllFiles.end());
  aPassed = Expect(aAllFiles == std::vector<std::string>{aLovePath, aLincolnPath, aWashingtonPath, aZebraPath},
                   "ListFilesRecursive should return the full input tree.") && aPassed;

  std::vector<unsigned char> aLoveBytes;
  std::vector<unsigned char> aLincolnBytes;
  std::vector<unsigned char> aWashingtonBytes;
  std::vector<unsigned char> aZebraBytes;
  aPassed = Expect(ReadAllBytes(aHardDrive, aLovePath, aLoveBytes), "Should read love.txt bytes.") && aPassed;
  aPassed = Expect(ReadAllBytes(aHardDrive, aLincolnPath, aLincolnBytes), "Should read lincoln.txt bytes.") && aPassed;
  aPassed = Expect(ReadAllBytes(aHardDrive, aWashingtonPath, aWashingtonBytes),
                   "Should read washington.txt bytes.") && aPassed;
  aPassed = Expect(ReadAllBytes(aHardDrive, aZebraPath, aZebraBytes), "Should read zebra.txt bytes.") && aPassed;

  aPassed = ExpectBytes(std::vector<unsigned char>{'c', 'p', 'u'}, aLoveBytes, "love.txt bytes") && aPassed;
  aPassed = ExpectBytes(std::vector<unsigned char>{'a', 'b', 'r', 'a', 'h', 'a', 'm'}, aLincolnBytes,
                        "lincoln.txt bytes") && aPassed;
  aPassed = ExpectBytes(std::vector<unsigned char>{'g', 'e', 'o', 'r', 'g', 'e'}, aWashingtonBytes,
                        "washington.txt bytes") && aPassed;
  aPassed = ExpectBytes(std::vector<unsigned char>{'a', 't', 't', 'a', 'c', 'k', ' ', 'z', 'z', 'z'}, aZebraBytes,
                        "zebra.txt bytes") && aPassed;

  std::vector<unsigned char> aLincolnPartial(3, 0);
  aPassed = Expect(aHardDrive.ReadFileBytes(aLincolnPath, 4, aLincolnPartial.data(), aLincolnPartial.size()),
                   "Should support partial byte reads.") && aPassed;
  aPassed = ExpectBytes(std::vector<unsigned char>{'h', 'a', 'm'}, aLincolnPartial,
                        "Partial lincoln.txt bytes") && aPassed;

  if (!aPassed) {
    return 1;
  }

  std::cout << "Test_MockHardDrive_ArchiveFormatExample passed\n";
  return 0;
}
