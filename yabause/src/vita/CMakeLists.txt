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
option(VITA_VDP2_WORKER "Share large Vita VDP2 rotation decodes with one worker" OFF)
option(VITA_SH2_DYNAREC_SMOKE "Compile one real SH2 block through the Vita VM-backed Ari64 path at startup" OFF)
option(VITA_ATLAS_OPTIMIZER "Reduce oversized VitaGL atlas uploads with comparison scan" OFF)
option(VITA_ATLAS_DIRECT_UPLOAD "Upload YGL dirty atlas rectangles directly" ON)
set(VITA_ATLAS_DIRECT_MODE "multi" CACHE STRING "Direct atlas mode: stock, bounding, or multi")
set_property(CACHE VITA_ATLAS_DIRECT_MODE PROPERTY STRINGS stock bounding multi)
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
if(NOT VITA_ATLAS_DIRECT_MODE STREQUAL "stock" AND
   NOT VITA_ATLAS_DIRECT_MODE STREQUAL "bounding" AND
   NOT VITA_ATLAS_DIRECT_MODE STREQUAL "multi")
  message(FATAL_ERROR "VITA_ATLAS_DIRECT_MODE must be stock, bounding, or multi")
endif()
if(VITA_ATLAS_OPTIMIZER AND VITA_ATLAS_DIRECT_UPLOAD)
  message(FATAL_ERROR
    "VITA_ATLAS_OPTIMIZER and VITA_ATLAS_DIRECT_UPLOAD cannot both be enabled")
endif()

add_executable(yabause-vita
  main.c
  pervita.c
  sndvita.c
  vitaprofile.c
)

# First production Ari64 gate: build the real ARM compiler and linkage against
# the validated Vita VM adapter, compile/publish one real SH2 block after
# YabauseInit, then tear the dynarec back down and continue on the interpreter.
# Generated SH2 code is deliberately NOT executed by this smoke test.
if(VITA_SH2_DYNAREC_SMOKE)
  include(${CMAKE_CURRENT_LIST_DIR}/PrepareVitaDynarec.cmake)

  target_sources(yabause PRIVATE
    ${VITA_DYNAREC_C_SOURCE}
    ${VITA_DYNAREC_ASM_SOURCE}
  )
  target_include_directories(yabause PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../sh2_dynarec
  )

  # These must be visible while compiling the generated Ari64 translation
  # unit.  Source-file properties on generated sources are not reliably
  # propagated by the VitaSDK Generic toolchain, so make them target-level
  # for this opt-in smoke build.  Normal Vita builds never enter this block.
  target_compile_definitions(yabause PRIVATE
    VITA_DYNAREC_TEST=1
    VITA_SH2_DYNAREC=1
    HAVE_ARMv6=1
    HAVE_ARMv7=1
    HAVE_STDINT_H=1
  )

  set_source_files_properties(${VITA_DYNAREC_C_SOURCE} PROPERTIES
    COMPILE_OPTIONS "-marm;-UNDEBUG;-Wno-pointer-to-int-cast;-Wno-int-to-pointer-cast"
  )
  # VitaSDK's Generic CMake toolchain does not provide CMAKE_ASM_COMPILE_OBJECT
  # reliably when ASM is enabled from this subdirectory.  linkage_arm_vita.s
  # is ordinary GNU ARM assembly, so compile it through the already-configured
  # arm-vita-eabi-gcc C driver instead of enabling a separate ASM language.
  set_source_files_properties(${VITA_DYNAREC_ASM_SOURCE} PROPERTIES
    LANGUAGE C
    COMPILE_OPTIONS "-marm;-x;assembler-with-cpp"
  )

  target_sources(yabause-vita PRIVATE
    vita_dynarec_vm.c
    vita_dynarec_smoke.c
  )
  target_compile_definitions(yabause-vita PRIVATE
    VITA_SH2_DYNAREC=1
    VITA_SH2_DYNAREC_SMOKE=1
  )
  target_link_options(yabause-vita PRIVATE
    -Wl,--wrap=YabauseInit
  )
endif()

if(VITA_VIDEO_BACKEND STREQUAL "vitagl")
  include(${CMAKE_CURRENT_LIST_DIR}/PrivateVitaGL.cmake)
  target_sources(yabause-vita PRIVATE
    vitagl_present.c
    vidvitagl.c
    vita_sce_metadata_pad.c
  )

  # vita-elf-create appends generated SCE metadata after the first PT_LOAD.
  # Keep the normal VitaSDK linker layout, but force a small read-only payload
  # to remain live so the RX segment crosses the current 64 KiB boundary and
  # the following RW segment naturally moves to the next one. This leaves a
  # real inter-segment gap without changing ELF page/section alignment rules.
  target_link_options(yabause-vita PRIVATE
    -Wl,--undefined=yabause_vita_sce_metadata_pad
  )
endif()

if(VITA_VIDEO_BACKEND STREQUAL "vitagl" AND VITA_ATLAS_DIRECT_UPLOAD)
  target_sources(yabause-vita PRIVATE atlas_direct_upload.c)
  set_source_files_properties(atlas_direct_upload.c PROPERTIES
    COMPILE_DEFINITIONS HAVE_LIBGL=1
  )
  target_compile_definitions(yabause-vita PRIVATE VITA_ATLAS_DIRECT_UPLOAD=1)
  if(VITA_ATLAS_DIRECT_MODE STREQUAL "stock")
    target_compile_definitions(yabause-vita PRIVATE VITA_ATLAS_DIRECT_MODE=0)
  elseif(VITA_ATLAS_DIRECT_MODE STREQUAL "bounding")
    target_compile_definitions(yabause-vita PRIVATE VITA_ATLAS_DIRECT_MODE=1)
  else()
    target_compile_definitions(yabause-vita PRIVATE VITA_ATLAS_DIRECT_MODE=2)
  endif()
  target_link_options(yabause-vita PRIVATE
    -Wl,--wrap=YglQuad
    -Wl,--wrap=YglQuadGrowShading
    -Wl,--wrap=YglQuadOffset
    -Wl,--wrap=YglVitaForcePersistentPartialAllocation
    -Wl,--wrap=YglVitaMarkPersistentDirty
    -Wl,--wrap=YglReset
    -Wl,--wrap=glTexSubImage2D
  )
endif()

if(VITA_VIDEO_BACKEND STREQUAL "vitagl" AND VITA_ATLAS_OPTIMIZER)
  target_sources(yabause-vita PRIVATE atlas_upload_optimizer.c)
  set_source_files_properties(atlas_upload_optimizer.c PROPERTIES
    COMPILE_DEFINITIONS HAVE_LIBGL=1
  )
  target_link_options(yabause-vita PRIVATE
    -Wl,--wrap=glBindTexture
    -Wl,--wrap=glTexImage2D
    -Wl,--wrap=glTexSubImage2D
  )
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
  if(VITA_VIDEO_BACKEND STREQUAL "vitagl")
    target_sources(yabause-vita PRIVATE
      vdp1_profile_pipeline.c
      core_profile_pipeline.c
    )
    if(VITA_ATLAS_DIRECT_UPLOAD)
      set_source_files_properties(vdp1_profile_pipeline.c PROPERTIES
        COMPILE_DEFINITIONS "HAVE_LIBGL=1;VITA_ATLAS_DIRECT_UPLOAD=1;__wrap_YglQuad=VitaProfileWrappedYglQuad;__wrap_YglQuadGrowShading=VitaProfileWrappedYglQuadGrowShading;__wrap_YglQuadOffset=VitaProfileWrappedYglQuadOffset"
      )
    else()
      set_source_files_properties(vdp1_profile_pipeline.c PROPERTIES
        COMPILE_DEFINITIONS HAVE_LIBGL=1
      )
    endif()
    target_link_options(yabause-vita PRIVATE
      -Wl,--wrap=VitaProfileBegin
      -Wl,--wrap=VitaProfileEnd
      -Wl,--wrap=VitaProfileFrameComplete
      -Wl,--wrap=VitaProfileShutdown
      -Wl,--wrap=VitaProfileRecordAtlasDirtyGenerated
      -Wl,--wrap=VitaProfileRecordAtlasUploadRegion
      -Wl,--wrap=VitaProfileRecordAtlasCoalescing
      -Wl,--wrap=VIDOGLVdp1NormalSpriteDraw
      -Wl,--wrap=VIDOGLVdp1ScaledSpriteDraw
      -Wl,--wrap=VIDOGLVdp1DistortedSpriteDraw
      -Wl,--wrap=VIDOGLVdp1PolygonDraw
      -Wl,--wrap=VIDOGLVdp1PolylineDraw
      -Wl,--wrap=VIDOGLVdp1LineDraw
      -Wl,--wrap=VIDOGLVdp1UserClipping
      -Wl,--wrap=VIDOGLVdp1SystemClipping
      -Wl,--wrap=VIDOGLVdp1LocalCoordinate
      -Wl,--wrap=YglQuad
      -Wl,--wrap=YglQuadGrowShading
      -Wl,--wrap=YglCacheQuadGrowShading
      -Wl,--wrap=YglCachedQuad
      -Wl,--wrap=YglQuadOffset
      -Wl,--wrap=YglCachedQuadOffset
      -Wl,--wrap=glDrawArrays
      -Wl,--wrap=glFinish
      -Wl,--wrap=glUseProgram
      -Wl,--wrap=glUniformMatrix4fv
      -Wl,--wrap=glEnable
      -Wl,--wrap=glDisable
      -Wl,--wrap=glStencilOp
      -Wl,--wrap=glBindFramebuffer
      -Wl,--wrap=glBlitFramebuffer
      -Wl,--wrap=YabauseExec
      -Wl,--wrap=SH2Exec
      -Wl,--wrap=ScuExec
      -Wl,--wrap=SmpcExec
      -Wl,--wrap=Cs2Exec
      -Wl,--wrap=Vdp2HBlankIN
      -Wl,--wrap=Vdp2HBlankOUT
      -Wl,--wrap=Vdp2VBlankIN
      -Wl,--wrap=Vdp2VBlankOUT
      -Wl,--wrap=SmpcINTBACKEnd
    )
  endif()
endif()
if(NOT VITA_AUDIO_ENABLED)
  target_compile_definitions(yabause-vita PRIVATE VITA_DISABLE_AUDIO=1)
endif()
if(VITA_TEXTURE_CACHE)
  target_compile_definitions(yabause-vita PRIVATE VITA_TEXTURE_CACHE=1)
  target_compile_definitions(yabause PRIVATE VITA_TEXTURE_CACHE=1)
endif()
if(VITA_VIDEO_BACKEND STREQUAL "vitagl" AND VITA_VDP2_WORKER)
  target_compile_definitions(yabause-vita PRIVATE VITA_VDP2_WORKER=1)
  target_compile_definitions(yabause PRIVATE VITA_VDP2_WORKER=1)
endif()
if(VITA_ATLAS_MODE STREQUAL "wide")
  target_compile_definitions(yabause PRIVATE VITA_ATLAS_WIDE=1)
  target_compile_definitions(yabause-vita PRIVATE VITA_ATLAS_WIDE=1)
elseif(VITA_ATLAS_MODE STREQUAL "square")
  target_compile_definitions(yabause PRIVATE VITA_ATLAS_SQUARE=1 VITA_ATLAS_PAGED=1)
  target_compile_definitions(yabause-vita PRIVATE VITA_ATLAS_SQUARE=1 VITA_ATLAS_PAGED=1)
else()
  target_compile_definitions(yabause PRIVATE
    VITA_ATLAS_SQUARE=1
    VITA_ATLAS_BUFFERED=1
  )
  target_compile_definitions(yabause-vita PRIVATE
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

if(VITA_SH2_DYNAREC_SMOKE)
  target_link_libraries(yabause-vita SceLibKernel_stub)
endif()

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
