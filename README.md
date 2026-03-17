# PeanutButterUltima Archive Flow

This README describes the current archive, unarchive, and recover behavior in the checked-in code. It intentionally treats encryption as a black box: archive seals (encrypts) L3 blocks, and decode unseals (decrypts) L3 blocks.

## Scope

The current Qt app path always enables encryption for archive, unarchive, and recover. The core library can run with encryption disabled, but this README follows the way the app currently drives it.

The default archive file suffix in the app is `.PBTR`.

## Archive

Archive turns a file or folder into one archive family made of fixed-size `.PBTR` files.

Current flow:

1. Collect source entries.
   - If the source is a single file, archive that file under its basename.
   - If the source is a directory, archive every file under it, recursively.
   - Empty directories are also recorded.
   - Non-empty directories are not stored as standalone records.
2. Sort the entries by relative path.
3. Build one logical record stream from those sorted entries.
4. Split that stream across archive files that all use the same configured L3 block count.
5. For each archive file:
   - write the archive header
   - for each L3 block:
     - zero the block
     - fill payload bytes from the logical record stream
     - compute the recovery stride to the next record start
     - write the recovery header
     - seal (encrypt) the full L3 block
     - append it to the archive file

The logical record stream is simple:

- `u16 path_length`
- path bytes
- `u64 content_length`
- file bytes for file records
- `0xFFFFFFFFFFFFFFFF` as the content length for an empty directory record
- a final terminator record with `path_length = 0`

Important current behavior:

- Only empty directories get explicit directory records.
- Non-empty directories are recreated later from file paths when files are written back out.

## Unarchive

Unarchive is the strict decode path. It expects the selected archive family to be complete and internally consistent.

Current flow:

1. Collect candidate files from the selected file or folder.
2. Choose one archive family by filename template and, when readable headers exist, by header family id.
3. Build a dense index window that includes empty slots for missing archive numbers.
4. Read the archive family block by block.
5. For each expected block:
   - open the archive file
   - read one L3 block
   - unseal (decrypt) it
   - parse the recovery header
   - verify the checksum
   - parse logical records from the payload
   - create empty directories or write file bytes into the destination
6. Stop successfully only when the terminator record is reached.

Strict unarchive stops immediately on the first:

- missing archive slot in the selected dense window
- short archive file
- read failure
- unseal (decrypt) failure
- recovery-header failure
- checksum mismatch
- record-parse failure

## Recover

Recover uses the same file format and the same discovery pipeline as unarchive, but it is best-effort.

Current flow:

1. Collect and select archive candidates the same way unarchive does.
2. Walk the same dense archive window.
3. For each expected block:
   - try to read it
   - try to unseal (decrypt) it
   - try to verify it
   - try to parse records from it
4. If the block is missing or damaged:
   - count it as failed
   - abort any partial output file that was being written from that damaged region
   - reset the parser
   - continue
5. If the block passes checksum but record parsing fails:
   - abort any partial output file
   - reset the parser
   - try to honor the recovery stride stored in that block
   - if that jump is invalid, continue with the next block

Recover succeeds when either of these is true:

- it reaches the logical terminator
- it runs out of archive space after recovering at least one file

Recover fails with `RECOVER_EXHAUSTED` when it exhausts the archive span without reaching the terminator and without recovering any files.

## Notes

- Archive filenames are written as `prefix + source_stem + "_" + zero_padded_index + suffix`.
- The archive header stores the archive family id, archive count, and a per-file encryption-strength enum.
- The recovery header stores five checksum words plus a forward recovery stride.
- Unused payload bytes in each block are zero-padded before the block is sealed.

## Companion Files

- `file_format.txt`: binary layout and ASCII diagrams
- `final_spec_file_format.txt`: formal file format spec
- `final_spec_algorithms_high_level.txt`: short process spec
- `final_spec_algorithms_detailed.txt`: detailed process spec
- `final_spec_error_codes.txt`: archive/unarchive/recover error codes
- `inconsistencies.txt`: current code oddities that are worth knowing
