// PORT: interfejs warstwy SDL2 portu Polanie (game-linux).
// Wideo 320x200x8 (VGA mode 13h) -> okno z calkowitym skalowaniem n*n
// nearest-neighbor, klawiatura/mysz przez zdarzenia SDL, proste audio WAV.
#ifndef POL_PORT_H
#define POL_PORT_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------- wideo ----------
// Ustawia mnoznnik skali (1 px gry = n x n px okna). Wywolywac PRZED
// POL_VideoInit. n<=0 = auto (najwiekszy calkowity mnoznik mieszczacy sie
// w fizycznej rozdzielczosci ekranu).
void POL_SetScale(int n);

// Inicjacja okna/rendere'a/tekstury. Zwraca 0 przy sukcesie.
int POL_VideoInit(void);
void POL_VideoQuit(void);
int POL_GetScale(void); // aktualny/uzywany mnoznik (0 przed inicjacja)

// Bufor ekranu gry: 320x200, 1 bajt = 1 indeks palety, wiersz 320 B.
unsigned char *POL_FrameBuffer(void);

// Paleta: 256 * 3 bajty w wartosciach DAC VGA (0-63) - jak outp(0x3C8/9).
void POL_SetPalette(const unsigned char *pal256x3);

// PORT: rozszerzenie 6-bit DAC VGA -> 8 bitow. To JEDYNE miejsce konwersji
// w porcie (port_sdl.cpp i port_gpu.cpp buduja LUT przez POL_Dac6To8).
// Wzor: (v*255+31)/63 == round(v*255/63) - referencyjna konwersja nowoczesnych
// emulatorow (DOSBox Staging rgb6_to_8: (c*259+33)>>6, identyczny wynik na
// calej dziedzinie). Bit-replikacja (v<<2)|(v>>4), popularna w emulacjach
// retro, rozni sie od round o +1 LSB na 10 wpisach (11..15, 48..52); wybor
// round daje minimalny blad i zgodnosc 1:1 z narzedziami ekstrakcji
// (extracted/, tools/polanie_extract.cpp stosuje v*255/63). Wynik jest
// scisle rosnacy na 0..63 (0 -> 0, 63 -> 255) - kazdy piksel wyswietlany
// pozostaje D O K L A D N I E jednym z 256 wpisow palety DAC gry.
static inline unsigned int POL_Dac6To8(unsigned int v6) {
  return (v6 * 255u + 31u) / 63u;
}

// Blit bufora ekranu do okna (skalowanie n x, nearest-neighbor).
void POL_Present(void);

// ---------- zdarzenia / wejscie ----------
// Przetworzenie zdarzen SDL (klawiatura, mysz, zamkniecie okna).
void POL_PumpEvents(void);
int POL_QuitRequested(void); // 1 po zamknieciu okna

// Kolejka klawiatury w konwencji DOS: zwykle znaki -> ASCII;
// klawisze rozszerzone (strzalki/F1-F12) -> najpierw 0, potem kod scan DOS.
int POL_Kbhit(void);
int POL_Getch(void); // blokujace (pompuje zdarzenia w petli)
void POL_PushKey(int c);

// PORT: tryb wpisywania tekstu (ustawia go ekran edycji Write13h,
// src/image13h.cpp). Wlaczone: litery W/A/S/D dochodza jako ASCII (da sie je
// wpisac w pole tekstowe); wylaczone (gra): WASD jest mapowane na kody
// strzalek do przewijania mapki (decyzja usera 2026-09-04).
void POL_SetTextInputMode(int on);

// ---------- mysz ----------
// Pozycja w koordynatach jak przerwanie int 33h: (0,0)-(639,199).
// Gra czyta X = cx>>1, Y = cy. Przyciski: bit0-lewy, bit1-prawy.
void POL_MousePos(int *cx, int *cy, int *buttons);
void POL_WarpMouse640(int cx, int cy);
int POL_ClickCount(int button); // ile klikniec od ostatniego odczytu (konsumuje)
int POL_ClickPeek(int button);  // PORT: podglad licznika bez konsumpcji

// ---------- czas ----------
void POL_Delay(int ms);
unsigned long POL_GetTicks(void);

// ---------- pliki ----------
// PORT: czysta normalizacja sciezki DOS -> Linux (zrzut napedu "X:",
// backslash -> slash, zdejmowanie wiodacych "./"). Bez dostepu do dysku -
// wydzielona z POL_fopen (port_fopen.cpp) dla testow jednostkowych.
void POL_normalize_path(const char *path, char *out, size_t outsz);

// ---------- katalogi danych (port_fopen.cpp) ----------
// Katalog danych gry (dysk/GRY/POLANIE, zmienna POLANIE_DATA) i katalog
// ekstraktu (extracted/, zmienna POLANIE_EXTRACTED); "" gdy niewykryte.
const char *POL_DataDir(void);
const char *POL_ExtractedDir(void);

// PORT: rozwiazanie sciezki DOS do realnego pliku po stemie nazwy (bez
// rozszerzenia): katalog danych ("data\\W001.dat" -> DATA/W001.DAT), potem
// ekstrakt (extracted/audio/dzwieki/W001.wav). Zwraca statyczny bufor albo
// NULL. Tor efektow (port_audio.cpp) i testy jednostkowe.
const char *POL_ResolveDataFile(const char *dos);

// ---------- audio ----------
// PORT: przeniesione do port_audio.h (mikser: muzyka S3M przez libopenmpt +
// efekty WAV w jednym strumieniu SDL3). Stara jednoglosowa sekcja 8-bit mono
// usunieta - implementacja w port/port_audio.{h,cpp}.
#include "port_audio.h"

#ifdef __cplusplus
}
#endif

#endif // POL_PORT_H