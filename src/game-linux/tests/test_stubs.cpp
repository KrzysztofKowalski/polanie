// PORT: stuby warstwy POL_* dla testow - zamiast port_sdl.cpp (SDL3 wideo).
// Testy musza sie linkowac bez okna i audio: image13h.o woła tylko te funkcje,
// a port_shims.o dodatkowo POL_Delay/POL_Getch/POL_Kbhit.
#include <string.h>

#include "port.h"

// ostatnia paleta podana do POL_SetPalette (podgladana przez test_image13h)
unsigned char POL_test_palette[768];
int POL_test_palette_set = 0;

// bufor ekranu 320x200 (VirtualScreen dla SetScreen(0))
static unsigned char framebuffer[64000];

// global z src/main.cpp (InitText13h dokleja drive do sciezki font.dat;
// w testach InitText13h nie jest wolany, symbol tylko musi sie linkowac)
char drive[4] = {0, 0, 0, 0};

extern "C" unsigned char *POL_FrameBuffer(void) { return framebuffer; }
extern "C" void POL_SetPalette(const unsigned char *pal) {
  memcpy(POL_test_palette, pal, 768);
  POL_test_palette_set++;
}
extern "C" int POL_VideoInit(void) { return 0; }
extern "C" void POL_VideoQuit(void) {}
extern "C" void POL_Present(void) {}
extern "C" void POL_SetTextInputMode(int) {} // Write13h (src/image13h.cpp)
extern "C" void POL_Delay(int) {}
extern "C" int POL_Getch(void) { return 0; }
extern "C" int POL_Kbhit(void) { return 0; }

// --- audio: zamiast port_audio.cpp (SDL3/libopenmpt); test_cd podglada ---
int POL_test_music_calls = 0;  // ile razy POL_MusicPlay
int POL_test_music_last = -1;  // ostatnio zadany utwor
int POL_test_music_ret = 1;    // co zwraca POL_MusicPlay (1 = wystartowal)
int POL_test_music_vol = -1;   // ostatnia glosnosc z POL_MusicSetVolume

extern "C" int POL_AudioInit(void) { return 1; }
extern "C" int POL_MusicPlay(int track) {
  POL_test_music_calls++;
  POL_test_music_last = track;
  return POL_test_music_ret;
}
extern "C" int POL_MusicStop(void) { return 1; }
extern "C" void POL_MusicSetVolume(int v) { POL_test_music_vol = v; }
