# WinZOX 3.1.0 - Build Guide

This document describes how to build the v3.1.0 source tree on Linux and on
Windows (MSYS2 + Qt 6). The legacy build instructions in `README.md` (if any)
are superseded by this file.

**3.1.0 toolchain bump.** WinZOX 3.1.0 requires a **C++20** compiler
(GCC ≥ 11 / Clang ≥ 13 / MSVC ≥ 19.30) and **CMake ≥ 3.20**. The optional
Rust components require **Rust 1.95** (stable, released 2026-04-14).
See `update.md` for the rationale and migration notes.

## Optional Rust components

WinZOX 3.1.0 can be built with the Rust 1.95 read-side core enabled. This
replaces the C++ implementations of `src/io/*` and the integrity / magic
detection in `src/archive/*` with the matching Rust crates from
`../rust-crates/`:

```sh
cmake -S . -B build-rust -DCMAKE_BUILD_TYPE=Release \
                       -DWINZOX_ENABLE_RUST_COMPONENTS=ON
cmake --build build-rust -j
```

When the flag is OFF (default), the build is pure C++20 and behaves
identically to a 3.0.0 build.

## Targets in the source tree

| Target | Output | Notes |
|---|---|---|
| `winzox` (executable) | `zox` (Linux) / `zox.exe` (Windows) | Headless CLI archiver. Always built. |
| `winzox_gorgon` (shared library) | `libwinzox_gorgon.so` / `winzox_gorgon.dll` | Stand-alone Gorgon AEAD library (C ABI). |
| `winzox_unzox` (shared library) | `libUnZOX.so` / `UnZOX.dll` | Read-only WZOX library used by the GUI and 3rd-party integrations. |
| `winzox_gui` (executable) | `WinZOX` (Linux) / `WinZOX.exe` (Windows) | Optional Qt 6 Fluent-style desktop UI. Built only when `-DWINZOX_BUILD_GUI=ON`. The GUI compiles the read-only archive sources directly so it does not depend on `UnZOX.dll` at runtime. |
| Extensions (under `extensions/`) | `libWinZOXChaCha20.so`, `libWinZOXRepairKit.so`, `WinZOXChaCha20.dll`, `WinZOXRepairKit.dll` | Built by default; disable with `-DWINZOX_ENABLE_EXTENSIONS=OFF`. The CLI loads `WinZOXRepairKit` dynamically when you invoke `zox repair`. |

### Windows resources (icon + Properties → Details)

Both `zox.exe` and `WinZOX.exe` embed:

- the application icon (`favicon.ico`) as `IDI_ICON1` so File Explorer
  shows the WinZOX logo for the executables and (via the `.zox` association)
  for archives;
- a `VS_VERSION_INFO` block (`CompanyName`, `FileDescription`, `FileVersion`,
  `LegalCopyright`, `ProductName`, `ProductVersion`) so right-click →
  *Properties* → *Details* shows the same fields you see for WinRAR / 7-Zip.

The resource files are at `windows/zox.rc` and `windows/winzox_gui.rc`. They
are compiled automatically on Windows builds — no manual `windres` invocation
needed.

## Linux (Ubuntu 22.04+/Debian 12+)

### Required packages

```bash
sudo apt update
sudo apt install -y \
    cmake build-essential pkg-config \
    libssl-dev nettle-dev zlib1g-dev \
    liblz4-dev libzstd-dev liblzma-dev libarchive-dev
```

For the optional Qt 6 GUI also install:

```bash
sudo apt install -y qt6-base-dev libgl-dev
```

### Configure & build (CLI only)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Configure & build (CLI + Qt 6 GUI)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DWINZOX_BUILD_GUI=ON
cmake --build build -j$(nproc)
```

The CLI is at `build/zox`; the GUI is at `build/WinZOX`. The GUI links against
the `libUnZOX.so` shared library — keep them next to each other or set
`LD_LIBRARY_PATH=build` when launching the GUI from a custom location.

### Smoke tests

```bash
mkdir -p /tmp/wzx_smoke/in
echo "hello" > /tmp/wzx_smoke/in/a.txt

# Plain
build/zox add /tmp/wzx_smoke/in /tmp/wzx_smoke/plain
build/zox extract /tmp/wzx_smoke/plain.zox /tmp/wzx_smoke/out_plain

# Encrypted with Gorgon-AEAD (default for v3)
build/zox add /tmp/wzx_smoke/in /tmp/wzx_smoke/sec -p - <<<'mypw'
build/zox extract /tmp/wzx_smoke/sec.zox /tmp/wzx_smoke/out_sec -p - <<<'mypw'

# Wrong password rejection (must fail with "Archive authentication failed")
build/zox extract /tmp/wzx_smoke/sec.zox /tmp/wzx_smoke/out_bad -p WRONG

# Repair (extension): truncate the footer, then rebuild it
python3 -c "import sys; d=open('/tmp/wzx_smoke/plain.zox','rb').read();
open('/tmp/wzx_smoke/broken.zox','wb').write(d[:d.rfind(b'ZCDR')])"
build/zox repair /tmp/wzx_smoke/broken.zox /tmp/wzx_smoke/repaired.zox
build/zox extract /tmp/wzx_smoke/repaired.zox /tmp/wzx_smoke/out_repaired
```

## Windows (MSYS2 UCRT64 + Qt 6)

WinZOX 3.0.0 is built on Windows with the MSYS2 UCRT64 toolchain so that the
binary is forward-compatible with Windows 10 1809 and later (UCRT is the default
C runtime). Qt 6 packages are available natively in `mingw-w64-ucrt-x86_64-qt6-*`.

### One-time setup

1. Install MSYS2 from <https://www.msys2.org> (default location `C:\msys64`).
2. Open the **MSYS2 UCRT64** shell (not MINGW64 or MSYS).
3. Update the package database:

   ```bash
   pacman -Syu
   ```

   (close and reopen the shell when it prompts you to)

4. Install the toolchain and dependencies:

   ```bash
   pacman -S --needed --noconfirm \
       mingw-w64-ucrt-x86_64-toolchain \
       mingw-w64-ucrt-x86_64-cmake \
       mingw-w64-ucrt-x86_64-ninja \
       mingw-w64-ucrt-x86_64-pkg-config \
       mingw-w64-ucrt-x86_64-openssl \
       mingw-w64-ucrt-x86_64-zlib \
       mingw-w64-ucrt-x86_64-zstd \
       mingw-w64-ucrt-x86_64-lz4 \
       mingw-w64-ucrt-x86_64-xz \
       mingw-w64-ucrt-x86_64-libarchive \
       mingw-w64-ucrt-x86_64-nettle \
       mingw-w64-ucrt-x86_64-qt6-base \
       mingw-w64-ucrt-x86_64-qt6-tools
   ```

### Build

From the WinZOX source directory inside the **UCRT64** shell:

```bash
cmake -S . -B build-native -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DWINZOX_BUILD_GUI=ON
cmake --build build-native -j
```

Outputs in `build-native/`:

- `zox.exe` — CLI
- `zox.exe.manifest` — Windows side-by-side manifest
- `WinZOX.exe` — Qt 6 desktop GUI
- `winzox_gorgon.dll` — Gorgon AEAD shared library
- `UnZOX.dll` — read-only library
- All required Qt 6 + OpenSSL DLLs

### Producing the Inno Setup installer

The installer is built from `windows/WinZOX.iss` and expects the binaries in
`build-native/`. Compile with Inno Setup 6:

```cmd
"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" windows\WinZOX.iss
```

The resulting `WinZOXSetup.exe` registers the file associations and (when
`WinZOX.exe` is present) makes the GUI the default `Open` handler for `.zox`
files. Right-click integration always uses the CLI (`zox.exe`).

#### Bundling Qt runtime DLLs

Run `windeployqt.exe` (shipped with Qt) against `WinZOX.exe` after the build
to copy the necessary Qt 6 runtime DLLs into `build-native/` before invoking
the Inno Setup compiler:

```cmd
C:\msys64\ucrt64\bin\windeployqt.exe --release --no-translations build-native\WinZOX.exe
```

The `Source: "{#BuildDir}\*.dll"` line in `WinZOX.iss` already picks up every
DLL in `build-native/`, so no further changes are needed.

## Cross-compiling

Cross compiling from Linux to Windows is supported via the bundled MSYS2
toolchain only. Mainstream `mingw-w64` cross toolchains do not always ship Qt 6
binaries; if you need a Linux-hosted build, install `qt6-base-dev:amd64` and
`mingw-w64-x86-64-dev` and provide your own toolchain file.

## Verifying a build

```bash
build/zox --help                    # prints `WinZOX v3.0.0 - Modular Archiver`
build/zox add <dir> <out>           # creates an unencrypted .zox
build/zox add <dir> <out> -p -      # AEAD-encrypts using Gorgon-AEAD (default)
build/zox add <dir> <out> -p - --encrypt aes-gcm     # AES-256-GCM
build/zox add <dir> <out> -p - --encrypt chacha20    # ChaCha20-Poly1305
build/zox add <dir> <out> -p - --encrypt aes-cbc     # legacy CBC mode
```
