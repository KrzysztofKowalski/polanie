// PORT: wspolny shim DOS/Watcom -> Linux/SDL2 dla portu Polanie.
// Dolaczany globalnie przez Makefile (-include), PRZED kazdym plikiem zrodowym.
// Nie zmienia logiki gry - tylko slowa kluczowe i sciezki plikow.
#ifndef POL_SHIM_H
#define POL_SHIM_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// gra definiuje globalna zmienna 'select' (struct SSelected, battle.cpp);
// kolizja z funkcja select() z glibc - zmieniamy nazwe globalnie
#define select game_select
// image13h.cpp definiuje tablice 'char *index[91]' - kolizja z index() glibc
#define index game_index

// Watcom keywords nieistniejace w g++
#define __far
#define __interrupt
#define far
#define cdecl
#define pascal

// PORT: fopen z normalizacja sciezek DOS -> Linux (backslashe, wielkosc
// liter, katolog bazowy z danymi gry). Implementacja w port/port_fopen.cpp.
#ifdef __cplusplus
extern "C" {
#endif
FILE *POL_fopen(const char *path, const char *mode);
#define fopen POL_fopen

// PORT: enumeracja katalogu (port/port_fopen.cpp) - zamiast slepego sondowania
// plikow. dir wzgledne = CWD, potem katalog danych; nazwy wpisow w out[][256],
// zwraca liczbe (0 = pusty, -1 = brak katalogu).
int POL_ListFiles(const char *dir, char out[][256], int maxn);

// deklaracje watcomowskie uzywane przez kod gry bez <dos.h> (np. delay w
// mapa.cpp) - definicje w port/port_shims.cpp
void delay(unsigned ms);
int inp(unsigned port);
void outp(unsigned port, int val);
int getch(void);
int kbhit(void);
void sound(int freq_hz);
void nosound(void);

// PORT: diagnostyka cichego wyjscia - przechwytujemy WSZYSTKIE exit() w
// programie (takze z niezmienionych plikow game/), wypisujac plik:linia
// wywolania i ostatnie nieudane otwarcie pliku. Logika bez zmian: POL_exit
// loguje i wywoluje prawdziwe exit(). Implementacja w port/port_shims.cpp.
void POL_exit(const char *plik, int linia, int kod);
#define exit(...) POL_exit(__FILE__, __LINE__, __VA_ARGS__)
#ifdef __cplusplus
}
#endif

#endif // POL_SHIM_H