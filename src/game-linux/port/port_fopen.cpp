// PORT: fopen z normalizacja sciezek DOS -> Linux.
// - zamiana '\\' na '/'
// - usuniecie napędu "X:"
// - katolog bazowy z danymi gry (SETUP.INI / GRAF.DAT), zamienny przez
//   zmienna srodowiskowa POLANIE_DATA; wykrywany tez automatycznie
//   (katalog repo: dysk/GRY/POLANIE)
// - odczyt case-insensitive (gra otwiera np. "setup.INI", plik: SETUP.INI)
#include "polshim.h"

#include <dirent.h>
#include <errno.h>
#include <strings.h>
#include <unistd.h>

#include <string>
#include <utility> // PORT: std::move (cache katalogow w POL_ResolveDataFile)
#include <vector>

#undef fopen // od teraz fopen = funkcja stdio (bez makra)

extern "C" FILE *POL_fopen(const char *path, const char *mode);

// ---- PORT: diagnostyka nieudanych otwarć ----
// Każde fopen kończące się NULLem (po wszystkich próbach/fallbackach)
// wypisujemy na stderr z proszoną ścieżką DOS-ową i znormalizowaną, z lekkim
// limitem (ta sama ścieżka: 3 pierwsze próby + co 200., żeby pętla otwierająca
// setki razy nie zalała terminala). Ostatnia porażka jest też zapamiętywana
// i drukowana przez POL_exit (port/port_shims.cpp) przy cichym wyjściu.
static char pol_last_fail[1300] = {0};

extern "C" const char *POL_last_fopen_fail(void) { return pol_last_fail; }

// PORT: sondy slotow zapisu (battle.cpp:26 FileName[4] = save.001..004;
// menu zapisu/wczytania robi fopen("save.00X","rb") przy KAZDYM odswiezeniu
// listy). Brak pliku to normalny stan ("Pusty" na liscie), nie blad - wiec
// nie lecimy do POL_LogFopenFail, tylko raz na slot wypisujemy sonde i nie
// zapamietujemy jej w POL_last_fopen_fail.
static int is_save_slot_probe(const char *norm, const char *mode) {
  if (!strchr(mode, 'r') || strchr(mode, 'w') || strchr(mode, 'a'))
    return 0; // zapis slotu to faktyczne uzycie, nie sonda
  const char *b = strrchr(norm, '/');
  b = b ? b + 1 : norm;
  return strncasecmp(b, "save.00", 7) == 0 && b[7] >= '1' && b[7] <= '4' &&
         b[8] == 0;
}

static void POL_LogSlotProbe(const char *dos) {
  static struct { char p[300]; int n; } seen[8];
  static int nseen = 0;
  int i = 0;
  while (i < nseen && strcmp(seen[i].p, dos) != 0)
    i++;
  if (i == nseen) {
    i = (nseen < 8) ? nseen++ : 0;
    snprintf(seen[i].p, sizeof(seen[i].p), "%s", dos);
  }
  if (++seen[i].n == 1)
    fprintf(stderr,
            "PORT: sonda slotu zapisu: '%s' - brak pliku (menu zapisu "
            "pokazuje \"Pusty\", to nie blad)\n",
            dos);
}

static void POL_LogFopenFail(const char *dos, const char *norm, const char *bd) {
  int e = errno; // errno z ostatniej próby fopen/opendir
  snprintf(pol_last_fail, sizeof(pol_last_fail), "'%s' -> '%s'", dos, norm);

  static struct { char p[300]; int n; } seen[32];
  static int nseen = 0;
  int i = 0;
  while (i < nseen && strcmp(seen[i].p, dos) != 0)
    i++;
  if (i == nseen) {
    i = (nseen < 32) ? nseen++ : 0; // tablica pełna: licz w slocie 0
    snprintf(seen[i].p, sizeof(seen[i].p), "%s", dos);
  }
  int n = ++seen[i].n;
  if (n <= 3 || (n % 200) == 0)
    fprintf(stderr,
            "PORT: fopen FAIL: '%s' -> '%s' (baza: '%s', próba #%d, errno=%d "
            "%s)\n",
            dos, norm, (bd && bd[0]) ? bd : "(brak)", n, e, strerror(e));
  fflush(stderr);
}

static char base_dir[1024] = {0};
static int base_found = 0;

static int looks_like_data_dir(const char *dir) {
  char p[1100];
  snprintf(p, sizeof(p), "%s/GRAF.DAT", dir);
  FILE *f = fopen(p, "rb");
  if (f) {
    fclose(f);
    return 1;
  }
  return 0;
}

// PORT: katalog bazowy danych gry - deklaracja w przod, bo get_extracted_dir()
// korzysta z niego, a definicja jest nizej (kolejnosc: patrz tez POL_fopen).
static const char *get_base_dir(void);

// PORT: katalog ekstraktu (extracted/) - pliki w formatach natywnych gry
// (teksty/LEVEL.DAT, teksty/LEVEL.INI), uzupelniajace katalog danych.
static int looks_like_extracted_dir(const char *dir) {
  char p[1100];
  snprintf(p, sizeof(p), "%s/teksty/LEVEL.DAT", dir);
  FILE *f = fopen(p, "rb");
  if (f) {
    fclose(f);
    return 1;
  }
  return 0;
}

static const char *get_extracted_dir(void) {
  static char extracted_dir[1024] = {0};
  static int extracted_found = 0;
  if (extracted_found)
    return extracted_dir;
  extracted_found = 1;
  const char *env = getenv("POLANIE_EXTRACTED");
  if (env && looks_like_extracted_dir(env)) {
    snprintf(extracted_dir, sizeof(extracted_dir), "%s", env);
    return extracted_dir;
  }
  static const char *cand[] = {
      ".",
      "./extracted",
      "../extracted",
      "../../extracted",
      "../../../extracted",
      "../../../../extracted",
  };
  for (size_t i = 0; i < sizeof(cand) / sizeof(cand[0]); i++) {
    if (looks_like_extracted_dir(cand[i])) {
      snprintf(extracted_dir, sizeof(extracted_dir), "%s", cand[i]);
      return extracted_dir;
    }
  }
  // wzgledem katalogu danych (dysk/GRY/POLANIE -> korzen repo to ../../..)
  const char *bd = get_base_dir();
  if (bd[0]) {
    char p[1100];
    snprintf(p, sizeof(p), "%s/../../extracted", bd);
    if (looks_like_extracted_dir(p)) {
      snprintf(extracted_dir, sizeof(extracted_dir), "%s", p);
      return extracted_dir;
    }
  }
  extracted_dir[0] = 0;
  return extracted_dir;
}

static const char *get_base_dir(void) {
  if (base_found)
    return base_dir;
  base_found = 1;
  const char *env = getenv("POLANIE_DATA");
  if (env && looks_like_data_dir(env)) {
    snprintf(base_dir, sizeof(base_dir), "%s", env);
    return base_dir;
  }
  static const char *cand[] = {
      ".",
      "./dysk/GRY/POLANIE",
      "../dysk/GRY/POLANIE",
      "../../dysk/GRY/POLANIE",
      "../../../dysk/GRY/POLANIE",
      "./dysk/GRY/POLANIE/..",
  };
  for (size_t i = 0; i < sizeof(cand) / sizeof(cand[0]); i++) {
    if (looks_like_data_dir(cand[i])) {
      snprintf(base_dir, sizeof(base_dir), "%s", cand[i]);
      return base_dir;
    }
  }
  base_dir[0] = 0;
  return base_dir;
}

// PORT: otwarcie KATALOGU z dopasowaniem wielkosci liter WSZYSTKICH
// komponentow sciezki (DOS byl case-insensitive; w tej kopii danych katalog
// to "DATA", a gra prosi o "data" - bez tego POL_ListFiles("data") = -1
// i rozwiazywanie sciezek efektow omijalo katalog danych). Jak ci_fopen:
// rekurencyjnie po komponentach, z realnymi nazwami z readdir. Poprawia
// litery w path; zwraca uchwyt katalogu albo NULL.
static DIR *ci_opendir(char *path) {
  DIR *d = opendir(path);
  if (d)
    return d;
  char *slash = strrchr(path, '/');
  char parent_dir[1024];
  const char *base;
  if (slash) {
    *slash = 0;
    snprintf(parent_dir, sizeof(parent_dir), "%s", path[0] ? path : "/");
    *slash = '/';
    base = slash + 1;
  } else {
    snprintf(parent_dir, sizeof(parent_dir), ".");
    base = path; // pojedynczy komponent ("data") - parentem jest CWD
  }
  DIR *parent = opendir(parent_dir);
  if (!parent)
    return NULL;
  char saved[256];
  snprintf(saved, sizeof(saved), "%s", base);
  struct dirent *e;
  DIR *res = NULL;
  while ((e = readdir(parent))) {
    if (strcasecmp(e->d_name, saved) == 0) {
      if (slash) {
        snprintf(slash + 1, 256, "%s", e->d_name);
      } else {
        snprintf(path, 1024, "%s", e->d_name);
      }
      res = ci_opendir(path);
      break;
    }
  }
  closedir(parent);
  return res;
}

// proby otwarcia z case-insensitive dopasowaniem komponentow sciezki.
// PORT: rodzic tez moze miec inna wielkosc liter ("<baza>/data/W001.dat"
// przy katalogu "DATA") - katalog rozwiazujemy przez ci_opendir, dopiero
// potem dopasowujemy ostatni komponent.
static FILE *ci_fopen(char *path, const char *mode) {
  FILE *f = fopen(path, mode);
  if (f)
    return f;
  char *slash = strrchr(path, '/');
  if (!slash)
    return NULL;
  char dirbuf[1100];
  *slash = 0;
  snprintf(dirbuf, sizeof(dirbuf), "%s", path[0] ? path : "/");
  *slash = '/';
  if (!ci_opendir(dirbuf)) // PORT: katalogi posrednie case-insensitive
    return NULL;
  *slash = '/';
  DIR *d = opendir(dirbuf);
  if (!d)
    return NULL;
  char saved[256];
  snprintf(saved, sizeof(saved), "%s", slash + 1);
  struct dirent *e;
  FILE *res = NULL;
  char full[1200];
  while ((e = readdir(d))) {
    if (strcasecmp(e->d_name, saved) == 0) {
      snprintf(full, sizeof(full), "%s/%s", dirbuf, e->d_name);
      res = fopen(full, mode);
      break;
    }
  }
  closedir(d);
  return res;
}

// PORT: czysta normalizacja sciezki DOS -> Linux, bez dotykania dysku.
// Wydzielona z POL_fopen dla testow jednostkowych (game-linux/tests/).
// 1. zrzucenie napedu "X:", 2. zamiana '\\' na '/', 3. zdejmowanie wiodacych
// "./" (takze powtarzanych "././"); samo "./" (CWD) zostaje, wiodace "../"
// nie ruszamy. Wynik zawsze 0-konczony; obcina do outsz-1 bajtow.
extern "C" void POL_normalize_path(const char *path, char *out, size_t outsz) {
  if (out == NULL || outsz == 0)
    return;
  const char *src = path;
  // "X:..." -> zrzuc napęd
  if (src[0] && src[1] == ':')
    src += 2;
  size_t j = 0;
  for (size_t i = 0; src[i] && j < outsz - 1; i++)
    out[j++] = (src[i] == '\\') ? '/' : src[i];
  out[j] = 0;

  // PORT: wiodace "./" (takze powtarzane "././") zdejmujemy, zeby fallbacki
  // dopasowywane prefiksem (np. "levels/" -> <baza>/GRAF/) dzialaly tez dla
  // sciezek z drive="./" - InitBattle prosi o "./levels\level.dat", ktore bez
  // tego nie lapie sie w regule "levels/" i konczy sie cichym exit(0) z
  // battle.cpp:2088. Samo "./" (CWD) zostaje, wiodace "../" nie ruszamy.
  char *n = out;
  while (n[0] == '.' && n[1] == '/' && n[2])
    n += 2;
  if (n != out)
    memmove(out, n, strlen(n) + 1);
}

extern "C" FILE *POL_fopen(const char *path, const char *mode) {
  static char norm[1024];
  // PORT: normalizacja wydzielona do POL_normalize_path (patrz wyzej)
  POL_normalize_path(path, norm, sizeof(norm));

  FILE *f = fopen(norm, mode); // 1. dokladnie tak, jak podano (CWD)
  if (f)
    return f;

  const char *bd = get_base_dir();
  if (bd[0]) {
    char full[1100];
    snprintf(full, sizeof(full), "%s/%s", bd, norm);
    f = fopen(full, mode); // 2. dokladnie w katalogu danych
    if (f)
      return f;
    f = ci_fopen(full, mode); // 3. case-insensitive w katalogu danych
    if (f)
      return f;
    // PORT: w tej kopii danych pliki kampanii (levels/level.dat - scenariusze,
    // levels/level.ini - teksty wstepow) leza w podkatalogu GRAF/ (instalacja
    // DOS: GRAF/LEVEL.DAT, GRAF/LEVEL.INI). Bez tego InitBattle robi cicho
    // Close13h()+exit(0) i gra "znika" po kliknieciu Nowej Gry.
    if (strncasecmp(norm, "levels/", 7) == 0) {
      char alt[1100];
      snprintf(alt, sizeof(alt), "%s/GRAF/%s", bd, norm + 7);
      f = fopen(alt, mode);
      if (f)
        return f;
      f = ci_fopen(alt, mode);
      if (f)
        return f;
      // PORT: fallback do ekstraktu (extracted/teksty/) - pliki tekstowe
      // w formatach natywnych gry: levels/level.ini -> extracted/teksty/
      // LEVEL.INI, levels/level.dat -> extracted/teksty/LEVEL.DAT (takze
      // level2.ini/level2.dat kampanii 2-6, gdy trafia do ekstraktu).
      // Kolejnosc: najpierw katalog danych (dysk/GRY/POLANIE + GRAF/),
      // potem ekstrakt - dane "zainstalowane" maja pierwszenstwo.
      const char *ed = get_extracted_dir();
      if (ed[0]) {
        const char *base = strrchr(norm, '/');
        base = base ? base + 1 : norm;
        snprintf(alt, sizeof(alt), "%s/teksty/%s", ed, base);
        f = fopen(alt, mode);
        if (f)
          return f;
        f = ci_fopen(alt, mode);
        if (f)
          return f;
      }
    }
  }
  // PORT: "gotowa instalacja" - gra wymaga SETUP.INI (konfiguracja karty
  // dzwiekowej pisanej przez SETUP.EXE). Gdy pliku nie ma nigdzie, syntetyzujemy
  // go w pamieci z domyslnymi wartosciami (Adlib=0, SB=1, irq=13, port=0x220,
  // DMA=0x83), zamiast zrywac z komunikatem "Uruchom program SETUP.EXE".
  // PORT: bajt[1] = 1 ("jest Sound Blaster") - port zawsze ma wlasny mikser
  // audio (port_audio.cpp), wiec syntetyzowana instalacja zgłasza karte (w
  // oryginale od tego bajtu zalezalo wczytanie SOUND.DAT w ladowani.cpp:57).
  // Bajt[0] zostaje 0 (bez napedu CD); gra nie czyta go juz do decyzji o
  // muzyce - domyslne wlaczenie robi src/main.cpp po odczycie SETUP.INI
  // (OnCDAudio, jak oryginal przy bajcie[0] != 0), bo port ma wlasny mikser.
  // Uwaga: realny SETUP.INI na dysku ma pierwszenstwo; port nie gateuje na
  // nim audio.
  {
    const char *base = strrchr(norm, '/');
    base = base ? base + 1 : norm;
    if (strcasecmp(base, "setup.ini") == 0 && strchr(mode, 'r')) {
      static const unsigned char def_setup[7] = {0x00, 0x01, 0x0D,
                                                 0x20, 0x02, 0x83, 0x00};
      return fmemopen((void *)def_setup, sizeof(def_setup), "rb");
    }
    // PORT: pic.dat - archiwum podkladow tekstow misji (ShowText): kazdy obraz
    // to [768 B palety][64000 B ekran], indeksy 0..35 (tablice wstep/vict/def/
    // specjal w graphics.cpp). W tej kopii danych go nie ma, a ShowPicture2 nie
    // sprawdza fopen (fseek(NULL) = crash, okno zamiera - wyglada jak "zepsuty
    // ekran"). Syntetyzujemy czarne ekrany + paleta z czytelnymi kolorami
    // tekstu (1 = cien/literki, 255 = literki; wartosci 8-bit, DAC to >>2).
    if (strcasecmp(base, "pic.dat") == 0 && strchr(mode, 'r')) {
      static unsigned char *picbuf = NULL;
      const int PIC_N = 64;
      if (!picbuf) {
        picbuf = (unsigned char *)calloc((size_t)PIC_N * 64768, 1);
        if (picbuf) {
          for (int n = 0; n < PIC_N; n++) {
            unsigned char *pal = picbuf + (size_t)n * 64768;
            pal[1 * 3 + 0] = 0x50;
            pal[1 * 3 + 1] = 0x50;
            pal[1 * 3 + 2] = 0xA0;
            pal[255 * 3 + 0] = 0xFF;
            pal[255 * 3 + 1] = 0xFF;
            pal[255 * 3 + 2] = 0xFF;
          }
        }
      }
      if (picbuf)
        return fmemopen(picbuf, (size_t)PIC_N * 64768, "rb");
    }
  }
  // PORT: nic nie znalazl sie nigdzie - sonda slotu zapisu leci osobno
  // (znany, nieszkodliwy wzorzec), reszta to realne nieudane otwarciu
  if (is_save_slot_probe(norm, mode))
    POL_LogSlotProbe(path);
  else
    POL_LogFopenFail(path, norm, bd);
  return NULL;
}
// PORT: dostep do wykrytych katalogow dla warstwy audio (port_audio.cpp):
// muzyka S3M z extracted/audio/muzyka/ (fallback: bd/GRAF/GRAF.0xx) i
// efekty WAV z extracted/audio/dzwieki/.
extern "C" const char *POL_DataDir(void) { return get_base_dir(); }
extern "C" const char *POL_ExtractedDir(void) { return get_extracted_dir(); }

// PORT: enumeracja katalogu (POSIX readdir) - zamiast slepego sondowania
// plikow ("jest X? a moze Y? a moze Z?" - kazda pusta sonda to fopen FAIL).
// dir: sciezka DOS/Linux; wzgledna jest probowana najpierw od CWD, potem
// wzgledem katalogu danych (jak w POL_fopen). Nazwy wpisow (bez "." i "..",
// wielkosc liter jak na dysku - porownuje strona wywolujaca) trafiaja do
// out (do maxn, kazda 0-konczona). Zwraca liczbe wpisow; 0 = pusty katalog,
// -1 = katalog nie istnieje.
extern "C" int POL_ListFiles(const char *dir, char out[][256], int maxn) {
  if (!dir || !out || maxn <= 0)
    return -1;
  char norm[1024];
  POL_normalize_path(dir, norm, sizeof(norm));
  // PORT: koncowy "/" zdjemujemy (poza korzeniem "/") - ci_opendir traktuje
  // "data/" jako pusta nazwe komponentu i nie znajduje katalogu
  size_t nl = strlen(norm);
  while (nl > 1 && norm[nl - 1] == '/')
    norm[--nl] = 0;
  DIR *d = opendir(norm);
  if (!d)
    d = ci_opendir(norm);
  if (!d && norm[0] != '/') {
    const char *bd = get_base_dir();
    if (bd[0]) {
      char full[1100];
      snprintf(full, sizeof(full), "%s/%s", bd, norm);
      d = opendir(full);
      if (!d)
        d = ci_opendir(full);
    }
  }
  if (!d)
    return -1;
  int n = 0;
  struct dirent *e;
  while (n < maxn && (e = readdir(d))) {
    if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
      continue;
    snprintf(out[n], 256, "%s", e->d_name);
    n++;
  }
  closedir(d);
  return n;
}

// ------------------------- rozwiazywanie sciezek po stempli -----------------
// PORT: zawartosc katalogu enumerowana RAZ (POL_ListFiles) zamiast slepych
// sond plik po pliku - kazda pusta sonda to fopen FAIL w logu. Plikow
// dzwiekowych w trakcie gry nie przybywa, cache nie wygasa.
static const std::vector<std::string> &list_dir_cached(const std::string &dir) {
  static std::vector<std::pair<std::string, std::vector<std::string>>> cache;
  for (auto &c : cache)
    if (c.first == dir)
      return c.second;
  static char entries[256][256];
  int n = POL_ListFiles(dir.c_str(), entries, 256);
  std::vector<std::string> names;
  for (int i = 0; i < n; i++)
    names.push_back(entries[i]);
  cache.push_back({dir, std::move(names)});
  return cache.back().second;
}

// PORT: pierwszy wpis katalogu o tym samym stemie (dowolne rozszerzenie),
// case-insensitive; hit = pelna nazwa pliku.
static bool stem_match(const std::vector<std::string> &names,
                       const char *stem, std::string *hit) {
  for (const auto &e : names) {
    size_t dot = e.rfind('.');
    std::string s = (dot == std::string::npos) ? e : e.substr(0, dot);
    if (strcasecmp(s.c_str(), stem) == 0) {
      *hit = e;
      return true;
    }
  }
  return false;
}

// PORT: rozwiazanie sciezki efektu. Gra prosi o "data\\W001.dat" (i001.dat).
// Zamiast sondowania kandydatow po kolei (kazda pudlo = fopen FAIL) -
// normalizujemy sciezke, enumerujemy katalog i szukamy wpisu o tym samym
// stemie, dowolne rozszerzenie (.dat w danych DOS, .wav w ekstrakcie).
// Kolejno: katalog danych (podkatalog z zadanej sciezki, case-insensitive
// od CWD i od bazy), ekstrakt extracted/audio/dzwieki/. Zwraca wskaznik na
// statyczny bufor albo NULL.
extern "C" const char *POL_ResolveDataFile(const char *dos) {
  static std::string keep;
  keep.clear();
  char norm[1024];
  POL_normalize_path(dos, norm, sizeof(norm)); // zrzut napedu, '\\' -> '/'
  const char *b = strrchr(norm, '/');
  b = b ? b + 1 : norm;
  const char *dot = strrchr(b, '.');
  std::string stem = dot ? std::string(b, (size_t)(dot - b)) : std::string(b);
  if (stem.empty())
    return NULL;
  std::string dirpart =
      (b != norm) ? std::string(norm, (size_t)(b - norm)) : std::string();

  std::string hit;
  // 1. katalog danych: podkatalog z zadanej sciezki ("data/", "GRAF/..."),
  //    dowolna wielkosc liter i rozszerzenie; bez podkatalogu - CWD/korzen bazy
  if (stem_match(list_dir_cached(dirpart.empty() ? "." : dirpart),
                 stem.c_str(), &hit)) {
    keep = dirpart + hit;
    return keep.c_str();
  }
  // 2. ekstrakt: extracted/audio/dzwieki/ (W001.wav ...)
  const char *ed = POL_ExtractedDir();
  if (ed[0] && stem_match(list_dir_cached(std::string(ed) + "/audio/dzwieki"),
                          stem.c_str(), &hit)) {
    keep = std::string(ed) + "/audio/dzwieki/" + hit;
    return keep.c_str();
  }
  return NULL;
}
