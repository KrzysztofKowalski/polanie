///////////////////////////////////////////////////////////////////////////
// menegdma.cpp
// PORT: wersja Linux/SDL2 zamiast Sound Blastera + DMA + przerwan DOS.
// - odtwarzanie WAV (efekty/mowa) przez SDL_Audio (8-bit unsigned mono)
// - zegar 18.2 Hz (przerwanie int 8) -> SDL_AddTimer inkrementujacy
//   'licznik' (zmienna uzywana przez caly kod gry)
// - pliki WAV sa czytane bezposrednio z katalogu danych (port_fopen.cpp)
// Interfejs klasy zgodny z game/menegdma.h - reszta kodu gry bez zmian.
///////////////////////////////////////////////////////////////////////////
#include "menegdma.h"
#include "define.h"
#include "polshim.h"
#include "port.h"
#include <SDL3/SDL.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// definicje zmiennych statycznych klasy (przeniesione z oryginalu)
int MENEGERDMA::MultiWav = 0;
int MENEGERDMA::DO_zagrania = 0;
int MENEGERDMA::ZAgrano = 0;
void *MENEGERDMA::MaxSample = 0;
void *MENEGERDMA::SamplesForSB = 0;
void *MENEGERDMA::NewSamplesForSB = 0;
void *MENEGERDMA::GlobalDataWAV = 0;
int MENEGERDMA::HiInterrupt = 0;
int MENEGERDMA::loaded = 0;
int MENEGERDMA::wartosc_maski_kanalu = 0;
int MENEGERDMA::jest_odtwarzany = 0;
int MENEGERDMA::Jest_SB = 0;

// PORT: zegar 18.2 Hz dla calej gry (w oryginale NewTime() na przerwaniu 8)
// PORT: licznik pisze watek timera SDL, czyta petla gry - w praktyce 18 Hz,
// int 32-bit; w oryginale pisalo ISR int 8h (ta sama semantyka).
// PORT: volatile - pisany z callbacku SDL_AddTimer (pol_tick_cb nizej),
// czytany z petli gry (battle.cpp) bez zadnego locka; bez volatile
// kompilator moglby cachowac odczyty i petla nigdy nie zobaczylaby
// podbicia.
volatile int licznik = 0;

static SDL_TimerID pol_timer = 0;

// PORT: sygnatura SDL3 (userdata, timerID, interval) - SDL2: (interval, param);
// return 0 kasuje timer w SDL3, wiec zwracamy interval (nowy interwal).
static Uint32 pol_tick_cb(void *userdata, SDL_TimerID timerID, Uint32 interval) {
  (void)userdata;
  (void)timerID;
  licznik++;
  // PORT: oryginal czyscil jest_odtwarzany w przerwaniu DMA po skonczeniu
  // probki (game/menegdma.cpp:332-335); tu robi to zegar 18.2 Hz, gdy mikser
  // (port_audio.cpp) skonczy grac wszystkie glosy efektow.
  if (MENEGERDMA::jest_odtwarzany && !POL_AudioPlaying()) {
    MENEGERDMA::jest_odtwarzany = NIE_JEST_GRANY;
    // PORT: diagnostyka (POL_MOUSE_DEBUG=1) - otwarcie bramki dzwieku w
    // battle.cpp:391 (`if (!SND.jest_odtwarzany)`); gdyby ta linia nie
    // padla nigdy, bramka bylaby utkwiona i glosy gralyby tylko raz.
    if (getenv("POL_MOUSE_DEBUG"))
      fprintf(stderr, "PORT: SND: bramka otwarta (glos skonczyl)\n");
  }
  return interval;
}

MENEGERDMA::MENEGERDMA(int ile_MAX_chcesz_miec_melodii_WAV, int paragraphs) {
  (void)ile_MAX_chcesz_miec_melodii_WAV;
  (void)paragraphs;
  Jest_SB = 0;
}

MENEGERDMA::~MENEGERDMA(void) { deInit(); }

// PORT: Init(jest_SB, irq, port, kanal) - parametry sprzetowe ignorowane
void MENEGERDMA::Init(int jest_SB, int, int, int) {
  Jest_SB = jest_SB ? 1 : 0;
  if (!pol_timer) {
    pol_timer = SDL_AddTimer(1000 / 18.2065, pol_tick_cb, NULL); // int 8 -> 18.2Hz
    if (!pol_timer)
      fprintf(stderr, "PORT: nie mogl zainstalowac zegara: %s\n", SDL_GetError());
    else if (getenv("POL_MOUSE_DEBUG")) // PORT: diagnostyka (POL_MOUSE_DEBUG=1)
      fprintf(stderr, "PORT: TIMER: zainstalowano id=%u, 18.2 Hz (interval=%d ms)\n",
              (unsigned)pol_timer, (int)(1000 / 18.2065));
  }
  // PORT: audio (mikser SDL3: muzyka S3M + efekty WAV) uruchamiane zawsze -
  // niezaleznie od flagi SB z SETUP.INI (w tej kopii danych SETUP.INI ma
  // bajt[1]=0 = "brak SB", a muzyka/efekty i tak pochodza z plikow, nie ze
  // sprzetu). PORT: od razu probujemy wczytac SOUND.DAT (glosy jednostek
  // 1-183) - w oryginale robil to dopiero `if (jest_SB)` w ladowani.cpp:57,
  // czyli przy SETUP.INI bez karty dzwiekowej glosy jednostek by znikly.
  // Port zawsze ma "karta dzwiekowa", wiec probujemy bez wzgledu na flage
  // (LoadGlobalData jest idempotentny; ladowanie() moze powtorzyc probe).
  POL_AudioInit();
  LoadGlobalData((char *)"data\\sound.dat", 183);
}

void MENEGERDMA::deInit(void) {
  if (pol_timer) {
    SDL_RemoveTimer(pol_timer);
    pol_timer = 0;
  }
  POL_AudioShutdown();
}

// PORT: PlayWav - odtwarza plik RIFF WAV 8-bit mono (np. data/i001.dat)
// jako glos miksera (port_audio.cpp) - miksowany z muzyka, bez jej wyciszania.
// Sciezki DOS rozwiazuje POL_SfxPlay (katalog danych lub ekstrakt WAV).
// Zwraca WAV_ZALADOWANY (0) przy sukcesie, BRAK_PLIKU_WAV (2) gdy brak pliku.
int MENEGERDMA::PlayWav(char *nazwa) {
  if (!POL_AudioPlayFile(nazwa))
    return BRAK_PLIKU_WAV;
  jest_odtwarzany = JEST_GRANY;
  return WAV_ZALADOWANY;
}

void MENEGERDMA::EndPlayWav(void) {
  POL_AudioStop();
  jest_odtwarzany = NIE_JEST_GRANY;
}

// PORT: operator()(int nr) - dzwiek nr (Msg.dzwiek-1); w oryginale odtwarzal
// probke nr z globalnego bufora sound.dat (183 probek z pelnej wersji gry).
// Priorytet: SOUND.DAT (POL_SfxPlayGlobalSet, gdy wczytany), potem pliki
// DATA/W001-W055.DAT (kopia demo; numerowanie 1-based), a gdy i tego nie ma
// - jednorazowy log z numerem (komendy grupy 168/177, typy jednostek >= 2
// w tej kopii danych nie maja probek bez SOUND.DAT).
int MENEGERDMA::operator()(int nr) {
  int plik = nr + 1;
  // PORT: diagnostyka toru glosow pod POL_MOUSE_DEBUG=1 - widac wywolanie
  // SND(...) (czyli ze Msg.dzwiek powstalo = komenda/klik dotarl do gry),
  // stan zbioru SOUND.DAT i wynik grania. Jednym przebiegiem rozstrzyga,
  // czy cisza to brak komend (mysz), czy bug toru audio.
  static int dbg = -1;
  if (dbg < 0)
    dbg = getenv("POL_MOUSE_DEBUG") ? 1 : 0;
  if (plik < 1 || plik > 183)
    return 0;
  if (dbg)
    fprintf(stderr, "PORT: SND: dzwiek nr %d (zbior=%d, jest_odtwarzany=%d)\n",
            plik, POL_SfxGlobalSetCount(), jest_odtwarzany);
  if (POL_SfxGlobalSetCount() > 0) {
    if (POL_SfxPlayGlobalSet(nr)) {
      jest_odtwarzany = JEST_GRANY;
      if (dbg)
        fprintf(stderr, "PORT: SND: gram probke %d ze zbioru SOUND.DAT\n",
                plik);
      return 0;
    }
    // PORT: pusta probka w zbiorze -> probujemy jeszcze plik W0nn (kopia demo)
    if (dbg)
      fprintf(stderr, "PORT: SND: probka %d pusta/brak w zbiorze - fallback "
                      "W0nn\n",
              plik);
  }
  if (plik > 55) {
    // PORT: raz na numer - wyjasnia, czego brakuje (pelna wersja: SOUND.DAT)
    static unsigned char pokazane[184] = {0};
    if (!pokazane[plik]) {
      pokazane[plik] = 1;
      fprintf(stderr,
              "PORT: brak dzwieku nr %d (brak DATA/SOUND.DAT z pelnej wersji "
              "- skopiuj go do dysk/GRY/POLANIE/DATA/)\n",
              plik);
    }
    return 0;
  }
  char ss[64];
  sprintf(ss, "data\\W%03d.dat", plik);
  PlayWav(ss);
  if (dbg)
    fprintf(stderr, "PORT: SND: fallback plik %s -> %s\n", ss,
            jest_odtwarzany ? "grany" : "BRAK pliku/audio");
  return 0;
}

int MENEGERDMA::Odtwarzaj(int uchwyt) { return operator()(uchwyt); }

int MENEGERDMA::InstalujWAV(char *nazwa) {
  // w oryginale: wczytywanie pojedynczego wav do pamieci (SB);
  // w porcie zwraca blad, zeby ladowanie() traktowalo jako pominiete
  FILE *f = fopen(nazwa, "rb");
  if (!f)
    return BRAK_PLIKU_WAV;
  fclose(f);
  return BRAK_PAMIECI_NA_PROBKI;
}

int MENEGERDMA::LoadGlobalData(char *nazwa, int ile) {
  // PORT: realne wczytanie kontenera SOUND.DAT ([PCM][count x i32 dlugosci])
  // przez warstwe audio; idempotentne. W oryginale z tego zbioru gral
  // operator()(nr) - tu ta sama semantyka (POL_SfxPlayGlobalSet).
  if (!POL_SfxLoadGlobalSet(nazwa, ile)) {
    GlobalDataWAV = (void *)1; // znacznik "wczytany" (diagnostyka)
    loaded = 1;
    return 0;
  }
  GlobalDataWAV = 0;
  loaded = 0;
  return BLAD_OTWARCIA_GLOBALNEGO_PLIKU_Z_WAVAMI;
}

int MENEGERDMA::Wolno_grac(void) { return 1; }
int MENEGERDMA::Przerwano_DMA(void) { return 0; }
void MENEGERDMA::Przerwij_odtwarzanie_DMA(void) { POL_AudioStop(); }

int MENEGERDMA::ile_mozesz_miec_DMA(void) { return 4; }
int MENEGERDMA::ile_masz_DMA(void) { return 0; }

int MENEGERDMA::ZaladujWAV_z_dysku(char *nazwa, opisWAVa *wav) {
  (void)nazwa;
  (void)wav;
  return BRAK_PLIKU_WAV;
}