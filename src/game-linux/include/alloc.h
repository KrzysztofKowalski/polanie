// PORT: shim <alloc.h> (Watcom - far heap) -> zwykly malloc
#ifndef POL_ALLOC_H
#define POL_ALLOC_H

#include "polshim.h"

#define farcalloc(n, s) calloc((n), (s))
#define farmalloc(n) malloc((size_t)(n))
#define farfree(p) free(p)
#define farcoreleft() 0L

#endif // POL_ALLOC_H