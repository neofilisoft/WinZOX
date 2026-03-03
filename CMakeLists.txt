cmake_minimum_required(VERSION 3.10)
project(ZipBox VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (WIN32)
    set(MSYS2_UCRT64_ROOT "C:/msys64/ucrt64")
    if (EXISTS "${MSYS2_UCRT64_ROOT}")
        list(APPEND CMAKE_PREFIX_PATH "${MSYS2_UCRT64_ROOT}")

        if (NOT DEFINED OPENSSL_ROOT_DIR)
            set(OPENSSL_ROOT_DIR "${MSYS2_UCRT64_ROOT}")
        endif()
    endif()
endif()

find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(LibArchive REQUIRED)
find_path(ZSTD_INCLUDE_DIR zstd.h
    PATHS
        "${MSYS2_UCRT64_ROOT}/include"
)
find_library(ZSTD_LIBRARY NAMES zstd libzstd
    PATHS
        "${MSYS2_UCRT64_ROOT}/lib"
)
find_library(NETTLE_LIBRARY NAMES nettle libnettle
    PATHS
        "${MSYS2_UCRT64_ROOT}/lib"
)

if (NOT ZSTD_INCLUDE_DIR OR NOT ZSTD_LIBRARY)
    message(FATAL_ERROR "Could not find Zstd. Install the Zstd development package or set ZSTD_INCLUDE_DIR/ZSTD_LIBRARY.")
endif()

if (NOT NETTLE_LIBRARY)
    message(FATAL_ERROR "Could not find Nettle. Install the Nettle development package or set NETTLE_LIBRARY.")
endif()

set(SOURCES
    src/app/main.cpp
    src/archive/archive.cpp
    src/compression/compressor.cpp
    src/crypto/aes256.cpp
    src/crypto/gorgon.cpp
    src/crypto/key_derivation.cpp
    src/extraction/extractor.cpp
    src/io/file_reader.cpp
    src/io/file_writer.cpp
    src/shell/windows_shell.cpp
    src/shell/windows_shell_add_dialog.cpp
    src/shell/windows_shell_browse_dialog.cpp
    src/io/volume_reader.cpp
    src/io/volume_writer.cpp
    src/utils/checksum.cpp
    src/utils/path_utils.cpp
)

if (WIN32)
    add_executable(zipbox WIN32 ${SOURCES})
else()
    add_executable(zipbox ${SOURCES})
endif()

target_include_directories(zipbox PRIVATE
    src
    ${OPENSSL_INCLUDE_DIR}
    ${ZLIB_INCLUDE_DIRS}
    ${LibArchive_INCLUDE_DIRS}
    ${ZSTD_INCLUDE_DIR}
)

target_link_libraries(zipbox PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
    ${LibArchive_LIBRARIES}
    ${ZSTD_LIBRARY}
    ${NETTLE_LIBRARY}
)

if (WIN32)
    target_link_libraries(zipbox PRIVATE comdlg32 shell32 user32 ole32 uuid)
    add_custom_command(
        TARGET zipbox POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/windows/zipbox.manifest"
            "$<TARGET_FILE_DIR:zipbox>/zipbox.exe.manifest"
    )
endif()

install(TARGETS zipbox RUNTIME DESTINATION bin)
install(FILES
    windows/install-shell-integration.ps1
    windows/shell-dispatch.ps1
    windows/uninstall-shell-integration.ps1
    DESTINATION tools
)
