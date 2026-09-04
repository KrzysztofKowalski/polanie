// PORT: testy mapowania "ekran -> paleta" GRAF.DAT/PAL.DAT (tabela PAIRY,
// wylaczona do tools/pairy.h i wspoldzielona z tools/polanie_extract.cpp).
// Wartości oczekiwane zakodowane na sztywno: test ma wychwycic przypadkowa
// zmiane mapowania (pary blokow gora/dol i indeks palety PAL.DAT).
#include <gtest/gtest.h>

#include "pairy.h" // -I../tools

TEST(Pairy, ZnaneParyEkranow) {
  static const struct {
    int top, bottom, pal;
  } oczekiwane[15] = {
      {0, 15, 0},  {1, 16, 1},  {2, 17, 2},  {3, 18, 3},  {4, 19, 3},
      {5, 20, 3},  {6, 21, 6},  {7, 22, 3},  {8, 23, 8},  {9, 25, 9},
      {10, 24, 10}, {11, 26, 11}, {12, 27, 6}, {13, 28, 0}, {14, 29, 13},
  };
  for (int t = 0; t < 15; t++) {
    EXPECT_EQ(PAIRY[t].top, oczekiwane[t].top) << "para " << t;
    EXPECT_EQ(PAIRY[t].bottom, oczekiwane[t].bottom) << "para " << t;
    EXPECT_EQ(PAIRY[t].pal, oczekiwane[t].pal) << "para " << t;
  }
}

TEST(Pairy, DolnaPolowaZaGornaWPierwszych15Blokach) {
  // bloki 0..14 to gorne polowy ekranow, 15..29 - dolne
  for (int t = 0; t < 15; t++) {
    EXPECT_GE(PAIRY[t].top, 0);
    EXPECT_LT(PAIRY[t].top, 15);
    EXPECT_GE(PAIRY[t].bottom, 15);
    EXPECT_LT(PAIRY[t].bottom, 30);
    EXPECT_GE(PAIRY[t].pal, 0);
    EXPECT_LT(PAIRY[t].pal, 14); // PAL.DAT ma 14 palet
  }
}