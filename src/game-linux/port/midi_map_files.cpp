// PORT: szukanie plikow .mid fanowskich na dysku + katalog vsco (VSCO CE)
// dla toru MIDI+sfizz. Czysty C++ (POSIX dirent), bez SDL - linkowalne w
// testach. Katalogi wykrywane tak jak POLANIE_DATA/extracted w port_fopen.cpp:
// zmienna srodowiskowa, potem kandydaci wzgledem CWD.
#include "midi_map.h"
#include "midi_map_files.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> // strcasecmp
#include <sys/stat.h>
#include <unistd.h> // realpath

// ---------------------------------------------------------------- MIDI ----
// PORT: kandydaci sa wzgledni ("../vsco" itd.) - po trafieniu kanonizujemy
// do sciezki absolutnej (realpath), zeby pozniejsze fopen w silniku sfz nie
// zalezalo od katalogu bazowego danych gry ani od CWD (run_port.sh cd-uje do
// korzenia repo, ale pol2 moze byc odpalony z dowolnego miejsca).
static void canon(char *dir, size_t outsz) {
  char abs[1100];
  if (realpath(dir, abs)) {
    snprintf(dir, outsz, "%s", abs);
  }
}

// Katalog korzenia fanowskich utworow (jprok_pliki/rozpakowane). Podkatalogi
// (utwory_mid/, luzyce/, luzyce_2/) sa stale, patrz midi_map.h.
static int looks_like_midi_root(const char *dir) {
  char p[1100];
  // wystarczy, ze istnieje ktorys z podkatalogow z utworami
  static const char *sub[] = {"utwory_mid", "luzyce", "luzyce_2"};
  for (size_t i = 0; i < sizeof(sub) / sizeof(sub[0]); i++) {
    snprintf(p, sizeof(p), "%s/%s", dir, sub[i]);
    DIR *d = opendir(p);
    if (d) {
      closedir(d);
      return 1;
    }
  }
  return 0;
}

static const char *midi_root_dir(void) {
  static char dir[1024] = {0};
  static int found = 0;
  if (found)
    return dir;
  found = 1;
  const char *env = getenv("POLANIE_MIDI");
  if (env && looks_like_midi_root(env)) {
    snprintf(dir, sizeof(dir), "%s", env);
    canon(dir, sizeof(dir));
    return dir;
  }
  static const char *cand[] = {
      "jprok_pliki/rozpakowane",       "./jprok_pliki/rozpakowane",
      "../jprok_pliki/rozpakowane",    "../../jprok_pliki/rozpakowane",
      "../../../jprok_pliki/rozpakowane",
      "../../../../jprok_pliki/rozpakowane",
  };
  for (size_t i = 0; i < sizeof(cand) / sizeof(cand[0]); i++) {
    if (looks_like_midi_root(cand[i])) {
      snprintf(dir, sizeof(dir), "%s", cand[i]);
      canon(dir, sizeof(dir));
      return dir;
    }
  }
  dir[0] = 0;
  return dir;
}

int POL_MidiFindFile(const char *subdir, const char *needle, char *out,
                     size_t outsz) {
  out[0] = 0;
  const char *root = midi_root_dir();
  if (!root[0])
    return 0;
  char path[1100];
  snprintf(path, sizeof(path), "%s/%s", root, subdir);
  DIR *d = opendir(path);
  if (!d)
    return 0;
  struct dirent *e;
  int found = 0;
  while ((e = readdir(d))) {
    if (e->d_name[0] == '.')
      continue;
    if (ascii_nocase_find(e->d_name, needle)) {
      snprintf(out, outsz, "%s/%s", path, e->d_name);
      found = 1;
      break;
    }
  }
  closedir(d);
  return found;
}

// ---------------------------------------------------------------- vsco ----
// PORT: sprawdzamy stat()em, a nie fopen (POL_fopen z polshim.h dolaża
// katalog bazowy danych gry do wzglednych sciezek, co psulo kandydatow
// "../vsco" itd.); stat jest bezposredni i case-sensitive - wystarczy.
static int looks_like_vsco_dir(const char *dir) {
  char p[1100];
  snprintf(p, sizeof(p), "%s/GM-StylePerc.sfz", dir); // jedyny w VSCO CE
  struct stat st;
  return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

const char *POL_VscoDir(void) {
  static char dir[1024] = {0};
  static int found = 0;
  if (found)
    return dir;
  found = 1;
  const char *env = getenv("POLANIE_VSCO");
  if (env && looks_like_vsco_dir(env)) {
    snprintf(dir, sizeof(dir), "%s", env);
    canon(dir, sizeof(dir));
    return dir;
  }
  static const char *cand[] = {
      "vsco",           "./vsco",           "../vsco",
      "../../vsco",     "../../../vsco",    "../../../../vsco",
  };
  for (size_t i = 0; i < sizeof(cand) / sizeof(cand[0]); i++) {
    if (looks_like_vsco_dir(cand[i])) {
      snprintf(dir, sizeof(dir), "%s", cand[i]);
      canon(dir, sizeof(dir));
      return dir;
    }
  }
  dir[0] = 0;
  return dir;
}

// ----------------------------------------------------------------- mt32 ----
// Soundfont MT32 (Hedsound, plik *GM.sf2) dla toru mt32 (FluidSynth).
// Zmienna POLANIE_MT32SF2 = pełna ścieżka do pliku .sf2 albo katalog z
// *GM.sf2; fallback: kandydaci "assets/soundfont" (te same zagnieżdżenia
// względem CWD co vsco/ powyżej). Wariant GM: nazwa kończy się ".sf2" i
// zawiera "GM" (ASCII, bez wielkości liter) - tak jak
// "MT-32-32K-v1-r65-Full-Hedsound GM.sf2".
static int mt32_sf2_in_dir(const char *dir, char *out, size_t outsz) {
  DIR *d = opendir(dir);
  if (!d)
    return 0;
  struct dirent *e;
  int found = 0;
  while ((e = readdir(d))) {
    size_t n = strlen(e->d_name);
    if (n < 5 || e->d_name[0] == '.')
      continue;
    if (strcasecmp(e->d_name + n - 4, ".sf2") != 0)
      continue;
    if (!ascii_nocase_find(e->d_name, "gm"))
      continue;
    snprintf(out, outsz, "%s/%s", dir, e->d_name);
    found = 1;
    break;
  }
  closedir(d);
  return found;
}

const char *POL_Mt32Sf2Path(void) {
  static char buf[1024] = {0};
  static int found = 0;
  if (found)
    return buf;
  found = 1;
  const char *env = getenv("POLANIE_MT32SF2");
  struct stat st;
  if (env && env[0] && stat(env, &st) == 0) {
    if (S_ISREG(st.st_mode)) {
      snprintf(buf, sizeof(buf), "%s", env);
      canon(buf, sizeof(buf));
      return buf;
    }
    if (S_ISDIR(st.st_mode) && mt32_sf2_in_dir(env, buf, sizeof(buf))) {
      canon(buf, sizeof(buf));
      return buf;
    }
  }
  static const char *cand[] = {
      "assets/soundfont",          "./assets/soundfont",
      "../assets/soundfont",       "../../assets/soundfont",
      "../../../assets/soundfont", "../../../../assets/soundfont",
  };
  for (size_t i = 0; i < sizeof(cand) / sizeof(cand[0]); i++) {
    if (mt32_sf2_in_dir(cand[i], buf, sizeof(buf))) {
      canon(buf, sizeof(buf));
      return buf;
    }
  }
  buf[0] = 0;
  return buf;
}

// ----------------------------------------------------------------- wav ----
// Katalog wygenerowanych WAV-ów toru MT32 (GRAF_NNN.mt32.wav) — tryb "zamiast
// MIDI/WAV leci jako muzyka" w grze. POLANIE_MT32_WAVDIR albo automat:
// "ephemeral/mt32-wav" (te same zagnieżdżenia co vsco/powyżej). Marker:
// jest chociaż jeden plik *.mt32.wav.
static int mt32_wavdir_ok(const char *dir) {
  DIR *d = opendir(dir);
  if (!d)
    return 0;
  struct dirent *e;
  int found = 0;
  while ((e = readdir(d))) {
    size_t n = strlen(e->d_name);
    if (n > 12 && ascii_nocase_find(e->d_name, ".mt32.wav")) {
      found = 1;
      break;
    }
  }
  closedir(d);
  return found;
}

const char *POL_Mt32WavDir(void) {
  static char dir[1024] = {0};
  static int found = 0;
  if (found)
    return dir;
  found = 1;
  const char *env = getenv("POLANIE_MT32_WAVDIR");
  if (env && env[0] && mt32_wavdir_ok(env)) {
    snprintf(dir, sizeof(dir), "%s", env);
    canon(dir, sizeof(dir));
    return dir;
  }
  static const char *cand[] = {
      "ephemeral/mt32-wav",          "./ephemeral/mt32-wav",
      "../ephemeral/mt32-wav",       "../../ephemeral/mt32-wav",
      "../../../ephemeral/mt32-wav", "../../../../ephemeral/mt32-wav",
  };
  for (size_t i = 0; i < sizeof(cand) / sizeof(cand[0]); i++) {
    if (mt32_wavdir_ok(cand[i])) {
      snprintf(dir, sizeof(dir), "%s", cand[i]);
      canon(dir, sizeof(dir));
      return dir;
    }
  }
  dir[0] = 0;
  return dir;
}

// ------------------------------------------------------- mapa ini (WAV) ----
// 'mt32-wav-map.ini': numer utworu gry (PlayTrack n) = plik WAV. Brak wpisu
// albo brak ini -> domyslne <wavdir>/GRAF_<modul>.mt32.wav (modul kTrackMap,
// jak dotad). User moze przez to podstawic dowolne pliki (np. inny render).
int POL_Mt32WavMapTrack(int track, int module, char *out, size_t outsz) {
  const char *wavdir = POL_Mt32WavDir();
  char def[1300];
  snprintf(def, sizeof(def), "%s/GRAF_%03d.mt32.wav", wavdir, module);
  snprintf(out, outsz, "%s", def);
  if (!wavdir[0])
    return 0;
  const char *inipath = getenv("POLANIE_MT32WAVMAP");
  char inibuf[1300];
  if (!(inipath && inipath[0])) {
    snprintf(inibuf, sizeof(inibuf), "%s/mt32-wav-map.ini", wavdir);
    inipath = inibuf;
  }
  FILE *f = fopen(inipath, "r");
  if (!f)
    return 1; // brak ini -> domyslne
  char ln[1024];
  while (fgets(ln, sizeof(ln), f)) {
    char *p = ln;
    while (*p == ' ' || *p == '\t')
      p++;
    if (*p == '#' || *p == ';' || *p == '\n' || *p == 0)
      continue;
    int nr = 0;
    if (sscanf(p, "%d", &nr) != 1)
      continue;
    char *eq = strchr(p, '=');
    if (!eq)
      continue;
    char *val = eq + 1;
    while (*val == ' ' || *val == '\t')
      val++;
    char *h = strchr(val, '#');
    if (h)
      *h = 0;
    h = strchr(val, ';');
    if (h)
      *h = 0;
    char *e = val + strlen(val) - 1;
    while (e >= val && (*e == '\n' || *e == '\r' || *e == ' ' || *e == '\t'))
      *e-- = 0;
    if (nr != track || !val[0])
      continue;
    if (val[0] == '/')
      snprintf(out, outsz, "%s", val);
    else
      snprintf(out, outsz, "%s/%s", wavdir, val);
    break; // jest wpis dla tego utworu
  }
  fclose(f);
  return 1;
}