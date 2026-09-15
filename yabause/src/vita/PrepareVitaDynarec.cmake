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
# proven unreliable for generated sources. Compile the generated linkage with
# an explicit gcc invocation, then hand the resulting object to target_sources.
# CMakeLists.txt already consumes VITA_DYNAREC_ASM_SOURCE, so expose the object
# under that existing variable name.
set(VITA_DYNAREC_ASM_SOURCE "${_vita_dynarec_asm_object}")

file(MAKE_DIRECTORY "${_generated_dir}")
file(READ "${_dynarec_source}" _dynarec)
file(READ "${_linkage_source}" _linkage)

function(vita_dynarec_replace_once variable needle replacement label)
  # New Vita-only assembly patches use readable \t escapes in bracket strings.
  # Normalize those to real tabs before matching/inserting GAS source; existing
  # anchors which already contain literal tabs are unchanged.
  string(REPLACE "\\t" "	" needle "${needle}")
  string(REPLACE "\\t" "	" replacement "${replacement}")
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
#ifdef VITA_DYNAREC_TEST
  extern int vita_dynarec_exec_test_active;
  extern unsigned int vita_dynarec_exec_test_instruction_limit;
  extern void vita_dynarec_test_return(void);
#endif
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

# The execution smoke can compile a deliberately short synthetic block. Stop
# decoding once the requested instruction count is reached while that explicit
# test is active; normal production-smoke compilation remains unchanged.
set(_single_step_anchor [=[    if(itype[i]==NI&&opcode[i]==0x11) {
      done=stop_after_jal=1;
      printf("Disabled speculative precompilation\n");
    }
    if(!done&&i<MAXBLOCK-1) {]=])
set(_single_step_replacement [=[    if(itype[i]==NI&&opcode[i]==0x11) {
      done=stop_after_jal=1;
      printf("Disabled speculative precompilation\n");
    }
#ifdef VITA_DYNAREC_TEST
    if(vita_dynarec_exec_test_active &&
       vita_dynarec_exec_test_instruction_limit != 0 &&
       (unsigned)(i + 1) >= vita_dynarec_exec_test_instruction_limit)
      done=1;
#endif
    if(!done&&i<MAXBLOCK-1) {]=])
vita_dynarec_replace_once(_dynarec "${_single_step_anchor}" "${_single_step_replacement}" "bounded execution smoke decode limit")

# Multi-instruction synthetic blocks take this normal fallthrough path. During
# the bounded smoke, branch directly to the return trampoline after Ari64 has
# written back dirty SH2 state and advanced the cycle register instead of
# entering the normal dynamic linker.
set(_bounded_multi_exit_anchor [=[      add_to_linker((int)out,start+i*2,0);
      emit_jmp(0);
    }
  }
  else]=])
set(_bounded_multi_exit_replacement [=[#ifdef VITA_DYNAREC_TEST
      if(vita_dynarec_exec_test_active) {
        emit_jmp((int)vita_dynarec_test_return);
      } else
#endif
      {
        add_to_linker((int)out,start+i*2,0);
        emit_jmp(0);
      }
    }
  }
  else]=])
vita_dynarec_replace_once(_dynarec "${_bounded_multi_exit_anchor}" "${_bounded_multi_exit_replacement}" "bounded multi-instruction execution return")

# Single-instruction synthetic blocks use the alternate fallthrough path; keep
# it bounded for the same reason.
set(_bounded_exit_anchor [=[  else
  {
    assert(i>0);
    store_regs_bt(regs[i-1].regmap,regs[i-1].dirty,start+i*2);
    if(regs[i-1].regmap[HOST_CCREG]!=CCREG)
      emit_loadreg(CCREG,HOST_CCREG);
    emit_addimm(HOST_CCREG,CLOCK_DIVIDER*(ccadj[i-1]+1),HOST_CCREG);
    add_to_linker((int)out,start+i*2,0);
    emit_jmp(0);
  }]=])
set(_bounded_exit_replacement [=[  else
  {
    assert(i>0);
    store_regs_bt(regs[i-1].regmap,regs[i-1].dirty,start+i*2);
    if(regs[i-1].regmap[HOST_CCREG]!=CCREG)
      emit_loadreg(CCREG,HOST_CCREG);
    emit_addimm(HOST_CCREG,CLOCK_DIVIDER*(ccadj[i-1]+1),HOST_CCREG);
#ifdef VITA_DYNAREC_TEST
    if(vita_dynarec_exec_test_active) {
      emit_jmp((int)vita_dynarec_test_return);
    } else
#endif
    {
      add_to_linker((int)out,start+i*2,0);
      emit_jmp(0);
    }
  }]=])
vita_dynarec_replace_once(_dynarec "${_bounded_exit_anchor}" "${_bounded_exit_replacement}" "bounded execution return")

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

# dyna_linker normally patches an already-published ARM branch with a direct
# STR. On Vita that write must happen inside the VM write domain and be
# published before execution resumes. Route the one runtime self-modifying
# store through the VM backend instead of writing executable memory directly.
set(_runtime_patch_anchor "\tstr\tr1, [r5]\n\tmov\tpc, r4")
set(_runtime_patch_replacement "\tmov\tr0, r5\n\tbl\tvita_dynarec_patch_word\n\tmov\tpc, r4")
vita_dynarec_replace_once(_linkage "${_runtime_patch_anchor}" "${_runtime_patch_replacement}" "runtime dyna_linker patch")

# Dynamic SH2 block targets are ARM code emitted into the Vita VM. A valid
# dispatch target must therefore be word aligned and lie inside the 16 MiB VM.
# Guard the two hash-table dispatch paths which use LDR PC and can otherwise
# switch ARM/Thumb state from bit 0 of a corrupted host pointer. On failure,
# branch to an ARM-only crash stub which leaves the bad target, guest target,
# dispatch-site ID, and hash-slot address in r0-r3 for the Vita core dump.
set(_dyna_hash_anchor [=[\tldr\tr7, [r6, r4]!
\tteq\tr7, r0
\tldreq\tpc, [r6, #4]
\tldr\tr7, [r6, #8]
\tteq\tr7, r0
\tldreq\tpc, [r6, #12]]=])
set(_dyna_hash_replacement [=[\tldr\tr7, [r6, r4]!
\tteq\tr7, r0
\tbne\t.vita_dynarec_dyna_hash_second
\tldr\tr12, [r6, #4]
\ttst\tr12, #3
\tbne\t.vita_dynarec_bad_dyna_hash0
\tldr\tr3, .vita_dynarec_target_ptr
\tldr\tr3, [r3]
\tcmp\tr12, r3
\tblo\t.vita_dynarec_bad_dyna_hash0
\tadd\tr3, r3, #0x01000000
\tcmp\tr12, r3
\tbhs\t.vita_dynarec_bad_dyna_hash0
\tmov\tpc, r12
.vita_dynarec_dyna_hash_second:
\tldr\tr7, [r6, #8]
\tteq\tr7, r0
\tbne\t.B6
\tldr\tr12, [r6, #12]
\ttst\tr12, #3
\tbne\t.vita_dynarec_bad_dyna_hash1
\tldr\tr3, .vita_dynarec_target_ptr
\tldr\tr3, [r3]
\tcmp\tr12, r3
\tblo\t.vita_dynarec_bad_dyna_hash1
\tadd\tr3, r3, #0x01000000
\tcmp\tr12, r3
\tbhs\t.vita_dynarec_bad_dyna_hash1
\tmov\tpc, r12]=])
vita_dynarec_replace_once(_linkage "${_dyna_hash_anchor}" "${_dyna_hash_replacement}" "dyna_linker hash dispatch guard")

set(_jump_hash_anchor [=[\tldr\tr2, [r1, r2]!
\tteq\tr2, r0
\tldreq\tpc, [r1, #4]
\tldr\tr2, [r1, #8]
\tteq\tr2, r0
\tldreq\tpc, [r1, #12]
\tbl\tget_addr
\tmov\tpc, r0]=])
set(_jump_hash_replacement [=[\tldr\tr2, [r1, r2]!
\tteq\tr2, r0
\tbne\t.vita_dynarec_jump_hash_second
\tldr\tr12, [r1, #4]
\ttst\tr12, #3
\tbne\t.vita_dynarec_bad_jump_hash0
\tldr\tr3, .vita_dynarec_target_ptr
\tldr\tr3, [r3]
\tcmp\tr12, r3
\tblo\t.vita_dynarec_bad_jump_hash0
\tadd\tr3, r3, #0x01000000
\tcmp\tr12, r3
\tbhs\t.vita_dynarec_bad_jump_hash0
\tmov\tpc, r12
.vita_dynarec_jump_hash_second:
\tldr\tr2, [r1, #8]
\tteq\tr2, r0
\tbne\t.vita_dynarec_jump_hash_miss
\tldr\tr12, [r1, #12]
\ttst\tr12, #3
\tbne\t.vita_dynarec_bad_jump_hash1
\tldr\tr3, .vita_dynarec_target_ptr
\tldr\tr3, [r3]
\tcmp\tr12, r3
\tblo\t.vita_dynarec_bad_jump_hash1
\tadd\tr3, r3, #0x01000000
\tcmp\tr12, r3
\tbhs\t.vita_dynarec_bad_jump_hash1
\tmov\tpc, r12
.vita_dynarec_jump_hash_miss:
\tbl\tget_addr
\tmov\tpc, r0]=])
vita_dynarec_replace_once(_linkage "${_jump_hash_anchor}" "${_jump_hash_replacement}" "jump_vaddr hash dispatch guard")

# ARM-only invalid-dispatch trap plus the bounded generated-code entry/return
# trampoline. The trap intentionally data-aborts at address zero after moving
# the diagnostic payload into r0-r3:
#   r0 = invalid native host target
#   r1 = guest SH2 target address
#   r2 = site (1/2=dyna_linker slot 0/1, 3/4=jump_vaddr slot 0/1)
#   r3 = hash-table slot address
string(APPEND _linkage [=[

\t.text
\t.align\t2
.vita_dynarec_bad_dyna_hash0:
\tmov\tr3, r6
\tmov\tr1, r0
\tmov\tr0, r12
\tmov\tr2, #1
\tb\tvita_dynarec_bad_dispatch
.vita_dynarec_bad_dyna_hash1:
\tmov\tr3, r6
\tmov\tr1, r0
\tmov\tr0, r12
\tmov\tr2, #2
\tb\tvita_dynarec_bad_dispatch
.vita_dynarec_bad_jump_hash0:
\tmov\tr3, r1
\tmov\tr1, r0
\tmov\tr0, r12
\tmov\tr2, #3
\tb\tvita_dynarec_bad_dispatch
.vita_dynarec_bad_jump_hash1:
\tmov\tr3, r1
\tmov\tr1, r0
\tmov\tr0, r12
\tmov\tr2, #4
\tb\tvita_dynarec_bad_dispatch

\t.align\t2
\t.global\tvita_dynarec_bad_dispatch
\t.type\tvita_dynarec_bad_dispatch, %function
vita_dynarec_bad_dispatch:
\tmov\tr12, #0
\tldr\tr12, [r12]
\tb\t.
\t.size\tvita_dynarec_bad_dispatch, .-vita_dynarec_bad_dispatch

\t.align\t2
.vita_dynarec_target_ptr:
\t.word\tsh2_dynarec_target

\t.align\t2
\t.global\tvita_dynarec_test_enter
\t.type\tvita_dynarec_test_enter, %function
vita_dynarec_test_enter:
\tstmdb\tsp!, {r4-r11, lr}
\tsub\tsp, sp, #4
\tldr\tfp, .vita_dynarec_test_dlptr
\tbx\tr0
\t.size\tvita_dynarec_test_enter, .-vita_dynarec_test_enter

\t.align\t2
\t.global\tvita_dynarec_test_return
\t.type\tvita_dynarec_test_return, %function
vita_dynarec_test_return:
\tldr\tr0, .vita_dynarec_test_ccptr
\tstr\tr10, [r0]
\tadd\tsp, sp, #4
\tldmia\tsp!, {r4-r11, pc}
\t.size\tvita_dynarec_test_return, .-vita_dynarec_test_return

\t.align\t2
.vita_dynarec_test_dlptr:
\t.word\tdynarec_local
.vita_dynarec_test_ccptr:
\t.word\tmaster_cc
]=])

# Keep all generated-code dispatch diagnostics beyond the original four hash
# guards in a separate post-processing step.  Include it after the trampoline
# append so the bounded smoke entry can be guarded there as well.
string(REPLACE "\\t" "\t" _linkage "${_linkage}")
include("${CMAKE_CURRENT_LIST_DIR}/ApplyVitaDynarecDispatchGuards.cmake")

# Normalize readable tab escapes in the appended diagnostic assembly too.
string(REPLACE "\\t" "	" _linkage "${_linkage}")

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

# The custom command lives in src/vita while the generated object is consumed
# by the parent-directory 'yabause' static-library target. Register an explicit
# custom target and dependency so Ninja emits the producer rule before it sees
# the object as an input to libyabause.a.
add_custom_target(vita_dynarec_linkage_object
  DEPENDS "${_vita_dynarec_asm_object}"
)
add_dependencies(yabause vita_dynarec_linkage_object)

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
