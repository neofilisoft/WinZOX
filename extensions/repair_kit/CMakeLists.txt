set(WINZOX_REPAIR_KIT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/repair_kit.cpp
)

add_library(winzox_repair_kit SHARED ${WINZOX_REPAIR_KIT_SOURCES})
target_compile_definitions(winzox_repair_kit PRIVATE
    WINZOX_REPAIR_KIT_BUILD_SHARED
    WINZOX_REPAIR_KIT_API_VERSION="${WINZOX_VERSION_STRING}"
)
target_include_directories(winzox_repair_kit PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if (WIN32)
    set_target_properties(winzox_repair_kit PROPERTIES PREFIX "" OUTPUT_NAME "WinZOXRepairKit")
else()
    set_target_properties(winzox_repair_kit PROPERTIES OUTPUT_NAME "WinZOXRepairKit")
endif()
