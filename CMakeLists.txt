# ttptt — TIM <-> PNG converter for PlayStation 1
# Copyright (C) 2026 pav13
# SPDX-License-Identifier: GPL-3.0-or-later


cmake_minimum_required(VERSION 3.5)
project(ttptt VERSION 0.0 LANGUAGES CXX)

# RC is only enabled where a resource compiler exists.
if(WIN32)
    enable_language(RC)
endif()

# =========================================================================
# Paths
# =========================================================================
set(TTPTT_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src" CACHE PATH
    "Directory containing project sources")

# Local Qt SDK: placed next to this CMakeLists.txt, git-ignored.
set(TTPTT_QT_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/qt" CACHE PATH
    "Local Qt SDK root (contains bin/, lib/, plugins/)")

if(EXISTS "${TTPTT_QT_DIR}")
    set(CMAKE_PREFIX_PATH "${TTPTT_QT_DIR};${CMAKE_PREFIX_PATH}")
    message(STATUS "Local Qt SDK:    ${TTPTT_QT_DIR}")
elseif(WIN32)
    # Windows builds expect the local SDK shipped with the project.
    message(FATAL_ERROR
        "Local Qt SDK not found at:\n  ${TTPTT_QT_DIR}\n"
        "Place it next to CMakeLists.txt or pass -DTTPTT_QT_DIR=<path>.")
endif()
message(STATUS "Project sources: ${TTPTT_SRC_DIR}")

# version.txt lives next to CMakeLists.txt
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" APP_VERSION_RAW)
string(STRIP "${APP_VERSION_RAW}" APP_VERSION)
add_definitions(-DAPP_VERSION="${APP_VERSION}")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(WIN32)
    add_definitions(-DUNICODE -D_UNICODE)
endif()

# =========================================================================
# Build options
# =========================================================================
option(TTPTT_LTO       "Enable link-time optimization for project code"  ON)
option(TTPTT_STRIP     "Run strip --strip-all on the executable"         ON)
option(TTPTT_UPX       "Pack the executable with UPX"                    OFF)
option(TTPTT_NO_DEBUG  "Define QT_NO_DEBUG_OUTPUT and QT_NO_DEBUG"       ON)
option(TTPTT_NO_OPENGL "Set if the Qt SDK was built with -no-opengl"     OFF)
option(TTPTT_DROP_QICO "Do not link qico (use PNG icon instead)"         OFF)

set(TTPTT_UPX_MODE "best" CACHE STRING "UPX mode: fast | best | brute")
set_property(CACHE TTPTT_UPX_MODE PROPERTY STRINGS fast best brute)

# =========================================================================
# MinGW: section GC + LTO + Os + static runtime
# =========================================================================
if(MINGW)
    set(CMAKE_CXX_FLAGS
        "${CMAKE_CXX_FLAGS} -finput-charset=UTF-8 -fexec-charset=UTF-8")
    set(CMAKE_CXX_FLAGS
        "${CMAKE_CXX_FLAGS} -ffunction-sections -fdata-sections")

    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Os -DNDEBUG")
    if(TTPTT_LTO)
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -flto")
    endif()

    set(CMAKE_EXE_LINKER_FLAGS
        "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++ -s")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE
        "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -Wl,--gc-sections -Wl,-O2")
    if(TTPTT_LTO)
        set(CMAKE_EXE_LINKER_FLAGS_RELEASE
            "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -flto")
    endif()

    set(CMAKE_RC_FLAGS
        "${CMAKE_RC_FLAGS} -I\"${TTPTT_SRC_DIR}/resources\"")
endif()

if(TTPTT_NO_DEBUG)
    add_definitions(-DQT_NO_DEBUG_OUTPUT -DQT_NO_DEBUG)
endif()

# =========================================================================
# Qt5
# =========================================================================
find_package(Qt5 5.6 REQUIRED COMPONENTS Core Gui Widgets)

# The "local static Qt SDK" layout assumptions only make sense on Windows.
if(WIN32)
    get_target_property(_qt_core_loc Qt5::Core LOCATION)
    if(NOT _qt_core_loc)
        message(FATAL_ERROR "Could not locate Qt5Core.")
    endif()
    get_filename_component(QT_LIB_DIR "${_qt_core_loc}" DIRECTORY)
    get_filename_component(QT_ROOT   "${QT_LIB_DIR}" DIRECTORY)
    message(STATUS "Qt lib dir: ${QT_LIB_DIR}")
    message(STATUS "Qt root:    ${QT_ROOT}")
endif()

# =========================================================================
# Sources
# =========================================================================
set(TTPTT_SOURCES
    "${TTPTT_SRC_DIR}/main.cpp"
    "${TTPTT_SRC_DIR}/timformat.cpp"
    "${TTPTT_SRC_DIR}/timcodec.cpp"
    "${TTPTT_SRC_DIR}/palettes.cpp"
    "${TTPTT_SRC_DIR}/logwindow.cpp"
    "${TTPTT_SRC_DIR}/placeholder.cpp"
    "${TTPTT_SRC_DIR}/aboutdialog.cpp"
    "${TTPTT_SRC_DIR}/batchworker.cpp"
    "${TTPTT_SRC_DIR}/timhelpers.cpp"
    "${TTPTT_SRC_DIR}/fileassoc.cpp"
    "${TTPTT_SRC_DIR}/themes.cpp"
    "${TTPTT_SRC_DIR}/mainwindow.cpp"
    "${TTPTT_SRC_DIR}/resources/resources.qrc"
)

if(WIN32)
    list(APPEND TTPTT_SOURCES "${TTPTT_SRC_DIR}/resources/win_icon.rc")
    add_executable(ttptt WIN32 ${TTPTT_SOURCES})
else()
    add_executable(ttptt ${TTPTT_SOURCES})
endif()

target_link_libraries(ttptt PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets)

# =========================================================================
# Static Qt components (Windows/MinGW-only paths)
# Each component is linked only if the corresponding file exists.
# =========================================================================
if(WIN32)
    function(link_static_qt_prv name path)
        if(EXISTS "${path}")
            target_link_libraries(ttptt PRIVATE "${path}")
            message(STATUS "  + ${name}: ${path}")
        else()
            message(STATUS "  - ${name}: not found, skipped")
        endif()
    endfunction()

    message(STATUS "Static Qt components:")
    link_static_qt_prv("PCRE"            "${QT_LIB_DIR}/libqtpcre.a")
    link_static_qt_prv("HarfBuzz"        "${QT_LIB_DIR}/libqtharfbuzzng.a")
    link_static_qt_prv("qwindows"        "${QT_ROOT}/plugins/platforms/libqwindows.a")
    if(NOT TTPTT_DROP_QICO)
        link_static_qt_prv("qico"        "${QT_ROOT}/plugins/imageformats/libqico.a")
    endif()
    link_static_qt_prv("PlatformSupport" "${QT_LIB_DIR}/libQt5PlatformSupport.a")
    link_static_qt_prv("FreeType"        "${QT_LIB_DIR}/libqtfreetype.a")
endif()

# =========================================================================
# Windows system libraries
# =========================================================================
if(WIN32)
    set(_sys_libs shell32 ws2_32 winmm imm32)
    if(NOT TTPTT_NO_OPENGL)
        list(APPEND _sys_libs opengl32)
    endif()
    target_link_libraries(ttptt PRIVATE ${_sys_libs})
endif()

if(MINGW AND NOT EXISTS "${QT_ROOT}/plugins/platforms/libqwindows.a")
    message(FATAL_ERROR
        "Platform plugin not found:\n  ${QT_ROOT}/plugins/platforms/libqwindows.a")
endif()

# =========================================================================
# Post-build: strip + UPX
# =========================================================================
if(MINGW)
    if(TTPTT_STRIP AND CMAKE_STRIP)
        add_custom_command(TARGET ttptt POST_BUILD
            COMMAND ${CMAKE_STRIP} --strip-all $<TARGET_FILE:ttptt>
            COMMENT "Stripping ttptt.exe")
    endif()

    if(TTPTT_UPX)
        find_program(UPX_EXECUTABLE NAMES upx upx.exe)
        if(NOT UPX_EXECUTABLE)
            message(WARNING "UPX not found in PATH; packing skipped.")
        else()
            if(TTPTT_UPX_MODE STREQUAL "fast")
                set(_upx_opts --fast)
            elseif(TTPTT_UPX_MODE STREQUAL "brute")
                set(_upx_opts --best --ultra-brute)
            else()
                set(_upx_opts --best --lzma)
            endif()
            message(STATUS "UPX: ${UPX_EXECUTABLE} (mode=${TTPTT_UPX_MODE})")
            add_custom_command(TARGET ttptt POST_BUILD
                COMMAND ${UPX_EXECUTABLE} ${_upx_opts} $<TARGET_FILE:ttptt>
                COMMENT "UPX compressing ttptt.exe (${TTPTT_UPX_MODE})")
        endif()
    endif()
endif()