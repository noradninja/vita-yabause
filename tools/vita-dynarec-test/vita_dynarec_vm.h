#ifndef VITA_DYNAREC_VM_H
#define VITA_DYNAREC_VM_H
/* The standalone driver owns VM allocation and write transactions. */
#include <stddef.h>
void vita_dynarec_clear_cache(void *begin, void *end);
#define __clear_cache vita_dynarec_clear_cache
#endif
