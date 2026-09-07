project(yabause-vita C)

yab_port_start()

if(NOT VITA)
  return()
endif()

include("${VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Yabause")
set(VITA_TITLEID "YABA00001")
set(VITA_VERSION "00.01")

set(VITA_VIDEO_BACKEND "vitagl" CACHE STRING "Vita video backend: vitagl or software")
set_property(CACHE VITA_VIDEO_BACKEND PROPERTY STRINGS vitagl software)
option(VITA_PROFILE "Write aggregated Vita performance measurements" OFF)
option(VITA_AUDIO_ENABLED "Enable Vita sound CPU and audio output" ON)
option(VITA_TEXTURE_CACHE "Cache decoded Vita VDP textures across frames" ON)
option(VITA_BOOT_GAME "Mount ux0:data/yabause/bin/game.cue at startup" OFF)
set(VITA_ATLAS_MODE "square" CACHE STRING "Vita atlas: wide, square, or bufferedsquare")
set_property(CACHE VITA_ATLAS_MODE PROPERTY STRINGS wide square bufferedsquare)
set(VITA_ATLAS_UPLOAD "dirty" CACHE STRING "Vita atlas upload: dirty or bands")
set_property(CACHE VITA_ATLAS_UPLOAD PROPERTY STRINGS dirty bands)
set(VITA_VGL_TEXTURE_UPDATES "synchronizedinplace" CACHE STRING "vitaGL texture updates: copyonwrite or synchronizedinplace")
set_property(CACHE VITA_VGL_TEXTURE_UPDATES PROPERTY STRINGS copyonwrite synchronizedinplace)

if(NOT VITA_VIDEO_BACKEND STREQUAL "vitagl" AND
   NOT VITA_VIDEO_BACKEND STREQUAL "software")
  message(FATAL_ERROR "VITA_VIDEO_BACKEND must be vitagl or software")
endif()
if(NOT VITA_ATLAS_UPLOAD STREQUAL "dirty" AND
   NOT VITA_ATLAS_UPLOAD STREQUAL "bands")
  message(FATAL_ERROR "VITA_ATLAS_UPLOAD must be dirty or bands")
endif()
if(NOT VITA_VGL_TEXTURE_UPDATES STREQUAL "copyonwrite" AND
   NOT VITA_VGL_TEXTURE_UPDATES STREQUAL "synchronizedinplace")
  message(FATAL_ERROR "VITA_VGL_TEXTURE_UPDATES must be copyonwrite or synchronizedinplace")
endif()
if(NOT VITA_ATLAS_MODE STREQUAL "wide" AND
   NOT VITA_ATLAS_MODE STREQUAL "square" AND
   NOT VITA_ATLAS_MODE STREQUAL "bufferedsquare")
  message(FATAL_ERROR "VITA_ATLAS_MODE must be wide, square, or bufferedsquare")
endif()

add_executable(yabause-vita
  main.c
  pervita.c
  sndvita.c
  vitaprofile.c
)

if(VITA_VIDEO_BACKEND STREQUAL "vitagl")
  include(${CMAKE_CURRENT_LIST_DIR}/PrivateVitaGL.cmake)
  target_sources(yabause-vita PRIVATE vitagl_present.c vidvitagl.c)
endif()

target_include_directories(yabause-vita PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${CMAKE_CURRENT_BINARY_DIR}/..
)

target_compile_definitions(yabause-vita PRIVATE VITA=1 NO_CLI=1)
if(VITA_BOOT_GAME)
  target_compile_definitions(yabause-vita PRIVATE VITA_BOOT_GAME=1)
endif()
if(VITA_VIDEO_BACKEND STREQUAL "vitagl")
  target_compile_definitions(yabause-vita PRIVATE
    VITA_USE_VITAGL=1
    VITA_PRIVATE_VITAGL_REVISION=\"cd3791e\"
  )
  target_compile_definitions(yabause PRIVATE
    VITA_PRIVATE_VITAGL_REVISION=\"cd3791e\"
    HAVE_LIBGL=1
    VITA=1
    _OGLES3_=1
    HAVE_FBO=1
  )
endif()
if(VITA_VIDEO_BACKEND STREQUAL "vitagl" AND
   VITA_VGL_TEXTURE_UPDATES STREQUAL "synchronizedinplace")
  target_compile_definitions(yabause PRIVATE VITA_VGL_INPLACE_TEXTURE_UPDATES=1)
  target_compile_definitions(yabause-vita PRIVATE VITA_VGL_INPLACE_TEXTURE_UPDATES=1)
endif()
if(VITA_PROFILE)
  target_compile_definitions(yabause-vita PRIVATE VITA_PROFILE=1)
  target_compile_definitions(yabause PRIVATE VITA_PROFILE=1)
endif()
if(NOT VITA_AUDIO_ENABLED)
  target_compile_definitions(yabause-vita PRIVATE VITA_DISABLE_AUDIO=1)
endif()
if(VITA_TEXTURE_CACHE)
  target_compile_definitions(yabause-vita PRIVATE VITA_TEXTURE_CACHE=1)
  target_compile_definitions(yabause PRIVATE VITA_TEXTURE_CACHE=1)
endif()
if(VITA_ATLAS_MODE STREQUAL "wide")
  target_compile_definitions(yabause PRIVATE VITA_ATLAS_WIDE=1)
elseif(VITA_ATLAS_MODE STREQUAL "square")
  target_compile_definitions(yabause PRIVATE VITA_ATLAS_SQUARE=1 VITA_ATLAS_PAGED=1)
else()
  target_compile_definitions(yabause PRIVATE
    VITA_ATLAS_SQUARE=1
    VITA_ATLAS_BUFFERED=1
  )
endif()
if(VITA_ATLAS_UPLOAD STREQUAL "bands")
  target_compile_definitions(yabause PRIVATE VITA_ATLAS_UPLOAD_BANDS=1)
endif()
target_compile_options(yabause-vita PRIVATE -Wall -Wextra -Wno-unused-parameter)

target_link_libraries(yabause-vita
  yabause
  ${YABAUSE_LIBRARIES}
  m
  pthread
  SceAudio_stub
  SceCtrl_stub
  SceDisplay_stub
  SceIofilemgr_stub
  SceKernelThreadMgr_stub
  SceSysmem_stub
)

if(VITA_VIDEO_BACKEND STREQUAL "vitagl")
  target_link_libraries(yabause-vita
    yabause-private-vitaGL
    stdc++
    mathneon
    vitashark
    SceShaccCgExt
    SceLibKernel_stub
    SceAppMgr_stub
    SceAppUtil_stub
    SceCommonDialog_stub
    SceGxm_stub
    SceSysmodule_stub
    taihen_stub
    SceShaccCg_stub
    SceKernelDmacMgr_stub
    SceDisplay_stub
    m
  )
endif()

vita_create_self(yabause.self yabause-vita)
vita_create_vpk(yabause.vpk ${VITA_TITLEID} yabause.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE icon0.png sce_sys/icon0.png
)

yab_port_stop()
