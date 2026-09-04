// PORT: testy toru efektow dzwiekowych (port/port_audio.cpp + port_fopen.cpp).
// Regresje do zgloszonego problemu "muzyka gra, efekty nie":
//  - case-insensitive otwarcie katalogu w POL_ListFiles (dane maja "DATA",
//    gra prosi o "data" - bez tego katalog danych byl omijany),
//  - resolve sciezki "data\\W0nn.dat" (stem W0nn, dowolne rozszerzenie),
//  - wczytanie kontenera SOUND.DAT ([PCM 8-bit][count x i32 LE dlugosci na
//    koncu]) - z niego pochodza glosy jednostek 1-183 w pelnej wersji gry.
// Mikser (urzadzenie audio) NIE jest inicjowany w testach - sprawdzamy
// rozwiazywanie sciezek i parsowanie kontenera, nie faktyczne granie.
#include <gtest/gtest.h>

#include <sys/stat.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "test_util.h"

extern "C" {
int POL_ListFiles(const char *dir, char out[][256], int maxn);
const char *POL_ResolveDataFile(const char *dos); // port_fopen.cpp
int POL_SoundDatLoad(const char *path, int count); // port/sound_dat.cpp
int POL_SoundDatCount(void);
const float *POL_SoundDatSample(int index, size_t *len);
}

static void test_mkdir(const std::string &dir) {
  ::mkdir(dir.c_str(), 0755); // istniejacy katalog = ok
}

TEST(ListFiles, CaseInsensitiveKatalogDanych) {
  // dane leza w <baza>/DATA (wielkie litery), gra prosi o "data"
  test_mkdir(testpath("DATA"));
  ASSERT_TRUE(test_write("DATA/W001.DAT", {'P', 'O', 'L'}));
  char entries[64][256];
  int n = POL_ListFiles("data", entries, 64);
  ASSERT_GT(n, 0) << "POL_ListFiles(\"data\") ma znalezc katalog DATA";
  bool widzi_w001 = false;
  for (int i = 0; i < n; i++)
    if (strcasecmp(entries[i], "W001.DAT") == 0)
      widzi_w001 = true;
  EXPECT_TRUE(widzi_w001);
}

TEST(Sfx, ResolveStemZKataloguDanych) {
  // "data\W001.dat" -> plik DATA/W001.DAT z katalogu danych (dowolna
  // wielkosc liter; kopi demo ma tylko W001-W055)
  test_mkdir(testpath("DATA"));
  ASSERT_TRUE(test_write("DATA/W001.DAT", {'P', 'O', 'L'}));
  const char *r = POL_ResolveDataFile("data\\W001.dat");
  ASSERT_NE(r, nullptr) << "resolve('data\\W001.dat') ma trafic na DATA/";
  std::string resolved = r;
  size_t pos = resolved.rfind('/');
  ASSERT_NE(pos, std::string::npos);
  EXPECT_EQ(strcasecmp(resolved.c_str() + pos + 1, "W001.DAT"), 0);
}

TEST(Sfx, ResolveNieznanyStem) {
  // pliku nie ma nigdzie (testowy katalog nie ma W099) -> NULL
  EXPECT_EQ(POL_ResolveDataFile("data\\W099.dat"), nullptr);
}

// ---------------------------------------------------------------- SOUND.DAT --
TEST(SfxGlobalSet, ParsowanieKontenera) {
  // najpierw zly kontener (dlugosci niezgodne z rozmiarem PCM) - ma byc
  // odrzucony; load jest idempotentny, wiec bad pliku musi biec PRZED
  // wczytaniem poprawnego zbioru
  {
    std::vector<unsigned char> zly = {0x80, 0x81, 0x82};
    zly.push_back(9); // falszywa tablica (1 probka, dlugosc 9 != 3 B PCM)
    zly.push_back(0);
    zly.push_back(0);
    zly.push_back(0);
    test_mkdir(testpath("DATA"));
    ASSERT_TRUE(test_write("DATA/zly_sound.dat", zly));
    EXPECT_NE(POL_SoundDatLoad("data\\zly_sound.dat", 1), 0);
    EXPECT_EQ(POL_SoundDatCount(), 0);
  }
  // syntetyczny kontener: [4+8+2 B PCM][3 x int32 LE dlugosci]
  std::vector<unsigned char> dat;
  for (int i = 0; i < 14; i++)
    dat.push_back((unsigned char)(0x80 + i * 3)); // surowe PCM (0x80 = cisza)
  const uint32_t lens[3] = {4, 8, 2};
  for (int i = 0; i < 3; i++) {
    dat.push_back(lens[i] & 0xFF);
    dat.push_back((lens[i] >> 8) & 0xFF);
    dat.push_back((lens[i] >> 16) & 0xFF);
    dat.push_back((lens[i] >> 24) & 0xFF);
  }
  test_mkdir(testpath("DATA"));
  ASSERT_TRUE(test_write("DATA/sound.dat", dat));

  EXPECT_EQ(POL_SoundDatLoad("data\\sound.dat", 3), 0);
  EXPECT_EQ(POL_SoundDatCount(), 3);
  // idempotentnie: drugi load nie zmienia stanu
  EXPECT_EQ(POL_SoundDatLoad("data\\sound.dat", 3), 0);
  EXPECT_EQ(POL_SoundDatCount(), 3);
  // probki zdekodowane do F32, 0x80 -> 0.0 (cisza)
  size_t len = 0;
  const float *s0 = POL_SoundDatSample(0, &len);
  ASSERT_NE(s0, nullptr);
  EXPECT_EQ(len, 4u);
  EXPECT_FLOAT_EQ(s0[0], 0.0f);
  // poza zakresem = NULL
  EXPECT_EQ(POL_SoundDatSample(3, &len), nullptr);
  EXPECT_EQ(POL_SoundDatSample(-1, &len), nullptr);
}