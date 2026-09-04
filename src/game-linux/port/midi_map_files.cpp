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

// Katalog korzenia dodatkowych utworow MIDI (env POLANIE_MIDI albo katalog
// midi_utwory/). Podkatalogi (utwory_mid/, luzyce/, luzyce_2/) sa stale,
// patrz midi_map.h.
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
      "midi_utwory",       "./midi_utwory",
      "../midi_utwory",    "../../midi_utwory",
      "../../../midi_utwory",
      "../../../../midi_utwory",
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