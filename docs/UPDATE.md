# WinZOX 3.1.0 & UnZOX SDK 2.0.0 - Update Notes

This release upgrades the C++ baseline to **C++20**, ports two safety-critical
modules to **Rust 1.95**, hardens the UnZOX SDK, and ships a new safe Rust
API on top of the UnZOX C ABI. Archive wire format and on-disk layout are
**unchanged** — `.zox` files produced by 3.0.0 are readable by 3.1.0 and
vice versa.

---

## 1. C++ language baseline

Both projects move from C++17 to **C++20**.

| Project           | Old standard | New standard | Minimum compilers                                    |
|-------------------|--------------|--------------|------------------------------------------------------|
| WinZOX            | C++17        | C++20        | GCC ≥ 11 / Clang ≥ 13 / MSVC ≥ 19.30 (VS 2022 17.0)  |
| UnZOX SDK         | C++17        | C++20        | same                                                 |

Why C++20:

* `<span>` / `<bit>` / `std::endian` / `std::byteswap` / `std::bit_cast` for
  the binary parser, replacing hand-rolled portability shims.
* `std::is_constant_evaluated()` and improved `constexpr` for the
  compression coders.
* Designated initializers cleaned up the I/O and GUI struct literals.

### Migration notes

* `std::filesystem::path::u8string()` now returns `std::u8string`
  (`char8_t`-based) instead of `std::string`. A single helper —
  `winzox::utils::PathToUtf8` — wraps that change behind a back-compat shim
  (`#if defined(__cpp_lib_char8_t)` cast). All 17 call sites across
  `archive.cpp`, `extractor.cpp`, `windows_shell.cpp`, `main.cpp`,
  `repair_kit.cpp`, and the path utilities were migrated to use it.
* No public API or ABI changes from the C++20 jump itself.

---

## 2. New Rust components

Located in the `rust-crates/` workspace. Built with Rust **1.95**
(stable, released 2026-04-14).

### 2.1 `winzox-io-rs`

A Rust 1.95 reimplementation of `WinZOX/src/io/*` with safety hardening
that the original C++ code did not have:

* **Per-file size cap (64 GiB).** `read_file_bytes` rejects oversized
  files instead of allocating a multi-gigabyte `std::vector`.
* **Total split-volume cap (1 TiB).** `read_all_volumes` short-circuits on
  any `.zNN` chain that crosses the cap.
* **Volume index cap (.z01 – .z99).** Prevents pathological numerics from
  driving a loop with no upper bound.
* **Refuse-to-follow on symbolic links** during input collection, file
  read, and file write — defense-in-depth on top of the extractor’s path
  sanitiser.
* **Stable C ABI** exported via `extern "C"` so the C++ binary can call
  the Rust functions through `winzox_io_rs.h` without a runtime mismatch.

Exported functions (C ABI):

```
winzox_io_read_file_bytes(path, &out_buffer)
winzox_io_write_file_bytes(path, data, len)
winzox_io_read_all_volumes(path, &out_buffer)
winzox_io_collect_input_files(input, &out_list)
winzox_io_volume_writer_new / write / close / free
winzox_io_buffer_free / winzox_io_path_list_free
winzox_io_last_error / winzox_io_abi_version
```

Tests: 6 passing — round-trip, lexicographic ordering, missing-file
rejection, symlink-parent rejection, split-volume reader, split-volume
writer.

### 2.2 `winzox-archive-read`

A Rust 1.95 implementation of the **read-side core** of the WZOX archive
format. Scope intentionally excludes the writer and decompression
pipeline — those remain in C++ and continue to drive
`zox add`/`zox extract`.

What’s in Rust:

* Magic detection (`WZOX`, `ZOX4`, `ZOX5`, `ZOX6`).
* Full header parse (current + legacy variants) with KDF parameter
  validation, AEAD/auth flag consistency checks, and integer-overflow
  guards.
* Footer parse (central-directory offset, sizes, entry count, paired
  SHA-512 and SHA3-256 digests, optional auth tag).
* Integrity accumulator backed by RustCrypto’s `sha2`/`sha3` crates and a
  constant-time comparator from the `subtle` crate.
* Central-directory parser with entry-count and per-entry caps.
* Plain (already decrypted) metadata + index parsing for the
  `unzox_probe_archive_file` / `winzox_ar_read_metadata_plain` paths.

Archive-bomb hardening enforced by the Rust parser:

| guard                                | value         |
|--------------------------------------|---------------|
| entries per archive                  | 10 000 000    |
| central directory plain size         | 1 GiB         |
| per-entry original/stored size       | 64 GiB        |
| stored-size monotonicity vs payload  | enforced      |
| paired SHA-512 + SHA3-256 verification | mandatory   |

Exported C ABI lives in
`winzox-archive-read/include/winzox_archive_read.h` and is consumed by
`archive.cpp::LooksLikeZoxArchiveBytes` and
`archive_integrity.cpp::ArchiveIntegrityAccumulator` when
`-DWINZOX_ENABLE_RUST_COMPONENTS=ON`.

Tests: 9 passing — including `cpp_parity_roundtrip` which constructs a
minimal WZOX archive byte-for-byte and verifies the Rust parser yields
the same `ArchiveMetadata` / `ArchiveEntryInfo` as the C++ side.

### 2.3 `unzox-sys` + `unzox`

A two-crate split following the standard Rust convention:

* **`unzox-sys`** — raw `extern "C"` bindings to UnZOX SDK 2.0.0
  (`unzox_c_api.h`). Hand-written, no `bindgen` step at build time. The
  build script picks up `UNZOX_LIB_DIR` and `UNZOX_LINK_KIND` so the
  linker can find `libUnZOX.so` / `UnZOX.dll`.
* **`unzox`** — the safe wrapper that almost every consumer will use.
  Highlights:
  * **Typed errors.** Every C status code maps to a `unzox::Error`
    variant: `PasswordRequired`, `AuthFailed`, `SizeCapExceeded`,
    `SymlinkRefused`, `KdfParamsInvalid`, `ArchiveBomb`, `Truncated`,
    `EntryOutOfRange`, etc. No more substring matching exception
    messages.
  * **RAII buffer/entry-list ownership** so a panic crossing into Rust
    never leaks SDK allocations.
  * **Panic-safe progress callback.** A trampoline catches Rust panics
    inside the closure and reports them as "cancel" to the SDK instead
    of unwinding into C.
  * **NUL-safe path validation.** Paths with embedded NUL bytes are
    rejected up front rather than truncated by the C string layer.
  * **Lossless raw-path access** via `Entry::path_bytes` for archives
    whose entries are not valid UTF-8.

Public surface (sketch):

```rust
let reader = unzox::Reader::new("archive.zox");
let probe  = reader.probe(Some("hunter2"))?;
for entry in probe.entries() {
    println!("{} ({} bytes)", entry.path(), entry.original_size());
}
reader.extract("./out", Some("hunter2"), None)?;
let bytes = reader.read_entry(0, Some("hunter2"))?;
```

Example: `examples/probe_archive.rs` is built into
`target/release/examples/probe_archive`.

---

## 3. WinZOX 3.1.0 build wiring

```sh
# Plain C++20 build (no Rust):
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# C++20 + Rust 1.95 components:
cmake -S . -B build-rust -DCMAKE_BUILD_TYPE=Release \
                       -DWINZOX_ENABLE_RUST_COMPONENTS=ON
cmake --build build-rust
```

When `WINZOX_ENABLE_RUST_COMPONENTS=ON`, the CMake script runs `cargo
build --release` over the workspace at `../rust-crates`, links
`libwinzox_io_rs.a` and `libwinzox_archive_read.a` into both the `zox`
CLI and the `libUnZOX.so` shared library, and defines
`WINZOX_USE_RUST_IO=1` / `WINZOX_USE_RUST_ARCHIVE_READ=1` so the C++
sources delegate to the Rust C ABI for:

* `WinZOX::io::CollectInputFiles`
* `WinZOX::io::ReadFileBytes`
* `WinZOX::io::WriteFileBytes`
* `WinZOX::io::ReadAllVolumes`
* `WinZOX::archive::LooksLikeZoxArchiveBytes`
* `WinZOX::archive::integrity::ArchiveIntegrityAccumulator`
* `WinZOX::archive::integrity::ComputeArchiveIntegrityDigests`
* `WinZOX::archive::integrity::DigestsEqual`

The C++ implementations remain in the source tree as the fallback path
for builds without Rust.

Smoke tests included in this release:

* Plain `.zox` round-trip with the Rust path on. ✅
* AES-256 encrypted `.zox` round-trip with the Rust path on. ✅
* Three password variants against an AES archive via the `unzox`
  example: correct password → OK, wrong password → `Error::AuthFailed`,
  no password → `Error::PasswordRequired`. ✅

---

## 4. UnZOX SDK 2.0.0

### Behavioural changes

* **Bumped to 2.0.0.** Major version because:
  * New status codes added to `UnZOXStatusCode` (numeric values 14–17).
  * C++ standard requirement raised to C++20.
  * `ClassifyError` is more aggressive — error categorisations that used
    to drop into `UNZOX_STATUS_INTERNAL` now surface as their proper
    specific code.
* `ClassifyError` rewritten. The 1.1.0 substring matcher was fragile and
  did not cover the new safety guard messages from WinZOX 3.1.0. The
  2.0.0 matcher recognises all of:
  * size caps (file, total, split-volume) → `SIZE_CAP_EXCEEDED`,
  * symbolic-link refusals → `SYMLINK_REFUSED`,
  * KDF parameter rejections → `KDF_PARAMS_INVALID`,
  * archive-bomb central-directory / entry-count rejections →
    `ARCHIVE_BOMB`,
  * paired hash mismatches (SHA-512, SHA3-256) → `INTEGRITY_FAILED`,
  * authentication and PKCS#7 padding failures → `AUTH_FAILED`,
  * "archive is encrypted" and missing-password phrasing →
    `PASSWORD_REQUIRED`.

### New status codes

```c
UNZOX_STATUS_SIZE_CAP_EXCEEDED   = 14,
UNZOX_STATUS_SYMLINK_REFUSED     = 15,
UNZOX_STATUS_KDF_PARAMS_INVALID  = 16,
UNZOX_STATUS_ARCHIVE_BOMB        = 17,
```

Mirrored in the C++ enum `unzox::StatusCode` as `SizeCapExceeded`,
`SymlinkRefused`, `KdfParamsInvalid`, `ArchiveBomb`.

### CMake source-glob fixes

The 1.1.0 SDK glob over `${UNZOX_ROOT}/Lib/*.cpp` only excluded the
`extraction/api/unzox` subdirectory. Outside that, it would happily pick
up GUI, shell, and extension code that the SDK has no business linking.
The 2.0.0 SDK now excludes:

* `app/` (CLI `main.cpp`)
* `gui/` (Qt6 desktop)
* `shell/` (Win32 explorer integration)
* `extensions/` (repair_kit and others)
* `third_party/`

---

## 5. Security & robustness fixes

| ID  | Layer        | Issue                                                                                                | Fix                                                                                                                          |
|-----|--------------|------------------------------------------------------------------------------------------------------|------------------------------------------------------------------------------------------------------------------------------|
| 1   | WinZOX I/O   | `file_reader.cpp` allocated `vector<uint8_t>(tellg())` unbounded.                                    | 64 GiB per-file cap. Mirrored in the Rust port.                                                                              |
| 2   | WinZOX I/O   | `volume_reader.cpp` looped `.z01..` with no cap on total bytes.                                      | 1 TiB total cap + `.z99` index ceiling. Mirrored in the Rust port.                                                           |
| 3   | WinZOX I/O   | `file_writer.cpp` used `create_directories(parent)` which follows symlinks.                          | Manual per-component walk that refuses any symlink in the parent chain plus refusal to overwrite an existing symlink target. |
| 4   | UnZOX SDK    | `ClassifyError` did substring matching of English exception messages, downgrading codes silently.    | Rewritten exhaustive matcher with new codes for size cap / symlink / KDF / archive-bomb / paired-hash failures.              |
| 5   | Rust crates  | Integrity comparison via plain `memcmp` would have been timing-leaky.                                | `subtle::ConstantTimeEq` for the SHA-512/SHA3-256 digest compare; OpenSSL's `CRYPTO_memcmp` for the C++ fallback path.       |
| 6   | Rust crates  | FFI callbacks can panic, which is UB across the C ABI.                                              | The `unzox` crate wraps every Rust closure in a panic-trampoline that converts panics into "cancel" returns.                 |

---

## 6. Compatibility

* **Wire format:** unchanged. `.zox`, `.z01..z99`, and the WZOX/ZOX6/ZOX5/ZOX4
  variants are bit-for-bit identical.
* **C ABI (UnZOX SDK):** **non-breaking additions only.** Existing code
  continues to build and run. The new status codes only appear when
  WinZOX surfaces a new error class — pre-3.1.0 archives won’t trigger
  them.
* **C++ ABI (UnZOX SDK):** **non-breaking additions only** to
  `unzox::StatusCode`. C++17 callers must now build with C++20 because
  the SDK headers use `<bit>` and `<span>`.
* **WinZOX CLI / GUI:** no command-line option changes. Existing scripts
  keep working.

---

## 7. Toolchain

| component | required version          |
|-----------|---------------------------|
| CMake     | ≥ 3.20                    |
| C++       | C++20 (GCC 11 / Clang 13 / MSVC 19.30 minimum) |
| Rust      | 1.95.0 stable             |

The Rust dependency tree is intentionally narrow:
`libc`, `thiserror`, `sha2`, `sha3`, `subtle`, `crc32fast`, `tempfile`
(dev), `hex-literal` (dev). All are widely-used, MIT/Apache-licensed
crates audited by the `cargo-vet` ecosystem.

---

## 8. Deliverables

This release ships as three zips:

* `WinZOX-3.1.0.zip` — full C++ source tree with the Rust integration
  hooks. Builds with or without Rust components.
* `UnZOX-SDK-2.0.0.zip` — standalone SDK that drops next to a `Lib/`
  symlink/folder pointing at the WinZOX `src/` tree.
* `rust-crates.zip` — the four-crate Rust workspace
  (`winzox-io-rs`, `winzox-archive-read`, `unzox-sys`, `unzox`).
