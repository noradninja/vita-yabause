# Prepare Vita-only copies of the legacy Ari64 sources without modifying the
# imported dynarec implementation for other platforms. Exact anchor checks
# deliberately fail configuration if the upstream source changes underneath
# these adaptations.

set(_dynarec_source "${CMAKE_CURRENT_LIST_DIR}/../sh2_dynarec/sh2_dynarec.c")
set(_linkage_source "${CMAKE_CURRENT_LIST_DIR}/../sh2_dynarec/linkage_arm.s")
set(_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/vita_dynarec")
set(VITA_DYNAREC_C_SOURCE "${_generated_dir}/sh2_dynarec_vita.c")
set(VITA_DYNAREC_ASM_SOURCE "${_generated_dir}/linkage_arm_vita.s")

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
string(REGEX REPLACE "[ 	]*\\.global[ 	]+sh2_dynarec_target\n" "" _linkage "${_linkage}")
string(REGEX REPLACE
  "[ 	]*\\.bss\n[ 	]*\\.align[ 	]+12\n[ 	]*\\.type[ 	]+sh2_dynarec_target,[ 	]*%object\n[ 	]*\\.size[ 	]+sh2_dynarec_target,[ 	]*16777216\nsh2_dynarec_target:\n[ 	]*\\.space[ 	]+16777216\n[ 	]*\\.align[ 	]+4\n[ 	]*\\.type[ 	]+dynarec_local,[ 	]*%object"
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
file(WRITE "${VITA_DYNAREC_ASM_SOURCE}" "${_linkage}")
