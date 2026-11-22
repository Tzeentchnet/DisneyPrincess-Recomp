cmake_minimum_required(VERSION 3.21)

project(DisneyPrincessEnchantedJourneyRecomp LANGUAGES C CXX)

option(
    DISNEY_PRINCESS_ENABLE_RUNNER_IPO
    "Enable whole-program optimization for the retail-scale generated runner"
    OFF
)

set(PS2X_BUILD_RECOMP OFF CACHE BOOL "" FORCE)
set(PS2X_BUILD_ANALYZER OFF CACHE BOOL "" FORCE)
set(PS2X_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(PS2X_BUILD_STUDIO OFF CACHE BOOL "" FORCE)
set(PS2X_VITA_APP_NAME "Disney Princess Recomp" CACHE STRING "" FORCE)
set(PS2X_VITA_TITLEID "DPRE21660" CACHE STRING "" FORCE)

if(CMAKE_SYSTEM_NAME STREQUAL "Vita")
    set(_default_boot_elf "ux0:data/DPRE21660/SLUS_216.60")
    set(_default_cd_image "ux0:data/DPRE21660/disc.iso")
else()
    set(_default_boot_elf "${CMAKE_CURRENT_SOURCE_DIR}/.work/disc/SLUS_216.60")
    set(_default_cd_image "")
endif()
set(PS2X_DEFAULT_BOOT_ELF "${_default_boot_elf}" CACHE STRING "" FORCE)

add_subdirectory(PS2Recomp)

if(NOT DISNEY_PRINCESS_ENABLE_RUNNER_IPO)
    set_target_properties(ps2EntryRunner PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF
        INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO OFF
    )
endif()

# FFmpeg's early user32 entry collides with raylib's same-named public API on MinGW.
if(WIN32 AND MINGW AND TARGET ffmpeg AND TARGET raylib)
    get_target_property(_ffmpeg_link_libraries ffmpeg INTERFACE_LINK_LIBRARIES)
    if(_ffmpeg_link_libraries)
        list(REMOVE_ITEM _ffmpeg_link_libraries user32)
        set_property(
            TARGET ffmpeg
            PROPERTY INTERFACE_LINK_LIBRARIES "${_ffmpeg_link_libraries}"
        )
    endif()
endif()

set(
    DISNEY_PRINCESS_GENERATED_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/.work/generated/game"
    CACHE PATH
    "Directory containing PS2Recomp-generated game sources"
)

set(_generated_headers
    "${DISNEY_PRINCESS_GENERATED_DIR}/ps2_recompiled_functions.h"
    "${DISNEY_PRINCESS_GENERATED_DIR}/ps2_recompiled_stubs.h"
)
set(_generated_registration
    "${DISNEY_PRINCESS_GENERATED_DIR}/register_functions.cpp"
)

foreach(_required_file IN LISTS _generated_headers _generated_registration)
    if(NOT EXISTS "${_required_file}")
        message(FATAL_ERROR
            "Missing generated file: ${_required_file}\n"
            "Run scripts/recompile.ps1 with your legally obtained ISO first.")
    endif()
endforeach()

file(GLOB _generated_sources CONFIGURE_DEPENDS
    "${DISNEY_PRINCESS_GENERATED_DIR}/*.cpp"
)
list(REMOVE_ITEM _generated_sources "${_generated_registration}")

if(NOT _generated_sources)
    message(FATAL_ERROR
        "No recompiled function sources found in ${DISNEY_PRINCESS_GENERATED_DIR}")
endif()

get_target_property(_runner_sources ps2EntryRunner SOURCES)
foreach(_runner_source IN LISTS _runner_sources)
    if(_runner_source MATCHES "register_functions\\.cpp$")
        list(REMOVE_ITEM _runner_sources "${_runner_source}")
    endif()
endforeach()
set_property(TARGET ps2EntryRunner PROPERTY SOURCES "${_runner_sources}")

target_sources(ps2EntryRunner PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/disney_princess_io.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/disney_princess_overrides.cpp"
    ${_generated_sources}
    ${_generated_registration}
    ${_generated_headers}
)
target_include_directories(ps2EntryRunner PRIVATE
    "${DISNEY_PRINCESS_GENERATED_DIR}"
)
target_compile_definitions(ps2EntryRunner PRIVATE
    DISNEY_PRINCESS_DEFAULT_CD_IMAGE="${_default_cd_image}"
)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Vita")
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/disney_princess_io.cpp"
        # Source properties are per directory, and the target is the submodule's.
        TARGET_DIRECTORY ps2EntryRunner
        PROPERTIES
        SKIP_UNITY_BUILD_INCLUSION TRUE
        COMPILE_DEFINITIONS
        "DISNEY_PRINCESS_CD_IMAGE_MARKER=\"${CMAKE_CURRENT_SOURCE_DIR}/.work/disc-image.path\""
    )
endif()

# Without this the runner loads whichever MinGW runtime is first on PATH and
# exits with 0xC0000139 when that is a different toolchain's.
if(WIN32 AND MINGW)
    target_link_options(ps2EntryRunner PRIVATE -static)
endif()
set_source_files_properties(
    "${_generated_registration}"
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION TRUE
)
set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/disney_princess_overrides.cpp"
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION TRUE
)
set_target_properties(ps2EntryRunner PROPERTIES
    OUTPUT_NAME DisneyPrincessRecomp
)
