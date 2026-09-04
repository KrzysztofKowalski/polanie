// Wspolna tabela par blokow GRAF.DAT: mapowanie "ekran -> paleta PAL.DAT".
// Uzywana przez tools/polanie_extract.cpp (dumpGraf) i testy portu
// (game-linux/tests/test_paire.cpp) - jedno zrodlo prawdy, zeby zmiana
// jednego nie rozjechala sie z drugim.
//
// GRAF.DAT sklada sie z par: blok 'top' = gorna polowa ekranu (y 0..99),
// blok 'bottom' = dolna polowa (y 100..199), a para t rysowana jest
// paleta 'pal' (indeks w PAL.DAT, 14 palet po 768 B).
#ifndef POL_PAIRY_H
#define POL_PAIRY_H

static const struct {
  int top, bottom, pal;
} PAIRY[15] = {
    {0, 15, 0},  {1, 16, 1},  {2, 17, 2},  {3, 18, 3},  {4, 19, 3},
    {5, 20, 3},  {6, 21, 6},  {7, 22, 3},  {8, 23, 8},  {9, 25, 9},
    {10, 24, 10}, {11, 26, 11}, {12, 27, 6}, {13, 28, 0}, {14, 29, 13},
};

#endif // POL_PAIRY_H