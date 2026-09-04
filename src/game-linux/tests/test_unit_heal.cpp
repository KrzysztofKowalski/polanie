// PORT: testy nowej mechaniki leczenia rannych (src/unit_heal.cpp):
//  - jednostki gracza leczace sie za mleko z zamku (POL_HealStep/prog 75%),
//  - krowy lecace sie naturalnie za darmo (POL_CowHealStep).
// Testowane sa czyste funkcje balansu; warunki dzialania POL_UnitHealTick
// na strukturach gry (tylko strona gracza, prog zapasow w dzialaniu) sa poza
// zasiegiem gtest - patrz komentarze w src/unit_heal.cpp.
#include <gtest/gtest.h>

// z src/unit_heal.cpp (linkowany do binarki testow jako build/unit_heal.o)
int POL_HealThreshold(int maxmilk);
int POL_HealStep(int hp, int maxhp, int milk, int *milkOut);
int POL_CowHealStep(int hp, int maxhp);

// POL_UnitHealTick operuje na globalnym zamku z game/mover1.cpp, ktorego nie
// linkujemy do testow - dostarczamy definicje i puste ctory Mover1 (Castle
// zawiera Mover1 m[40] i Building b[20] z Mover1 m[6], a Missile nie ma
// konstruktora - tyle wystarczy do linku).
#include "mover.h"
Mover1::Mover1(void) {}
Mover1::~Mover1(void) {}
class Castle castle[2];

TEST(UnitHeal, PelneHpNieLeczyINieZjadaMleka) {
  int milk = 500;
  EXPECT_EQ(POL_HealStep(100, 100, 500, &milk), 100);
  EXPECT_EQ(milk, 500); // mleko nietkniete
}

TEST(UnitHeal, RannaPrzyZerowymMlekuNieLeczy) {
  int milk = 0;
  EXPECT_EQ(POL_HealStep(40, 100, 0, &milk), 40);
  EXPECT_EQ(milk, 0);
}

TEST(UnitHeal, RannaZMlekiemLeczyIZjadaMleko) {
  int milk = 750;
  EXPECT_EQ(POL_HealStep(40, 100, 750, &milk), 41);
  EXPECT_EQ(milk, 749);
}

TEST(UnitHeal, OstatniPunktHpDochodziDoMaxhp) {
  int milk = 10;
  EXPECT_EQ(POL_HealStep(99, 100, 10, &milk), 100); // clamp do maxhp
  EXPECT_EQ(milk, 9);                               // mleko pobrane za ten hp
}

TEST(UnitHeal, ProgZapasow) {
  EXPECT_EQ(POL_HealThreshold(1000), 750);
  EXPECT_EQ(POL_HealThreshold(10), 7); // 7.5 obciete do 7
  EXPECT_EQ(POL_HealThreshold(0), 0);
}

TEST(UnitHeal, KrowaRannaLeczySieZaDarmo) {
  EXPECT_EQ(POL_CowHealStep(40, 100), 41); // +1 hp, bez zadnych zapasow
}

TEST(UnitHeal, KrowaPelnaStoi) {
  EXPECT_EQ(POL_CowHealStep(100, 100), 100);
}

TEST(UnitHeal, KrowaOstatniPunktDoMaxhp) {
  EXPECT_EQ(POL_CowHealStep(49, 50), 50); // clamp do maxhp
}