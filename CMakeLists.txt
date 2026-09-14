cmake_minimum_required(VERSION 3.20)

project(DOOM3DO
    VERSION 1.0.0
    LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# -----------------------------------------------------------------------------
# Dependencies
# -----------------------------------------------------------------------------

find_package(SDL3 CONFIG REQUIRED)

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

    # Build a native macOS application bundle.
    add_executable(doom3do MACOSX_BUNDLE
        ${DOOM3DO_SOURCE_FILES}
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
)

# -----------------------------------------------------------------------------
# Compiler warnings
# -----------------------------------------------------------------------------

target_compile_options(doom3do PRIVATE
    $<$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>:-Wall;-Wextra;>
)

# -----------------------------------------------------------------------------
# Libraries
# -----------------------------------------------------------------------------

target_link_libraries(doom3do PRIVATE
    SDL3::SDL3
    m
    WildMidi
)

# -----------------------------------------------------------------------------
# macOS application bundle properties
# -----------------------------------------------------------------------------

if(APPLE)

    set_target_properties(doom3do PROPERTIES
        OUTPUT_NAME "DOOM3DO"
        MACOSX_BUNDLE_BUNDLE_NAME "DOOM3DO"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.doom3do.doom3do"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )

endif()

# -----------------------------------------------------------------------------
# Installation
# -----------------------------------------------------------------------------

include(GNUInstallDirs)

if(WIN32)

    # Windows:
    # Install the executable at the root of the package.
    install(TARGETS doom3do
        RUNTIME DESTINATION .
    )

    # If SDL3 exposes runtime artifacts through its imported target, install
    # them alongside the executable.
    if(TARGET SDL3::SDL3)
        install(IMPORTED_RUNTIME_ARTIFACTS SDL3::SDL3
            DESTINATION .
        )
    endif()

elseif(APPLE)

    # macOS:
    # Install the native .app bundle.
    install(TARGETS doom3do
        BUNDLE DESTINATION .
        RUNTIME DESTINATION .
    )

else()

    # Linux:
    # Install the raw native executable.
    install(TARGETS doom3do
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )

endif()

# -----------------------------------------------------------------------------
# CPack
#
# Windows -> ZIP
# macOS   -> DMG containing the .app
# Linux   -> TGZ fallback (normal builds still produce raw doom3do ELF)
# -----------------------------------------------------------------------------

set(CPACK_PACKAGE_NAME "DOOM3DO")
set(CPACK_PACKAGE_VENDOR "DOOM3DO")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "DOOM3DO")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")

if(WIN32)

    set(CPACK_GENERATOR "ZIP")

    set(CPACK_ARCHIVE_FILE_NAME
        "DOOM3DO-${PROJECT_VERSION}-win64"
    )

elseif(APPLE)

    set(CPACK_GENERATOR "DragNDrop")

    set(CPACK_DMG_VOLUME_NAME
        "DOOM3DO"
    )

    set(CPACK_PACKAGE_FILE_NAME
        "DOOM3DO-${PROJECT_VERSION}-macOS"
    )

else()

    # Linux remains primarily a raw binary build.
    # TGZ is merely available as an optional CPack distribution archive.
    set(CPACK_GENERATOR "TGZ")

    set(CPACK_PACKAGE_FILE_NAME
        "DOOM3DO-${PROJECT_VERSION}-Linux"
    )

endif()

include(CPack)

