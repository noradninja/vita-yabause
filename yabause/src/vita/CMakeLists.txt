project(yabause-vita C)

yab_port_start()

if(NOT VITA)
  return()
endif()

include("${VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Yabause")
set(VITA_TITLEID "YABA00001")
set(VITA_VERSION "00.01")

add_executable(yabause-vita
  main.c
  pervita.c
  sndvita.c
)

target_include_directories(yabause-vita PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${CMAKE_CURRENT_BINARY_DIR}/..
)

target_compile_definitions(yabause-vita PRIVATE VITA=1 NO_CLI=1)
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

vita_create_self(yabause.self yabause-vita)
vita_create_vpk(yabause.vpk ${VITA_TITLEID} yabause.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE icon0.png sce_sys/icon0.png
)

yab_port_stop()
