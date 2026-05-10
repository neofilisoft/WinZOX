# WinZOX

**WinZOX** is a file archiver focused on **own file format**, a **clean CLI**, and an internal architecture that stays maintainable as features grow.

> Status: **Active development**

---

## Why WinZOX
- **C++ performance**: predictable speed + easier native integration.
- **Modular architecture** to keep compression, archive, extraction, and crypto layers clean.
- **Practical UX** with progress reporting, speed/ETA, and shell integration on Windows.

---

## Features
### Current

- Create / extract archives via CLI

- `.zox` create + extract

- `.zip` create + extract

- `.7z` / `.rar` extract support

- Compression presets (Fast / Normal / Maximum / Ultra)

- Per-operation progress reporting (where supported)

- Encryption options: **AES-256** and **Gorgon**

- Archive integrity checks (including hash-based verification)

### Planned
- Multi-format support (ZIP / 7Z / RAR extract, etc.)
- Multi-thread compression where applicable (e.g., zstd)
- Archive integrity + metadata
- GUI frontend (optional)

---

## Supported Formats
| Format | Create | Extract | Notes |
|-----|--------|--------|----------|
| `.zox` | Yes | Yes | WinZOX native format |
| `.zip` | Yes | Yes | Standard ZIP workflow |
| `.7z` | No | Yes | Extract-only |
| `.rar` | No | Yes | Extract-only |

> `.zox` is WinZOX native format.
---

## Compression Presets (zstd defaults)
These are **recommended defaults** for a modern archiver

| Preset | zstd level | Goal |
|-----|--------|----------|
| Fast | 3-5 | very fast, decent ratio |
| Normal | 8-12 | balanced default |
| Maximum | 15-20 | smaller archive, slower |
| Ultra | 22-30 | strongest compression, much slower |

---

## Build
### Requirements
- C++17-compatible compiler
- CMake 3.10+
- Required libraries installed in your environment (for example: OpenSSL, zlib, zstd, libarchive)

### Build (CMake)
```bash
# Clone repository
git clone https://github.com/neofilisoft/WinZOX.git
cd WinZOX
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

For Windows Users
- If the installed build includes the GUI, .zox files can be opened directly with WinZOX.exe.
- The installer creates shortcuts for WinZOX GUI and WinZOX Console.
- Right-click integration still uses the CLI for extraction commands.
Important Files Changed from v2.12.0
- Added the `src/gui/` folder.
- Added `BUILD.md` and `AUDIT.md`.
- Updated `CMakeLists.txt` for version 3.0.0 and the optional GUI target.
- Updated `src/app/main.cpp` for the new default encryption mode and safer password input.
- Updated `src/archive/archive.cpp` for mandatory authentication and archive-bomb caps.
- Updated crypto providers for AES-GCM, ChaCha20-Poly1305, and Gorgon-AEAD.
- Updated `src/compression/coder/range_coder.cpp` and `src/compression/compressor.cpp`.
- Updated `src/extraction/extractor.cpp` and `src/io/file_reader.cpp` for path/symlink hardening.
- Updated `windows/WinZOX.iss` for the v3.0.0 installer and GUI packaging.
