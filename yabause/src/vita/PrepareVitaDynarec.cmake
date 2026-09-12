# Prepare Vita-only copies of the legacy Ari64 sources without modifying the
# imported dynarec implementation for other platforms. Exact anchor checks
# deliberately fail configuration if the upstream source changes underneath
# these adaptations.

set(_dynarec_source "${CMAKE_CURRENT_LIST_DIR}/../sh2_dynarec/sh2_dynarec.c")
set(_linkage_source "${CMAKE_CURRENT_LIST_DIR}/../sh2_dynarec/linkage_arm.s")
set(_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/vita_dynarec")
set(VITA_DYNAREC_C_SOURCE "${_generated_dir}/sh2_dynarec_vita.c")
set(_vita_dynarec_asm_source "${_generated_dir}/linkage_arm_vita.s")
set(_vita_dynarec_asm_object "${_generated_dir}/linkage_arm_vita.o")

# CMake's VitaSDK Generic toolchain does not provide a reliable standalone ASM
# compile rule here, and source-specific -x assembler-with-cpp flags have also
# proven unreliable for generated sources.  Compile the generated linkage with
# an explicit gcc invocation, then hand the resulting object to target_sources.
# CMakeLists.txt already consumes VITA_DYNAREC_ASM_SOURCE, so expose the object
# under that existing variable name.
set(VITA_DYNAREC_ASM_SOURCE "${_vita_dynarec_asm_object}")

file(MAKE_DIRECTORY "${_generated_dir}")
file(READ "${_dynarec_source}" _dynarec)
file(READ "${_linkage_source}" _linkage)

function(vita_dynarec_replace_once variable needle replacement label)
  string(FIND "${${variable}}" "${needle}" _first)
  if(_first EQUAL -1)
    message(FATAL_ERROR "Vita dynarec preparation failed: ${label} anchor not found")
  endif()
  math(EXPR _after "${_first} + 1")
  string(SUBSTRING "${${variable}}" ${_after} -1 _tail)
  string(FIND "${_tail}" "${needle}" _second)
  if(NOT _second EQUAL -1)
    message(FATAL_ERROR "Vita dynarec preparation failed: ${label} anchor is not unique")
  endif()
  string(REPLACE "${needle}" "${replacement}" _updated "${${variable}}")
  set(${variable} "${_updated}" PARENT_SCOPE)
endfunction()

set(_init_anchor [=[  //printf("Init new dynarec\n");
  out=(u8 *)BASE_ADDR;]=])
# CMake bracket arguments preserve backslashes. The C source contains literal
# quotes, so normalize this one compact anchor before matching it.
string(REPLACE [=[\"]=] [=["]=] _init_anchor "${_init_anchor}")
set(_init_replacement [=[  //printf("Init new dynarec\n");
#ifdef VITA_SH2_DYNAREC
  {
    int vm_rc = vita_dynarec_vm_init();
    if (vm_rc < 0) {
      fprintf(stderr, "vita dynarec VM init failed: %08x\n", (unsigned)vm_rc);
      out = NULL;
      return;
    }
    vm_rc = vita_dynarec_vm_begin();
    if (vm_rc < 0) {
      fprintf(stderr, "vita dynarec VM begin/reset failed: %08x\n", (unsigned)vm_rc);
      vita_dynarec_vm_free();
      out = NULL;
      return;
    }
    vm_rc = vita_dynarec_vm_reset();
    if (vm_rc < 0) {
      fprintf(stderr, "vita dynarec VM reset failed: %08x\n", (unsigned)vm_rc);
      vita_dynarec_vm_free();
      out = NULL;
      return;
    }
    vm_rc = vita_dynarec_vm_end();
    if (vm_rc < 0) {
      fprintf(stderr, "vita dynarec VM initial publish failed: %08x\n", (unsigned)vm_rc);
      vita_dynarec_vm_free();
      out = NULL;
      return;
    }
  }
#endif
  out=(u8 *)BASE_ADDR;]=])
string(REPLACE [=[\"]=] [=["]=] _init_replacement "${_init_replacement}")
vita_dynarec_replace_once(_dynarec "${_init_anchor}" "${_init_replacement}" "VM initialization")

set(_cleanup_anchor [=[  for(n=0;n<2048;n++) ll_clear(jump_in+n);
  for(n=0;n<2048;n++) ll_clear(jump_out+n);
  for(n=0;n<2048;n++) ll_clear(jump_dirty+n);
}]=])
set(_cleanup_replacement [=[  for(n=0;n<2048;n++) ll_clear(jump_in+n);
  for(n=0;n<2048;n++) ll_clear(jump_out+n);
  for(n=0;n<2048;n++) ll_clear(jump_dirty+n);
#ifdef VITA_SH2_DYNAREC
  {
    int vm_rc = vita_dynarec_vm_free();
    if (vm_rc < 0)
      fprintf(stderr, "vita dynarec VM free failed: %08x\n", (unsigned)vm_rc);
  }
#endif
}]=])
string(REPLACE [=[\"]=] [=["]=] _cleanup_replacement "${_cleanup_replacement}")
vita_dynarec_replace_once(_dynarec "${_cleanup_anchor}" "${_cleanup_replacement}" "VM cleanup")

set(_compile_anchor [=[  int cached_addr;

  //if(Count==365117028) tracedebug=1;]=])
set(_compile_replacement [=[  int cached_addr;
#ifdef VITA_SH2_DYNAREC
  int vita_vm_rc = vita_dynarec_vm_begin();
  if (vita_vm_rc < 0) {
    fprintf(stderr, "vita dynarec begin failed pc=%08x rc=%08x\n",
            (unsigned)addr, (unsigned)vita_vm_rc);
    return vita_vm_rc;
  }
#endif

  //if(Count==365117028) tracedebug=1;]=])
string(REPLACE [=[\"]=] [=["]=] _compile_replacement "${_compile_replacement}")
vita_dynarec_replace_once(_dynarec "${_compile_anchor}" "${_compile_replacement}" "compile transaction begin")

set(_compile_end_anchor [=[  }
  return 0;
}

#include "../sh2core.h"]=])
set(_compile_end_replacement [=[  }
#ifdef VITA_SH2_DYNAREC
  vita_vm_rc = vita_dynarec_vm_end();
  if (vita_vm_rc < 0) {
    fprintf(stderr, "vita dynarec publish failed pc=%08x rc=%08x\n",
            (unsigned)addr, (unsigned)vita_vm_rc);
    return vita_vm_rc;
  }
#endif
  return 0;
}

#include "../sh2core.h"]=])
string(REPLACE [=[\"]=] [=["]=] _compile_end_anchor "${_compile_end_anchor}")
string(REPLACE [=[\"]=] [=["]=] _compile_end_replacement "${_compile_end_replacement}")
vita_dynarec_replace_once(_dynarec "${_compile_end_anchor}" "${_compile_end_replacement}" "compile transaction end")

# The Vita VM backend owns sh2_dynarec_target. Keep dynarec_local and the
# memory-map/state block in the assembly BSS, but remove the legacy 16 MiB
# static code cache and its public symbol from the Vita-only copy. Regexes use
# horizontal whitespace classes so the original GAS tab formatting is retained
# everywhere else.
string(REGEX REPLACE "[ \t]*\\.global[ \t]+sh2_dynarec_target\n" "" _linkage "${_linkage}")
string(REGEX REPLACE
  "[ \t]*\\.bss\n[ \t]*\\.align[ \t]+12\n[ \t]*\\.type[ \t]+sh2_dynarec_target,[ \t]*%object\n[ \t]*\\.size[ \t]+sh2_dynarec_target,[ \t]*16777216\nsh2_dynarec_target:\n[ \t]*\\.space[ \t]+16777216\n[ \t]*\\.align[ \t]+4\n[ \t]*\\.type[ \t]+dynarec_local,[ \t]*%object"
  "\t.bss\n\t.align\t4\n\t.type\tdynarec_local, %object"
  _linkage "${_linkage}")

string(FIND "${_linkage}" "sh2_dynarec_target" _leftover_target)
if(NOT _leftover_target EQUAL -1)
  message(FATAL_ERROR "Vita dynarec preparation failed: generated linkage still references sh2_dynarec_target")
endif()
string(FIND "${_linkage}" ".type\tdynarec_local" _dynarec_local_found)
if(_dynarec_local_found EQUAL -1)
  message(FATAL_ERROR "Vita dynarec preparation failed: dynarec_local linkage block was not preserved")
endif()

file(WRITE "${VITA_DYNAREC_C_SOURCE}" "${_dynarec}")
file(WRITE "${_vita_dynarec_asm_source}" "${_linkage}")

# The 'yabause' target is created in the parent src directory. Source-file
# properties are directory-scoped in CMake, so apply the generated C-source
# options explicitly in the target's directory scope.
set_property(SOURCE "${VITA_DYNAREC_C_SOURCE}"
  TARGET_DIRECTORY yabause
  APPEND PROPERTY COMPILE_OPTIONS
    -marm
    -UNDEBUG
    -Wno-pointer-to-int-cast
    -Wno-int-to-pointer-cast
)

# Do not ask CMake to infer or enable an ASM language for VitaSDK. Build this
# one generated GAS file ourselves with the C compiler driver, then mark the
# result as a generated external object so target_sources links it verbatim.
add_custom_command(
  OUTPUT "${_vita_dynarec_asm_object}"
  COMMAND ${CMAKE_C_COMPILER}
    -marm
    -x assembler-with-cpp
    -DHAVE_ARMv6=1
    -DHAVE_ARMv7=1
    -DVITA=1
    -DVITA_DYNAREC_TEST=1
    -DVITA_SH2_DYNAREC=1
    -I"${CMAKE_CURRENT_LIST_DIR}"
    -I"${CMAKE_CURRENT_LIST_DIR}/../sh2_dynarec"
    -c "${_vita_dynarec_asm_source}"
    -o "${_vita_dynarec_asm_object}"
  DEPENDS "${_vita_dynarec_asm_source}"
  COMMENT "Assembling Vita Ari64 linkage"
  VERBATIM
)

set_source_files_properties("${_vita_dynarec_asm_object}" PROPERTIES
  GENERATED TRUE
  EXTERNAL_OBJECT TRUE
)
set_property(SOURCE "${_vita_dynarec_asm_object}"
  TARGET_DIRECTORY yabause
  PROPERTY GENERATED TRUE
)
set_property(SOURCE "${_vita_dynarec_asm_object}"
  TARGET_DIRECTORY yabause
  PROPERTY EXTERNAL_OBJECT TRUE
)
