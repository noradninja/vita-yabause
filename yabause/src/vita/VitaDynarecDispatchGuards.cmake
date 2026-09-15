# Vita-only post-processing for Ari64 native dispatch diagnostics.
#
# Run after CMake configure and before the build, for example:
#   cmake -DVITA_DYNAREC_ASM_SOURCE_FILE=<build>/yabause/src/vita/vita_dynarec/linkage_arm_vita.s \
#         -P <source>/yabause/src/vita/VitaDynarecDispatchGuards.cmake
#
# It can also be included from the normal Vita CMake scope, in which case the
# generated linkage path from PrepareVitaDynarec.cmake is used automatically.

if(DEFINED VITA_DYNAREC_ASM_SOURCE_FILE)
  set(_vita_dispatch_source "${VITA_DYNAREC_ASM_SOURCE_FILE}")
elseif(DEFINED _vita_dynarec_asm_source)
  set(_vita_dispatch_source "${_vita_dynarec_asm_source}")
else()
  message(FATAL_ERROR "Vita dynarec dispatch guards: generated linkage path unavailable")
endif()

if(NOT EXISTS "${_vita_dispatch_source}")
  message(FATAL_ERROR "Vita dynarec dispatch guards: ${_vita_dispatch_source} does not exist")
endif()

file(READ "${_vita_dispatch_source}" _vita_dispatch_linkage)

function(vita_dispatch_replace_once variable needle replacement label)
  string(REPLACE "\\t" "\t" needle "${needle}")
  string(REPLACE "\\t" "\t" replacement "${replacement}")
  string(FIND "${${variable}}" "${needle}" _first)
  if(_first EQUAL -1)
    message(FATAL_ERROR "Vita dynarec dispatch guard failed: ${label} anchor not found")
  endif()
  math(EXPR _after "${_first} + 1")
  string(SUBSTRING "${${variable}}" ${_after} -1 _tail)
  string(FIND "${_tail}" "${needle}" _second)
  if(NOT _second EQUAL -1)
    message(FATAL_ERROR "Vita dynarec dispatch guard failed: ${label} anchor is not unique")
  endif()
  string(REPLACE "${needle}" "${replacement}" _updated "${${variable}}")
  set(${variable} "${_updated}" PARENT_SCOPE)
endfunction()

set(_dyna_clean_anchor [=[\tmov\tr5, r1
\tadd\tr1, r1, r12, asr #6
\tteq\tr1, r4
\tmoveq\tpc, r4 /* Stale i-cache */
\tbl\tadd_link]=])
set(_dyna_clean_replacement [=[\tmov\tr5, r1
\tadd\tr1, r1, r12, asr #6
\tteq\tr1, r4
\tbne\t.vita_dynarec_dyna_clean_link
\tmov\tr12, r4
\ttst\tr12, #3
\tbne\t.vita_dynarec_bad_dyna_clean_fast
\tldr\tr3, .vita_dynarec_target_ptr
\tldr\tr3, [r3]
\tcmp\tr12, r3
\tblo\t.vita_dynarec_bad_dyna_clean_fast
\tadd\tr3, r3, #0x01000000
\tcmp\tr12, r3
\tbhs\t.vita_dynarec_bad_dyna_clean_fast
\tmov\tpc, r12
.vita_dynarec_dyna_clean_link:
\tbl\tadd_link]=])
vita_dispatch_replace_once(_vita_dispatch_linkage "${_dyna_clean_anchor}" "${_dyna_clean_replacement}" "dyna_linker clean fast path")

set(_dyna_patch_anchor [=[\tmov\tr0, r5
\tbl\tvita_dynarec_patch_word
\tmov\tpc, r4]=])
set(_dyna_patch_replacement [=[\tmov\tr0, r5
\tbl\tvita_dynarec_patch_word
\tmov\tr12, r4
\ttst\tr12, #3
\tbne\t.vita_dynarec_bad_dyna_clean_linked
\tldr\tr3, .vita_dynarec_target_ptr
\tldr\tr3, [r3]
\tcmp\tr12, r3
\tblo\t.vita_dynarec_bad_dyna_clean_linked
\tadd\tr3, r3, #0x01000000
\tcmp\tr12, r3
\tbhs\t.vita_dynarec_bad_dyna_clean_linked
\tmov\tpc, r12]=])
vita_dispatch_replace_once(_vita_dispatch_linkage "${_dyna_patch_anchor}" "${_dyna_patch_replacement}" "dyna_linker post-link target")

set(_dyna_dirty_anchor [=[\tstr\tr2, [r6, #8]
\tstr\tr3, [r6, #12]
\tmov\tpc, r1
.B8:]=])
set(_dyna_dirty_replacement [=[\tstr\tr2, [r6, #8]
\tstr\tr3, [r6, #12]
\tmov\tr12, r1
\ttst\tr12, #3
\tbne\t.vita_dynarec_bad_dyna_dirty
\tldr\tr3, .vita_dynarec_target_ptr
\tldr\tr3, [r3]
\tcmp\tr12, r3
\tblo\t.vita_dynarec_bad_dyna_dirty
\tadd\tr3, r3, #0x01000000
\tcmp\tr12, r3
\tbhs\t.vita_dynarec_bad_dyna_dirty
\tmov\tpc, r12
.B8:]=])
vita_dispatch_replace_once(_vita_dispatch_linkage "${_dyna_dirty_anchor}" "${_dyna_dirty_replacement}" "dyna_linker dirty target")

set(_jump_miss_anchor [=[.vita_dynarec_jump_hash_miss:
\tbl\tget_addr
\tmov\tpc, r0]=])
set(_jump_miss_replacement [=[.vita_dynarec_jump_hash_miss:
\tstmdb\tsp!, {r0, r1}
\tbl\tget_addr
\tldr\tr3, [sp], #4
\tadd\tsp, sp, #4
\tmov\tr12, r0
\ttst\tr12, #3
\tbne\t.vita_dynarec_bad_jump_getaddr
\tldr\tr2, .vita_dynarec_target_ptr
\tldr\tr2, [r2]
\tcmp\tr12, r2
\tblo\t.vita_dynarec_bad_jump_getaddr
\tadd\tr2, r2, #0x01000000
\tcmp\tr12, r2
\tbhs\t.vita_dynarec_bad_jump_getaddr
\tmov\tpc, r12]=])
vita_dispatch_replace_once(_vita_dispatch_linkage "${_jump_miss_anchor}" "${_jump_miss_replacement}" "jump_vaddr get_addr target")

set(_slave_irq_anchor [=[slave_handle_interrupts:
\tbl\tDynarecSlaveHandleInterrupts
\tldr\tr10, [fp, #slave_cc-dynarec_local]
\tsub\tr10, r10, r6
\tldr\tpc, [fp, #slave_ip-dynarec_local]]=])
set(_slave_irq_replacement [=[slave_handle_interrupts:
\tbl\tDynarecSlaveHandleInterrupts
\tldr\tr10, [fp, #slave_cc-dynarec_local]
\tsub\tr10, r10, r6
\tldr\tr12, [fp, #slave_ip-dynarec_local]
\ttst\tr12, #3
\tbne\t.vita_dynarec_bad_slave_irq
\tldr\tr0, .vita_dynarec_target_ptr
\tldr\tr0, [r0]
\tcmp\tr12, r0
\tblo\t.vita_dynarec_bad_slave_irq
\tadd\tr0, r0, #0x01000000
\tcmp\tr12, r0
\tbhs\t.vita_dynarec_bad_slave_irq
\tmov\tpc, r12]=])
vita_dispatch_replace_once(_vita_dispatch_linkage "${_slave_irq_anchor}" "${_slave_irq_replacement}" "slave interrupt generated-code return")

set(_master_entry_anchor [=[\tldr\tr10, [fp, #master_cc-dynarec_local]
\tsub\tr10, r10, r6
\tmov\tpc, r14
master_handle_interrupts:]=])
set(_master_entry_replacement [=[\tldr\tr10, [fp, #master_cc-dynarec_local]
\tsub\tr10, r10, r6
\ttst\tr14, #3
\tbne\t.vita_dynarec_bad_master_entry
\tldr\tr12, .vita_dynarec_target_ptr
\tldr\tr12, [r12]
\tcmp\tr14, r12
\tblo\t.vita_dynarec_bad_master_entry
\tadd\tr12, r12, #0x01000000
\tcmp\tr14, r12
\tbhs\t.vita_dynarec_bad_master_entry
\tmov\tpc, r14
master_handle_interrupts:]=])
vita_dispatch_replace_once(_vita_dispatch_linkage "${_master_entry_anchor}" "${_master_entry_replacement}" "master generated-code entry")

set(_master_irq_anchor [=[\tldr\tr14, [fp, #master_ip-dynarec_local]
\tsub\tr10, r10, r6
\tmov\tpc, r14
.dlptr:]=])
set(_master_irq_replacement [=[\tldr\tr14, [fp, #master_ip-dynarec_local]
\tsub\tr10, r10, r6
\ttst\tr14, #3
\tbne\t.vita_dynarec_bad_master_irq
\tldr\tr12, .vita_dynarec_target_ptr
\tldr\tr12, [r12]
\tcmp\tr14, r12
\tblo\t.vita_dynarec_bad_master_irq
\tadd\tr12, r12, #0x01000000
\tcmp\tr14, r12
\tbhs\t.vita_dynarec_bad_master_irq
\tmov\tpc, r14
.dlptr:]=])
vita_dispatch_replace_once(_vita_dispatch_linkage "${_master_irq_anchor}" "${_master_irq_replacement}" "master interrupt generated-code return")

set(_slave_entry_anchor [=[\tldr\tr10, [fp, #slave_cc-dynarec_local]
\tsub\tr10, r10, r6
\tmov\tpc, r4
slave_handle_interrupts:]=])
set(_slave_entry_replacement [=[\tldr\tr10, [fp, #slave_cc-dynarec_local]
\tsub\tr10, r10, r6
\tmov\tr12, r4
\ttst\tr12, #3
\tbne\t.vita_dynarec_bad_slave_entry
\tldr\tr0, .vita_dynarec_target_ptr
\tldr\tr0, [r0]
\tcmp\tr12, r0
\tblo\t.vita_dynarec_bad_slave_entry
\tadd\tr0, r0, #0x01000000
\tcmp\tr12, r0
\tbhs\t.vita_dynarec_bad_slave_entry
\tmov\tpc, r12
slave_handle_interrupts:]=])
vita_dispatch_replace_once(_vita_dispatch_linkage "${_slave_entry_anchor}" "${_slave_entry_replacement}" "slave generated-code entry")

string(APPEND _vita_dispatch_linkage [=[

\t.text
\t.align\t2
.vita_dynarec_bad_master_entry:
\tmov\tr0, r14
\tldr\tr1, [fp, #master_pc-dynarec_local]
\tmov\tr2, #5
\tmov\tr3, fp
\tb\tvita_dynarec_bad_dispatch
.vita_dynarec_bad_master_irq:
\tmov\tr0, r14
\tldr\tr1, [fp, #master_pc-dynarec_local]
\tmov\tr2, #6
\tmov\tr3, fp
\tb\tvita_dynarec_bad_dispatch
.vita_dynarec_bad_slave_entry:
\tmov\tr0, r12
\tldr\tr1, [fp, #slave_pc-dynarec_local]
\tmov\tr2, #7
\tmov\tr3, fp
\tb\tvita_dynarec_bad_dispatch
.vita_dynarec_bad_slave_irq:
\tmov\tr0, r12
\tldr\tr1, [fp, #slave_pc-dynarec_local]
\tmov\tr2, #8
\tmov\tr3, fp
\tb\tvita_dynarec_bad_dispatch
.vita_dynarec_bad_dyna_clean_fast:
\tmov\tr3, r5
\tmov\tr1, r0
\tmov\tr0, r12
\tmov\tr2, #9
\tb\tvita_dynarec_bad_dispatch
.vita_dynarec_bad_dyna_clean_linked:
\tmov\tr3, r5
\tmov\tr1, r0
\tmov\tr0, r12
\tmov\tr2, #10
\tb\tvita_dynarec_bad_dispatch
.vita_dynarec_bad_dyna_dirty:
\tmov\tr3, r4
\tmov\tr1, r0
\tmov\tr0, r12
\tmov\tr2, #11
\tb\tvita_dynarec_bad_dispatch
.vita_dynarec_bad_jump_getaddr:
\tmov\tr1, r3
\tmov\tr0, r12
\tmov\tr2, #12
\tmov\tr3, #0
\tb\tvita_dynarec_bad_dispatch
]=])

string(REPLACE "\\t" "\t" _vita_dispatch_linkage "${_vita_dispatch_linkage}")
file(WRITE "${_vita_dispatch_source}" "${_vita_dispatch_linkage}")
message(STATUS "Applied Vita Ari64 extended native-dispatch guards to ${_vita_dispatch_source}")
