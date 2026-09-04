// PORT: main dla testow jednostkowych portu Polanie (Google Test).
// Zanim wystartuja testy: budujemy katalog tymczasowy z danymi (GRAF.DAT,
// PAL.DAT, GRAF/..., teksty/...) i ustawiamy POLANIE_DATA / POLANIE_EXTRACTED,
// zeby POL_fopen (port_fopen.cpp) znalazl "instalacje gry" tam, gdzie testy
// tego oczekuja. Katalog jest kasowany po testach.
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "test_util.h"

namespace fs = std::filesystem;

std::string POL_TEST_TMP;

static void test_mkdir(const std::string &dir) {
  if (::mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) {
    fprintf(stderr, "testy: mkdir(%s) nie powiodl sie (%s)\n", dir.c_str(),
            strerror(errno));
    exit(2);
  }
}

static std::vector<unsigned char> bytes(const char *s) {
  return std::vector<unsigned char>(s, s + strlen(s));
}

static void test_write_or_die(const std::string &name,
                              const std::vector<unsigned char> &data) {
  if (!test_write(name, data)) {
    fprintf(stderr, "testy: nie moge zapisac %s\n", testpath(name).c_str());
    exit(2);
  }
}

static std::vector<unsigned char> sized_pattern(size_t n, int salt) {
  std::vector<unsigned char> v(n);
  for (size_t i = 0; i < n; i++)
    v[i] = (unsigned char)((i * 7 + salt) & 0xFF);
  return v;
}

int main(int argc, char **argv) {
  // --- katalog tymczasowy ---
  std::string tpl =
      (fs::temp_directory_path() / "polanie_testy_XXXXXX").string();
  std::vector<char> buf(tpl.begin(), tpl.end());
  buf.push_back('\0');
  char *dir = mkdtemp(buf.data());
  if (!dir) {
    fprintf(stderr, "testy: mkdtemp nie powiodl sie\n");
    return 2;
  }
  POL_TEST_TMP = dir;

  // --- struktura "instalacji" gry ---
  test_mkdir(testpath("GRAF"));
  test_mkdir(testpath("teksty"));

  // GRAF.DAT: 2 bloki po 33000 B (naglowek [u16 size][u16 w=319][u16 h=100] +
  // 100 wierszy po 319 B) - dane do LoadToScreen13h. Wartosc bajtu na pozycji
  // p: (p*7 + 11) & 0xFF (test liczy to samo wzorem).
  {
    std::vector<unsigned char> graf = sized_pattern(2 * 33000, 11);
    graf[0] = 0; // size (zawyzone, nieistotne dla LoadToScreen13h)
    graf[2] = 319;
    graf[4] = 100;
    graf[33000 + 0] = 0;
    graf[33000 + 2] = 319;
    graf[33000 + 4] = 100;
    test_write_or_die("GRAF.DAT", graf);
  }
  // PAL.DAT: 14 palet po 768 B; paleta i wypelniona wartoscia 0x10 + i.
  {
    std::vector<unsigned char> pal(14 * 768);
    for (int p = 0; p < 14; p++)
      memset(pal.data() + (size_t)p * 768, 0x10 + p, 768);
    test_write_or_die("PAL.DAT", pal);
  }
  // pliki do testow fopen: case-insensitive, fallback levels/ -> GRAF/,
  // fallback do ekstraktu (teksty/)
  test_write_or_die("GRAF/pal.dat", bytes("PAL-CI"));
  test_write_or_die("GRAF/level.dat", bytes("LEVEL-DATA"));
  test_write_or_die("teksty/LEVEL.DAT", bytes("EKSTRAKT-DAT"));
  test_write_or_die("teksty/LEVEL.INI", bytes("EKSTRAKT-INI"));

  // --- srodowisko dla POL_fopen (cache w port_fopen.cpp inicjuje sie raz) ---
  setenv("POLANIE_DATA", POL_TEST_TMP.c_str(), 1);
  setenv("POLANIE_EXTRACTED", POL_TEST_TMP.c_str(), 1);

  ::testing::InitGoogleTest(&argc, argv);
  int rc = RUN_ALL_TESTS();

  std::error_code ec;
  fs::remove_all(POL_TEST_TMP, ec);
  return rc;
}