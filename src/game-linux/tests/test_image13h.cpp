// PORT: testy loadera image13h (src/image13h.cpp) bez okna/SDL - warstwa
// POL_* jest podmieniona na stuby (tests/test_stubs.cpp).
// Regresje: naglowek [u16 size][u16 w][u16 h] (size zawyzony, wiersz 319 B -
// FONT.DAT 319x100) vs naglowek [w][h]; wiersze 319 B w LoadToScreen13h;
// palety PAL.DAT (offset pal*768) i DAC >>2 w SetExtendedPalette.
#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "image13h.h" // -I../game
#include "test_util.h"

// globals z image13h.cpp (nie ma ich w naglowku)
extern char *rgb;
extern char *VirtualScreen;
extern FILE *palettefile;
extern FILE *graphicfile;
// global ze stubow (test_stubs.cpp) - ostatnia paleta podana do POL_SetPalette
extern unsigned char POL_test_palette[768];
extern int POL_test_palette_set;

static std::vector<unsigned char> font_naglowek(unsigned w, unsigned h,
                                                unsigned size_zawyzone) {
  std::vector<unsigned char> v;
  auto u16 = [&v](unsigned x) {
    v.push_back(x & 0xFF);
    v.push_back((x >> 8) & 0xFF);
  };
  u16(size_zawyzone);
  u16(w);
  u16(h);
  for (unsigned i = 0; i < w * h; i++)
    v.push_back((unsigned char)((i * 13 + 5) & 0xFF));
  return v;
}

class Image13h : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(InitBuffers13h(), 0); // alokuje rgb/Rgb/Buffer330
    SetScreen(0);                   // VirtualScreen = bufor ze stubu
    OpenPaletteFile();              // pal.dat -> POL_fopen -> katalog testow
    ASSERT_NE(palettefile, nullptr);
  }
  void TearDown() override {
    ClosePaletteFile();
    FreeBuffers13h();
  }
};

// ---------------------------------------------------------------- LoadImage13h

TEST_F(Image13h, NaglowekFontu_SizeWH_Wiersz319B) {
  // FONT.DAT: [u16 size=32326][u16 w=319][u16 h=100] + 31900 B danych
  std::vector<unsigned char> dane = font_naglowek(319, 100, 32326);
  ASSERT_TRUE(test_write("font_test.img", dane));

  std::string sciezka = testpath("font_test.img");
  char *pic = LoadImage13h((char *)sciezka.c_str());
  ASSERT_NE(pic, nullptr);

  short *b = (short *)pic;
  EXPECT_EQ(b[0], 319); // w z pola 1
  EXPECT_EQ(b[1], 100); // h z pola 2
  EXPECT_EQ((unsigned char)pic[4], 1); // naglowek [w][h][1][0] dla PutImage13h
  EXPECT_EQ(pic[5], 0);
  // dane od offsetu 6, wiersze po 319 B - rowne danym z pliku
  EXPECT_EQ((unsigned char)pic[6], (unsigned char)((0 * 13 + 5) & 0xFF));
  EXPECT_EQ((unsigned char)pic[6 + 318], (unsigned char)((318 * 13 + 5) & 0xFF));
  EXPECT_EQ((unsigned char)pic[6 + 319 * 100 - 1],
            (unsigned char)(((319 * 100 - 1) * 13 + 5) & 0xFF));
  free(pic);
}

TEST_F(Image13h, NaglowekWH_MalyObraz) {
  // format [w][h] tworzony przez gre: pole 0 to od razu szerokosc
  std::vector<unsigned char> dane = font_naglowek(8, 5, 0 /*nieistotne*/);
  dane[0] = 8; // pole 0 = w
  dane[1] = 0;
  dane[2] = 5; // pole 1 = h
  dane[3] = 0;
  ASSERT_TRUE(test_write("maly_test.img", dane));

  std::string sciezka = testpath("maly_test.img");
  char *pic = LoadImage13h((char *)sciezka.c_str());
  ASSERT_NE(pic, nullptr);
  short *b = (short *)pic;
  EXPECT_EQ(b[0], 8);
  EXPECT_EQ(b[1], 5);
  EXPECT_EQ((unsigned char)pic[4], 1);
  free(pic);
}

TEST_F(Image13h, BrakPliku_ZwracaNull) {
  std::string sciezka = testpath("naprawde_nie_ma.img");
  EXPECT_EQ(LoadImage13h((char *)sciezka.c_str()), nullptr);
}

// -------------------------------------------------------------- LoadToScreen13h

TEST_F(Image13h, LoadToScreen_Wiersze319B) {
  OpenGraphicFile(); // graf.dat -> GRAF.DAT z test_main.cpp (2 bloki 33000 B)
  ASSERT_NE(graphicfile, nullptr);

  ASSERT_EQ(LoadToScreen13h(0, 0), 1); // gorna polowa bloku 0
  // bajt bloku na pozycji 6 + y*319 + x wypada na ekranie x + y*320
  for (int y : {0, 50, 99})
    for (int x : {0, 100, 318}) {
      unsigned char oczekiwane =
          (unsigned char)(((6 + y * 319 + x) * 7 + 11) & 0xFF);
      EXPECT_EQ((unsigned char)VirtualScreen[y * 320 + x], oczekiwane)
          << "y=" << y << " x=" << x;
    }
  // 320. bajt wiersza (x=319) nie nalezy do bloku - zostaje 0
  EXPECT_EQ((unsigned char)VirtualScreen[0 * 320 + 319], 0);
  EXPECT_EQ((unsigned char)VirtualScreen[99 * 320 + 319], 0);

  ASSERT_EQ(LoadToScreen13h(1, 0), 1); // blok 1: offset 1*33000
  for (int y : {0, 99}) {
    unsigned char oczekiwane =
        (unsigned char)(((33000 + 6 + y * 319) * 7 + 11) & 0xFF);
    EXPECT_EQ((unsigned char)VirtualScreen[y * 320], oczekiwane);
  }
  CloseGraphicFile();
}

// ---------------------------------------------------- sprite'y panelu bitwy
// Panel bitwy (game/graphics.cpp:1167-1253, ShowPanel) sklada sie WYLACZNIE
// z obrazow wycinanych z arkuszy GRAF.DAT przez GetImage13h i wklejanych
// PutImage13h: tlo gniazda Buttons[3] = wycinek 18x16 px z (274,58) arkusza 3
// (game/graphics.cpp:795), ikona krowy movers[0][0][1][1] = 16x14 px z
// (16,14) arkusza 4 (game/graphics.cpp:855-858). Zaden z tych sprite'ow nie
// przechodzi przez LoadImage13h (naglowek tworzy GetImage13h), wiec regresja
// ubezpiecza caly lancuch GetImage13h -> PutImage13h: naglowek [w][h][1][0],
// pitch wiersza, przezroczystosc (how=1) i pelne kopiowanie (how=0).

// Wypelnia obszar ekranu wzorem (i*13+5)&0xFF liczonym od poczatku obszaru.
static void wypelnij_wzor(char *ekran, int x1, int y1, int w, int h) {
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++)
      ekran[(y1 + j) * 320 + x1 + i] =
          (char)(unsigned char)((j * w + i) * 13 + 5);
}

TEST_F(Image13h, SpritePanelu_GetImagePutImage_PikselWPiksel) {
  // wycinek jak ikona krowy na panelu: 16x14 z (16,14) arkusza 4
  const int W = 16, H = 14, X1 = 16, Y1 = 14;
  char *sprite = (char *)malloc(GetImageSize13h(0, 0, W - 1, H - 1));
  ASSERT_NE(sprite, nullptr);

  memset(VirtualScreen, 0x2A, 64000);      // tlo pod calym ekranem
  wypelnij_wzor(VirtualScreen, X1, Y1, W, H); // obszar sprite'u
  GetImage13h(X1, Y1, X1 + W, Y1 + H, sprite);

  short *b = (short *)sprite;
  EXPECT_EQ(b[0], W);                  // szerokosc w polu 0
  EXPECT_EQ(b[1], H);                  // wysokosc w polu 1
  EXPECT_EQ((unsigned char)sprite[4], 1); // naglowek [w][h][1][0]
  EXPECT_EQ(sprite[5], 0);
  for (int j = 0; j < H; j++)
    for (int i = 0; i < W; i++)
      ASSERT_EQ((unsigned char)sprite[6 + i + j * W],
                (unsigned char)((j * W + i) * 13 + 5))
          << "sprite j=" << j << " i=" << i;

  // wklejenie w inne miejsce ekranu (jak ShowPanel na (275,99))
  memset(VirtualScreen, 0x2A, 64000);
  PutImage13h(100, 50, sprite, 0);
  for (int j = 0; j < H; j++)
    for (int i = 0; i < W; i++)
      EXPECT_EQ((unsigned char)VirtualScreen[(50 + j) * 320 + 100 + i],
                (unsigned char)((j * W + i) * 13 + 5))
          << "ekran j=" << j << " i=" << i;
  // pitch: zadne piksele obok prostokata nie moga byc nadpisane
  for (int j = 0; j < H; j++) {
    EXPECT_EQ((unsigned char)VirtualScreen[(50 + j) * 320 + 99], 0x2A)
        << "lewy brzeg j=" << j;
    EXPECT_EQ((unsigned char)VirtualScreen[(50 + j) * 320 + 100 + W], 0x2A)
        << "prawy brzeg j=" << j;
  }
  for (int i = -1; i <= W; i++) {
    EXPECT_EQ((unsigned char)VirtualScreen[49 * 320 + 100 + i], 0x2A)
        << "gora i=" << i;
    EXPECT_EQ((unsigned char)VirtualScreen[(50 + H) * 320 + 100 + i], 0x2A)
        << "dol i=" << i;
  }
  free(sprite);
}

TEST_F(Image13h, SpritePanelu_TransparentnoscHow1) {
  // ikony panelu sa wklejane z how=1 (kolor 0 = przezroczysty)
  const int W = 16, H = 14;
  char *sprite = (char *)malloc(GetImageSize13h(0, 0, W - 1, H - 1));
  ASSERT_NE(sprite, nullptr);
  sprite[0] = (char)(W & 0xFF);
  sprite[1] = (char)((W >> 8) & 0xFF);
  sprite[2] = (char)(H & 0xFF);
  sprite[3] = (char)((H >> 8) & 0xFF);
  sprite[4] = 1;
  sprite[5] = 0;
  for (int p = 0; p < W * H; p++)
    sprite[6 + p] = (p % 3 == 0) ? (char)0 : (char)(0x10 + (p & 0x3F));

  memset(VirtualScreen, 0x2A, 64000);
  PutImage13h(100, 50, sprite, 1);
  for (int j = 0; j < H; j++)
    for (int i = 0; i < W; i++) {
      int p = j * W + i;
      unsigned char oczekiwane =
          (p % 3 == 0) ? 0x2A : (unsigned char)(0x10 + (p & 0x3F));
      EXPECT_EQ((unsigned char)VirtualScreen[(50 + j) * 320 + 100 + i],
                oczekiwane)
          << "how=1 j=" << j << " i=" << i;
    }
  free(sprite);
}

TEST_F(Image13h, SpritePanelu_How0_NadpisujeTezKolor0) {
  // how=0 (tlo gniazda Buttons[3] w ShowPanel) kopiuje caly prostokat,
  // takze piksele koloru 0
  const int W = 16, H = 14;
  char *sprite = (char *)malloc(GetImageSize13h(0, 0, W - 1, H - 1));
  ASSERT_NE(sprite, nullptr);
  sprite[0] = (char)(W & 0xFF);
  sprite[1] = (char)((W >> 8) & 0xFF);
  sprite[2] = (char)(H & 0xFF);
  sprite[3] = (char)((H >> 8) & 0xFF);
  sprite[4] = 1;
  sprite[5] = 0;
  for (int p = 0; p < W * H; p++)
    sprite[6 + p] = (p % 3 == 0) ? (char)0 : (char)(0x10 + (p & 0x3F));

  memset(VirtualScreen, 0x2A, 64000);
  PutImage13h(100, 50, sprite, 0);
  for (int j = 0; j < H; j++)
    for (int i = 0; i < W; i++) {
      int p = j * W + i;
      unsigned char oczekiwane =
          (p % 3 == 0) ? 0 : (unsigned char)(0x10 + (p & 0x3F));
      EXPECT_EQ((unsigned char)VirtualScreen[(50 + j) * 320 + 100 + i],
                oczekiwane)
          << "how=0 j=" << j << " i=" << i;
    }
  free(sprite);
}

TEST_F(Image13h, SpritePanelu_Gniazdo18x16_PitchPoSzerokosc) {
  // tlo gniazda panelu: Buttons[2]/Buttons[3] wyciete z (274,58)-(292,74)
  // (game/graphics.cpp:793-795) - 18 px szerokosci, 16 wysokosci;
  // PutImage13h musi czytac wiersze po 18 B, nie po 19 (rozmiar alokacji)
  const int W = 18, H = 16;
  char *sprite = (char *)malloc(GetImageSize13h(0, 0, W - 1, H - 1));
  ASSERT_NE(sprite, nullptr);

  memset(VirtualScreen, 0x2A, 64000);
  wypelnij_wzor(VirtualScreen, 274, 58, W, H);
  GetImage13h(274, 58, 292, 74, sprite);
  short *b = (short *)sprite;
  ASSERT_EQ(b[0], W);
  ASSERT_EQ(b[1], H);

  memset(VirtualScreen, 0x2A, 64000);
  PutImage13h(274, 18, sprite, 0); // pozycja gniazda nr 0 w ShowPanel
  for (int j = 0; j < H; j++)
    for (int i = 0; i < W; i++)
      EXPECT_EQ((unsigned char)VirtualScreen[(18 + j) * 320 + 274 + i],
                (unsigned char)((j * W + i) * 13 + 5))
          << "gniazdo j=" << j << " i=" << i;
  EXPECT_EQ((unsigned char)VirtualScreen[17 * 320 + 274], 0x2A);
  EXPECT_EQ((unsigned char)VirtualScreen[34 * 320 + 274], 0x2A);
  free(sprite);
}

// ------------------------------------------------------------------- palety

TEST_F(Image13h, LoadExtendedPalette_OffsetPal768) {
  // PAL.DAT: paleta i = 768 B o wartosci 0x10 + i -> LoadExtendedPalette(i)
  // musi wczytac z offsetu i*768
  for (int pal : {0, 2, 10, 13}) {
    LoadExtendedPalette(pal);
    ASSERT_NE(rgb, nullptr);
    EXPECT_EQ((unsigned char)rgb[0], 0x10 + pal);
    EXPECT_EQ((unsigned char)rgb[383], 0x10 + pal);
    EXPECT_EQ((unsigned char)rgb[767], 0x10 + pal);
  }
}

TEST_F(Image13h, SetExtendedPalette_DacDzielenieNa4) {
  // PAL.DAT ma wartosci 0..255, DAC VGA przyjmuje 0..63 (gra robi >>2)
  LoadExtendedPalette(13); // cala paleta 0x1D
  rgb[0] = (char)0x50;     // kontrolnie dwie wartosci
  rgb[1] = (char)0xFF;
  int przed = POL_test_palette_set;
  SetExtendedPalette();
  EXPECT_EQ(POL_test_palette_set, przed + 1); // paleta poszla do POL_SetPalette
  EXPECT_EQ(POL_test_palette[0], 0x50 >> 2);  // 0x14
  EXPECT_EQ(POL_test_palette[1], 0xFF >> 2);  // 63 = max DAC
  EXPECT_EQ(POL_test_palette[10], 0x1D >> 2);
  for (int i = 0; i < 768; i++)
    ASSERT_LE(POL_test_palette[i], 63) << "i=" << i;
}