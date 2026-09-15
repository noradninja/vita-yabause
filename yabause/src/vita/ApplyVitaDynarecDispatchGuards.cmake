# Vita Ari64 generated-code dispatch diagnostics.
#
# This file is included by PrepareVitaDynarec.cmake after the legacy ARM
# linkage has received the four hash-table guards.  Every transfer patched
# here must enter ARM code in the 16 MiB Vita dynarec VM.  Native helper
# returns are deliberately outside this experiment.

function(vita_dynarec_guard_replace needle replacement label)
  vita_dynarec_replace_once(_linkage "${needle}" "${replacement}" "${label}")
  set(_linkage "${_linkage}" PARENT_SCOPE)
endfunction()

# master_ip is live in r14 at each scheduler resume.  Preserve every allocatable
# caller-save register on the valid path; a resumed block may have any of them
# mapped.  Site 9 is the initial frame entry, 13 follows interrupt handling,
# and 12 is the normal deciline resume.
set(_master_initial_anchor [=[\tldr\tr10, [fp, #master_cc-dynarec_local]
\tsub\tr10, r10, r6
\tmov\tpc, r14
master_handle_interrupts:]=])
set(_master_initial_replacement [=[\tldr\tr10, [fp, #master_cc-dynarec_local]
\tsub\tr10, r10, r6
\tstmdb\tsp!, {r0-r3, r12}
\ttst\tr14, #3
\tbne\t.vita_dynarec_bad_master_initial
\tldr\tr2, .vita_dynarec_scheduler_target_ptr
\tldr\tr2, [r2]
\tcmp\tr14, r2
\tblo\t.vita_dynarec_bad_master_initial
\tadd\tr2, r2, #0x01000000
\tcmp\tr14, r2
\tbhs\t.vita_dynarec_bad_master_initial
\tldmia\tsp!, {r0-r3, r12}
\tmov\tpc, r14
master_handle_interrupts:]=])
vita_dynarec_guard_replace("${_master_initial_anchor}" "${_master_initial_replacement}" "master initial generated-code resume guard")

set(_master_interrupt_anchor [=[\tldr\tr14, [fp, #master_ip-dynarec_local]
\tsub\tr10, r10, r6
\tmov\tpc, r14
.dlptr:]=])
set(_master_interrupt_replacement [=[\tldr\tr14, [fp, #master_ip-dynarec_local]
\tsub\tr10, r10, r6
\tstmdb\tsp!, {r0-r3, r12}
\ttst\tr14, #3
\tbne\t.vita_dynarec_bad_master_interrupt
\tldr\tr2, .vita_dynarec_scheduler_target_ptr
\tldr\tr2, [r2]
\tcmp\tr14, r2
\tblo\t.vita_dynarec_bad_master_interrupt
\tadd\tr2, r2, #0x01000000
\tcmp\tr14, r2
\tbhs\t.vita_dynarec_bad_master_interrupt
\tldmia\tsp!, {r0-r3, r12}
\tmov\tpc, r14
.dlptr:]=])
vita_dynarec_guard_replace("${_master_interrupt_anchor}" "${_master_interrupt_replacement}" "master post-interrupt generated-code resume guard")

# Keep a literal pool close to the scheduler guards; ARM LDR literals are
# range-limited and the common diagnostic tail is intentionally much later.
set(_scheduler_literal_anchor [=[.ccptr:
\t.word\tcached_code
\t.size\tYabauseDynarecOneFrameExec, .-YabauseDynarecOneFrameExec]=])
set(_scheduler_literal_replacement [=[.ccptr:
\t.word\tcached_code
.vita_dynarec_scheduler_target_ptr:
\t.word\tsh2_dynarec_target
\t.size\tYabauseDynarecOneFrameExec, .-YabauseDynarecOneFrameExec]=])
vita_dynarec_guard_replace("${_scheduler_literal_anchor}" "${_scheduler_literal_replacement}" "scheduler dynarec target literal")

set(_slave_normal_anchor [=[\tldr\tr10, [fp, #slave_cc-dynarec_local]
\tsub\tr10, r10, r6
\tmov\tpc, r4
slave_handle_interrupts:]=])
set(_slave_normal_replacement [=[\tldr\tr10, [fp, #slave_cc-dynarec_local]
\tsub\tr10, r10, r6
\tstmdb\tsp!, {r0-r3, r12}
\ttst\tr4, #3
\tbne\t.vita_dynarec_bad_slave_normal
\tldr\tr2, .vita_dynarec_scheduler_target_ptr
\tldr\tr2, [r2]
\tcmp\tr4, r2
\tblo\t.vita_dynarec_bad_slave_normal
\tadd\tr2, r2, #0x01000000
\tcmp\tr4, r2
\tbhs\t.vita_dynarec_bad_slave_normal
\tldmia\tsp!, {r0-r3, r12}
\tmov\tpc, r4
slave_handle_interrupts:]=])
vita_dynarec_guard_replace("${_slave_normal_anchor}" "${_slave_normal_replacement}" "slave normal generated-code resume guard")

set(_slave_interrupt_anchor [=[\tldr\tr10, [fp, #slave_cc-dynarec_local]
\tsub\tr10, r10, r6
\tldr\tpc, [fp, #slave_ip-dynarec_local]]=])
set(_slave_interrupt_replacement [=[\tldr\tr10, [fp, #slave_cc-dynarec_local]
\tsub\tr10, r10, r6
\tldr\tr4, [fp, #slave_ip-dynarec_local]
\tstmdb\tsp!, {r0-r3, r12}
\ttst\tr4, #3
\tbne\t.vita_dynarec_bad_slave_interrupt
\tldr\tr2, .vita_dynarec_scheduler_target_ptr
\tldr\tr2, [r2]
\tcmp\tr4, r2
\tblo\t.vita_dynarec_bad_slave_interrupt
\tadd\tr2, r2, #0x01000000
\tcmp\tr4, r2
\tbhs\t.vita_dynarec_bad_slave_interrupt
\tldmia\tsp!, {r0-r3, r12}
\tmov\tpc, r4]=])
vita_dynarec_guard_replace("${_slave_interrupt_anchor}" "${_slave_interrupt_replacement}" "slave post-interrupt generated-code resume guard")

set(_master_deciline_anchor [=[\tldr\tr10, [fp, #master_cc-dynarec_local]
\tsub\tr10, r10, r6
\tmov\tpc, r14
.A2:]=])
set(_master_deciline_replacement [=[\tldr\tr10, [fp, #master_cc-dynarec_local]
\tsub\tr10, r10, r6
\tstmdb\tsp!, {r0-r3, r12}
\ttst\tr14, #3
\tbne\t.vita_dynarec_bad_master_deciline
\tldr\tr2, .vita_dynarec_scheduler_target_ptr
\tldr\tr2, [r2]
\tcmp\tr14, r2
\tblo\t.vita_dynarec_bad_master_deciline
\tadd\tr2, r2, #0x01000000
\tcmp\tr14, r2
\tbhs\t.vita_dynarec_bad_master_deciline
\tldmia\tsp!, {r0-r3, r12}
\tmov\tpc, r14
.A2:]=])
vita_dynarec_guard_replace("${_master_deciline_anchor}" "${_master_deciline_replacement}" "master deciline generated-code resume guard")

# dyna_linker retains the native target in r4.  Preserve the original guest
# target and patch address on the diagnostic stack so the failure payload can
# identify both the bad consumer and its linkage metadata.
set(_dyna_stale_anchor [=[\tteq\tr1, r4
\tmoveq\tpc, r4 /* Stale i-cache */
\tbl\tadd_link]=])
set(_dyna_stale_replacement [=[\tteq\tr1, r4
\tbne\t.vita_dynarec_dyna_needs_link
\tstmdb\tsp!, {r0-r3, r12}
\ttst\tr4, #3
\tbne\t.vita_dynarec_bad_dyna_stale
\tldr\tr2, .vita_dynarec_target_ptr
\tldr\tr2, [r2]
\tcmp\tr4, r2
\tblo\t.vita_dynarec_bad_dyna_stale
\tadd\tr2, r2, #0x01000000
\tcmp\tr4, r2
\tbhs\t.vita_dynarec_bad_dyna_stale
\tldmia\tsp!, {r0-r3, r12}
\tmov\tpc, r4 /* Stale i-cache */
.vita_dynarec_dyna_needs_link:
\tbl\tadd_link]=])
vita_dynarec_guard_replace("${_dyna_stale_anchor}" "${_dyna_stale_replacement}" "dyna_linker stale-cache dispatch guard")

# PrepareVitaDynarec has already routed the runtime patch through the Vita VM
# backend, so this anchor intentionally matches that generated form.
set(_dyna_linked_anchor [=[\tmov\tr0, r5
\tbl\tvita_dynarec_patch_word
\tmov\tpc, r4
.B3:]=])
set(_dyna_linked_replacement [=[\tstmdb\tsp!, {r0, r1}
\tmov\tr0, r5
\tbl\tvita_dynarec_patch_word
\ttst\tr4, #3
\tbne\t.vita_dynarec_bad_dyna_linked
\tldr\tr2, .vita_dynarec_target_ptr
\tldr\tr2, [r2]
\tcmp\tr4, r2
\tblo\t.vita_dynarec_bad_dyna_linked
\tadd\tr2, r2, #0x01000000
\tcmp\tr4, r2
\tbhs\t.vita_dynarec_bad_dyna_linked
\tadd\tsp, sp, #8
\tmov\tpc, r4
.B3:]=])
vita_dynarec_guard_replace("${_dyna_linked_anchor}" "${_dyna_linked_replacement}" "dyna_linker post-link dispatch guard")

set(_dyna_dirty_anchor [=[\tstr\tr2, [r6, #8]
\tstr\tr3, [r6, #12]
\tmov\tpc, r1
.B8:]=])
set(_dyna_dirty_replacement [=[\tstr\tr2, [r6, #8]
\tstr\tr3, [r6, #12]
\tstmdb\tsp!, {r0-r3, r12}
\ttst\tr1, #3
\tbne\t.vita_dynarec_bad_dyna_dirty
\tldr\tr3, .vita_dynarec_target_ptr
\tldr\tr3, [r3]
\tcmp\tr1, r3
\tblo\t.vita_dynarec_bad_dyna_dirty
\tadd\tr3, r3, #0x01000000
\tcmp\tr1, r3
\tbhs\t.vita_dynarec_bad_dyna_dirty
\tldmia\tsp!, {r0-r3, r12}
\tmov\tpc, r1
.B8:]=])
vita_dynarec_guard_replace("${_dyna_dirty_anchor}" "${_dyna_dirty_replacement}" "dyna_linker dirty-list dispatch guard")

# Save the guest key across get_addr so the trap can distinguish a bad return
# from the lookup that produced it.  Both callers already cross a C ABI call,
# so no caller-save register other than the returned r0 remains live.
set(_jump_get_addr_anchor [=[.vita_dynarec_jump_hash_miss:
\tbl\tget_addr
\tmov\tpc, r0]=])
set(_jump_get_addr_replacement [=[.vita_dynarec_jump_hash_miss:
\tstmdb\tsp!, {r0, r1}
\tbl\tget_addr
\ttst\tr0, #3
\tbne\t.vita_dynarec_bad_jump_get_addr
\tldr\tr3, .vita_dynarec_target_ptr
\tldr\tr3, [r3]
\tcmp\tr0, r3
\tblo\t.vita_dynarec_bad_jump_get_addr
\tadd\tr3, r3, #0x01000000
\tcmp\tr0, r3
\tbhs\t.vita_dynarec_bad_jump_get_addr
\tadd\tsp, sp, #8
\tmov\tpc, r0]=])
vita_dynarec_guard_replace("${_jump_get_addr_anchor}" "${_jump_get_addr_replacement}" "jump_vaddr get_addr return guard")

# Generated-code helpers return through lr.  Their callers are emitted ARM BL
# instructions, so lr should always be a word-aligned address in the dynarec
# VM.  The macro preserves all caller-save values on a valid return and records
# the saved-register frame in r3 on failure.
set(_lr_guard_macro_anchor [=[\t.global\tverify_code
\t.type\tverify_code, %function]=])
set(_lr_guard_macro_replacement [=[\t.macro\tvita_dynarec_guard_lr site
\tstmdb\tsp!, {r0-r3, r12}
\ttst\tlr, #3
\tbne\t.Lvita_dynarec_bad_lr\@
\tldr\tr2, .vita_dynarec_target_ptr
\tldr\tr2, [r2]
\tcmp\tlr, r2
\tblo\t.Lvita_dynarec_bad_lr\@
\tadd\tr2, r2, #0x01000000
\tcmp\tlr, r2
\tbhs\t.Lvita_dynarec_bad_lr\@
\tldmia\tsp!, {r0-r3, r12}
\tmov\tpc, lr
.Lvita_dynarec_bad_lr\@:
\tmov\tr0, lr
\tmov\tr1, #0
\tmov\tr2, #\site
\tmov\tr3, sp
\tb\tvita_dynarec_bad_dispatch
\t.endm

\t.macro\tvita_dynarec_guard_lr_guest site, guest_pc
\tstmdb\tsp!, {r0-r3, r12}
\ttst\tlr, #3
\tbne\t.Lvita_dynarec_bad_lr_guest\@
\tldr\tr2, .vita_dynarec_target_ptr
\tldr\tr2, [r2]
\tcmp\tlr, r2
\tblo\t.Lvita_dynarec_bad_lr_guest\@
\tadd\tr2, r2, #0x01000000
\tcmp\tlr, r2
\tbhs\t.Lvita_dynarec_bad_lr_guest\@
\tldmia\tsp!, {r0-r3, r12}
\tmov\tpc, lr
.Lvita_dynarec_bad_lr_guest\@:
\tmov\tr0, lr
\tldr\tr1, [fp, #\guest_pc-dynarec_local]
\tmov\tr2, #\site
\tmov\tr3, sp
\tb\tvita_dynarec_bad_dispatch
\t.endm

\t.global\tverify_code
\t.type\tverify_code, %function]=])
vita_dynarec_guard_replace("${_lr_guard_macro_anchor}" "${_lr_guard_macro_replacement}" "generated helper return guard macros")

set(_verify_lr_anchor [=[.D4:
\tmoveq\tpc, lr
.D5:]=])
set(_verify_lr_replacement [=[.D4:
\tbne\t.D5
\tvita_dynarec_guard_lr\t16
.D5:]=])
vita_dynarec_guard_replace("${_verify_lr_anchor}" "${_verify_lr_replacement}" "verify_code clean return guard")

set(_verify_get_addr_anchor [=[.D5:
\tbl\tget_addr
\tmov\tpc, r0]=])
set(_verify_get_addr_replacement [=[.D5:
\tstmdb\tsp!, {r0, r1}
\tbl\tget_addr
\ttst\tr0, #3
\tbne\t.vita_dynarec_bad_verify_get_addr
\tldr\tr3, .vita_dynarec_target_ptr
\tldr\tr3, [r3]
\tcmp\tr0, r3
\tblo\t.vita_dynarec_bad_verify_get_addr
\tadd\tr3, r3, #0x01000000
\tcmp\tr0, r3
\tbhs\t.vita_dynarec_bad_verify_get_addr
\tadd\tsp, sp, #8
\tmov\tpc, r0]=])
vita_dynarec_guard_replace("${_verify_get_addr_anchor}" "${_verify_get_addr_replacement}" "verify_code get_addr return guard")

set(_div_positive_lr_anchor [=[*/
\tmov\tpc, lr
div1_negative_divisor:]=])
set(_div_positive_lr_replacement [=[*/
\tvita_dynarec_guard_lr\t17
div1_negative_divisor:]=])
vita_dynarec_guard_replace("${_div_positive_lr_anchor}" "${_div_positive_lr_replacement}" "DIV1 positive-divisor return guard")

set(_div_negative_lr_anchor [=[\torr\tr2, r2, r3, lsl #8 /* save new Q (=T) */
\tmov\tpc, lr
\t.size\tdiv1, .-div1]=])
set(_div_negative_lr_replacement [=[\torr\tr2, r2, r3, lsl #8 /* save new Q (=T) */
\tvita_dynarec_guard_lr\t18
\t.size\tdiv1, .-div1]=])
vita_dynarec_guard_replace("${_div_negative_lr_anchor}" "${_div_negative_lr_replacement}" "DIV1 negative-divisor return guard")

set(_macl_lr_anchor [=[\ttst\tr4, #2
\tmoveq\tpc, lr
macl_saturation:]=])
set(_macl_lr_replacement [=[\ttst\tr4, #2
\tbne\tmacl_saturation
\tvita_dynarec_guard_lr\t19
macl_saturation:]=])
vita_dynarec_guard_replace("${_macl_lr_anchor}" "${_macl_lr_replacement}" "MACL normal return guard")

set(_macl_saturated_lr_anchor [=[\tsubge\tr1, r7, #1
\tmov\tpc, lr
\t.size\tmacl, .-macl]=])
set(_macl_saturated_lr_replacement [=[\tsubge\tr1, r7, #1
\tvita_dynarec_guard_lr\t20
\t.size\tmacl, .-macl]=])
vita_dynarec_guard_replace("${_macl_saturated_lr_anchor}" "${_macl_saturated_lr_replacement}" "MACL saturated return guard")

set(_macw_lr_anchor [=[\ttst\tr4, #2
\tmoveq\tpc, lr
macw_saturation:]=])
set(_macw_lr_replacement [=[\ttst\tr4, #2
\tbne\tmacw_saturation
\tvita_dynarec_guard_lr\t21
macw_saturation:]=])
vita_dynarec_guard_replace("${_macw_lr_anchor}" "${_macw_lr_replacement}" "MACW normal return guard")

set(_macw_saturated_lr_anchor [=[\tmov\tr1, r9
\tmov\tpc, lr
\t.size\tmacw, .-macw]=])
set(_macw_saturated_lr_replacement [=[\tmov\tr1, r9
\tvita_dynarec_guard_lr\t22
\t.size\tmacw, .-macw]=])
vita_dynarec_guard_replace("${_macw_saturated_lr_anchor}" "${_macw_saturated_lr_replacement}" "MACW saturated return guard")

set(_master_bios_lr_anchor [=[\tldr\tr14, [fp, #master_ip-dynarec_local]
\tldr\tr10, [fp, #master_cc-dynarec_local]
\tmov\tpc, lr
\t.size\tmaster_handle_bios, .-master_handle_bios]=])
set(_master_bios_lr_replacement [=[\tldr\tr14, [fp, #master_ip-dynarec_local]
\tldr\tr10, [fp, #master_cc-dynarec_local]
\tvita_dynarec_guard_lr_guest\t23, master_pc
\t.size\tmaster_handle_bios, .-master_handle_bios]=])
vita_dynarec_guard_replace("${_master_bios_lr_anchor}" "${_master_bios_lr_replacement}" "master BIOS helper return guard")

set(_slave_bios_lr_anchor [=[\tldr\tr14, [fp, #slave_ip-dynarec_local]
\tldr\tr10, [fp, #slave_cc-dynarec_local]
\tmov\tpc, lr
\t.size\tslave_handle_bios, .-slave_handle_bios]=])
set(_slave_bios_lr_replacement [=[\tldr\tr14, [fp, #slave_ip-dynarec_local]
\tldr\tr10, [fp, #slave_cc-dynarec_local]
\tvita_dynarec_guard_lr_guest\t24, slave_pc
\t.size\tslave_handle_bios, .-slave_handle_bios]=])
vita_dynarec_guard_replace("${_slave_bios_lr_anchor}" "${_slave_bios_lr_replacement}" "slave BIOS helper return guard")

# The bounded smoke trampoline is not used by normal Yabause execution, but it
# is another generated-ARM entry and should obey the same invariant.  Its C ABI
# caller does not provide a guest PC, so r1 is zero and r3 identifies the VM
# base-pointer variable in a failure dump.
set(_test_enter_anchor [=[\tldr\tfp, .vita_dynarec_test_dlptr
\tbx\tr0
\t.size\tvita_dynarec_test_enter, .-vita_dynarec_test_enter]=])
set(_test_enter_replacement [=[\tldr\tfp, .vita_dynarec_test_dlptr
\ttst\tr0, #3
\tbne\t.vita_dynarec_bad_test_enter
\tldr\tr1, .vita_dynarec_target_ptr
\tldr\tr2, [r1]
\tcmp\tr0, r2
\tblo\t.vita_dynarec_bad_test_enter
\tadd\tr2, r2, #0x01000000
\tcmp\tr0, r2
\tbhs\t.vita_dynarec_bad_test_enter
\tbx\tr0
\t.size\tvita_dynarec_test_enter, .-vita_dynarec_test_enter]=])
vita_dynarec_guard_replace("${_test_enter_anchor}" "${_test_enter_replacement}" "bounded smoke generated-code entry guard")

# All failure paths converge on the existing ARM-state data-abort trap.  The
# valid paths above never mask target bits and preserve live registers where a
# generated block can observe them.
string(APPEND _linkage [=[

\t.text
\t.align\t2
.vita_dynarec_bad_master_initial:
\tmov\tr0, r14
\tldr\tr1, [fp, #master_pc-dynarec_local]
\tmov\tr2, #9
\tadd\tr3, fp, #master_ip-dynarec_local
\tb\tvita_dynarec_bad_dispatch
.vita_dynarec_bad_master_interrupt:
\tmov\tr0, r14
\tldr\tr1, [fp, #master_pc-dynarec_local]
\tmov\tr2, #13
\tadd\tr3, fp, #master_ip-dynarec_local
\tb\tvita_dynarec_bad_dispatch
.vita_dynarec_bad_slave_normal:
\tmov\tr0, r4
\tldr\tr1, [fp, #slave_pc-dynarec_local]
\tmov\tr2, #10
\tadd\tr3, fp, #slave_ip-dynarec_local
\tb\tvita_dynarec_bad_dispatch
.vita_dynarec_bad_slave_interrupt:
\tmov\tr0, r4
\tldr\tr1, [fp, #slave_pc-dynarec_local]
\tmov\tr2, #11
\tadd\tr3, fp, #slave_ip-dynarec_local
\tb\tvita_dynarec_bad_dispatch
.vita_dynarec_bad_master_deciline:
\tmov\tr0, r14
\tldr\tr1, [fp, #master_pc-dynarec_local]
\tmov\tr2, #12
\tadd\tr3, fp, #master_ip-dynarec_local
\tb\tvita_dynarec_bad_dispatch
.vita_dynarec_bad_dyna_stale:
\tmov\tr0, r4
\tldr\tr1, [sp]
\tmov\tr2, #5
\tldr\tr3, [sp, #4]
\tb\tvita_dynarec_bad_dispatch
.vita_dynarec_bad_dyna_linked:
\tmov\tr0, r4
\tldr\tr1, [sp]
\tmov\tr2, #6
\tmov\tr3, r5
\tb\tvita_dynarec_bad_dispatch
.vita_dynarec_bad_dyna_dirty:
\tmov\tr0, r1
\tldr\tr1, [sp]
\tmov\tr2, #7
\tmov\tr3, r4
\tb\tvita_dynarec_bad_dispatch
.vita_dynarec_bad_jump_get_addr:
\tmov\tr3, sp
\tldr\tr1, [sp]
\tmov\tr2, #8
\tb\tvita_dynarec_bad_dispatch
.vita_dynarec_bad_verify_get_addr:
\tmov\tr3, sp
\tldr\tr1, [sp]
\tmov\tr2, #14
\tb\tvita_dynarec_bad_dispatch
.vita_dynarec_bad_test_enter:
\tmov\tr3, r1
\tmov\tr1, #0
\tmov\tr2, #15
\tb\tvita_dynarec_bad_dispatch
]=])

# Normalize the readable tab escapes used by this diagnostic file before the
# caller performs its final append and write.
string(REPLACE "\\t" "\t" _linkage "${_linkage}")
