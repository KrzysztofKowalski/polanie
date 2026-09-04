/*
 Plik : DPMI.CPP (oryginal: Artur Bidzinski, 1996)
 PORT: wersja Linux - brak DPMI. alloc_DOS_memory przydziela zwykla pamiec
       (malloc); Selector jest tylko licznikiem sluzacym do zwalniania.
       Wywolywane wylacznie z cd.cpp (w porcie - stub).
*/

#include <stdlib.h>
#include "polshim.h"

struct REGS {
  struct {
    unsigned int eax, ebx, ecx, edx;
  } x;
} r;

int nie_ma_DPMI = 1;
int nie_ma_DPMI_w_trybie_rzeczywistym = 1;
int nie_ma_IPX = 1;

// PORT: mala tabela przydzialow (gra przydziela tylko bufor CD)
#define POL_MAX_DPMI_ALLOC 8
static void *pol_allocs[POL_MAX_DPMI_ALLOC] = {0};

int alloc_DOS_memory(unsigned short how_paragraphs,
                     unsigned short &Segment_Address,
                     unsigned short &Selector) {
  for (int i = 0; i < POL_MAX_DPMI_ALLOC; i++) {
    if (!pol_allocs[i]) {
      pol_allocs[i] = malloc((size_t)how_paragraphs * 16);
      if (!pol_allocs[i])
        return 1;
      Selector = (unsigned short)(i + 1);
      Segment_Address = 0; // w trybie flat segmenty nie istnieja
      return 0;
    }
  }
  return 1;
}

int free_DOS_memory(unsigned short Selector) {
  if (Selector >= 1 && Selector <= POL_MAX_DPMI_ALLOC) {
    free(pol_allocs[Selector - 1]);
    pol_allocs[Selector - 1] = 0;
  }
  return 0;
}

int Presence_DPMI(void) { return 0; }
int Presence_DPMI_in_real_mode(void) { return 0; }