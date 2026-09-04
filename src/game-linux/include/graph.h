// PORT: shim <graph.h> (Watcom - tryb tekstowy) -> terminal
#ifndef POL_GRAPH_H
#define POL_GRAPH_H

#include "polshim.h"

#define _GCLEARSCREEN 0

#ifdef __cplusplus
extern "C" {
#endif
void _clearscreen(int what);
long _setbkcolor(long color);
short _settextcolor(short color);
#ifdef __cplusplus
}
#endif

#endif // POL_GRAPH_H