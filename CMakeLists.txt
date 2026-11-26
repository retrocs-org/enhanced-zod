cmake_minimum_required(VERSION 3.16)
project(zod-enhanced CXX)

# Pin to C++14: the code is pre-C++11, and on Windows `using namespace std`
# (C++17's std::byte) clashes with <windows.h>'s `byte` type ("reference to
# 'byte' is ambiguous"). C++14 has no std::byte, so the collision disappears.
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to a Release (optimized) build.
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# --- dependencies (portable across macOS Homebrew and Linux via pkg-config) ---
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(CURL 7.62 REQUIRED)
# Package public CA roots separately from the build machine's absolute trust path.
set(ZOD_CA_BUNDLE "" CACHE FILEPATH "Public CA trust bundle for portable HTTPS")
if(NOT ZOD_CA_BUNDLE)
  find_file(ZOD_SYSTEM_CA NAMES cert.pem ca-certificates.crt ca-bundle.crt
    PATHS /etc/ssl /etc/ssl/certs /etc/pki/tls/certs
      /opt/homebrew/etc/ca-certificates C:/msys64/mingw64/etc/ssl/certs)
  if(ZOD_SYSTEM_CA)
    set(ZOD_CA_BUNDLE "${ZOD_SYSTEM_CA}")
  endif()
endif()
if(ZOD_CA_BUNDLE)
  configure_file("${ZOD_CA_BUNDLE}" "${CMAKE_CURRENT_SOURCE_DIR}/assets/portal-ca.crt" COPYONLY)
else()
  message(WARNING "No portable public CA bundle found; HTTPS uses libcurl's native trust store. Set ZOD_CA_BUNDLE for OpenSSL-based portable packages.")
endif()
link_libraries(ZLIB::ZLIB)
link_libraries(Threads::Threads)
pkg_check_modules(SDL2       REQUIRED IMPORTED_TARGET sdl3)
pkg_check_modules(SDL2_IMAGE REQUIRED IMPORTED_TARGET sdl3-image)
pkg_check_modules(SDL2_MIXER REQUIRED IMPORTED_TARGET sdl3-mixer>=3.2.4)
pkg_check_modules(SDL2_TTF   REQUIRED IMPORTED_TARGET sdl3-ttf)

# UI fonts are third-party build dependencies, never committed source media.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootstrap_fonts.py"
  RESULT_VARIABLE ZOD_FONT_RESULT)
if(NOT ZOD_FONT_RESULT EQUAL 0)
  message(FATAL_ERROR "Could not bootstrap UI fonts; see packaging/font-sources.json")
endif()

# --- sources ---
# The game is every *.cpp except the separate map-editor / utility / Windows
# files (mirrors the source selection the hand-written makefile used).
file(GLOB ZOD_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(REMOVE_ITEM ZOD_SRCS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/map_editor.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/map_merger.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/map_gen_main.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/zod_tests.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/tile_info_editor.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/win_tray.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/zgfile.cpp")

# Where assets are installed; baked into the binary as a fallback so it can
# find them after `make install` (see chdir_to_data_dir() in main.cpp).
set(ZOD_DATADIR "${CMAKE_INSTALL_PREFIX}/share/zod")

# Release version baked into the binary (startup banner + window title), so
# bug reports unambiguously say what build they are about. From git tags when
# available; override with -DZOD_VERSION=... .
if(NOT ZOD_VERSION AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
  file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" ZOD_VERSION LIMIT_COUNT 1)
endif()
if(NOT ZOD_VERSION)
  execute_process(COMMAND git describe --tags --always --dirty
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE ZOD_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
endif()
if(NOT ZOD_VERSION)
  set(ZOD_VERSION "unknown")
endif()
message(STATUS "zod version: ${ZOD_VERSION}")

# Share identical engine objects between the game, editor and headless tests.
set(ZOD_ENGINE_SRCS ${ZOD_SRCS})
list(REMOVE_ITEM ZOD_ENGINE_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
add_library(zod_engine OBJECT ${ZOD_ENGINE_SRCS})
target_link_libraries(zod_engine PUBLIC CURL::libcurl)
add_executable(zod src/main.cpp)
set_target_properties(zod PROPERTIES OUTPUT_NAME "zod-enhanced")
target_link_libraries(zod PRIVATE zod_engine)

# Functional tests (pathfinding etc.) - links only the pathfinding sources +
# SDL, with stubbed debug-log symbols. Run with ctest or ./build/zod_tests.
add_executable(zod_tests src/zperf.cpp src/zod_tests.cpp src/zpath_finding.cpp src/zpath_finding_astar.cpp)
target_link_libraries(zod_tests PRIVATE PkgConfig::SDL2 m)
enable_testing()
add_test(NAME zod_tests COMMAND zod_tests)

# Headless regressions for save replacement, malformed maps and HTTP deadlines.
add_executable(zod_runtime_driver tests/runtime_driver.cpp src/zhttp.cpp src/zlocalserver.cpp)
target_include_directories(zod_runtime_driver PRIVATE src)
if(WIN32)
  target_link_libraries(zod_runtime_driver PRIVATE ws2_32)
endif()
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME zod_regressions COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/regressions.py" "$<TARGET_FILE:zod>"
    "$<TARGET_FILE:zod_runtime_driver>" "${CMAKE_CURRENT_SOURCE_DIR}/maps/p02_bb_orig01.map")
  set_tests_properties(zod_regressions PROPERTIES TIMEOUT 30)
else()
  message(WARNING "Python 3 was not found; save/network integration tests will not be registered")
endif()

# Standalone random skirmish map generator — no engine code, no SDL; writes
# .map files the game loads like any hand-made map.
add_executable(zod_mapgen src/map_gen.cpp src/map_gen_main.cpp)
if(Python3_Interpreter_FOUND)
  add_test(NAME zod_portal_tls COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/portal_tls_test.py" "$<TARGET_FILE:zod_engine_regressions>")
  set_tests_properties(zod_portal_tls PROPERTIES TIMEOUT 45)
endif()

# Map editor: the engine's own editor, sharing all of the game's map / object
# code (everything except the game's main()). macOS and Linux only for now;
# -DZOD_MAP_EDITOR=OFF skips it (it compiles the engine sources a second time).
if(WIN32 OR ANDROID OR EMSCRIPTEN)
  set(ZOD_MAP_EDITOR_DEFAULT OFF)
else()
  set(ZOD_MAP_EDITOR_DEFAULT ON)
endif()
option(ZOD_MAP_EDITOR "Build zod_map_editor" ${ZOD_MAP_EDITOR_DEFAULT})
if(ZOD_MAP_EDITOR)
  add_executable(zod_map_editor src/map_editor.cpp)
  target_link_libraries(zod_map_editor PRIVATE zod_engine)
  target_compile_definitions(zod_map_editor PRIVATE
    DISABLE_OPENGL DISABLE_REGCHECK DISABLE_MYSQL
    ZOD_DATADIR="${ZOD_DATADIR}" ZOD_VERSION="${ZOD_VERSION}")
  target_compile_options(zod_map_editor PRIVATE -Wno-deprecated -Wno-write-strings)
  target_link_libraries(zod_map_editor PRIVATE
    PkgConfig::SDL2 PkgConfig::SDL2_IMAGE PkgConfig::SDL2_MIXER PkgConfig::SDL2_TTF m)
endif()

target_compile_definitions(zod_engine PUBLIC
  DISABLE_OPENGL
  DISABLE_REGCHECK
  DISABLE_MYSQL
  ZOD_DATADIR="${ZOD_DATADIR}"
  ZOD_VERSION="${ZOD_VERSION}")

target_compile_options(zod_engine PUBLIC -Wno-deprecated -Wno-write-strings)

target_link_libraries(zod_engine PUBLIC
  PkgConfig::SDL2
  PkgConfig::SDL2_IMAGE
  PkgConfig::SDL2_MIXER
  PkgConfig::SDL2_TTF
  m)

# Windows: the engine's client/server code uses Winsock (WSAStartup, socket, …)
# macOS: set the Dock icon at runtime (SDL can't on macOS). A tiny Objective-C++
# file using Cocoa - the *.cpp glob doesn't pick up .mm, so add it explicitly.
if(APPLE)
  enable_language(OBJCXX)
  # zmovie_mac.mm plays the original game's cutscene videos (AVFoundation)
  target_sources(zod_engine PRIVATE src/mac_dock_icon.mm src/zmovie_mac.mm)
  target_link_libraries(zod_engine PUBLIC "-framework Cocoa" "-framework AVFoundation"
                                    "-framework QuartzCore" "-framework CoreMedia")
endif()

# and the IP Helper API (GetAdaptersInfo).
if(WIN32)
  target_link_libraries(zod_engine PUBLIC ws2_32 iphlpapi)
  if(ZOD_MAP_EDITOR)
    target_link_libraries(zod_map_editor PRIVATE ws2_32 iphlpapi)
    target_compile_definitions(zod_map_editor PRIVATE WIN32_LEAN_AND_MEAN)
  endif()
  # Define WIN32_LEAN_AND_MEAN for EVERY translation unit (before any header,
  # incl. SDL's, pulls in <windows.h>). The socket headers now use <winsock2.h>
  # explicitly; without LEAN, a <windows.h> pulled in elsewhere first would drag
  # in the old <winsock.h> (1.1) and clash with winsock2 ("redefinition"). A
  # per-header #define is too late once a full <windows.h> is already in the TU.
  # (It also keeps <rpcndr.h>'s `byte` out, which future-proofs a move past
  # C++14, where std::byte would otherwise be ambiguous under `using namespace std`.)
  target_compile_definitions(zod_engine PUBLIC WIN32_LEAN_AND_MEAN)
endif()

# --- SoundFont for the original MIDI soundtrack ------------------------------
# The 1996 score ships as MIDI (assets/sounds/*.MID) and is played through
# FluidSynth, which needs a General MIDI SoundFont. It is a 31 MB binary, so it
# is not kept in git: configure fetches GeneralUser GS (pinned + checksummed)
# into assets/soundfonts/. Without it the game simply plays the recorded
# tracks. -DZOD_FETCH_SOUNDFONT=OFF skips the download (offline / package
# builds); any .sf2 you drop into assets/soundfonts/ is used instead.
option(ZOD_FETCH_SOUNDFONT "Download the GeneralUser GS SoundFont for MIDI music" ON)
set(ZOD_SOUNDFONT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/soundfonts")
set(ZOD_SOUNDFONT_REV "684543d5e5efaef08d02be50dcda8d552478fa60")
set(ZOD_SOUNDFONT_SHA256 "9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe")
file(GLOB ZOD_HAVE_SOUNDFONT "${ZOD_SOUNDFONT_DIR}/*.sf2" "${ZOD_SOUNDFONT_DIR}/*.sf3")
if(ZOD_FETCH_SOUNDFONT AND NOT ZOD_HAVE_SOUNDFONT)
  message(STATUS "Fetching the GeneralUser GS SoundFont (31 MB) for MIDI music")
  set(ZOD_SF_TMP "${CMAKE_CURRENT_BINARY_DIR}/GeneralUser-GS.sf2.part")
  file(DOWNLOAD
    "https://raw.githubusercontent.com/mrbumpy409/GeneralUser-GS/${ZOD_SOUNDFONT_REV}/GeneralUser-GS.sf2"
    "${ZOD_SF_TMP}" STATUS ZOD_SF_STATUS TIMEOUT 300)
  list(GET ZOD_SF_STATUS 0 ZOD_SF_CODE)
  set(ZOD_SF_HASH "")
  if(ZOD_SF_CODE EQUAL 0)
    file(SHA256 "${ZOD_SF_TMP}" ZOD_SF_HASH)
  endif()
  if(ZOD_SF_HASH STREQUAL ZOD_SOUNDFONT_SHA256)
    file(MAKE_DIRECTORY "${ZOD_SOUNDFONT_DIR}")
    file(RENAME "${ZOD_SF_TMP}" "${ZOD_SOUNDFONT_DIR}/GeneralUser-GS.sf2")
    file(DOWNLOAD
      "https://raw.githubusercontent.com/mrbumpy409/GeneralUser-GS/${ZOD_SOUNDFONT_REV}/documentation/LICENSE.txt"
      "${ZOD_SOUNDFONT_DIR}/GeneralUser-GS-LICENSE.txt" TIMEOUT 60)
  else()
    file(REMOVE "${ZOD_SF_TMP}")
    message(WARNING "Could not fetch the SoundFont (${ZOD_SF_STATUS}). The game will play the "
                    "recorded music instead of the original MIDI score. Put any General MIDI "
                    ".sf2 into assets/soundfonts/ and re-run cmake to enable it.")
  endif()
endif()

# --- macOS: self-contained zod-enhanced.app -------------------------------------------
# Off by default so plain `cmake --install` (Homebrew formula, CI) keeps the
# bin/ + share/zod layout. packaging/macos/build-app.sh turns it on, then runs
# the `app` target (bundle the SDL3 dylibs + code-sign) and optionally `dmg`.
option(ZOD_MACOS_APP "macOS: build a self-contained zod-enhanced.app bundle" OFF)
# Retain the platform identity so renamed app upgrades keep existing permissions.
set(ZOD_BUNDLE_ID "org.retrocs.zod-ee" CACHE STRING "macOS bundle identifier")
if(ZOD_BUNDLE_ID STREQUAL "org.zodengine.zod" OR ZOD_BUNDLE_ID STREQUAL "org.openzee.OpenZEE")
  set(ZOD_BUNDLE_ID "org.retrocs.zod-ee" CACHE STRING "macOS bundle identifier" FORCE)
endif()
set(ZOD_MIN_MACOS "13.0" CACHE STRING "LSMinimumSystemVersion written into Info.plist")

if(APPLE AND ZOD_MACOS_APP)
  set(ZOD_IMPORTER_PYTHON "${CMAKE_CURRENT_SOURCE_DIR}/build-importer-venv/bin/python3" CACHE FILEPATH "Python with packaging/importer/requirements.txt installed")
  set(ZOD_ICNS "${CMAKE_CURRENT_BINARY_DIR}/Zod.icns")
  add_custom_command(OUTPUT "${ZOD_ICNS}"
    COMMAND "${ZOD_IMPORTER_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/z_project_art.py"
            "${CMAKE_CURRENT_BINARY_DIR}/Zod-icon.png"
    COMMAND /bin/sh "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/make-icns.sh"
            "${CMAKE_CURRENT_BINARY_DIR}/Zod-icon.png" "${ZOD_ICNS}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/z_project_art.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/make-icns.sh"
    COMMENT "Generating Zod.icns")
  target_sources(zod PRIVATE "${ZOD_ICNS}")
  set_source_files_properties("${ZOD_ICNS}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

  string(REGEX MATCH "[0-9]+\\.[0-9]+(\\.[0-9]+)?" ZOD_SHORT_VERSION "${ZOD_VERSION}")
  if(NOT ZOD_SHORT_VERSION)
    set(ZOD_SHORT_VERSION "0.0.0")
  endif()

  set_target_properties(zod PROPERTIES
    MACOSX_BUNDLE TRUE
    OUTPUT_NAME "zod-enhanced"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in"
    MACOSX_BUNDLE_BUNDLE_NAME "zod-enhanced"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${ZOD_BUNDLE_ID}"
    MACOSX_BUNDLE_ICON_FILE "Zod.icns"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${ZOD_SHORT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${ZOD_SHORT_VERSION}"
    MACOSX_BUNDLE_LONG_VERSION_STRING "zod-enhanced ${ZOD_VERSION}"
    MACOSX_BUNDLE_COPYRIGHT "GPLv3. Zod Engine by Michael Bok / Nighsoft; SDL3 port by fenio.")

  # game data lives in Contents/Resources (main.cpp looks there first)
  set(ZOD_RES "$<TARGET_BUNDLE_CONTENT_DIR:zod>/Resources")
  add_custom_command(TARGET zod POST_BUILD
    COMMAND ${CMAKE_COMMAND} "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}" "-DDESTINATION=${ZOD_RES}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/copy-resources.cmake"
    COMMENT "Copying game data into zod-enhanced.app")

  # App packaging must refresh data even when the binary did not relink.
  add_custom_target(macos-resources
    COMMAND ${CMAKE_COMMAND} "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}" "-DDESTINATION=${ZOD_RES}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/copy-resources.cmake"
    DEPENDS zod
    COMMENT "Refreshing zod-enhanced.app resources and removing imported bitmaps"
    VERBATIM)

  # the map editor rides along inside the bundle, next to the game binary
  if(ZOD_MAP_EDITOR)
    add_dependencies(zod zod_map_editor)
    add_custom_command(TARGET zod POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:zod_map_editor>"
              "$<TARGET_BUNDLE_CONTENT_DIR:zod>/MacOS/zod_map_editor"
      COMMENT "Copying the map editor into zod-enhanced.app")
  endif()

  add_custom_target(asset-importer
    COMMAND "${ZOD_IMPORTER_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/packaging/importer/build.py"
            --destination "$<TARGET_BUNDLE_CONTENT_DIR:zod>/MacOS"
    DEPENDS macos-resources
    COMMENT "Bundling the offline disc importer and media converters"
    VERBATIM)
  add_custom_target(app
    COMMAND /bin/bash "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/bundle-dylibs.sh" "$<TARGET_BUNDLE_DIR:zod>"
    DEPENDS asset-importer
    COMMENT "Bundling SDL3 libraries into zod-enhanced.app and code-signing it"
    VERBATIM)

  add_custom_target(dmg
    COMMAND /bin/sh "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/make-dmg.sh"
            "$<TARGET_BUNDLE_DIR:zod>" "${CMAKE_CURRENT_BINARY_DIR}/zod-enhanced-${ZOD_VERSION}-macOS-arm64.dmg"
    DEPENDS app
    COMMENT "Creating the zod-enhanced disk image"
    VERBATIM)
endif()

# --- install (binary -> bin/, game data -> share/zod/) ---
install(TARGETS zod RUNTIME DESTINATION bin BUNDLE DESTINATION .)
if(ZOD_MAP_EDITOR)
  install(TARGETS zod_map_editor RUNTIME DESTINATION bin)
endif()
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/packaging/install-assets.cmake.in"
               "${CMAKE_CURRENT_BINARY_DIR}/install-assets.cmake" @ONLY)
install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/install-assets.cmake")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/maps/README.md" DESTINATION share/zod/maps)
install(FILES     "${CMAKE_CURRENT_SOURCE_DIR}/map_list.txt" DESTINATION share/zod)

# Exercise the real server save/load code and packet decoder without graphics.
add_executable(zod_engine_regressions tests/engine_regressions.cpp)
target_include_directories(zod_engine_regressions PRIVATE src)
target_link_libraries(zod_engine_regressions PRIVATE zod_engine)
if(Python3_Interpreter_FOUND)
  add_test(NAME zod_engine_regressions COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/engine_regressions.py"
    "$<TARGET_FILE:zod>" "$<TARGET_FILE:zod_engine_regressions>"
    "${CMAKE_CURRENT_SOURCE_DIR}")
  set_tests_properties(zod_engine_regressions PROPERTIES TIMEOUT 120)
endif()
