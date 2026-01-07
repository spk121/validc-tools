# smoltar - A Simple Tar-like Archiver

`smoltar` is a simple archiver tool written in pure ISO C with no POSIX or platform-specific extensions. It implements a modified ustar format that is compatible with standard tar implementations.

## Features

- **ISO C Compliance**: Uses only standard C library functions (no POSIX extensions)
- **Ustar Format**: Creates archives compatible with standard tar tools
- **SHA-256 Hashing**: Computes and stores SHA-256 hash for each file
- **Smart Extraction**: Three extraction modes (force, normal, safe)
- **Prefix Support**: Optional prefix for extracted filenames
- **Checksum Validation**: Verifies archive integrity using header checksums

## Building

The tool is built as part of the main CMake build:

```bash
cmake -S . -B build
cmake --build build --target smoltar
```

The executable will be located at `build/bin/smoltar`.

## Usage

### Create an Archive

```bash
smoltar -cf archive.tar file1.txt file2.txt file3.txt
```

### Extract an Archive

```bash
# Force mode (default): Always extract, overwrite existing files
smoltar -xf archive.tar

# Normal mode: Only extract if SHA-256 differs or file doesn't exist
smoltar -xf archive.tar -m normal

# Safe mode: Never overwrite existing files
smoltar -xf archive.tar -m safe

# Extract with prefix
smoltar -xf archive.tar -p output/
```

### List Archive Contents

```bash
smoltar -tf archive.tar
```

## Options

- `-c` - Create a new archive
- `-x` - Extract files from archive
- `-t` - List contents of archive
- `-f FILE` - Specify archive file (required)
- `-m MODE` - Extraction mode: `force` (default), `normal`, or `safe`
- `-p PREFIX` - Prepend PREFIX to extracted filenames
- `-h` - Display help message

### Extraction Modes

- **force**: Always extract all files, overwriting existing ones
- **normal**: Extract only if file doesn't exist or SHA-256 hash differs
- **safe**: Never overwrite existing files

Options can be combined (e.g., `-cf` or `-cfarchive.tar`).

## Archive Format

`smoltar` uses a modified ustar format with the following characteristics:

### Header Structure (512 bytes)

| Field    | Offset | Size | Description                                    |
|----------|--------|------|------------------------------------------------|
| name     | 0      | 100  | Filename (must not contain path elements)      |
| mode     | 100    | 8    | File mode in octal (always 644)                |
| uid      | 108    | 8    | User ID in octal (always 0)                    |
| gid      | 116    | 8    | Group ID in octal (always 0)                   |
| size     | 124    | 12   | File size in octal                             |
| mtime    | 136    | 12   | Modification time in octal (current time)      |
| chksum   | 148    | 8    | Header checksum in octal                       |
| typeflag | 156    | 1    | Type flag ('0' for regular file)               |
| linkname | 157    | 100  | SHA-256 hash of file content (64 hex chars)    |
| magic    | 257    | 6    | Magic string ("ustar\0")                       |
| version  | 263    | 2    | Version ("00")                                 |
| uname    | 265    | 32   | User name (unused)                             |
| gname    | 297    | 32   | Group name (unused)                            |
| devmajor | 329    | 8    | Device major number (unused)                   |
| devminor | 337    | 8    | Device minor number (unused)                   |
| prefix   | 345    | 155  | Filename prefix (unused)                       |
| padding  | 500    | 12   | Padding to 512 bytes                           |

### Design Constraints

Since `smoltar` uses only ISO C functions:

- **No directory operations**: Cannot create directories or list files
- **No file metadata**: Cannot get or set file permissions, ownership, or timestamps
- **No symlinks**: Cannot handle symbolic or hard links
- **File existence**: Can only test file existence by trying to open it
- **File size**: Determined using `fseek`/`ftell`

### Checksum Calculation

The checksum is the sum of all bytes in the header, treating the checksum field itself as 8 space characters (ASCII 32).

### SHA-256 Hash Storage

For each file added to the archive, `smoltar` computes a SHA-256 hash of the file content and stores the 64-character hexadecimal representation in the `linkname` field (offset 157, 100 bytes). This enables:

- **Content-based comparison**: The `normal` extraction mode uses SHA-256 to determine if a file has changed
- **Integrity verification**: Future enhancements could verify file integrity after extraction
- **Deduplication**: The hash can be used to identify duplicate files

Since ISO C provides no way to query file modification times, SHA-256 hashing provides the only reliable method to detect file changes between the filesystem and archive.

## Limitations

- Files must not have path separators in their names
- All files are stored with mode 644
- Modification time is set to the current time when creating archives
- No support for directories, special files, or links
- Maximum filename length is 100 characters

## Compatibility

Archives created by `smoltar` can be extracted by standard `tar` implementations, and `smoltar` can extract standard tar archives (with the limitations noted above). The SHA-256 hash stored in the linkname field will be ignored by standard tar tools.

## Examples

```bash
# Create an archive with SHA-256 hashes
smoltar -cf backup.tar file1.txt file2.txt

# List contents
smoltar -tf backup.tar

# Extract all files (force mode - always overwrites)
smoltar -xf backup.tar

# Extract only changed files (normal mode)
smoltar -xf backup.tar -m normal

# Extract with prefix (e.g., into a subdirectory)
smoltar -xf backup.tar -p extracted/

# Safe extraction (never overwrite)
smoltar -xf backup.tar -m safe

# Create with combined options
smoltar -cfbackup.tar file1.txt file2.txt
```

## Error Handling

The tool provides clear error messages for common issues:

- Cannot open file for reading
- Cannot create archive file
- Invalid archive format
- Checksum mismatches
- File I/O errors

Exit codes:
- `0` - Success
- `1` - Error occurred
