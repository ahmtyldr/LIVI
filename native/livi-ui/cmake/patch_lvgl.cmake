# Applied by FetchContent right after LVGL is downloaded (PATCH_COMMAND), and
# by hand on an already populated tree:
#   cmake -DLVGL_SRC=build/_deps/lvgl-src -P cmake/patch_lvgl.cmake
#
# LIVI's UI window must be ARGB: the compositor keeps the projection video
# plane *under* the UI surface and the UI shows it through transparent
# areas, exactly like the Electron window does. Two things in LVGL 9.5's
# Wayland driver stand in the way:
#  1. lv_wl_window.c creates the display with LV_COLOR_FORMAT_NATIVE
#     (XRGB8888) and the shm backend picks the wl_shm format from it before
#     the application can change it.
#  2. lv_wl_shm_backend.c treats a wl_shm format of 0 as "unsupported" -
#     but WL_SHM_FORMAT_ARGB8888 *is* 0, so ARGB always falls back to XRGB.
if(NOT DEFINED LVGL_SRC)
  set(LVGL_SRC "${CMAKE_CURRENT_LIST_DIR}/../build/_deps/lvgl-src")
endif()

function(livi_patch file from to)
  file(READ "${file}" src)
  string(FIND "${src}" "LIVI_UI_ARGB" already)
  if(NOT already EQUAL -1)
    return()
  endif()
  string(FIND "${src}" "${from}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "livi-ui: pattern not found in ${file}: ${from}")
  endif()
  string(REPLACE "${from}" "${to}" src "${src}")
  file(WRITE "${file}" "${src}")
  message(STATUS "livi-ui: patched ${file}")
endfunction()

livi_patch("${LVGL_SRC}/src/drivers/wayland/lv_wl_window.c"
  "window->lv_disp = lv_display_create(hor_res, ver_res);"
  "window->lv_disp = lv_display_create(hor_res, ver_res);\n    lv_display_set_color_format(window->lv_disp, LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED); /* LIVI_UI_ARGB */")

set(shm "${LVGL_SRC}/src/drivers/wayland/lv_wl_shm_backend.c")
file(READ "${shm}" src)
string(FIND "${src}" "LIVI_UI_ARGB" already)
if(already EQUAL -1)
  string(REPLACE "        default:\n            return 0;" "        default:\n            return 0xFFFFFFFFu; /* LIVI_UI_ARGB: WL_SHM_FORMAT_ARGB8888 is 0 */" src "${src}")
  string(REPLACE "if(!ddata->shm_cf) {" "if(ddata->shm_cf == 0xFFFFFFFFu) {" src "${src}")
  file(WRITE "${shm}" "${src}")
  message(STATUS "livi-ui: patched ${shm}")
endif()
