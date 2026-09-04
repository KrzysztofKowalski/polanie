// PORT: implementacje funkcji z shimow (dos.h/conio.h/graph.h) - Linux/SDL2.
#include "conio.h"
#include "dos.h"
#include "graph.h"
#include "polshim.h"
#include "port.h"
#include <stdarg.h>

// ---- porty I/O: stuby (brak dostepu do sprzetu DOS) ----
int inp(unsigned) { return 0; }
unsigned inpw(unsigned) { return 0; }
int inportb(unsigned) { return 0; }
void outp(unsigned, int) {}
void outportb(unsigned, int) {}
void sound(int) {}
void nosound(void) {}

// ---- int386/int86: brak przerwan BIOS - zwracaja zero ----
int int386(int, union REGS *in, union REGS *out) {
  if (out)
    memset(out, 0, sizeof(*out));
  if (in)
    memset(in, 0, sizeof(*in));
  return 0;
}
int int386x(int, union REGS *in, union REGS *out, struct SREGS *) {
  return int386(0, in, out);
}
int int86(int ivec, union REGS *in, union REGS *out) {
  return int386(ivec, in, out);
}

// ---- wektory przerwan: stuby ----
void far *_dos_getvect(int) { return NULL; }
void _dos_setvect(int, void (*)(...)) {}

// ---- PORT: diagnostyka cichego wyjscia ----
// polshim.h zamienia kazde exit(...) na POL_exit(plik, linia, kod), wiec
// widzimy rowniez wyjscia z niezmienionych plikow game/ (np. Close13h()+exit(0)
// w InitBattle przy braku levels/level.dat). Wypisujemy powod + ostatnie
// nieudane fopen (rejestrowane w port/port_fopen.cpp), potem prawdziwe exit().
#undef exit // od teraz exit = prawdziwe exit() z stdlib
extern "C" const char *POL_last_fopen_fail(void);

void POL_exit(const char *plik, int linia, int kod) {
  fflush(stdout); // zeby komunikaty gry (cprintf) nie zgubily sie przed wyjsciem
  fprintf(stderr, "PORT: exit(%d) z %s:%d\n", kod, plik, linia);
  const char *lf = POL_last_fopen_fail();
  if (lf && lf[0])
    fprintf(stderr, "PORT: ostatnie nieudane fopen: %s\n", lf);
  fflush(stderr);
  exit(kod);
}

// PORT: stdout w rurze/pod gamescope jest domyslnie pelno-buforowany - bez
// tego wydruk diagnostyczny moglby zgubic sie przy cichym wyjsciu. Ustawiamy
// line-buffering zanim cokolwiek zagramy.
__attribute__((constructor)) static void POL_LogInit(void) {
  setvbuf(stdout, NULL, _IOLBF, BUFSIZ);
  setvbuf(stderr, NULL, _IONBF, 0);
}

// ---- czas ----
void delay(unsigned ms) { POL_Delay((int)ms); }

// ---- tryb tekstowy (graph.h) - terminal ----
void _clearscreen(int) { fputs("\033[2J\033[H", stdout); }
long _setbkcolor(long c) {
  static const char *bg[] = {"\033[40m", "\033[44m", "\033[42m", "\033[46m",
                             "\033[41m", "\033[45m", "\033[43m", "\033[47m"};
  printf("%s", (c >= 0 && c < 8) ? bg[c] : "");
  return 0;
}
short _settextcolor(short c) {
  if (c >= 8 && c < 16)
    printf("\033[9%dm", c - 8);
  else if (c >= 0 && c < 8)
    printf("\033[3%dm", c);
  return 0;
}

// ---- conio ----
int getch(void) { return POL_Getch(); }
int getche(void) { return POL_Getch(); }
int kbhit(void) { return POL_Kbhit(); }
int putch(int c) { putchar(c); return c; }
void clrscr(void) { _clearscreen(_GCLEARSCREEN); }

int cprintf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = vprintf(fmt, ap);
  va_end(ap);
  fflush(stdout);
  return r;
}