// PORT: shim <dos.h> (Watcom) -> Linux/SDL2
#ifndef POL_DOS_H
#define POL_DOS_H

#include "polshim.h"
#include "port.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- porty I/O: stuby (nic nie podlaczane do sprzetu) ---
int inp(unsigned port);
unsigned inpw(unsigned port);
int inportb(unsigned port);
void outp(unsigned port, int val);
void outportb(unsigned port, int val);

// --- union REGS jak w Watcom (uzywane przez int386) ---
union REGS {
  struct {
    unsigned char al, ah, bl, bh, cl, ch, dl, dh;
  } h;
  struct {
    unsigned short ax, bx, cx, dx, si, di, cflag, flags;
  } w;
  struct {
    unsigned int eax, ebx, ecx, edx, esi, edi, cflag;
  } x;
};

struct SREGS {
  unsigned short es, cs, ss, ds, fs, gs;
};

int int386(int ivec, union REGS *in, union REGS *out);
int int386x(int ivec, union REGS *in, union REGS *out, struct SREGS *seg);
int int86(int ivec, union REGS *in, union REGS *out);

// --- wektory przerwan: stuby ---
typedef void (*POL_ISR)(...);
void far *_dos_getvect(int vec);
void _dos_setvect(int vec, void (*isr)(...));

// --- czas ---
void delay(unsigned ms);

#ifdef __cplusplus
}
#endif

#endif // POL_DOS_H