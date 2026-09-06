// PORT: nowa mechanika portu - powolne leczenie rannych, dwie galezie:
//  1) KROWY (type == 0) lecza sie same, naturalnie i ZA DARMO (bez mleka) -
//     na obu stronach (gracz i przeciwnik), co POL_COW_HEAL_INTERVAL klatek.
//  2) RANNE JEDNOSTKI GRACZA (type != 0) lecza sie mlekiem z zapasow WLASNEGO
//     zamku - zamek "wydaje" zapasy na szpital tylko, gdy jest pelny co
//     najmniej w 75%. TYLKO strona gracza (castle[0]) - przeciwnik nie ma tej
//     mechaniki. Krowy NIE sa objete leczeniem za mleko (one z zapasow cora).
// Zasady galezi mlecznej:
//  - co POL_HEAL_INTERVAL klatek symulacji ranna jednostka (hp < maxhp)
//    dochodzi o POL_HEAL_HP hp, placac POL_HEAL_MLEKO mleka za 1 hp.
//  - Leczenie dziala TYLKO, gdy zapasy zamku sa >= POL_HEAL_PROCENT pojemnosci
//    (milk >= maxmilk * 3/4). Ponizej progu nic nie pobieraja i sie nie lecza.
//  - Obejmuje jednostki luzem (castle.m[]) i siedzace w budynkach
//    (castle.b[k].m[j]) - jednostka moze byc w jednym z tych dwoch zbiorow.
//  - W jednym przebiegu pobieramy tylko, gdy milk PRZED pobraniem >= prog;
//    zapas moze spasc minimalnie ponizej progu po ostatnim pobraniu (to ok -
//    nie cofamy juz pobranego hp).
// Hook: POL_UnitHealTick() wywolywane z Battle() (src/battle.cpp) po obu
// parach castle[0/1].Run() - rozgrzewka przed petla glowna i petla glowna.
// Czyste funkcje balansu (POL_HealThreshold, POL_HealStep, POL_CowHealStep)
// sa wydzielone bez struktur Castle - pokrywa je tests/test_unit_heal.cpp.
#include "mover.h"
#include "port.h" // POL_UnitHealTick() - prototyp w interfejsie portu

extern class Castle castle[2]; // definicja w game/mover1.cpp

// --- stale balansu -------------------------------------------------------
static const int POL_HEAL_INTERVAL = 18;     // klatek miedzy krokami leczenia
                                             // za mleko (~1 s przy zegarze
                                             // DOS 18.2 Hz)
static const int POL_HEAL_HP = 1;            // hp na krok leczenia
static const int POL_HEAL_MLEKO = 1;         // mleko za 1 hp
static const int POL_HEAL_PROCENT = 75;      // prog zapasow w % maxmilk
static const int POL_COW_HEAL_INTERVAL = 48; // klatek miedzy krokami krow
                                             // (~2.6 s) - naturalne, za darmo
// -------------------------------------------------------------------------

// Zapas, od ktorego zamek zaczyna leczyc: 75% pojemnosci.
// PORT: int64 - zamkowe mleko/maxmilk (skala zapasow)
long long POL_HealThreshold(long long maxmilk) {
  return maxmilk * POL_HEAL_PROCENT / 100;
}

// Krok leczenia jednej jednostki. Zwraca nowe hp; *milkOut dostaje zapas po
// pobraniu. Leczy (+POL_HEAL_HP, clamp do maxhp) i odejmuje POL_HEAL_MLEKO
// TYLKO gdy jednostka jest ranna (hp < maxhp) i jest co pobrac (milk > 0).
// Przy pelnym hp: hp bez zmian i mleko nietkniete.
// PORT: milk/milkOut int64 (skala zapasow), hp/maxhp zostaja int (hp <= 300)
long long POL_HealStep(int hp, int maxhp, long long milk,
                       long long *milkOut) {
  *milkOut = milk; // domyslnie: zapas nietkniety
  if (hp >= maxhp || milk <= 0)
    return hp;
  *milkOut = milk - POL_HEAL_MLEKO;
  hp += POL_HEAL_HP;
  if (hp > maxhp)
    hp = maxhp;
  return hp;
}

// Naturalne samoleczenie krowy (za darmo, bez zapasow): ranna dochodzi o
// POL_HEAL_HP, pelna stoi w miejscu.
int POL_CowHealStep(int hp, int maxhp) {
  if (hp >= maxhp)
    return hp;
  hp += POL_HEAL_HP;
  if (hp > maxhp)
    hp = maxhp;
  return hp;
}

// Krok obu mechanik, wywolywany co klatke symulacji (z Battle()); kazda
// galez ma wlasny licznik klatek i wlasny interwal.
void POL_UnitHealTick(void) {
  // --- krowy: naturalne samoleczenie za darmo, obie strony ---
  static int klatkaKrow = 0;
  if (++klatkaKrow >= POL_COW_HEAL_INTERVAL) {
    klatkaKrow = 0;
    for (int side = 0; side < 2; side++) {
      Castle &c = castle[side];

      // krowy luzem w zamku
      for (int i = 0; i < MaxUnitsInCastle; i++) {
        Mover1 &m = c.m[i];
        if (!m.exist || m.type) // nieobecna albo nie-krowa (type != 0)
          continue;
        m.hp = POL_CowHealStep(m.hp, m.maxhp);
      }

      // krowy siedzace w budynkach (b[MaxBuildings]/m[6] - tablice z mover.h)
      for (int k = 0; k < MaxBuildings; k++) {
        if (!c.b[k].exist)
          continue;
        for (int j = 0; j < 6; j++) {
          Mover1 &m = c.b[k].m[j];
          if (!m.exist || m.type)
            continue;
          m.hp = POL_CowHealStep(m.hp, m.maxhp);
        }
      }
    }
  }

  // --- ranne jednostki gracza: leczenie za mleko z zamku (TYLKO castle[0],
  //     przeciwnik nie ma tej mechaniki) ---
  static int klatka = 0;
  if (++klatka < POL_HEAL_INTERVAL)
    return;
  klatka = 0;

  Castle &c = castle[0];
  long long prog = POL_HealThreshold(c.maxmilk);

  // za malo zapasow: zamek nic nie wydaje, ranni czekaja
  if (c.milk < prog)
    return;

  // ranni luzem w zamku; warunek pętli zatrzymuje przeglądanie, gdy zapas
  // spadnie ponizej progu
  for (int i = 0; i < MaxUnitsInCastle && c.milk >= prog; i++) {
    Mover1 &m = c.m[i];
    if (!m.exist || !m.type) // nieobecna albo krowa (type==0)
      continue;
    if (m.hp >= m.maxhp)
      continue;
    long long milk = c.milk;
    m.hp = POL_HealStep(m.hp, m.maxhp, milk, &milk);
    c.milk = milk;
  }

  // ranni siedzacy w budynkach (b[MaxBuildings]/m[6] - tablice z game/mover.h)
  for (int k = 0; k < MaxBuildings && c.milk >= prog; k++) {
    if (!c.b[k].exist)
      continue;
    for (int j = 0; j < 6 && c.milk >= prog; j++) {
      Mover1 &m = c.b[k].m[j];
      if (!m.exist || !m.type)
        continue;
      if (m.hp >= m.maxhp)
        continue;
      long long milk = c.milk;
      m.hp = POL_HealStep(m.hp, m.maxhp, milk, &milk);
      c.milk = milk;
    }
  }
}