#include <config.h>
#include "core.h"
int veejay_core_version(void) {
    return 1;
}

const char *veejay_core_build(void) {
    return GIT_HASH_VEEJAYCORE;
}