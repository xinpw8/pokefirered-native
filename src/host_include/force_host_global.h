/*
 * Force-include wrapper for -include flag.
 *
 * Process system headers BEFORE host global.h to avoid macro conflicts:
 * global.h defines abs/min/max macros that would mangle <stdlib.h>'s
 * function prototypes if stdlib hadn't been parsed first.
 *
 * After this file is processed, GUARD_GLOBAL_H is set, so any upstream
 * #include "global.h" (e.g. via librfu.h) will be skipped — the host
 * version's definitions take precedence.
 */
#ifndef __ASSEMBLER__
#include <stdlib.h>
#include <limits.h>
#include <string.h>

#include "global.h"
#endif
