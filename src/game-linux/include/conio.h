// PORT: shim <conio.h> (Watcom) -> Linux/SDL2
#ifndef POL_CONIO_H
#define POL_CONIO_H

#include "polshim.h"

#ifdef __cplusplus
extern "C" {
#endif

// Wejscie klawiatury przez kolejke zdarzen SDL (format jak DOS/Watcom getch:
// zwykly znak -> ASCII; klawisz rozszerzony -> najpierw 0, potem kod scan DOS)
int getch(void);
int getche(void);
int kbhit(void);
int putch(int c);
int cprintf(const char *fmt, ...);
void clrscr(void);

#ifdef __cplusplus
}
#endif

#endif // POL_CONIO_H