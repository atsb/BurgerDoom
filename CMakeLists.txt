cmake_minimum_required(VERSION 3.20)

project(DOOM3DO
    VERSION 2.0.0
    LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# -----------------------------------------------------------------------------
# Dependency roots
#
# These can be supplied when configuring CMake, for example:
#
#   cmake -S . -B build ^
#       -DSDL3_ROOT=C:/Libraries/SDL3 ^
#       -DWILDMIDI_ROOT=C:/Libraries/WildMidi
#
# Or from the CMake GUI.
# -----------------------------------------------------------------------------

set(SDL3_ROOT "" CACHE PATH
    "Root directory of the SDL3 installation"
)

set(WILDMIDI_ROOT "" CACHE PATH
    "Root directory of the WildMidi installation"
)

# -----------------------------------------------------------------------------
# SDL3
# -----------------------------------------------------------------------------

if(SDL3_ROOT)

    message(STATUS
        "DOOM3DO: Using SDL3 root: ${SDL3_ROOT}"
    )

    list(PREPEND CMAKE_PREFIX_PATH
        "${SDL3_ROOT}"
    )

endif()

find_package(SDL3 CONFIG QUIET)

if(NOT SDL3_FOUND)

    message(FATAL_ERROR
        "\n"
        "============================================================\n"
        " SDL3 was not found.\n"
        "============================================================\n"
        "\n"
        "Please provide the SDL3 installation directory using:\n"
        "\n"
        "  -DSDL3_ROOT=<SDL3 installation directory>\n"
        "\n"
        "Example:\n"
        "\n"
        "  -DSDL3_ROOT=C:/Libraries/SDL3\n"
        "\n"
    )

endif()

message(STATUS "DOOM3DO: SDL3 found")

# -----------------------------------------------------------------------------
# Verify the SDL3 target
# -----------------------------------------------------------------------------

if(TARGET SDL3::SDL3)

    message(STATUS "DOOM3DO: SDL3 target: SDL3::SDL3")

elseif(TARGET SDL3)

    message(STATUS "DOOM3DO: SDL3 target: SDL3")

else()

    message(FATAL_ERROR
        "\n"
        "SDL3 was found, but no usable SDL3 target was provided.\n"
        "\n"
        "Expected one of:\n"
        "  SDL3::SDL3\n"
        "  SDL3\n"
        "\n"
    )

endif()

# Select the SDL3 target used by the executable.

if(TARGET SDL3::SDL3)

    set(DOOM3DO_SDL3_TARGET SDL3::SDL3)

else()

    set(DOOM3DO_SDL3_TARGET SDL3)

endif()

# -----------------------------------------------------------------------------
# WildMidi
# -----------------------------------------------------------------------------

if(WILDMIDI_ROOT)

    message(STATUS
        "DOOM3DO: Using WildMidi root: ${WILDMIDI_ROOT}"
    )

    # Header directory.

    find_path(WILDMIDI_INCLUDE_DIR
        NAMES
            wildmidi_lib.h
            wildmidi.h
        PATHS
            "${WILDMIDI_ROOT}/include"
            "${WILDMIDI_ROOT}/include/wildmidi"
        NO_DEFAULT_PATH
    )

    # Library directory.

    find_library(WILDMIDI_LIBRARY
        NAMES
            WildMidi
            wildmidi
            LibWildMidi
        PATHS
            "${WILDMIDI_ROOT}/lib"
            "${WILDMIDI_ROOT}/lib64"
            "${WILDMIDI_ROOT}/lib/x64"
            "${WILDMIDI_ROOT}/lib/Release"
            "${WILDMIDI_ROOT}/lib/Debug"
            "${WILDMIDI_ROOT}/lib/x64/Release"
            "${WILDMIDI_ROOT}/lib/x64/Debug"
        NO_DEFAULT_PATH
    )

else()

    message(STATUS
        "DOOM3DO: Searching system paths for WildMidi"
    )

    find_path(WILDMIDI_INCLUDE_DIR
        NAMES
            wildmidi_lib.h
            wildmidi.h
    )

    find_library(WILDMIDI_LIBRARY
        NAMES
            WildMidi
            wildmidi
            LibWildMidi
    )

endif()

# -----------------------------------------------------------------------------
# Validate WildMidi
# -----------------------------------------------------------------------------

if(NOT WILDMIDI_INCLUDE_DIR)

    message(FATAL_ERROR
        "\n"
        "============================================================\n"
        " WildMidi headers were not found.\n"
        "============================================================\n"
        "\n"
        "Please provide the WildMidi installation directory using:\n"
        "\n"
        "  -DWILDMIDI_ROOT=<WildMidi installation directory>\n"
        "\n"
        "Example:\n"
        "\n"
        "  -DWILDMIDI_ROOT=C:/Libraries/WildMidi\n"
        "\n"
    )

endif()

if(NOT WILDMIDI_LIBRARY)

    message(FATAL_ERROR
        "\n"
        "============================================================\n"
        " WildMidi library was not found.\n"
        "============================================================\n"
        "\n"
        "Expected a library named one of:\n"
        "\n"
        "  WildMidi\n"
        "  wildmidi\n"
        "  LibWildMidi\n"
        "\n"
        "Please check WILDMIDI_ROOT.\n"
        "\n"
    )

endif()

message(STATUS
    "DOOM3DO: WildMidi include: ${WILDMIDI_INCLUDE_DIR}"
)

message(STATUS
    "DOOM3DO: WildMidi library: ${WILDMIDI_LIBRARY}"
)

# -----------------------------------------------------------------------------
# Create imported WildMidi target
# -----------------------------------------------------------------------------

if(NOT TARGET WildMidi::WildMidi)

    add_library(WildMidi::WildMidi UNKNOWN IMPORTED)

    set_target_properties(WildMidi::WildMidi PROPERTIES
        IMPORTED_LOCATION
            "${WILDMIDI_LIBRARY}"

        INTERFACE_INCLUDE_DIRECTORIES
            "${WILDMIDI_INCLUDE_DIR}"
    )

endif()

# -----------------------------------------------------------------------------
# Locate WildMidi runtime DLL on Windows
# -----------------------------------------------------------------------------

if(WIN32)

    find_file(WILDMIDI_RUNTIME
        NAMES
            WildMidi.dll
            wildmidi.dll
            LibWildMidi.dll
        PATHS
            "${WILDMIDI_ROOT}/bin"
            "${WILDMIDI_ROOT}/bin/x64"
            "${WILDMIDI_ROOT}/bin/Release"
            "${WILDMIDI_ROOT}/bin/Debug"
            "${WILDMIDI_ROOT}/bin/x64/Release"
            "${WILDMIDI_ROOT}/bin/x64/Debug"
            "${WILDMIDI_ROOT}/lib"
            "${WILDMIDI_ROOT}/lib/x64"
        NO_DEFAULT_PATH
    )

    if(WILDMIDI_RUNTIME)

        message(STATUS
            "DOOM3DO: WildMidi runtime: ${WILDMIDI_RUNTIME}"
        )

    else()

        message(WARNING
            "DOOM3DO: WildMidi DLL was not found. "
            "The executable may require the DLL to be supplied manually."
        )

    endif()

endif()

# -----------------------------------------------------------------------------
# Source files
# -----------------------------------------------------------------------------

set(DOOM3DO_SOURCE_FILES

    source/ammain.c
    source/base.c
    source/ceilng.c
    source/change.c
    source/data.c
    source/dmain.c
    source/doors.c
    source/enemy.c
    source/floor.c
    source/fmain.c
    source/game.c
    source/info.c
    source/inmain.c
    source/inter.c
    source/lights.c
    source/map.c
    source/maputl.c
    source/mmain.c
    source/mobj.c
    source/move.c
    source/omain.c
    source/phase1.c
    source/phase2.c
    source/phase6.c
    source/phase7.c
    source/phase8.c
    source/plats.c
    source/pspr.c
    source/rdata.c
    source/rmain.c
    source/setup.c
    source/shoot.c
    source/sight.c
    source/slide.c
    source/sound.c
    source/spec.c
    source/stmain.c
    source/switch.c
    source/tables.c
    source/telept.c
    source/tick.c
    source/user.c
    source/threedo.c
    source/celutils.c
    source/ResourceMgr.c
)

# -----------------------------------------------------------------------------
# Executable
# -----------------------------------------------------------------------------

if(APPLE)

    # Native macOS application bundle.

    add_executable(doom3do MACOSX_BUNDLE
        ${DOOM3DO_SOURCE_FILES}
    )

elseif(WIN32)

    # Native Windows GUI executable.
    #
    # doom3do.rc contains:
    #   - Application icon
    #   - File version information
    #   - Product information
    #   - Copyright information

    add_executable(doom3do WIN32
        ${DOOM3DO_SOURCE_FILES}
        doom3do.rc
    )

else()

    add_executable(doom3do
        ${DOOM3DO_SOURCE_FILES}
    )

endif()

# -----------------------------------------------------------------------------
# Include directories
# -----------------------------------------------------------------------------

target_include_directories(doom3do PRIVATE

    ${CMAKE_CURRENT_SOURCE_DIR}/source

    ${WILDMIDI_INCLUDE_DIR}

)

# -----------------------------------------------------------------------------
# Compiler warnings
# -----------------------------------------------------------------------------

target_compile_options(doom3do PRIVATE

    $<$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>:
        -Wall;
        -Wextra;
    >

)

# -----------------------------------------------------------------------------
# Libraries
# -----------------------------------------------------------------------------

target_link_libraries(doom3do PRIVATE

    ${DOOM3DO_SDL3_TARGET}

    WildMidi::WildMidi

)

# -----------------------------------------------------------------------------
# Linux math library
# -----------------------------------------------------------------------------

if(UNIX AND NOT APPLE)

    target_link_libraries(doom3do PRIVATE
        m
    )

endif()

# -----------------------------------------------------------------------------
# Windows linker settings
#
# Build as a GUI application without a console window while retaining:
#
#     int main(int argc, char **argv)
#
# No WinMain() wrapper is required.
# -----------------------------------------------------------------------------

if(MSVC)

    target_link_options(doom3do PRIVATE
        "/SUBSYSTEM:WINDOWS"
        "/ENTRY:mainCRTStartup"
    )

endif()

# -----------------------------------------------------------------------------
# macOS application bundle properties
# -----------------------------------------------------------------------------

if(APPLE)

    set_target_properties(doom3do PROPERTIES

        OUTPUT_NAME
            "DOOM3DO"

        MACOSX_BUNDLE_BUNDLE_NAME
            "DOOM3DO"

        MACOSX_BUNDLE_GUI_IDENTIFIER
            "com.doom3do.doom3do"

        MACOSX_BUNDLE_BUNDLE_VERSION
            "${PROJECT_VERSION}"

        MACOSX_BUNDLE_SHORT_VERSION_STRING
            "${PROJECT_VERSION}"

    )

endif()

# -----------------------------------------------------------------------------
# Installation
# -----------------------------------------------------------------------------

include(GNUInstallDirs)

if(WIN32)

    # -------------------------------------------------------------------------
    # Windows executable
    # -------------------------------------------------------------------------

    install(TARGETS doom3do
        RUNTIME DESTINATION .
    )

    # -------------------------------------------------------------------------
    # SDL3 runtime DLL
    #
    # Use TARGET_FILE rather than IMPORTED_RUNTIME_ARTIFACTS because
    # SDL3::SDL3 may be an ALIAS target.
    # -------------------------------------------------------------------------

    install(FILES
        "$<TARGET_FILE:${DOOM3DO_SDL3_TARGET}>"
        DESTINATION .
    )

    # -------------------------------------------------------------------------
    # WildMidi runtime DLL
    # -------------------------------------------------------------------------

    if(WILDMIDI_RUNTIME)

        install(FILES
            "${WILDMIDI_RUNTIME}"
            DESTINATION .
        )

    endif()

elseif(APPLE)

    # -------------------------------------------------------------------------
    # macOS application bundle
    # -------------------------------------------------------------------------

    install(TARGETS doom3do
        BUNDLE DESTINATION .
        RUNTIME DESTINATION .
    )

else()

    # -------------------------------------------------------------------------
    # Linux executable
    # -------------------------------------------------------------------------

    install(TARGETS doom3do
        RUNTIME DESTINATION
            "${CMAKE_INSTALL_BINDIR}"
    )

endif()

# -----------------------------------------------------------------------------
# CPack
#
# Windows -> ZIP
# macOS   -> DMG containing the .app
# Linux   -> TGZ
# -----------------------------------------------------------------------------

set(CPACK_PACKAGE_NAME
    "DOOM3DO"
)

set(CPACK_PACKAGE_VENDOR
    "DOOM3DO"
)

set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "DOOM3DO"
)

set(CPACK_PACKAGE_VERSION
    "${PROJECT_VERSION}"
)

# -----------------------------------------------------------------------------
# Windows CPack
# -----------------------------------------------------------------------------

if(WIN32)

    set(CPACK_GENERATOR
        "ZIP"
    )

    set(CPACK_ARCHIVE_FILE_NAME
        "DOOM3DO-${PROJECT_VERSION}-win64"
    )

# -----------------------------------------------------------------------------
# macOS CPack
# -----------------------------------------------------------------------------

elseif(APPLE)

    set(CPACK_GENERATOR
        "DragNDrop"
    )

    set(CPACK_DMG_VOLUME_NAME
        "DOOM3DO"
    )

    set(CPACK_PACKAGE_FILE_NAME
        "DOOM3DO-${PROJECT_VERSION}-macOS"
    )

# -----------------------------------------------------------------------------
# Linux CPack
# -----------------------------------------------------------------------------

else()

    set(CPACK_GENERATOR
        "TGZ"
    )

    set(CPACK_PACKAGE_FILE_NAME
        "DOOM3DO-${PROJECT_VERSION}-Linux"
    )

endif()

include(CPack)