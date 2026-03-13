# Archive Layout Format (Current Version)

This document describes the active PBTR byte layout used by the current engine.

## Constants

- `ARCHIVE_HEADER_LENGTH = 40`
- `RECOVERY_HEADER_LENGTH = 40`
- `MAX_VALID_FILE_PATH_LENGTH = 2048`
- `MAX_ARCHIVE_COUNT = 65535`
- `MAX_BLOCKS_PER_ARCHIVE = 2048`
- `kMajorVersion = 1`
- `kMinorVersion = 2`
- `kMagicHeaderBytes = 0xDECAFBAD`

Per test/config:

- `kBlockSizeL3` is configurable.
- `payloadBytesPerBlock = kBlockSizeL3 - RECOVERY_HEADER_LENGTH`

## Archive file naming

Archive file name pattern:

`{prefix}{source_stem}_{zero_padded_ordinal}{suffix}`

Padding width is based on `archive_count - 1` decimal digit width.

## Archive file binary structure

Each archive file is:

1. Archive header (40 bytes)
2. `target_archive_block_count` blocks

Each block is:

1. Recovery header (40 bytes)
2. Block payload (`payloadBytesPerBlock` bytes)

## Archive header (40 bytes, little-endian)

| Offset | Size | Field |
|---|---:|---|
| 0 | 4 | magic (`u32`) |
| 4 | 2 | version_major (`u16`) |
| 6 | 2 | version_minor (`u16`) |
| 8 | 4 | archive_index (`u32`) |
| 12 | 4 | archive_count (`u32`) |
| 16 | 4 | payload_length (`u32`) |
| 20 | 1 | record_count_mod256 (`u8`) |
| 21 | 1 | folder_count_mod256 (`u8`) |
| 22 | 2 | reserved16 (`u16`) |
| 24 | 8 | reservedA (`u64`) |
| 32 | 8 | reservedB (`u64`) |

Validation rules:

- `magic == 0xDECAFBAD`
- `version_major == 1`
- `version_minor == 2`
- `payload_length` equals `target_archive_block_count * kBlockSizeL3`

## Recovery header (40 bytes, little-endian)

| Offset | Size | Field |
|---|---:|---|
| 0 | 8 | checksum_word1 (`u64`) |
| 8 | 8 | checksum_word2 (`u64`) |
| 16 | 8 | checksum_word3 (`u64`) |
| 24 | 8 | checksum_word4 (`u64`) |
| 32 | 2 | skip_archive_distance (`u16`) |
| 34 | 2 | skip_block_distance (`u16`) |
| 36 | 4 | skip_byte_distance (`u32`) |

Skip record meaning:

- `skip_archive_distance`: forward archive delta
- `skip_block_distance`: forward block delta inside archive grid
- `skip_byte_distance`: byte offset inside target payload block

## Payload logical record stream

Records are concatenated in payload space across blocks/archives:

1. `path_length` (`u16`, little-endian)
2. `path_bytes` (`path_length` bytes)
3. `content_length` (`u64`, little-endian)
4. `content_bytes` (`content_length` bytes)

Special values:

- Terminator record: `path_length == 0`
- Directory record marker: `content_length == 0xFFFFFFFFFFFFFFFF`

## Checksum definition (per block)

Checksum covers **payload bytes + full skip record**.

Inputs:

- block payload bytes (`kBlockSizeL3 - RECOVERY_HEADER_LENGTH` bytes)
- skip record serialized as 8 bytes:
  - `archive_distance` (u16 LE)
  - `block_distance` (u16 LE)
  - `byte_distance` (u32 LE)

Output:

- 4 x 64-bit words (`word1..word4`) written into recovery header.

Behavioral requirement:

- Any payload mutation before checksum write must change checksum accordingly.
- This is exactly why `BundleWithMutations` applies data mutation first, then computes checksum.

## Size invariants

For each archive file:

`archive_size == ARCHIVE_HEADER_LENGTH + target_archive_block_count * kBlockSizeL3`

Tail bytes in the final written archive are expected to be zero-filled where logical payload does not consume full capacity.

## Decode/recover expectations tied to layout

- Unbundle is strict: gaps/checksum/parse issues are terminal.
- Recover is best-effort: may skip damage and continue.
- Recover may use valid skip headers to jump forward when parse fails on an otherwise checksum-valid block.

## Mutation-oriented testing implications

When generating adversarial tests:

1. You can target archive header fields directly (file-level corruption).
2. You can target recovery header fields directly (file-level corruption).
3. You can target payload record framing fields (`path_length`, `content_length`) by logical offset mutation.
4. You can delete archive files to create index gaps and exercise `GAP_001` behavior.
