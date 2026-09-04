// PORT: wczytywanie kontenera SOUND.DAT (pelna wersja gry) - patrz
// sound_dat.h. Czyta plik przez fopen = POL_fopen (shim; -include
// polshim.h), wiec sciezki DOS ("data\\sound.dat") rozwiazuja sie jak
// wszedzie w porcie: CWD, potem katalog danych, case-insensitive.
#include "sound_dat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "polshim.h" // fopen -> POL_fopen

// PORT: <vector> moze zrobic "#undef fopen" PO polshim.h - przywracamy,
// zeby czytanie kontenera na pewno szlo przez POL_fopen (sciezki DOS)
#ifndef fopen
#define fopen POL_fopen
#endif
#include "port.h"    // POL_ExtractedDir()

// ------------------------------- cache probek (po loadzie nie zmieniany) ---
static std::vector<std::vector<float>> g_set;
static int g_loaded = 0;

static std::vector<unsigned char> read_whole(const char *path) {
  std::vector<unsigned char> d;
  FILE *f = fopen(path, "rb"); // shim: POL_fopen
  if (!f)
    return d;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return d;
  }
  long n = ftell(f);
  if (n <= 0) {
    fclose(f);
    return d;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return d;
  }
  d.resize((size_t)n);
  size_t got = fread(d.data(), 1, d.size(), f);
  fclose(f);
  d.resize(got);
  return d;
}

// kandydaci pliku zbioru: sciezka podana, ekstrakt, dane pelnej wersji CD
static std::vector<unsigned char> load_global_raw(const char *path) {
  std::vector<unsigned char> d = read_whole(path);
  if (!d.empty())
    return d;
  const char *ed = POL_ExtractedDir();
  if (ed[0]) {
    char alt[1100];
    snprintf(alt, sizeof(alt), "%s/audio/dzwieki/sound.dat", ed);
    d = read_whole(alt);
    if (!d.empty())
      return d;
  }
  // PORT: dane pelnej wersji CD (katalog cd/, np. ephemeral/cd z instalatora
  // scripts/install.sh --cd) - ostatni kandydat; obie wielkosci liter (plik na
  // dysku to SOUND.DAT, a fopen probuje dokladnie taka nazwe jaka podano)
  static const char *extra[] = {
      "cd/polanie_cd/DATA/SOUND.DAT",
      "cd/polanie_cd/DATA/sound.dat",
      "../cd/polanie_cd/DATA/SOUND.DAT",
      "../cd/polanie_cd/DATA/sound.dat"};
  for (const char *xp : extra) {
    d = read_whole(xp);
    if (!d.empty()) {
      printf("PORT: SOUND.DAT z pelnej wersji: %s\n", xp);
      return d;
    }
  }
  return {};
}

int POL_SoundDatLoad(const char *path, int count) {
  if (g_loaded) // idempotentnie (Init + ladowanie() moga chcec 2x)
    return 0;
  if (!path || count <= 0 || count > 4096)
    return 1;
  std::vector<unsigned char> raw = load_global_raw(path);
  if (raw.empty())
    return 1; // brak pliku - glosy moga padac z plikow W0nn (log w menegdma)
  size_t tab = (size_t)count * 4;
  if (raw.size() <= tab) {
    fprintf(stderr, "PORT: '%s' za maly na zbior %d probek\n", path, count);
    return 1;
  }
  // tablica dlugosci NA KONCU pliku, int32 LE (w oryginale czytana
  // sekwencyjnie po buforze PCM, game/menegdma.cpp:758-770)
  std::vector<size_t> lens((size_t)count);
  long long sum = 0;
  for (int i = 0; i < count; i++) {
    const unsigned char *b = raw.data() + raw.size() - tab + (size_t)i * 4;
    lens[i] = (size_t)b[0] | ((size_t)b[1] << 8) | ((size_t)b[2] << 16) |
              ((size_t)b[3] << 24);
    sum += (long long)lens[i];
  }
  if ((long long)(raw.size() - tab) != sum) {
    fprintf(stderr, "PORT: '%s' zly kontener (suma dlugosci %lld != %zu B PCM)\n",
            path, sum, raw.size() - tab);
    return 1;
  }
  // dekodowanie 8-bit unsigned -> F32 (-1..1); 0x80 = cisza
  g_set.resize((size_t)count);
  size_t off = 0;
  int n_ok = 0;
  for (int i = 0; i < count; i++) {
    std::vector<float> &p = g_set[(size_t)i];
    p.resize(lens[i]);
    for (size_t k = 0; k < lens[i]; k++)
      p[k] = ((int)raw[off + k] - 128) / 128.0f;
    off += lens[i];
    if (lens[i])
      n_ok++;
  }
  g_loaded = 1;
  printf("PORT: SOUND.DAT: %d probek glosow (z %d) wczytane\n", n_ok, count);
  return 0;
}

int POL_SoundDatCount(void) { return g_loaded ? (int)g_set.size() : 0; }

const float *POL_SoundDatSample(int index, size_t *len) {
  if (!g_loaded || index < 0 || index >= (int)g_set.size())
    return NULL;
  const std::vector<float> &p = g_set[(size_t)index];
  if (len)
    *len = p.size();
  return p.empty() ? NULL : p.data();
}