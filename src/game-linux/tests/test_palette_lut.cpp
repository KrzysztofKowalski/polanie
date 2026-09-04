// PORT: testy konwersji palety DAC VGA 6 bitow -> 8 bitow (POL_Dac6To8,
// port/port.h) - jedynej konwersji palety w porcie (port_sdl.cpp i
// port_gpu.cpp buduja LUT wylacznie przez nia).
// Wlasciwosci wymagane przez wariant A (palette-exact):
//  * 0 -> 0, 63 -> 255 (pelny zakres DAC),
//  * scisle rosnaca (bez inwersji - kazdy wpis palety ma unikalny kolor),
//  * tozsama z round(v*255/63) na calej dziedzinie (referencyjna konwersja
//    emulatorow, np. DOSBox Staging rgb6_to_8) - dzieki temu port i
//    narzedzia ekstrakcji (extracted/) pokazuja identyczne kolory.
#include <gtest/gtest.h>

#include "port.h"

TEST(PaletteLut, Dac6To8_ZakresyKrancowe) {
  EXPECT_EQ(POL_Dac6To8(0), 0u);    // czarny
  EXPECT_EQ(POL_Dac6To8(63), 255u); // pelna jasnosc DAC
}

TEST(PaletteLut, Dac6To8_ScisleRosnaca) {
  for (unsigned v = 0; v < 63; v++)
    EXPECT_LT(POL_Dac6To8(v), POL_Dac6To8(v + 1)) << "v=" << v;
}

TEST(PaletteLut, Dac6To8_TozsamaZRound255Przez63) {
  // (v*255+31)/63 == floor(v*255/63 + 0.5) == round(v*255/63) dla calkowitych v
  for (unsigned v = 0; v <= 63; v++) {
    unsigned oczekiwane = (unsigned)(v * 255.0 / 63.0 + 0.5);
    EXPECT_EQ(POL_Dac6To8(v), oczekiwane) << "v=" << v;
  }
}

TEST(PaletteLut, Dac6To8_WszystkieWartosciMieszczaSieW8Bitach) {
  for (unsigned v = 0; v <= 63; v++)
    EXPECT_LE(POL_Dac6To8(v), 255u);
}
