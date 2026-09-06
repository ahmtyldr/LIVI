# livi-ui: the LVGL front end of LIVI. A plain Wayland client that draws into
# the nested livi-compositor (title/app_id dev.f-io.livi → main screen) and
# talks to the main process over the NDJSON JSON-RPC bridge
# ($XDG_RUNTIME_DIR/livi-ui.sock).
#
#   cmake -S native/livi-ui -B native/livi-ui/build -DCMAKE_BUILD_TYPE=Release
#   cmake --build native/livi-ui/build -j
#
# Dependencies (Debian/Raspberry Pi OS): cmake libwayland-dev wayland-protocols
# libxkbcommon-dev libfreetype-dev. LVGL and cJSON are fetched at configure time.
cmake_minimum_required(VERSION 3.22)
project(livi-ui C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)
if(POLICY CMP0169)
  cmake_policy(SET CMP0169 OLD) # FetchContent_Populate(<name>) before add_subdirectory
endif()
set(FETCHCONTENT_QUIET OFF)
FetchContent_Declare(lvgl
  GIT_REPOSITORY https://github.com/lvgl/lvgl.git
  GIT_TAG v9.5.0
  GIT_SHALLOW TRUE
  PATCH_COMMAND ${CMAKE_COMMAND} -DLVGL_SRC=<SOURCE_DIR> -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_lvgl.cmake)
FetchContent_Declare(cjson
  GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
  GIT_TAG v1.7.18
  GIT_SHALLOW TRUE)

# --- LVGL -------------------------------------------------------------------
# lv_conf.h is derived from LVGL's own template at configure time so an LVGL
# upgrade cannot leave us with a stale option list (cmake/lv_conf.cmake).
FetchContent_Populate(lvgl)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/lv_conf.cmake)
livi_generate_lv_conf(${lvgl_SOURCE_DIR}/lv_conf_template.h ${CMAKE_CURRENT_BINARY_DIR}/lv_conf/lv_conf.h)
set(LV_BUILD_CONF_PATH ${CMAKE_CURRENT_BINARY_DIR}/lv_conf/lv_conf.h CACHE STRING "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory(${lvgl_SOURCE_DIR} ${lvgl_BINARY_DIR})

# --- cJSON ------------------------------------------------------------------
set(ENABLE_CJSON_TEST OFF CACHE BOOL "" FORCE)
set(ENABLE_CJSON_UTILS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ENABLE_TARGET_EXPORT OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cjson)

# --- Wayland / FreeType ------------------------------------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND REQUIRED wayland-client wayland-cursor xkbcommon)
pkg_check_modules(FREETYPE REQUIRED freetype2)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
pkg_get_variable(WAYLAND_SCANNER wayland-scanner wayland_scanner)
if(NOT WAYLAND_SCANNER)
  set(WAYLAND_SCANNER wayland-scanner)
endif()

# LVGL's Wayland driver includes the generated xdg-shell header by name.
set(XDG_SHELL_XML ${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml)
set(GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/protocols)
file(MAKE_DIRECTORY ${GEN_DIR})
add_custom_command(
  OUTPUT ${GEN_DIR}/wayland_xdg_shell.h ${GEN_DIR}/wayland_xdg_shell.c
  COMMAND ${WAYLAND_SCANNER} client-header ${XDG_SHELL_XML} ${GEN_DIR}/wayland_xdg_shell.h
  COMMAND ${WAYLAND_SCANNER} private-code ${XDG_SHELL_XML} ${GEN_DIR}/wayland_xdg_shell.c
  DEPENDS ${XDG_SHELL_XML}
  COMMENT "wayland-scanner xdg-shell")
# LVGL's driver includes the header too, so it must exist before lvgl compiles.
add_custom_target(livi_xdg_shell DEPENDS ${GEN_DIR}/wayland_xdg_shell.h ${GEN_DIR}/wayland_xdg_shell.c)
add_dependencies(lvgl livi_xdg_shell)

# --- livi-ui ----------------------------------------------------------------
add_executable(livi-ui
  src/main.c
  src/bridge.c
  src/theme.c
  src/i18n.c
  src/ui/shell.c
  src/ui/pages.c
  src/ui/projection.c
  src/ui/settings.c
  src/ui/devices.c
  ${GEN_DIR}/wayland_xdg_shell.c)
target_include_directories(livi-ui PRIVATE src ${GEN_DIR} ${cjson_SOURCE_DIR} ${WAYLAND_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS})
target_compile_options(livi-ui PRIVATE -Wall -Wextra -Wno-unused-parameter ${WAYLAND_CFLAGS_OTHER})
target_link_libraries(livi-ui PRIVATE lvgl cjson ${WAYLAND_LIBRARIES} ${FREETYPE_LIBRARIES} m pthread)
target_compile_definitions(livi-ui PRIVATE LIVI_UI_VERSION="${LIVI_UI_VERSION}")

# The driver needs the protocol header and freetype too.
target_include_directories(lvgl PUBLIC ${GEN_DIR} ${WAYLAND_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS})
target_link_libraries(lvgl PUBLIC ${WAYLAND_LIBRARIES} ${FREETYPE_LIBRARIES})

install(TARGETS livi-ui RUNTIME DESTINATION .)
