#ifndef VITA_DYNAREC_VM_H
#define VITA_DYNAREC_VM_H

#include <stddef.h>
#include <stdint.h>

#define VITA_DYNAREC_CACHE_BYTES (16u * 1024u * 1024u)
#define VITA_DYNAREC_VENEER_BYTES 4096u

extern unsigned char *sh2_dynarec_target;

int vita_dynarec_vm_init(void);
int vita_dynarec_vm_begin(void);
int vita_dynarec_vm_reset(void);
int vita_dynarec_vm_end(void);
int vita_dynarec_vm_free(void);
int vita_dynarec_vm_is_writable(void);
unsigned int vita_dynarec_vm_write_depth(void);

uint32_t vita_dynarec_branch_target(uint32_t source, uint32_t target);
void vita_dynarec_patch_word(uint32_t address, uint32_t value);
unsigned int vita_dynarec_runtime_patch_count(void);
unsigned int vita_dynarec_invalidation_publish_count(void);
int vita_dynarec_implicit_patch_active(void);
void vita_dynarec_clear_cache(void *begin, void *end);

/* Ari64 calls __clear_cache after emitting or patching generated ARM code.
 * On Vita publication is normally owned by the outer VM write transaction.
 * Legacy invalidation is the exception: kill_pointer()/set_jump_target()
 * starts an implicit write transaction and this hook closes/publishes it at
 * Ari64's existing cache-clear boundary. */
#define __clear_cache vita_dynarec_clear_cache

#endif
