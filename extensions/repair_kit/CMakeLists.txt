set(WINZOX_REPAIR_KIT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/repair_kit.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/archive/archive_integrity.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/io/file_reader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/io/volume_reader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/utils/checksum.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/utils/path_utils.cpp
)

add_library(winzox_repair_kit SHARED ${WINZOX_REPAIR_KIT_SOURCES})
target_compile_definitions(winzox_repair_kit PRIVATE
    WINZOX_REPAIR_KIT_BUILD_SHARED
    WINZOX_REPAIR_KIT_API_VERSION="${WINZOX_VERSION_STRING}"
)
target_include_directories(winzox_repair_kit PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${OPENSSL_INCLUDE_DIR}
    ${ZLIB_INCLUDE_DIRS}
)
target_link_libraries(winzox_repair_kit PRIVATE
    OpenSSL::Crypto
    ZLIB::ZLIB
)

if (WIN32)
    set_target_properties(winzox_repair_kit PROPERTIES PREFIX "" OUTPUT_NAME "WinZOXRepairKit")
else()
    set_target_properties(winzox_repair_kit PROPERTIES OUTPUT_NAME "WinZOXRepairKit")
endif()
