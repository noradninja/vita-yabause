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

if(NOT VITA_VIDEO_BACKEND STREQUAL "vitagl" AND
   NOT VITA_VIDEO_BACKEND STREQUAL "software")
  message(FATAL_ERROR "VITA_VIDEO_BACKEND must be vitagl or software")
endif()

add_executable(yabause-vita
  main.c
  pervita.c
  sndvita.c
  vitaprofile.c
)

if(VITA_VIDEO_BACKEND STREQUAL "vitagl")
  target_sources(yabause-vita PRIVATE vitagl_present.c vidvitagl.c)
endif()

target_include_directories(yabause-vita PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${CMAKE_CURRENT_BINARY_DIR}/..
)

target_compile_definitions(yabause-vita PRIVATE VITA=1 NO_CLI=1)
if(VITA_VIDEO_BACKEND STREQUAL "vitagl")
  target_compile_definitions(yabause-vita PRIVATE VITA_USE_VITAGL=1)
  target_compile_definitions(yabause PRIVATE
    HAVE_LIBGL=1
    VITA=1
    _OGLES3_=1
    HAVE_FBO=1
  )
endif()
if(VITA_PROFILE)
  target_compile_definitions(yabause-vita PRIVATE VITA_PROFILE=1)
  target_compile_definitions(yabause PRIVATE VITA_PROFILE=1)
endif()
if(NOT VITA_AUDIO_ENABLED)
  target_compile_definitions(yabause-vita PRIVATE VITA_DISABLE_AUDIO=1)
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
    vitaGL
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
