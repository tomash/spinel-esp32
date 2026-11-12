/* Force-included into the runtime build for MCU targets with no
   environment: every SPINEL_* tuning knob reads as unset, so the compiler
   folds `e ? atof(e) : default` to the default and the parsers drop out. */
#pragma once
#ifndef __ASSEMBLER__
#include <stdlib.h>
#define getenv(name) ((char *)0)
#endif
