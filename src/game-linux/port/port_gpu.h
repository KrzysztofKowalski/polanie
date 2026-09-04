// PORT: opcjonalny backend prezentacji SDL3 GPU (Vulkan) dla portu Polanie.
// Zamiennik renderera SDL w port_sdl.cpp, pod TYM SAMYM interfejsem POL_*
// wobec gry (port.h) - gra nie widzi roznicy. Wybor i sterowanie env:
//   POL_VIDEO=gpu  - proba backendu GPU przy starcie (przy bledzie
//                    urzadzenia/powierzchni automatyczny fallback do
//                    renderera; domyslnie: renderer, bez zmian)
//   POL_CRT=1      - efekt CRT "a'la crt" w shaderze (scanlines + lekka
//                    krzywizna + winieta); bez tego tryb domyslny = nearest
//                    + integer scaling (pikselo-perfect jak dzis)
// Implementacja: port/port_gpu.cpp; shadery GLSL port/present.{vert,frag}
// kompilowane do SPIR-V w Makefile i wbudowane jako tablice bajtow.
// Dokumentacja architektury i wariant v2: komentarz na poczatku port_gpu.cpp.
#ifndef POL_PORT_GPU_H
#define POL_PORT_GPU_H

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

// 1 gdy env POL_VIDEO=gpu (backend ma byc probowany). Czyta env raz.
int POL_GPU_Want(void);

// 1 gdy env POL_CRT=1 (efekt CRT w shaderze fragmentow). Czyta env raz.
int POL_GPU_Crt(void);

// Utworzenie urzadzenia GPU, powierzchni/swapchaina okna (okno tworzy
// wczesniej port_sdl) i zasobow (shadery SPIR-V, pipeline, sampler, tekstura
// ramki, vertex buffer). Zwraca 0 przy sukcesie; -1 przy bledzie - wolno wtedy
// isc w fallback do zwyklego renderera (obiektu nie zostaje w stanie
// czesciowym: urzadzenie zwolnione).
int POL_GPU_Init(SDL_Window *win);

// Sprzatanie zasobow GPU (wolac przed SDL_Quit).
void POL_GPU_Quit(void);

// Kopia palety DAC (256x3, 0-63) dla konwersji LUT na CPU. Wolac gdy backend
// aktywny (przekazuje port_sdl z POL_SetPalette).
void POL_GPU_SetPalette(const unsigned char *pal768);

// Prezentacja ramki gry 320x200 8bpp (fb = POL_FrameBuffer()). Ta sama
// konwersja CPU przez LUT co w port_sdl.cpp, upload tekstury RGBA i quad
// fullscreen z shaderem (tryb wg POL_GPU_Crt). Dla okna zminimalizowanego
// klatka jest pomijana (swapchain zwraca NULL) - czeka na resize/restore.
void POL_GPU_Present(const unsigned char *fb);

// Biezaca skala calkowita okna (fizyczne px / px gry) - dla logow i myszy
// w trybie GPU (bez renderera brak window_refresh_metrics).
int POL_GPU_GetScale(void);

#ifdef __cplusplus
}
#endif

#endif // POL_PORT_GPU_H
