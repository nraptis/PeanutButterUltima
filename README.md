# PeanutButterUltima Archiver API and File Format

This is the one README.

It documents the checked-in archiver API, the request/response types, the progress phases, the bundle/unbundle/recover flows, and the on-disk archive format.

It intentionally does not explain encryption internals. When this README says "seal" or "unseal", treat that as an opaque crypt step.

## What This Library Does

The core archiver API exposes five main operations:

- `DiscoverBundlePlan(...)`
- `PerformBundleFlight(...)`
- `Bundle(...)`
- `Unbundle(...)`
- `Recover(...)`

At a high level:

- `Bundle` turns a caller-supplied list of source entries into one archive family made of fixed-size archive files.
- `Unbundle` strictly decodes an archive family and stops on the first hard problem.
- `Recover` decodes the same format in best-effort mode and skips damage when it can.

The current GUI default archive suffix is `.PBTR`, but the core API accepts any suffix.

## Include Surface

The main public headers for the archiver path are:

```cpp
#include "AppShell_Bundle.hpp"
#include "AppShell_Unbundle.hpp"
#include "AppShell_Types.hpp"
#include "AppShell_Common.hpp"
#include "AppShell_ArchiveFormat.hpp"
#include "IO/LocalFileSystem.hpp"
```

## Quick Start

### Bundle in One Call

```cpp
#include "AppShell_Bundle.hpp"
#include "AppShell_Types.hpp"
#include "IO/LocalFileSystem.hpp"

using namespace peanutbutter;

static SourceEntry MakeFileEntry(const std::string& path,
                                 const std::string& relativePath) {
  SourceEntry entry;
  entry.mSourcePath = path;
  entry.mRelativePath = relativePath;
  entry.mIsDirectory = false;
  return entry;
}

static SourceEntry MakeEmptyDirectoryEntry(const std::string& relativePath) {
  SourceEntry entry;
  entry.mSourcePath.clear();
  entry.mRelativePath = relativePath;
  entry.mIsDirectory = true;
  return entry;
}

int main() {
  LocalFileSystem fs;
  CapturingLogger logger;

  BundleRequest request;
  request.mDestinationDirectory = "/tmp/out_archives";
  request.mSourceStem = "bundle_input";
  request.mArchivePrefix = "";
  request.mArchiveSuffix = ".PBTR";
  request.mArchiveBlockCount = 1;
  request.mUseEncryption = false;

  std::vector<SourceEntry> entries = {
      MakeFileEntry("/tmp/input/hello.txt", "hello.txt"),
      MakeEmptyDirectoryEntry("empty_dir"),
  };

  OperationResult result = Bundle(request, entries, fs, logger, nullptr);
  return result.mSucceeded ? 0 : 1;
}
```

### Unbundle or Recover in One Call

```cpp
#include "AppShell_Unbundle.hpp"
#include "AppShell_Types.hpp"
#include "IO/LocalFileSystem.hpp"

using namespace peanutbutter;

int main() {
  LocalFileSystem fs;
  CapturingLogger logger;

  UnbundleRequest request;
  request.mDestinationDirectory = "/tmp/unbundled";
  request.mUseEncryption = false;

  std::vector<std::string> archiveFiles = {
      "/tmp/out_archives/bundle_input_0.PBTR",
  };

  OperationResult strictResult =
      Unbundle(request, archiveFiles, fs, logger, nullptr);

  OperationResult recoverResult =
      Recover(request, archiveFiles, fs, logger, nullptr);

  return strictResult.mSucceeded || recoverResult.mSucceeded ? 0 : 1;
}
```

### Two-Phase Bundle

Use the two-step flow if you want to inspect the plan before writing files:

```cpp
BundleDiscovery discovery;
OperationResult planned =
    DiscoverBundlePlan(request, entries, fs, logger, discovery, nullptr);
if (!planned.mSucceeded) {
  return 1;
}

OperationResult written =
    PerformBundleFlight(request, discovery, fs, logger, nullptr);
```

## How To Prepare Inputs

The bundle API does not walk the filesystem for you. You provide `std::vector<SourceEntry>`.

Recommended caller behavior:

- If the source is a single file, add one file entry whose `mRelativePath` is the filename.
- If the source is a directory, add one file entry for every file under it, recursively.
- Add directory entries only for empty directories.
- Use relative paths only.
- Do not include `.` or `..` segments.

The GUI's own collector follows this shape:

1. `ListFilesRecursive(root)` and add those as file entries.
2. `ListDirectoriesRecursive(root)` and add only directories for which `DirectoryHasEntries(path)` is false.
3. Sort by relative path.

For decode:

- `pArchiveFileList` may be one file or many candidate files.
- The decoder will sort them, parse trailing digits before the extension as the archive index, choose a filename-template family, and then choose one archive family from that candidate set.

## API Reference

### Functions

#### `OperationResult DiscoverBundlePlan(...)`

```cpp
OperationResult DiscoverBundlePlan(const BundleRequest& request,
                                   const std::vector<SourceEntry>& sourceEntries,
                                   FileSystem& fileSystem,
                                   Logger& logger,
                                   BundleDiscovery& outDiscovery,
                                   CancelCoordinator* cancelCoordinator = nullptr);
```

What it does:

- Validates the bundle request.
- Sorts and filters the supplied source entries.
- Recomputes file lengths from the filesystem.
- Computes logical record offsets.
- Computes archive count and per-archive plan data.
- Produces `BundleDiscovery`.

#### `OperationResult PerformBundleFlight(...)`

```cpp
OperationResult PerformBundleFlight(const BundleRequest& request,
                                    const BundleDiscovery& discovery,
                                    FileSystem& fileSystem,
                                    Logger& logger,
                                    CancelCoordinator* cancelCoordinator = nullptr);
```

What it does:

- Creates the destination directory if needed.
- Creates archive files.
- Writes archive headers, block recovery headers, payloads, and final status flags.

#### `OperationResult Bundle(...)`

```cpp
OperationResult Bundle(const BundleRequest& request,
                       const std::vector<SourceEntry>& sourceEntries,
                       FileSystem& fileSystem,
                       Logger& logger,
                       CancelCoordinator* cancelCoordinator = nullptr);
```

What it does:

- Runs `DiscoverBundlePlan(...)`.
- If planning succeeds, runs `PerformBundleFlight(...)`.

#### `OperationResult Unbundle(...)`

```cpp
OperationResult Unbundle(const UnbundleRequest& request,
                         const std::vector<std::string>& archiveFileList,
                         FileSystem& fileSystem,
                         Logger& logger,
                         CancelCoordinator* cancelCoordinator = nullptr);
```

What it does:

- Strictly decodes the selected archive family.
- Stops on the first gap, short file, crypt failure, checksum failure, or parse failure.

#### `OperationResult Recover(...)`

```cpp
OperationResult Recover(const UnbundleRequest& request,
                        const std::vector<std::string>& archiveFileList,
                        FileSystem& fileSystem,
                        Logger& logger,
                        CancelCoordinator* cancelCoordinator = nullptr);
```

What it does:

- Uses the same discovery pipeline and same file format as `Unbundle(...)`.
- Best-effort decodes and skips damaged regions when possible.

### Request and Result Types

#### `OperationResult`

| Field | Type | Meaning |
| --- | --- | --- |
| `mSucceeded` | `bool` | `true` only on success. |
| `mCanceled` | `bool` | `true` only when the operation ended as a user cancel. |
| `mErrorCode` | `ErrorCode` | High-level result code. |
| `mFailureMessage` | `std::string` | Human-readable failure detail. |

#### `BundleRequest`

| Field | Type | Meaning |
| --- | --- | --- |
| `mDestinationDirectory` | `std::string` | Folder that will receive the archive files. Required. |
| `mSourceStem` | `std::string` | Base filename stem used in generated archive names. Default: `"archive_data"`. |
| `mArchivePrefix` | `std::string` | Prepended before `mSourceStem` when naming archive files. |
| `mArchiveSuffix` | `std::string` | Filename suffix or extension. If it has no leading dot, the writer adds one. |
| `mPasswordOne` | `std::string` | First password string, only meaningful when encryption is enabled. |
| `mPasswordTwo` | `std::string` | Second password string, only meaningful when encryption is enabled. |
| `mArchiveBlockCount` | `std::uint32_t` | Number of L3 blocks per archive file. Must be `1..2048`. |
| `mUseEncryption` | `bool` | If `true`, the writer asks `mCryptGenerator` to create a crypt engine. |
| `mEncryptionStrength` | `EncryptionStrength` | Strength enum stored into the archive header and passed into the crypt generator. |
| `mCryptGenerator` | `CryptGenerator` | Factory for the opaque crypt engine. Required only when `mUseEncryption == true`. |

Important notes:

- One archive file size is `48 + (mArchiveBlockCount * 1000704)` bytes.
- The current writer mirrors `mEncryptionStrength` into the archive header's `mExpansionStrength`.

#### `UnbundleRequest`

| Field | Type | Meaning |
| --- | --- | --- |
| `mDestinationDirectory` | `std::string` | Folder that will receive recovered or unbundled output. Required. |
| `mPasswordOne` | `std::string` | First password string, only meaningful when encryption is enabled. |
| `mPasswordTwo` | `std::string` | Second password string, only meaningful when encryption is enabled. |
| `mUseEncryption` | `bool` | If `true`, decode creates a crypt engine and unseals each block. |
| `mRecoverMode` | `bool` | Present on the struct, but the public mode selector is the function you call: `Unbundle(...)` vs `Recover(...)`. |
| `mCryptGenerator` | `CryptGenerator` | Factory for the opaque crypt engine. Required only when `mUseEncryption == true`. |

#### `SourceEntry`

| Field | Type | Meaning |
| --- | --- | --- |
| `mSourcePath` | `std::string` | Source file path for file records. Empty for empty-directory records. |
| `mRelativePath` | `std::string` | Stored path inside the archive. Required. |
| `mIsDirectory` | `bool` | `true` only for empty-directory records. |
| `mFileLength` | `std::uint64_t` | Ignored as input for file records during discovery; the library recomputes it from the filesystem. |

#### `BundleArchivePlan`

| Field | Type | Meaning |
| --- | --- | --- |
| `mArchiveOrdinal` | `std::size_t` | Zero-based ordinal in the planned output list. |
| `mArchiveIndex` | `std::uint32_t` | Numeric archive index stored in the archive header and filename. |
| `mBlockCount` | `std::uint32_t` | L3 blocks in this archive file. |
| `mPayloadBytes` | `std::uint32_t` | Serialized block-region length after the archive header. Current value: `mBlockCount * 1000704`. |
| `mRecordCountMod256` | `std::uint8_t` | Count of non-terminator records whose starts land inside this archive's logical payload window, modulo 256. |
| `mFolderCountMod256` | `std::uint8_t` | Count of empty-directory records whose starts land inside this archive's logical payload window, modulo 256. |
| `mArchivePath` | `std::string` | Full output path of the archive file. |

#### `BundleDiscovery`

| Field | Type | Meaning |
| --- | --- | --- |
| `mResolvedEntries` | `std::vector<SourceEntry>` | Final sorted source entries that survived discovery. |
| `mArchives` | `std::vector<BundleArchivePlan>` | Planned archive files. |
| `mRecordStartLogicalOffsets` | `std::vector<std::uint64_t>` | Logical record start offsets. The vector also includes the terminator start offset as the last element. |
| `mArchiveFamilyId` | `std::uint64_t` | Family identifier stored into every archive header in the family. |
| `mTotalLogicalBytes` | `std::uint64_t` | Logical record stream length including the terminator field. |
| `mTotalFileBytes` | `std::uint64_t` | Sum of file-content bytes, excluding record metadata. |
| `mFileCount` | `std::size_t` | Number of file entries. |
| `mFolderCount` | `std::size_t` | Number of empty-directory entries. |

### Supporting Interfaces

#### `Logger`

```cpp
class Logger {
 public:
  virtual ~Logger() = default;
  virtual void LogStatus(const std::string& message) = 0;
  virtual void LogError(const std::string& message) = 0;
  virtual void LogProgress(const ProgressInfo&) {}
};
```

Use:

- `NullLogger` if you do not care about logs.
- `CapturingLogger` if you want to inspect status, error, and progress events after the call.

#### `ProgressInfo`

| Field | Type | Meaning |
| --- | --- | --- |
| `mModeName` | `std::string` | `"Bundle"`, `"Unbundle"`, or `"Recover"`. |
| `mPhase` | `ProgressPhase` | Current high-level phase. |
| `mOverallFraction` | `double` | Overall progress in the range `0.0..1.0`. |
| `mDetail` | `std::string` | Human-readable phase detail. |

#### `FileSystem`

The archiver API is filesystem-abstracted. The main calls used by the bundle/decode path are:

- `Exists`
- `IsDirectory`
- `IsFile`
- `EnsureDirectory`
- `DirectoryHasEntries`
- `ListFilesRecursive`
- `ListDirectoriesRecursive`
- `ListFiles`
- `OpenReadStream`
- `OpenWriteStream`
- `OverwriteFileRegion`
- `JoinPath`
- `ParentPath`
- `FileName`
- `StemName`
- `Extension`

`LocalFileSystem` is the concrete implementation shipped in this repo.

#### `CryptGenerator`

```cpp
using CryptGenerator =
    std::function<std::unique_ptr<Crypt>(const CryptGeneratorRequest& request,
                                         std::string* errorMessage)>;
```

You only need this when encryption is enabled.

What the archiver expects from it:

- bundle calls `SealData(...)`
- decode calls `UnsealData(...)`
- the generator receives passwords, strength, recover-mode flag, and progress/status callbacks

No more encryption details belong in this README.

### Error Codes

| Value | Name | Meaning |
| --- | --- | --- |
| `0` | `NONE` | Success. |
| `1` | `CANCELED` | User cancellation. |
| `2` | `INVALID_REQUEST` | Bad top-level input. |
| `3` | `FILE_SYSTEM` | Read/write/create/open failure. |
| `4` | `CRYPT` | Crypt setup or seal/unseal failure. |
| `5` | `ARCHIVE_HEADER` | Archive-header or recovery-header issue, or strict decode exhausted space before the terminator. |
| `6` | `GAP_001` | Strict decode encountered a missing archive slot in the selected dense window. |
| `7` | `BLOCK_CHECKSUM` | Strict decode found a checksum mismatch in one L3 block. |
| `8` | `RECORD_PARSE` | Strict decode could not parse or materialize the logical record stream. |
| `9` | `RECOVER_EXHAUSTED` | Recover ran out of archive span without reaching the terminator and without recovering any files. |
| `255` | `INTERNAL` | Internal invariant or format-math failure. |

## Phases

Progress is reported in six named phases:

### `Preflight`

Very early validation and setup.

Bundle:

- reports bundle preflight complete before discovery starts

Unbundle / Recover:

- validates the destination directory
- creates the destination directory if needed

### `Discovery`

The library figures out what it is working with.

Bundle:

- validates `BundleRequest`
- sorts and filters source entries
- computes file lengths
- computes logical record offsets
- computes archive count and archive paths

Unbundle / Recover:

- scans archive candidates
- parses filename templates
- reads headers when possible
- chooses one archive family
- builds a dense archive-index window with empty boxes for missing slots

### `Expansion`

Opaque crypt-generator setup stage.

- Used only when encryption is enabled.
- Reported as immediately complete when encryption is disabled.

### `LayerCake`

Second opaque crypt-generator setup stage.

- Used only when encryption is enabled.
- Reported as immediately complete when encryption is disabled.

### `Flight`

The main data path.

Bundle:

- writes archive files, block headers, and block payloads

Unbundle:

- reads archive files, unseals blocks, verifies checksums, parses records, and writes output

Recover:

- same as unbundle, but tolerates more per-block failures and can jump by recovery stride after a parse failure

### `Finalizing`

Bundle only.

- rewrites byte `15` of every written archive header
- converts the archive from initial `DirtyType::kInvalid` to a final status:
  - finished
  - finished with error
  - finished with cancel
  - finished with cancel and error

Unbundle and recover do not use a finalizing phase.

## Bundle, Unbundle, Recover in One Page

### Bundle

Bundle turns caller-provided source entries into one archive family.

Rules that matter:

- source entries are sorted by `mRelativePath`
- file records store path + content length + file bytes
- empty directories store path + directory marker
- non-empty directories are not stored as standalone records
- a terminator record ends the logical stream
- every archive file has the same configured L3 block count
- every block has its own recovery header

### Unbundle

Unbundle is the strict path.

It fails immediately on the first:

- missing archive slot in the chosen dense window
- short archive file
- block read failure
- unseal failure
- recovery-header issue
- checksum mismatch
- logical-record parse failure
- exhausting the selected archive span before seeing the terminator

### Recover

Recover is the best-effort path.

It will keep going past:

- missing archive slots
- unreadable archive files
- short archive files
- unseal failures
- checksum failures
- record-parse failures

On parse failure in an otherwise readable block, recover:

1. aborts any partial output file
2. resets the parser
3. tries to honor the block's recovery stride
4. falls back to the next block if the jump is invalid

Recover succeeds when either of these is true:

- it reaches the terminator
- it runs out of archive span after recovering at least one file

## Format Constants

| Constant | Value |
| --- | --- |
| magic header | `0xDECAFBAD` |
| footer magic constant in code | `0xF01DAB1E` |
| version | `1.7` |
| archive header length | `48` bytes |
| recovery header length | `48` bytes |
| L1 block size | `250176` bytes |
| L2 block size | `500352` bytes |
| L3 block size | `1000704` bytes |
| payload bytes per L3 block | `1000656` bytes |
| max stored path length | `2048` bytes |
| max archive count | `65535` |
| max blocks per archive | `2048` |

Important notes:

- All integer fields are little-endian.
- The footer magic constant is not written into archive files.
- `mPayloadLength` in the archive header is the serialized L3-block region after the archive header, not the logical record-stream length.

## Archive Layout

One archive file looks like this:

```text
+--------------------------------------------------------------+
| Archive Header (48)                                          |
+--------------------------------------------------------------+
| L3 Block 0 (1000704)                                         |
|  +--------------------------------------------------------+  |
|  | Recovery Header (48)                                   |  |
|  +--------------------------------------------------------+  |
|  | Payload (1000656)                                      |  |
|  +--------------------------------------------------------+  |
+--------------------------------------------------------------+
| L3 Block 1 (1000704)                                       |
|  +--------------------------------------------------------+  |
|  | Recovery Header (48)                                   |  |
|  +--------------------------------------------------------+  |
|  | Payload (1000656)                                      |  |
|  +--------------------------------------------------------+  |
+--------------------------------------------------------------+
| ...                                                          |
+--------------------------------------------------------------+
```

## Archive Header

Byte offsets `0..47`:

| Offset | Size | Field | Meaning |
| --- | --- | --- | --- |
| `0` | `4` | `mMagic` | Must be `0xDECAFBAD`. |
| `4` | `2` | `mVersionMajor` | Must be `1`. |
| `6` | `2` | `mVersionMinor` | Must be `7`. |
| `8` | `1` | `mArchiverVersion` | Archiver recipe/version tag. |
| `9` | `1` | `mPasswordExpanderVersion` | Password-expander recipe/version tag. |
| `10` | `1` | `mCipherStackVersion` | Cipher recipe/version tag. |
| `11` | `1` | `mEncryptionStrength` | `1 = high`, `2 = medium`, `3 = low`. |
| `12` | `1` | `mExpansionStrength` | `1 = high`, `2 = medium`, `3 = low`. |
| `13` | `1` | `mRecordCountMod256` | Record starts in this archive's logical window, modulo 256. |
| `14` | `1` | `mFolderCountMod256` | Empty-directory record starts in this archive's logical window, modulo 256. |
| `15` | `1` | `mDirtyType` | Finalization status byte. |
| `16` | `4` | `mArchiveIndex` | Zero-based archive index. |
| `20` | `4` | `mArchiveCount` | Total files in the archive family. |
| `24` | `4` | `mPayloadLength` | Serialized block-region length after the archive header. |
| `28` | `4` | `mReserved32` | Currently zero. |
| `32` | `8` | `mReservedB` | Currently zero. |
| `40` | `8` | `mArchiveFamilyId` | Family identifier used during decode-family selection. |

`DirtyType` byte values:

| Value | Name |
| --- | --- |
| `0` | `kInvalid` |
| `1` | `kFinishedWithCancel` |
| `2` | `kFinishedWithError` |
| `3` | `kFinishedWithCancelAndError` |
| `4` | `kFinished` |

## Recovery Header

Byte offsets `0..47` within each L3 block:

| Offset | Size | Field | Meaning |
| --- | --- | --- | --- |
| `0` | `8` | `checksum.word1` | Integrity word 1 |
| `8` | `8` | `checksum.word2` | Integrity word 2 |
| `16` | `8` | `checksum.word3` | Integrity word 3 |
| `24` | `8` | `checksum.word4` | Integrity word 4 |
| `32` | `8` | `checksum.word5` | Integrity word 5 |
| `40` | `2` | `skip.archive_distance` | Forward archive delta |
| `42` | `2` | `skip.block_distance` | Forward block delta |
| `44` | `4` | `skip.byte_distance` | Forward payload-byte delta |

What it means:

- the checksum covers the plain payload bytes and the 8-byte skip record
- the checksum does not cover the recovery-header bytes themselves
- the payload region is zero-filled before record bytes are written, so zero padding is protected too

What the skip record means:

- it points forward from the current block's logical payload start to the next record start known at bundle time
- recover uses it only after a parse failure in an otherwise readable block
- a zero skip is not a valid jump target

Special first-block rule:

- on archive `0`, block `0`, the writer stores the first non-zero record start if one exists
- this makes the first block's skip useful for recover even when the first record starts at logical offset `0`

## Logical Record Stream

Records are streamed back-to-back across block payload regions.

Every record is:

1. `u16 path_length`
2. `path_length` raw path bytes
3. `u64 content_length`
4. `content_length` raw file bytes for file records

Special values:

- `path_length == 0`
  - terminator record
  - ends the logical stream
- `content_length == 0xFFFFFFFFFFFFFFFF`
  - empty-directory record
  - no content bytes follow

Decode path rules:

- paths must be relative
- paths must not contain `.` segments
- paths must not contain `..` segments
- paths must not contain control bytes
- paths must be `<= 2048` bytes

Directory behavior:

- only empty directories get explicit directory records
- non-empty directories are rebuilt from file paths when files are written out

## Archive Naming and Selection

Writer naming rule:

```text
archive_file_name =
    archive_prefix + source_stem + "_" + zero_padded_index + archive_suffix
```

Examples:

- `bundle_0.PBTR`
- `bundle_01.PBTR`
- `prefix_bundle_002.pbtr`

Parser rule during decode:

- the trailing digit run before the extension is the archive index
- the parser does not require an underscore before those digits

Family selection behavior:

1. sort all candidate paths
2. parse the first usable filename as the template anchor
3. keep candidates with the same parsed prefix and suffix
4. read headers when possible
5. in strict mode, prefer the dominant readable `mArchiveFamilyId`
6. build a dense index window with empty slots for gaps

## Worked Example Rules

The byte-labeled examples below follow the real field order and real field sizes.

To keep them readable:

- family ids are fixed illustrative values
- checksum words are fixed illustrative values
- encryption is omitted entirely
- zero padding is shown as a byte range, not as one million individual lines

Everything else in the examples is literal:

- little-endian ordering
- field boundaries
- absolute file offsets
- path bytes
- content-length bytes
- terminator bytes

## Worked Example A: One Archive, One Block

Scenario:

- archive file: `demo_one_0.PBTR`
- archive count: `1`
- blocks per archive: `1`
- file record: path `"a"`, content `"A"`
- empty-directory record: path `"d"`
- terminator record
- illustrative family id: `0x8877665544332211`
- illustrative checksum words: `0x1111111111111111`, `0x2222222222222222`, `0x3333333333333333`, `0x4444444444444444`, `0x5555555555555555`

Logical payload used:

- file record: `12` bytes
- empty-directory record: `11` bytes
- terminator: `2` bytes
- total used payload bytes: `25`

Archive size:

- archive header: `48`
- one L3 block: `1000704`
- total file size: `1000752`

Absolute file layout:

```text
0        .. 47       Archive Header
48       .. 95       Recovery Header for Block 0
96       .. 120      Logical payload bytes actually used
121      .. 1000751  Zero padding
```

### Example A Archive Header Bytes

| File Offset | Hex | ASCII | Meaning |
| --- | --- | --- | --- |
| `0` | `AD` | `.` | `mMagic` byte 0 |
| `1` | `FB` | `.` | `mMagic` byte 1 |
| `2` | `CA` | `.` | `mMagic` byte 2 |
| `3` | `DE` | `.` | `mMagic` byte 3 |
| `4` | `01` | `.` | `mVersionMajor` byte 0 |
| `5` | `00` | `.` | `mVersionMajor` byte 1 |
| `6` | `07` | `.` | `mVersionMinor` byte 0 |
| `7` | `00` | `.` | `mVersionMinor` byte 1 |
| `8` | `01` | `.` | `mArchiverVersion` |
| `9` | `01` | `.` | `mPasswordExpanderVersion` |
| `10` | `01` | `.` | `mCipherStackVersion` |
| `11` | `01` | `.` | `mEncryptionStrength = high` |
| `12` | `01` | `.` | `mExpansionStrength = high` |
| `13` | `02` | `.` | `mRecordCountMod256 = 2` |
| `14` | `01` | `.` | `mFolderCountMod256 = 1` |
| `15` | `04` | `.` | `mDirtyType = kFinished` |
| `16` | `00` | `.` | `mArchiveIndex` byte 0 |
| `17` | `00` | `.` | `mArchiveIndex` byte 1 |
| `18` | `00` | `.` | `mArchiveIndex` byte 2 |
| `19` | `00` | `.` | `mArchiveIndex` byte 3 |
| `20` | `01` | `.` | `mArchiveCount` byte 0 |
| `21` | `00` | `.` | `mArchiveCount` byte 1 |
| `22` | `00` | `.` | `mArchiveCount` byte 2 |
| `23` | `00` | `.` | `mArchiveCount` byte 3 |
| `24` | `00` | `.` | `mPayloadLength` byte 0 |
| `25` | `45` | `E` | `mPayloadLength` byte 1 |
| `26` | `0F` | `.` | `mPayloadLength` byte 2 |
| `27` | `00` | `.` | `mPayloadLength` byte 3 |
| `28` | `00` | `.` | `mReserved32` byte 0 |
| `29` | `00` | `.` | `mReserved32` byte 1 |
| `30` | `00` | `.` | `mReserved32` byte 2 |
| `31` | `00` | `.` | `mReserved32` byte 3 |
| `32` | `00` | `.` | `mReservedB` byte 0 |
| `33` | `00` | `.` | `mReservedB` byte 1 |
| `34` | `00` | `.` | `mReservedB` byte 2 |
| `35` | `00` | `.` | `mReservedB` byte 3 |
| `36` | `00` | `.` | `mReservedB` byte 4 |
| `37` | `00` | `.` | `mReservedB` byte 5 |
| `38` | `00` | `.` | `mReservedB` byte 6 |
| `39` | `00` | `.` | `mReservedB` byte 7 |
| `40` | `11` | `.` | `mArchiveFamilyId` byte 0 |
| `41` | `22` | `"` | `mArchiveFamilyId` byte 1 |
| `42` | `33` | `3` | `mArchiveFamilyId` byte 2 |
| `43` | `44` | `D` | `mArchiveFamilyId` byte 3 |
| `44` | `55` | `U` | `mArchiveFamilyId` byte 4 |
| `45` | `66` | `f` | `mArchiveFamilyId` byte 5 |
| `46` | `77` | `w` | `mArchiveFamilyId` byte 6 |
| `47` | `88` | `.` | `mArchiveFamilyId` byte 7 |

### Example A Block 0 Recovery Header Bytes

| File Offset | Hex | ASCII | Meaning |
| --- | --- | --- | --- |
| `48` | `11` | `.` | `checksum.word1` byte 0 |
| `49` | `11` | `.` | `checksum.word1` byte 1 |
| `50` | `11` | `.` | `checksum.word1` byte 2 |
| `51` | `11` | `.` | `checksum.word1` byte 3 |
| `52` | `11` | `.` | `checksum.word1` byte 4 |
| `53` | `11` | `.` | `checksum.word1` byte 5 |
| `54` | `11` | `.` | `checksum.word1` byte 6 |
| `55` | `11` | `.` | `checksum.word1` byte 7 |
| `56` | `22` | `"` | `checksum.word2` byte 0 |
| `57` | `22` | `"` | `checksum.word2` byte 1 |
| `58` | `22` | `"` | `checksum.word2` byte 2 |
| `59` | `22` | `"` | `checksum.word2` byte 3 |
| `60` | `22` | `"` | `checksum.word2` byte 4 |
| `61` | `22` | `"` | `checksum.word2` byte 5 |
| `62` | `22` | `"` | `checksum.word2` byte 6 |
| `63` | `22` | `"` | `checksum.word2` byte 7 |
| `64` | `33` | `3` | `checksum.word3` byte 0 |
| `65` | `33` | `3` | `checksum.word3` byte 1 |
| `66` | `33` | `3` | `checksum.word3` byte 2 |
| `67` | `33` | `3` | `checksum.word3` byte 3 |
| `68` | `33` | `3` | `checksum.word3` byte 4 |
| `69` | `33` | `3` | `checksum.word3` byte 5 |
| `70` | `33` | `3` | `checksum.word3` byte 6 |
| `71` | `33` | `3` | `checksum.word3` byte 7 |
| `72` | `44` | `D` | `checksum.word4` byte 0 |
| `73` | `44` | `D` | `checksum.word4` byte 1 |
| `74` | `44` | `D` | `checksum.word4` byte 2 |
| `75` | `44` | `D` | `checksum.word4` byte 3 |
| `76` | `44` | `D` | `checksum.word4` byte 4 |
| `77` | `44` | `D` | `checksum.word4` byte 5 |
| `78` | `44` | `D` | `checksum.word4` byte 6 |
| `79` | `44` | `D` | `checksum.word4` byte 7 |
| `80` | `55` | `U` | `checksum.word5` byte 0 |
| `81` | `55` | `U` | `checksum.word5` byte 1 |
| `82` | `55` | `U` | `checksum.word5` byte 2 |
| `83` | `55` | `U` | `checksum.word5` byte 3 |
| `84` | `55` | `U` | `checksum.word5` byte 4 |
| `85` | `55` | `U` | `checksum.word5` byte 5 |
| `86` | `55` | `U` | `checksum.word5` byte 6 |
| `87` | `55` | `U` | `checksum.word5` byte 7 |
| `88` | `00` | `.` | `skip.archive_distance` byte 0 |
| `89` | `00` | `.` | `skip.archive_distance` byte 1 |
| `90` | `00` | `.` | `skip.block_distance` byte 0 |
| `91` | `00` | `.` | `skip.block_distance` byte 1 |
| `92` | `00` | `.` | `skip.byte_distance` byte 0 |
| `93` | `00` | `.` | `skip.byte_distance` byte 1 |
| `94` | `00` | `.` | `skip.byte_distance` byte 2 |
| `95` | `00` | `.` | `skip.byte_distance` byte 3 |

### Example A Block 0 Payload Bytes

| File Offset | Hex | ASCII | Meaning |
| --- | --- | --- | --- |
| `96` | `01` | `.` | file record: `path_length` byte 0 |
| `97` | `00` | `.` | file record: `path_length` byte 1 |
| `98` | `61` | `a` | file record: path byte 0 (`"a"`) |
| `99` | `01` | `.` | file record: `content_length` byte 0 |
| `100` | `00` | `.` | file record: `content_length` byte 1 |
| `101` | `00` | `.` | file record: `content_length` byte 2 |
| `102` | `00` | `.` | file record: `content_length` byte 3 |
| `103` | `00` | `.` | file record: `content_length` byte 4 |
| `104` | `00` | `.` | file record: `content_length` byte 5 |
| `105` | `00` | `.` | file record: `content_length` byte 6 |
| `106` | `00` | `.` | file record: `content_length` byte 7 |
| `107` | `41` | `A` | file record: content byte 0 |
| `108` | `01` | `.` | empty-dir record: `path_length` byte 0 |
| `109` | `00` | `.` | empty-dir record: `path_length` byte 1 |
| `110` | `64` | `d` | empty-dir record: path byte 0 (`"d"`) |
| `111` | `FF` | `.` | empty-dir record: marker byte 0 |
| `112` | `FF` | `.` | empty-dir record: marker byte 1 |
| `113` | `FF` | `.` | empty-dir record: marker byte 2 |
| `114` | `FF` | `.` | empty-dir record: marker byte 3 |
| `115` | `FF` | `.` | empty-dir record: marker byte 4 |
| `116` | `FF` | `.` | empty-dir record: marker byte 5 |
| `117` | `FF` | `.` | empty-dir record: marker byte 6 |
| `118` | `FF` | `.` | empty-dir record: marker byte 7 |
| `119` | `00` | `.` | terminator: `path_length` byte 0 |
| `120` | `00` | `.` | terminator: `path_length` byte 1 |
| `121 .. 1000751` | `00 ... 00` | `.` | zero padding to the end of the L3 block |

## Worked Example B: One Archive, Two Blocks

Scenario:

- archive file: `demo_two_0.PBTR`
- archive count: `1`
- blocks per archive: `2`
- file record 1: path `"a"`, content `"X"`
- file record 2: path `"b"`, content `"Y"`
- terminator record
- illustrative family id: `0x0102030405060708`
- illustrative checksum words:
  - block 0: `0x1010101010101010`, `0x2020202020202020`, `0x3030303030303030`, `0x4040404040404040`, `0x5050505050505050`
  - block 1: `0xABABABABABABABAB`, `0xCDCDCDCDCDCDCDCD`, `0xEFEFEFEFEFEFEFEF`, `0x1212121212121212`, `0x3434343434343434`

Logical payload used:

- file record 1: `12` bytes
- file record 2: `12` bytes
- terminator: `2` bytes
- total used payload bytes: `26`

Archive size:

- archive header: `48`
- two L3 blocks: `2001408`
- total file size: `2001456`

Absolute file layout:

```text
0          .. 47        Archive Header
48         .. 95        Block 0 Recovery Header
96         .. 121       Block 0 Used Payload
122        .. 1000751   Block 0 Zero Padding
1000752    .. 1000799   Block 1 Recovery Header
1000800    .. 2001455   Block 1 Payload (all zero in this example)
```

Why this example matters:

- the archive file still contains two physical blocks because `mArchiveBlockCount = 2`
- the logical stream still finishes in block 0
- block 1 exists and is fully valid, but it carries only zero payload in this scenario
- block 0's stored skip is non-zero because the first block stores the first non-zero record start when one exists

### Example B Archive Header Bytes

| File Offset | Hex | ASCII | Meaning |
| --- | --- | --- | --- |
| `0` | `AD` | `.` | `mMagic` byte 0 |
| `1` | `FB` | `.` | `mMagic` byte 1 |
| `2` | `CA` | `.` | `mMagic` byte 2 |
| `3` | `DE` | `.` | `mMagic` byte 3 |
| `4` | `01` | `.` | `mVersionMajor` byte 0 |
| `5` | `00` | `.` | `mVersionMajor` byte 1 |
| `6` | `07` | `.` | `mVersionMinor` byte 0 |
| `7` | `00` | `.` | `mVersionMinor` byte 1 |
| `8` | `01` | `.` | `mArchiverVersion` |
| `9` | `01` | `.` | `mPasswordExpanderVersion` |
| `10` | `01` | `.` | `mCipherStackVersion` |
| `11` | `01` | `.` | `mEncryptionStrength = high` |
| `12` | `01` | `.` | `mExpansionStrength = high` |
| `13` | `02` | `.` | `mRecordCountMod256 = 2` |
| `14` | `00` | `.` | `mFolderCountMod256 = 0` |
| `15` | `04` | `.` | `mDirtyType = kFinished` |
| `16` | `00` | `.` | `mArchiveIndex` byte 0 |
| `17` | `00` | `.` | `mArchiveIndex` byte 1 |
| `18` | `00` | `.` | `mArchiveIndex` byte 2 |
| `19` | `00` | `.` | `mArchiveIndex` byte 3 |
| `20` | `01` | `.` | `mArchiveCount` byte 0 |
| `21` | `00` | `.` | `mArchiveCount` byte 1 |
| `22` | `00` | `.` | `mArchiveCount` byte 2 |
| `23` | `00` | `.` | `mArchiveCount` byte 3 |
| `24` | `00` | `.` | `mPayloadLength` byte 0 |
| `25` | `8A` | `.` | `mPayloadLength` byte 1 |
| `26` | `1E` | `.` | `mPayloadLength` byte 2 |
| `27` | `00` | `.` | `mPayloadLength` byte 3 |
| `28` | `00` | `.` | `mReserved32` byte 0 |
| `29` | `00` | `.` | `mReserved32` byte 1 |
| `30` | `00` | `.` | `mReserved32` byte 2 |
| `31` | `00` | `.` | `mReserved32` byte 3 |
| `32` | `00` | `.` | `mReservedB` byte 0 |
| `33` | `00` | `.` | `mReservedB` byte 1 |
| `34` | `00` | `.` | `mReservedB` byte 2 |
| `35` | `00` | `.` | `mReservedB` byte 3 |
| `36` | `00` | `.` | `mReservedB` byte 4 |
| `37` | `00` | `.` | `mReservedB` byte 5 |
| `38` | `00` | `.` | `mReservedB` byte 6 |
| `39` | `00` | `.` | `mReservedB` byte 7 |
| `40` | `08` | `.` | `mArchiveFamilyId` byte 0 |
| `41` | `07` | `.` | `mArchiveFamilyId` byte 1 |
| `42` | `06` | `.` | `mArchiveFamilyId` byte 2 |
| `43` | `05` | `.` | `mArchiveFamilyId` byte 3 |
| `44` | `04` | `.` | `mArchiveFamilyId` byte 4 |
| `45` | `03` | `.` | `mArchiveFamilyId` byte 5 |
| `46` | `02` | `.` | `mArchiveFamilyId` byte 6 |
| `47` | `01` | `.` | `mArchiveFamilyId` byte 7 |

### Example B Block 0 Recovery Header Bytes

| File Offset | Hex | ASCII | Meaning |
| --- | --- | --- | --- |
| `48` | `10` | `.` | `checksum.word1` byte 0 |
| `49` | `10` | `.` | `checksum.word1` byte 1 |
| `50` | `10` | `.` | `checksum.word1` byte 2 |
| `51` | `10` | `.` | `checksum.word1` byte 3 |
| `52` | `10` | `.` | `checksum.word1` byte 4 |
| `53` | `10` | `.` | `checksum.word1` byte 5 |
| `54` | `10` | `.` | `checksum.word1` byte 6 |
| `55` | `10` | `.` | `checksum.word1` byte 7 |
| `56` | `20` | ` ` | `checksum.word2` byte 0 |
| `57` | `20` | ` ` | `checksum.word2` byte 1 |
| `58` | `20` | ` ` | `checksum.word2` byte 2 |
| `59` | `20` | ` ` | `checksum.word2` byte 3 |
| `60` | `20` | ` ` | `checksum.word2` byte 4 |
| `61` | `20` | ` ` | `checksum.word2` byte 5 |
| `62` | `20` | ` ` | `checksum.word2` byte 6 |
| `63` | `20` | ` ` | `checksum.word2` byte 7 |
| `64` | `30` | `0` | `checksum.word3` byte 0 |
| `65` | `30` | `0` | `checksum.word3` byte 1 |
| `66` | `30` | `0` | `checksum.word3` byte 2 |
| `67` | `30` | `0` | `checksum.word3` byte 3 |
| `68` | `30` | `0` | `checksum.word3` byte 4 |
| `69` | `30` | `0` | `checksum.word3` byte 5 |
| `70` | `30` | `0` | `checksum.word3` byte 6 |
| `71` | `30` | `0` | `checksum.word3` byte 7 |
| `72` | `40` | `@` | `checksum.word4` byte 0 |
| `73` | `40` | `@` | `checksum.word4` byte 1 |
| `74` | `40` | `@` | `checksum.word4` byte 2 |
| `75` | `40` | `@` | `checksum.word4` byte 3 |
| `76` | `40` | `@` | `checksum.word4` byte 4 |
| `77` | `40` | `@` | `checksum.word4` byte 5 |
| `78` | `40` | `@` | `checksum.word4` byte 6 |
| `79` | `40` | `@` | `checksum.word4` byte 7 |
| `80` | `50` | `P` | `checksum.word5` byte 0 |
| `81` | `50` | `P` | `checksum.word5` byte 1 |
| `82` | `50` | `P` | `checksum.word5` byte 2 |
| `83` | `50` | `P` | `checksum.word5` byte 3 |
| `84` | `50` | `P` | `checksum.word5` byte 4 |
| `85` | `50` | `P` | `checksum.word5` byte 5 |
| `86` | `50` | `P` | `checksum.word5` byte 6 |
| `87` | `50` | `P` | `checksum.word5` byte 7 |
| `88` | `00` | `.` | `skip.archive_distance` byte 0 |
| `89` | `00` | `.` | `skip.archive_distance` byte 1 |
| `90` | `00` | `.` | `skip.block_distance` byte 0 |
| `91` | `00` | `.` | `skip.block_distance` byte 1 |
| `92` | `0C` | `.` | `skip.byte_distance` byte 0 (`12`) |
| `93` | `00` | `.` | `skip.byte_distance` byte 1 |
| `94` | `00` | `.` | `skip.byte_distance` byte 2 |
| `95` | `00` | `.` | `skip.byte_distance` byte 3 |

### Example B Block 0 Payload Bytes

| File Offset | Hex | ASCII | Meaning |
| --- | --- | --- | --- |
| `96` | `01` | `.` | file record 1: `path_length` byte 0 |
| `97` | `00` | `.` | file record 1: `path_length` byte 1 |
| `98` | `61` | `a` | file record 1: path byte 0 (`"a"`) |
| `99` | `01` | `.` | file record 1: `content_length` byte 0 |
| `100` | `00` | `.` | file record 1: `content_length` byte 1 |
| `101` | `00` | `.` | file record 1: `content_length` byte 2 |
| `102` | `00` | `.` | file record 1: `content_length` byte 3 |
| `103` | `00` | `.` | file record 1: `content_length` byte 4 |
| `104` | `00` | `.` | file record 1: `content_length` byte 5 |
| `105` | `00` | `.` | file record 1: `content_length` byte 6 |
| `106` | `00` | `.` | file record 1: `content_length` byte 7 |
| `107` | `58` | `X` | file record 1: content byte 0 |
| `108` | `01` | `.` | file record 2: `path_length` byte 0 |
| `109` | `00` | `.` | file record 2: `path_length` byte 1 |
| `110` | `62` | `b` | file record 2: path byte 0 (`"b"`) |
| `111` | `01` | `.` | file record 2: `content_length` byte 0 |
| `112` | `00` | `.` | file record 2: `content_length` byte 1 |
| `113` | `00` | `.` | file record 2: `content_length` byte 2 |
| `114` | `00` | `.` | file record 2: `content_length` byte 3 |
| `115` | `00` | `.` | file record 2: `content_length` byte 4 |
| `116` | `00` | `.` | file record 2: `content_length` byte 5 |
| `117` | `00` | `.` | file record 2: `content_length` byte 6 |
| `118` | `00` | `.` | file record 2: `content_length` byte 7 |
| `119` | `59` | `Y` | file record 2: content byte 0 |
| `120` | `00` | `.` | terminator: `path_length` byte 0 |
| `121` | `00` | `.` | terminator: `path_length` byte 1 |
| `122 .. 1000751` | `00 ... 00` | `.` | zero padding to the end of block 0 |

### Example B Block 1 Recovery Header Bytes

| File Offset | Hex | ASCII | Meaning |
| --- | --- | --- | --- |
| `1000752` | `AB` | `.` | `checksum.word1` byte 0 |
| `1000753` | `AB` | `.` | `checksum.word1` byte 1 |
| `1000754` | `AB` | `.` | `checksum.word1` byte 2 |
| `1000755` | `AB` | `.` | `checksum.word1` byte 3 |
| `1000756` | `AB` | `.` | `checksum.word1` byte 4 |
| `1000757` | `AB` | `.` | `checksum.word1` byte 5 |
| `1000758` | `AB` | `.` | `checksum.word1` byte 6 |
| `1000759` | `AB` | `.` | `checksum.word1` byte 7 |
| `1000760` | `CD` | `.` | `checksum.word2` byte 0 |
| `1000761` | `CD` | `.` | `checksum.word2` byte 1 |
| `1000762` | `CD` | `.` | `checksum.word2` byte 2 |
| `1000763` | `CD` | `.` | `checksum.word2` byte 3 |
| `1000764` | `CD` | `.` | `checksum.word2` byte 4 |
| `1000765` | `CD` | `.` | `checksum.word2` byte 5 |
| `1000766` | `CD` | `.` | `checksum.word2` byte 6 |
| `1000767` | `CD` | `.` | `checksum.word2` byte 7 |
| `1000768` | `EF` | `.` | `checksum.word3` byte 0 |
| `1000769` | `EF` | `.` | `checksum.word3` byte 1 |
| `1000770` | `EF` | `.` | `checksum.word3` byte 2 |
| `1000771` | `EF` | `.` | `checksum.word3` byte 3 |
| `1000772` | `EF` | `.` | `checksum.word3` byte 4 |
| `1000773` | `EF` | `.` | `checksum.word3` byte 5 |
| `1000774` | `EF` | `.` | `checksum.word3` byte 6 |
| `1000775` | `EF` | `.` | `checksum.word3` byte 7 |
| `1000776` | `12` | `.` | `checksum.word4` byte 0 |
| `1000777` | `12` | `.` | `checksum.word4` byte 1 |
| `1000778` | `12` | `.` | `checksum.word4` byte 2 |
| `1000779` | `12` | `.` | `checksum.word4` byte 3 |
| `1000780` | `12` | `.` | `checksum.word4` byte 4 |
| `1000781` | `12` | `.` | `checksum.word4` byte 5 |
| `1000782` | `12` | `.` | `checksum.word4` byte 6 |
| `1000783` | `12` | `.` | `checksum.word4` byte 7 |
| `1000784` | `34` | `4` | `checksum.word5` byte 0 |
| `1000785` | `34` | `4` | `checksum.word5` byte 1 |
| `1000786` | `34` | `4` | `checksum.word5` byte 2 |
| `1000787` | `34` | `4` | `checksum.word5` byte 3 |
| `1000788` | `34` | `4` | `checksum.word5` byte 4 |
| `1000789` | `34` | `4` | `checksum.word5` byte 5 |
| `1000790` | `34` | `4` | `checksum.word5` byte 6 |
| `1000791` | `34` | `4` | `checksum.word5` byte 7 |
| `1000792` | `00` | `.` | `skip.archive_distance` byte 0 |
| `1000793` | `00` | `.` | `skip.archive_distance` byte 1 |
| `1000794` | `00` | `.` | `skip.block_distance` byte 0 |
| `1000795` | `00` | `.` | `skip.block_distance` byte 1 |
| `1000796` | `00` | `.` | `skip.byte_distance` byte 0 |
| `1000797` | `00` | `.` | `skip.byte_distance` byte 1 |
| `1000798` | `00` | `.` | `skip.byte_distance` byte 2 |
| `1000799` | `00` | `.` | `skip.byte_distance` byte 3 |

### Example B Block 1 Payload Bytes

| File Offset | Hex | ASCII | Meaning |
| --- | --- | --- | --- |
| `1000800 .. 2001455` | `00 ... 00` | `.` | entire payload is zero padding in this example |

## Practical Rules Worth Remembering

- `mPayloadLength` counts serialized L3 blocks, not just logical payload bytes.
- The bundle path writes archive headers first with `mDirtyType = kInvalid`, then rewrites byte `15` during finalization.
- Strict decode requires a terminator.
- Recover can succeed without a terminator if it recovered at least one file before archive space ran out.
- The decoder chooses archive families by filename template first, then uses readable header family ids to refine selection.
- The parser reads the trailing digit run before the extension as the archive index.
- Empty directories are explicit records; non-empty directories are implicit.
- The first block gets special skip handling so recover has a useful jump target early.

## Bottom Line

If you only need the operational recipe:

1. Build `SourceEntry` values.
2. Fill a `BundleRequest`.
3. Call `Bundle(...)` or `DiscoverBundlePlan(...)` then `PerformBundleFlight(...)`.
4. For decode, collect candidate archive paths.
5. Fill an `UnbundleRequest`.
6. Call `Unbundle(...)` for strict behavior or `Recover(...)` for best-effort behavior.

If you need to implement a reader or writer, the format is:

1. archive header
2. repeated L3 blocks
3. each L3 block = recovery header + payload
4. payload = logical records + terminator + zero padding
