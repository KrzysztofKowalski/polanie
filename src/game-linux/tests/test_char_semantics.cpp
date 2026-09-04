// PORT: regresja semantyki plain char - Watcom (DOS) mial plain char BEZ znaku,
// GCC na x86-64 ma ze znakiem. Tablica znacznikow terenu placeN[MaxX][MaxY]
// to plain char (definicja game/battle.cpp:28, extern game/mover1.cpp:110,
// game/world.cpp:24) i trzyma wartosci 190-227:
//   * 220-227 - postep naprawy budynku (Mover1::Repare, mover1.cpp:1495-1498),
//   * 200     - swiezo sciete drzewo, ktore ma sie przewrocic (world.cpp:341,352),
//   * >225    - postep budowy palisady/mostu (world.cpp:286,304).
// Bez -funsigned-char (Makefile) wartosc 220 czytana jest jako -36, wiec progi
// "> 219", "< 220", "> 226", "> 225" sa zawsze falszywe - naprawa hp, przewracanie
// drzew i budowa palisady/mostu sa martwe. Ogień (70-100) miesci sie w signed
// i dzialal rowniez bez tej flagi.
// Test nie dotyka SDL ani plikow gry - kompiluje miniaturke logiki znacznikow
// z ta sama semantyka char, jaka wymusza CXXFLAGS/-funsigned-char w Makefile.
#include <gtest/gtest.h>

// (a) literalna asercja zlecenia: 220 w plain char musi byc 220, nie -36
TEST(CharSemantics, Znacznik220JestDodatni) {
  char c = 220;
  EXPECT_TRUE(c > 219); // prog z mover1.cpp:1495 (placeN[xe][ye] < 220) musi
                        // byc osiagalny od dolu, a > 219 (mover1.cpp:2390) trafny
  EXPECT_EQ((int)c, 220);
}

// (b) miniaturka Mover1::Repare (game/mover1.cpp:1470 oraz 1495-1498):
//       if (placeN[xe][ye] > 226 || ...) { commandN = 0; placeN[xe][ye] = 1; return; }
//       ...
//       if (placeN[xe][ye] < 220) placeN[xe][ye] = 220; else placeN[xe][ye]++;
//     kazde "uderzenie" robotnika podnosi znacznik od 220 do 227; przy > 226
//     naprawa sie konczy i znacznik wraca do 1 (8 uderzen na cykl).
TEST(CharSemantics, Naprawa_InkrementacjaDo227IStopPrzy226) {
  char m[8] = {0}; // lokalna miniatura placeN (jedno pole budynku)
  int uderzenia = 0;
  while (!(m[0] > 226) && uderzenia < 64) { // warunek konca: mover1.cpp:1470
    if (m[0] < 220)                         // mover1.cpp:1495-1498
      m[0] = 220;
    else
      m[0]++;
    uderzenia++;
  }
  // 220 (1. uderzenie) + 7 inkrementacji = 8 uderzen do pelnego znacznika 227
  EXPECT_EQ(uderzenia, 8);
  EXPECT_GT((int)m[0], 226); // petla zakonczona progiem > 226, nie limitem 64
}

// (b) miniaturka Building::Run (game/mover1.cpp:2384-2404):
//       if (placeN[i][j] > 219) { dd += (placeN[i][j] - 219) * 2; placeN[i][j] = 1; }
//       ... hp += dd; (stop przy maxhp)
//     +2 hp za kazdy kratownik z aktywnym znacznikiem naprawy.
TEST(CharSemantics, Odbudowa_ZbiorHpZProg219) {
  char m[8] = {0}; // kratowniki 3x3 budynku (skrot do 8 pol)
  m[3] = 220;      // robotnik "machnal" nad tym kratownikiem
  int dd = 0;
  for (int i = 0; i < 8; i++)
    if (m[i] > 219) {
      dd += (m[i] - 219) * 2;
      m[i] = 1;
    }
  EXPECT_EQ(dd, 2);        // (220-219)*2 = +2 hp za uderzenie
  EXPECT_EQ((int)m[3], 1); // znacznik zdejmany po zsumowaniu hp
  // bez -funsigned-char: 220 == -36, prog > 219 nigdy nieprawdziwy, dd == 0
  // (hp nigdy nie rośnie, robotnik macha bez konca)
}

// znaczniki 190-227 z kontekstu: drzewa (200) i palisada/most (> 225)
TEST(CharSemantics, Drzewa200IPalisada225WDodatnich) {
  char drzewo = 200; // sciete drzewo: world.cpp:341,352 (placeN = 200), przewracanie
                     // przy > 190 && < 201 (world.cpp:213,222,332)
  EXPECT_GT((int)drzewo, 190);
  EXPECT_LT((int)drzewo, 201);

  char palisada = 226; // budowa palisady/mostu: world.cpp:286,304 (placeN > 225)
  EXPECT_GT((int)palisada, 225);
}