#ifndef VITA_DYNAREC_VM_H
#define VITA_DYNAREC_VM_H
/* The standalone driver owns VM allocation and write transactions. */
#include <stddef.h>
#include <stdint.h>
#define VITA_DYNAREC_CACHE_BYTES (16u * 1024u * 1024u)
#define VITA_DYNAREC_VENEER_BYTES 4096u
extern unsigned char *sh2_dynarec_target;
int vita_dynarec_vm_init(void);
int vita_dynarec_vm_begin(void);
/* Test-only: caller must discard every generated entry pointer first. */
int vita_dynarec_vm_reset(void);
int vita_dynarec_vm_end(void);
int vita_dynarec_vm_free(void);
uint32_t vita_dynarec_branch_target(uint32_t source, uint32_t target);
void vita_dynarec_clear_cache(void *begin, void *end);
#define __clear_cache vita_dynarec_clear_cache
#endif
