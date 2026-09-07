set(VITA_PRIVATE_VITAGL_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../third_party/vitaGL")
set(VITA_PRIVATE_VITAGL_REVISION "cd3791e")

if(NOT EXISTS "${VITA_PRIVATE_VITAGL_DIR}/source/vitaGL.h" OR
   NOT EXISTS "${VITA_PRIVATE_VITAGL_DIR}/source/textures.c")
  message(FATAL_ERROR
    "The pinned vitaGL submodule is missing. Run: git submodule update --init --recursive")
endif()

file(GLOB VITA_PRIVATE_VITAGL_C_SOURCES CONFIGURE_DEPENDS
  "${VITA_PRIVATE_VITAGL_DIR}/source/*.c"
  "${VITA_PRIVATE_VITAGL_DIR}/source/utils/*.c"
  "${VITA_PRIVATE_VITAGL_DIR}/source/utils/preprocessor/*.c"
)
file(GLOB VITA_PRIVATE_VITAGL_CXX_SOURCES CONFIGURE_DEPENDS
  "${VITA_PRIVATE_VITAGL_DIR}/source/*.cpp"
  "${VITA_PRIVATE_VITAGL_DIR}/source/utils/*.cpp"
  "${VITA_PRIVATE_VITAGL_DIR}/source/utils/preprocessor/*.cpp"
)

add_library(yabause-private-vitaGL STATIC
  ${VITA_PRIVATE_VITAGL_C_SOURCES}
  ${VITA_PRIVATE_VITAGL_CXX_SOURCES}
)
target_include_directories(yabause-private-vitaGL PUBLIC
  "${VITA_PRIVATE_VITAGL_DIR}/source"
)
target_include_directories(yabause PRIVATE
  "${VITA_PRIVATE_VITAGL_DIR}/source"
)
target_include_directories(yabause-vita PRIVATE
  "${VITA_PRIVATE_VITAGL_DIR}/source"
)

# This pinned vitaGL exposes an optional shader-association setter added after
# the vitaShaRK ABI shipped by some VitaSDK installations. Yabause does not use
# that setter, so keep the private library compatible without replacing the
# user's SDK-wide vitaShaRK package.
set(VITA_PRIVATE_VITAGL_SHARK_COMPAT
  "${CMAKE_CURRENT_BINARY_DIR}/vitagl_vitashark_compat.h"
)
file(WRITE "${VITA_PRIVATE_VITAGL_SHARK_COMPAT}"
"#ifndef YABAUSE_VITAGL_VITASHARK_COMPAT_H\n"
"#define YABAUSE_VITAGL_VITASHARK_COMPAT_H\n"
"#include <vitasdk.h>\n"
"static inline void yabause_vitagl_set_shader_association_path(const char *path) { (void)path; }\n"
"#define shark_set_shader_association_path yabause_vitagl_set_shader_association_path\n"
"#endif\n"
)
set_source_files_properties(
  "${VITA_PRIVATE_VITAGL_DIR}/source/vgl.c"
  PROPERTIES COMPILE_OPTIONS "-include;${VITA_PRIVATE_VITAGL_SHARK_COMPAT}"
)

target_compile_definitions(yabause-private-vitaGL PRIVATE
  SKIP_SPLASHSCREEN
  VGL_GIT_HASH="${VITA_PRIVATE_VITAGL_REVISION}"
)
if(VITA_VGL_TEXTURE_UPDATES STREQUAL "synchronizedinplace")
  target_compile_definitions(yabause-private-vitaGL PRIVATE TEXTURES_SPEEDHACK)
endif()
target_compile_options(yabause-private-vitaGL PRIVATE
  -O3
  -ffast-math
  -mtune=cortex-a9
  -mfpu=neon
  -mfp16-format=ieee
  -Wno-incompatible-pointer-types
  -Wno-stringop-overflow
)
target_compile_features(yabause-private-vitaGL PRIVATE cxx_std_11)
