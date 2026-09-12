cmake_minimum_required(VERSION 3.16)
project(vita_dynarec_test C ASM)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
set(SRC "${CMAKE_CURRENT_LIST_DIR}/../../yabause/src")
add_library(ari64-compiler OBJECT ${SRC}/sh2_dynarec/sh2_dynarec.c)
target_include_directories(ari64-compiler PRIVATE . ${SRC} ../../yabause/mini18n/src)
target_compile_definitions(ari64-compiler PRIVATE VITA=1 VITA_DYNAREC_TEST=1
  HAVE_ARMv6=1 HAVE_ARMv7=1 HAVE_STDINT_H=1)
target_compile_options(ari64-compiler PRIVATE -marm -UNDEBUG -ffunction-sections -fdata-sections)
