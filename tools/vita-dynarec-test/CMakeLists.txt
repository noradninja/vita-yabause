cmake_minimum_required(VERSION 3.16)
project(vita_dynarec_test C ASM)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
set(SRC "${CMAKE_CURRENT_LIST_DIR}/../../yabause/src")
add_library(ari64-compiler OBJECT ${SRC}/sh2_dynarec/sh2_dynarec.c)
target_include_directories(ari64-compiler PRIVATE . ${SRC} ../../yabause/mini18n/src)
target_compile_definitions(ari64-compiler PRIVATE VITA=1 VITA_DYNAREC_TEST=1
  HAVE_ARMv6=1 HAVE_ARMv7=1 HAVE_STDINT_H=1)
target_compile_options(ari64-compiler PRIVATE -marm -UNDEBUG -ffunction-sections -fdata-sections)
add_executable(ari64-vm-test vm_test.c vita_dynarec_vm.c)
set(VITA_DEBUGSCREEN_DIR "$ENV{VITASDK}/share/gcc-arm-vita-eabi/samples/common"
  CACHE PATH "VitaSDK samples/common directory containing debugScreen.c")
if(NOT EXISTS "${VITA_DEBUGSCREEN_DIR}/debugScreen.c")
  message(FATAL_ERROR "Set VITA_DEBUGSCREEN_DIR to VitaSDK samples/common")
endif()
target_sources(ari64-vm-test PRIVATE "${VITA_DEBUGSCREEN_DIR}/debugScreen.c")
target_include_directories(ari64-vm-test PRIVATE "${VITA_DEBUGSCREEN_DIR}")
add_dependencies(ari64-vm-test ari64-compiler)
target_compile_options(ari64-vm-test PRIVATE -marm -Wall -Wextra -Werror)
target_link_libraries(ari64-vm-test SceSysmem_stub SceIofilemgr_stub SceLibKernel_stub
  SceDisplay_stub SceCtrl_stub)
vita_create_self(ari64-vm-test.self ari64-vm-test UNSAFE)
vita_create_vpk(ari64-vm-test.vpk SH2ARIT01 ari64-vm-test.self
  VERSION 01.00 NAME "Ari64 VM validation")
