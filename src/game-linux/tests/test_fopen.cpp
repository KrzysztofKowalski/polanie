// PORT: testy POL_fopen / POL_normalize_path (port/port_fopen.cpp).
// Normalizacja testowana czysto (bez dysku); otwarcia plikow - na katalogu
// tymczasowym z test_main.cpp (POLANIE_DATA / POLANIE_EXTRACTED), sprzatany
// po testach. Regresje, ktore testuje: sciezki z backslashami i napędem X:,
// wiodace "./" (InitBattle: "./levels\level.dat"), case-insensitive,
// levels/ -> GRAF/, levels/*.ini -> extracted/teksty/, synteza SETUP.INI
// (7 B) i pic.dat ([768 B palety][64000 B ekran] x 64).
#include <gtest/gtest.h>

#include <sys/stat.h>
#include <unistd.h> // getcwd/chdir (PORT: test slotow niezalezny od CWD)

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "port.h" // POL_normalize_path
#include "test_util.h"

extern "C" {
FILE *POL_fopen(const char *path, const char *mode);
const char *POL_last_fopen_fail(void);
}

// ---------------------------------------------------------------- normalizacja

static std::string norm(const char *p) {
  char out[1024];
  POL_normalize_path(p, out, sizeof(out));
  return out;
}

TEST(NormalizacjaSciezki, BackslashNaSlash) {
  EXPECT_EQ(norm("graf\\tlo.pcx"), "graf/tlo.pcx");
}

TEST(NormalizacjaSciezki, ZrzutNapedu) {
  // po napędzie zostaje wiodący backslash -> "/" (zachowanie portu; POL_fopen
  // dokleja potem katalog bazowy, a fopen("/x") po prostu się nie udaje, więc
  // sciezka i tak leci do <baza>//GRAF/PAL.DAT - tolerowane przez system plikow)
  EXPECT_EQ(norm("C:\\GRAF\\PAL.DAT"), "/GRAF/PAL.DAT");
  EXPECT_EQ(norm("d:dane\\plik"), "dane/plik");
}

TEST(NormalizacjaSciezki, WiodaceKropkaSlash) {
  // InitBattle prosi o "./levels\level.dat" - musi sie lapac w regule "levels/"
  EXPECT_EQ(norm("./levels\\level.dat"), "levels/level.dat");
}

TEST(NormalizacjaSciezki, WieleWiodacychKropkaSlash) {
  EXPECT_EQ(norm("./././x.txt"), "x.txt");
}

TEST(NormalizacjaSciezki, SamoKropkaSlashZostaje) {
  EXPECT_EQ(norm("./"), "./"); // CWD zostaje
}

TEST(NormalizacjaSciezki, WzgledneWsteczBezZmian) {
  EXPECT_EQ(norm("../dane/x"), "../dane/x");
}

TEST(NormalizacjaSciezki, ZwyklaSciezkaBezZmian) {
  EXPECT_EQ(norm("SETUP.INI"), "SETUP.INI");
  EXPECT_EQ(norm("levels/level.dat"), "levels/level.dat");
}

// ------------------------------------------------------------------ POL_fopen

static std::string read_all(FILE *f) {
  std::string out;
  int c;
  while ((c = fgetc(f)) != EOF)
    out.push_back((char)c);
  return out;
}

TEST(POLFopen, IstniejacyWKataloguDanych) {
  FILE *f = POL_fopen("GRAF.DAT", "rb");
  ASSERT_NE(f, nullptr);
  fseek(f, 0, SEEK_END);
  EXPECT_EQ(ftell(f), 2 * 33000); // 2 bloki z test_main.cpp
  fclose(f);
}

TEST(POLFopen, BackslashNapędILevelsFallbackDoGraf) {
  // levels/level.dat nie ma w korzeniu bazy - leci z GRAF/ (instalacja DOS),
  // a sciezka od gry przychodzi jako "./levels\level.dat".
  FILE *f = POL_fopen("./levels\\level.dat", "rb");
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(read_all(f), "LEVEL-DATA");
  fclose(f);
}

TEST(POLFopen, CaseInsensitive) {
  // plik to GRAF/pal.dat - gra otwiera np. "setup.INI"
  FILE *f = POL_fopen("GRAF/PAL.DAT", "rb");
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(read_all(f), "PAL-CI");
  fclose(f);
}

TEST(POLFopen, LevelsIniFallbackDoEkstraktu) {
  // levels/level.ini: nie ma ani w bazie, ani w GRAF/ - bierze z ekstraktu
  // (extracted/teksty/LEVEL.INI)
  FILE *f = POL_fopen("levels/level.ini", "rb");
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(read_all(f), "EKSTRAKT-INI");
  fclose(f);
}

TEST(POLFopen, SetupIniSynteza) {
  // brak SETUP.INI nigdzie -> 7 bajtow domyslnej konfiguracji karty
  // (Adlib=0, SB=1, irq=13, port=0x220, DMA=0x83; PORT: SB=1 - port zawsze
  // ma wlasny mikser audio, a od tego bajtu zalezalo wczytanie SOUND.DAT)
  FILE *f = POL_fopen("SETUP.INI", "rb");
  ASSERT_NE(f, nullptr);
  static const unsigned char oczekiwane[7] = {0x00, 0x01, 0x0D,
                                              0x20, 0x02, 0x83, 0x00};
  unsigned char buf[8] = {0};
  size_t n = fread(buf, 1, sizeof(buf), f);
  EXPECT_EQ(n, 7u); // dokladnie 7 bajtow, 8. = EOF
  EXPECT_EQ(memcmp(buf, oczekiwane, 7), 0);
  fclose(f);
}

TEST(POLFopen, PicDatSynteza) {
  // brak pic.dat -> 64 rekordy po 64768 B: [768 B palety][64000 B ekran],
  // paleta z czytelnymi kolorami tekstu (1 = cien, 255 = literki).
  FILE *f = POL_fopen("pic.dat", "rb");
  ASSERT_NE(f, nullptr);
  fseek(f, 0, SEEK_END);
  EXPECT_EQ(ftell(f), 64 * 64768);
  fseek(f, 0, SEEK_SET);
  for (int rekord : {0, 5, 63}) {
    unsigned char pal[768] = {0};
    ASSERT_EQ(fseek(f, (long)rekord * 64768, SEEK_SET), 0);
    ASSERT_EQ(fread(pal, 1, 768, f), 768u); // czytelny caly rekord
    // kolor 1: (0x50, 0x50, 0xA0)
    EXPECT_EQ(pal[1 * 3 + 0], 0x50);
    EXPECT_EQ(pal[1 * 3 + 1], 0x50);
    EXPECT_EQ(pal[1 * 3 + 2], 0xA0);
    // kolor 255: biel
    EXPECT_EQ(pal[255 * 3 + 0], 0xFF);
    EXPECT_EQ(pal[255 * 3 + 1], 0xFF);
    EXPECT_EQ(pal[255 * 3 + 2], 0xFF);
    // ekran za paleta: czarny
    for (int i = 0; i < 64; i++) {
      unsigned char b;
      ASSERT_EQ(fseek(f, (long)rekord * 64768 + 768 + i * 1000, SEEK_SET), 0);
      ASSERT_EQ(fread(&b, 1, 1, f), 1u);
      EXPECT_EQ(b, 0) << "rekord " << rekord << ", offset " << i * 1000;
    }
  }
  fclose(f);
}

TEST(POLFopen, BrakPlikuZwracaNullILoguje) {
  FILE *f = POL_fopen("brakujacy_plik_123.bin", "rb");
  EXPECT_EQ(f, nullptr);
  const char *log = POL_last_fopen_fail();
  ASSERT_NE(log, nullptr);
  EXPECT_NE(strstr(log, "brakujacy_plik_123.bin"), nullptr)
      << "POL_last_fopen_fail: " << log;
}

// ---------------------------------------------------- enumeracja katalogu ----

extern "C" int POL_ListFiles(const char *dir, char out[][256], int maxn);

TEST(POLListFiles, ListaWpisowKataloguDanych) {
  ::mkdir(testpath("lista").c_str(), 0755); // podkatalog na wpisy
  ASSERT_TRUE(test_write("lista/AAA.txt", {'x'}));
  ASSERT_TRUE(test_write("lista/bbb.dat", {'x'}));
  // sciezka wzgledna: POL_ListFiles probuje CWD, potem katalog danych
  char out[8][256];
  int n = POL_ListFiles("lista", out, 8);
  ASSERT_EQ(n, 2) << "POL_ListFiles('lista')";
  // wielkosc liter jak na dysku, "." i ".." pominiete
  int ma = 0, mb = 0;
  for (int i = 0; i < n; i++) {
    if (strcmp(out[i], "AAA.txt") == 0)
      ma = 1;
    if (strcmp(out[i], "bbb.dat") == 0)
      mb = 1;
    EXPECT_NE(out[i], ".") << "kropka nie powinna trafic do listy";
  }
  EXPECT_TRUE(ma);
  EXPECT_TRUE(mb);
}

TEST(POLListFiles, BrakKataloguZwracaMinusJeden) {
  char out[4][256];
  EXPECT_EQ(POL_ListFiles("napewno_nie_ma_katalogu_xyz", out, 4), -1);
  EXPECT_EQ(POL_ListFiles(NULL, out, 4), -1);
}

// ------------------------------------------------ sondy slotow zapisu ------
// menu zapisu (battle.cpp:3389) sonduje save.001..004 fopen-em co odswiezenie;
// brak pliku = normalny stan ("Pusty") - nie moze trafiac do
// POL_last_fopen_fail (diagnostyka cichego wyjscia ma raportowac realne braki).

TEST(POLFopen, SondaSlotuZapisuNieLadujeSieJakoFail) {
  // PORT: POL_fopen probuje najpierw CWD, a w katalogu uruchomienia leza
  // REALNE save'i uzytkownika (gra zapisuje save.001..004 do CWD) - sonda
  // lapala cudzy save zamiast testowac brak slotu, stad flaky FAIL. Na czas
  // testu CWD = katalog testowy; save.003 tam nie tworzymy (ma byc NULL).
  char cwd[1024];
  ASSERT_NE(getcwd(cwd, sizeof(cwd)), nullptr);
  ASSERT_EQ(chdir(POL_TEST_TMP.c_str()), 0);
  // slot bez pliku: fopen NULL, ale bez wpisu w POL_last_fopen_fail
  FILE *f = POL_fopen("save.003", "rb");
  EXPECT_EQ(f, nullptr);
  const char *log = POL_last_fopen_fail();
  EXPECT_EQ(strstr(log ? log : "", "save.003"), nullptr)
      << "sonda slotu nie powinna byc logowana jako FAIL: "
      << (log ? log : "(null)");
  ASSERT_EQ(chdir(cwd), 0); // przywroc CWD (tez po ADD_FAILURE)
}

TEST(POLFopen, SlotZapisuZPlikiemDzialaNormalnie) {
  // PORT: POL_fopen probuje najpierw CWD - w korzeniu repo moze lezec REALNY
  // save.001 (uzytkownik go ma), co dalo flaky FAIL zalezny od katalogu
  // uruchomienia. Na czas testu zmieniamy CWD na katalog testowy.
  char cwd[1024];
  ASSERT_NE(getcwd(cwd, sizeof(cwd)), nullptr);
  ASSERT_EQ(chdir(POL_TEST_TMP.c_str()), 0);
  ASSERT_TRUE(test_write("save.001", {1, 2, 3}));
  FILE *f = POL_fopen("save.001", "rb");
  if (f) {
    unsigned char b = 0;
    EXPECT_EQ(fread(&b, 1, 1, f), 1u);
    EXPECT_EQ(b, 1);
    fclose(f);
  } else {
    ADD_FAILURE() << "POL_fopen(\"save.001\") = NULL po zapisie slotu";
  }
  // zapis slotu to tez faktyczne uzycie (nie sonda) - nie ruszamy
  FILE *g = POL_fopen("save.002", "wb");
  if (g)
    fclose(g);
  else
    ADD_FAILURE() << "POL_fopen(\"save.002\", \"wb\") = NULL";
  ASSERT_EQ(chdir(cwd), 0); // przywroc CWD (tez po ADD_FAILURE)
}