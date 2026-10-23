# SPDX-License-Identifier: GPL-3.0-only
# Prysm as a CLAP plugin (macOS, Windows, Linux):
#   cmake -B build/plugin -S plugin -DCMAKE_BUILD_TYPE=Release && cmake --build build/plugin --config Release
# -> build/plugin/Prysm.clap. The editor page comes from site/editor/ (emu/build.sh).
cmake_minimum_required(VERSION 3.22)
project(PrysmCLAP VERSION 1.2.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET 10.15 CACHE STRING "")
set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

include(FetchContent)
FetchContent_Declare(clap GIT_REPOSITORY https://github.com/free-audio/clap.git GIT_TAG 1.2.10 GIT_SHALLOW ON
                     SOURCE_SUBDIR none)
FetchContent_Declare(choc GIT_REPOSITORY https://github.com/Tracktion/choc.git
                     GIT_TAG c0d3a1ba52af7b2a877675495973391808a7a768 SOURCE_SUBDIR none)
set(RTMIDI_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(RTMIDI_API_JACK OFF CACHE BOOL "" FORCE)
set(RTMIDI_BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(rtmidi GIT_REPOSITORY https://github.com/thestk/rtmidi.git GIT_TAG 6.0.0 GIT_SHALLOW ON)
FetchContent_MakeAvailable(clap choc rtmidi)

# the firmware, as a library (plugin/core)
add_library(prysm_core STATIC core/core.c)
target_include_directories(prysm_core PRIVATE ${ROOT}/firmware/src ${ROOT}/firmware/gen PUBLIC core)
target_compile_definitions(prysm_core PRIVATE DINGHY_HOST)
if(MSVC)
    target_compile_options(prysm_core PRIVATE /w)
else()
    target_compile_options(prysm_core PRIVATE -w -O2)
endif()

# the editor page, inside the plugin
set(EDITOR ${ROOT}/site/editor)
set(ASSETS ${CMAKE_CURRENT_BINARY_DIR}/assets.cpp)
add_custom_command(OUTPUT ${ASSETS}
    COMMAND ${CMAKE_COMMAND} -DSRC=${EDITOR} -DOUT=${ASSETS} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
    DEPENDS ${EDITOR}/index.html ${EDITOR}/editor.js ${EDITOR}/prysm.js ${EDITOR}/prysm.wasm cmake/embed.cmake)

add_library(Prysm MODULE src/plugin.cpp src/fm1link.cpp ${ASSETS})
target_include_directories(Prysm PRIVATE src ${clap_SOURCE_DIR}/include ${choc_SOURCE_DIR})
target_compile_definitions(Prysm PRIVATE PRYSM_PLUGIN_VERSION="${PROJECT_VERSION}")
target_link_libraries(Prysm PRIVATE prysm_core rtmidi)
set_target_properties(Prysm PROPERTIES CXX_VISIBILITY_PRESET hidden PREFIX "")

if(APPLE)
    set_target_properties(Prysm PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION clap
        MACOSX_BUNDLE_GUI_IDENTIFIER com.nicholaspratt.prysm MACOSX_BUNDLE_BUNDLE_NAME Prysm
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION} MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
    target_link_libraries(Prysm PRIVATE "-framework WebKit" "-framework Cocoa" "-framework CoreMIDI"
                          "-framework CoreAudio" "-framework CoreFoundation")
elseif(WIN32)
    set_target_properties(Prysm PROPERTIES SUFFIX ".clap")
    target_compile_definitions(Prysm PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(Prysm PRIVATE winmm)
else()
    set_target_properties(Prysm PROPERTIES SUFFIX ".clap")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(gtk3 REQUIRED IMPORTED_TARGET gtk+-3.0)
    pkg_check_modules(webkit2 REQUIRED IMPORTED_TARGET webkit2gtk-4.1)
    pkg_check_modules(x11 REQUIRED IMPORTED_TARGET x11)
    target_link_libraries(Prysm PRIVATE PkgConfig::gtk3 PkgConfig::webkit2 PkgConfig::x11)
endif()
