#include <stdio.h>

#include "../sh2core.h"
#include "vita_dynarec_vm.h"

/* The Vita-generated Ari64 source keeps the bounded-smoke hooks compiled in so
 * it can share the same branch veneer and VM adaptation path.  Real runtime
 * builds leave these controls permanently disabled. */
int vita_dynarec_exec_test_active = 0;
unsigned int vita_dynarec_exec_test_instruction_limit = 0;
