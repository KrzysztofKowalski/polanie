// PORT: warstwa SDL3 (natywna, decyzja projektowa 2026-09-03) - wideo
// (320x200x8 -> okno, skala calkowita przez logical presentation),
// klawiatura, mysz, czas, proste audio WAV. Skalowanie do ekranu robi
// gamescope (port renderuje natywne 320x200); reczna skala [n] zostala
// dla bezposredniego uruchomienia. Caly kod oznaczony PORT; logika gry
// pozostaje w oryginalnych plikach.
#include "port.h"
// PORT: GPU backend (opcjonalny; wlaczany env POL_VIDEO=gpu) - port_gpu.cpp
#include "port_gpu.h"

#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------- wideo ---
#define GW 320
#define GH 200

static SDL_Window *win = NULL;
static SDL_Renderer *ren = NULL;
static SDL_Texture *tex = NULL;

static unsigned char fb[GW * GH]; // bufor ekranu gry (mode 13h)
static unsigned char pal[768];    // paleta DAC VGA (0-63)
static unsigned int lut[256];     // paleta -> ARGB8888

static int req_scale = 0; // zadany mnoznik przez CLI (0 = natywne 320x200)
static int scale = 1;     // aktualny mnoznik (fizyczne px / px gry)
static int quit_req = 0;
// PORT: GPU backend - gdy 1, prezentacja i paleta ida do port_gpu.cpp
// (SDL3 GPU/Vulkan), a renderer SDL nie istnieje (ren==NULL; zdarzenia myszy
// i POL_GetScale musza to uwzgledniac - patrz nizej).
static int video_is_gpu = 0;

static void palette_update_lut(void) {
  // DAC VGA trzyma 6 bitow (0-63) -> 8 bitow (POL_Dac6To8, port.h); slowo
  // ARGB -> w pamieci bajty B,G,R,A (format ARGB8888 little-endian).
  for (int i = 0; i < 256; i++) {
    unsigned r = POL_Dac6To8((unsigned char)pal[i * 3 + 0]);
    unsigned g = POL_Dac6To8((unsigned char)pal[i * 3 + 1]);
    unsigned b = POL_Dac6To8((unsigned char)pal[i * 3 + 2]);
    lut[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
  }
}

static void window_refresh_metrics(void) {
  // PORT: GPU backend - renderer nie istnieje, skale liczymy z rozmiaru okna
  // (koordynaty logiczne myszy; rendering liczy wlasna skale z pikseli).
  if (video_is_gpu) {
    int ww = 0, wh = 0;
    SDL_GetWindowSize(win, &ww, &wh);
    if (ww < GW || wh < GH) {
      scale = 1;
      return;
    }
    int n = ww / GW;
    if (wh / GH < n)
      n = wh / GH;
    scale = (n < 1) ? 1 : n;
    return;
  }
  if (!ren)
    return;
  int outW = 0, outH = 0;
  SDL_GetCurrentRenderOutputSize(ren, &outW, &outH);
  if (outW < GW || outH < GH) {
    scale = 1;
    return;
  }
  int n = outW / GW;
  if (outH / GH < n)
    n = outH / GH;
  scale = (n < 1) ? 1 : n;
}

void POL_SetScale(int n) { req_scale = n; }

int POL_GetScale(void) { return scale; }

// PORT: podwojny kursor - gra rysuje wlasny sprite kursora (jak int 33h pod
// DOS-em), wiec systemowy kursor okna ukrywamy na stale: SDL_HideCursor plus
// calkiem przezroczysty kursor jako fallback (czesc kompozytorow/gamescope
// ignoruje hide). Nigdzie nie wywolujemy SDL_ShowCursor().
static SDL_Cursor *hidden_cursor = NULL;
static void hide_system_cursor(void) {
  // 16x16, 1bpp MSB (2 bajty na wiersz); data=0 mask=0 -> piksel przezroczysty
  static const unsigned char blank[2 * 16];
  SDL_HideCursor();
  if (!hidden_cursor) {
    hidden_cursor = SDL_CreateCursor(blank, blank, 16, 16, 0, 0);
    if (!hidden_cursor)
      fprintf(stderr, "PORT: SDL_CreateCursor: %s\n", SDL_GetError());
  }
  if (hidden_cursor)
    SDL_SetCursor(hidden_cursor);
}

int POL_VideoInit(void) {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
    fprintf(stderr, "PORT: SDL_Init: %s\n", SDL_GetError());
    return 1;
  }

  // PORT-DECYZJA: skalowanie robi gamescope (nearest do 2560x1600), wiec port
  // renderuje zawsze natywne 320x200 (skala 1). Reczna skala [n] zostaje dla
  // bezposredniego uruchomienia bez gamescope.
  int w0 = req_scale > 0 ? req_scale * GW : GW;
  int h0 = req_scale > 0 ? req_scale * GH : GH;

  win = SDL_CreateWindow("Polanie", w0, h0,
                         SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE);
  if (!win) {
    fprintf(stderr, "PORT: SDL_CreateWindow: %s\n", SDL_GetError());
    return 1;
  }
  hide_system_cursor(); // PORT: gra ma wlasny sprite kursora (podwojny kursor)
  // PORT: GPU backend (SDL3 GPU/Vulkan) - opcjonalny, wlaczany env
  // POL_VIDEO=gpu. Przy jakimkolwiek bledzie urzadzenia/powierzchni port_gpu
  // zwraca porazke i automatycznie spadamy do zwyklego renderera ponizej
  // (domyslna sciezka i jej zachowanie bez zmian).
  if (POL_GPU_Want()) {
    if (POL_GPU_Init(win) == 0) {
      video_is_gpu = 1;
      memset(fb, 0, sizeof(fb));
      memset(pal, 0, sizeof(pal));
      palette_update_lut();
      window_refresh_metrics();
      printf("PORT: GPU: skala: 1 px gry = %dx%d fizycznych pikseli "
             "(okno %dx%d, Vulkan/SDL_GPU)%s\n",
             scale, scale, w0, h0, POL_GPU_Crt() ? ", CRT=1" : "");
      return 0;
    }
    fprintf(stderr,
            "PORT: GPU: brak urzadzenia Vulkan, fallback do renderera\n");
  }
  ren = SDL_CreateRenderer(win, NULL);
  if (!ren) {
    fprintf(stderr, "PORT: SDL_CreateRenderer: %s\n", SDL_GetError());
    return 1;
  }
  // NEAREST-NEIGHBOR: skala calkowita bez filtrowania (wycentrowana przez SDL)
  SDL_SetRenderLogicalPresentation(ren, GW, GH,
                                   SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);

  tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                          SDL_TEXTUREACCESS_STREAMING, GW, GH);
  if (!tex) {
    fprintf(stderr, "PORT: SDL_CreateTexture: %s\n", SDL_GetError());
    return 1;
  }
  SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);

  memset(fb, 0, sizeof(fb));
  memset(pal, 0, sizeof(pal));
  palette_update_lut();

  window_refresh_metrics();
  printf("PORT: skala: 1 px gry = %dx%d fizycznych pikseli (okno %dx%d)\n",
         scale, scale, w0, h0);
  return 0;
}

void POL_VideoQuit(void) {
  // PORT: GPU backend - sprzatanie urzadzenia/zasobow przed oknem
  if (video_is_gpu) {
    POL_GPU_Quit();
    video_is_gpu = 0;
  }
  if (hidden_cursor) {
    SDL_DestroyCursor(hidden_cursor);
    hidden_cursor = NULL;
  }
  if (tex) {
    SDL_DestroyTexture(tex);
    tex = NULL;
  }
  if (ren) {
    SDL_DestroyRenderer(ren);
    ren = NULL;
  }
  if (win) {
    SDL_DestroyWindow(win);
    win = NULL;
  }
  SDL_Quit();
}

unsigned char *POL_FrameBuffer(void) { return fb; }

void POL_SetPalette(const unsigned char *p) {
  if (!p)
    return;
  memcpy(pal, p, 768);
  palette_update_lut();
  // PORT: GPU backend - port_gpu ma wlasna kopie palety/LUT (konwersja
  // identyczna); bez tego fade'y nie zmienialyby kolorow na tym torze.
  if (video_is_gpu)
    POL_GPU_SetPalette(p);
}

void POL_Present(void) {
  // PORT: GPU backend - cala prezentacja (konwersja LUT, upload, quad ze
  // shaderem, vsync) zyje w port_gpu.cpp; fb pozostaje ten sam bufor gry.
  if (video_is_gpu) {
    POL_GPU_Present(fb);
    return;
  }
  if (!ren || !tex)
    return;
  window_refresh_metrics();
  // 8bpp -> ARGB8888 (LUT, bez filtrowania)
  static unsigned int argb[GW * GH];
  for (int i = 0; i < GW * GH; i++)
    argb[i] = lut[fb[i]];
  SDL_UpdateTexture(tex, NULL, argb, GW * sizeof(unsigned int));
  SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
  SDL_RenderClear(ren);
  SDL_RenderTexture(ren, tex, NULL, NULL); // nearest (logical INTEGER_SCALE)
  SDL_RenderPresent(ren);
}

// ------------------------------------------------- zdarzenia / klawiatura ---
// Mapa: SDL_Scancode -> kod scan DOS (klawisze rozszerzone)
static int sdl_to_dos_extended(SDL_Keycode k) {
  switch (k) {
  case SDLK_UP:
    return 0x48;
  case SDLK_DOWN:
    return 0x50;
  case SDLK_LEFT:
    return 0x4B;
  case SDLK_RIGHT:
    return 0x4D;
  case SDLK_HOME:
    return 0x47;
  case SDLK_END:
    return 0x4F;
  case SDLK_PAGEUP:
    return 0x49;
  case SDLK_PAGEDOWN:
    return 0x51;
  case SDLK_INSERT:
    return 0x52;
  case SDLK_DELETE:
    return 0x53;
  case SDLK_F1:
    return 0x3B;
  case SDLK_F2:
    return 0x3C;
  case SDLK_F3:
    return 0x3D;
  case SDLK_F4:
    return 0x3E;
  case SDLK_F5:
    return 0x3F;
  case SDLK_F6:
    return 0x40;
  case SDLK_F7:
    return 0x41;
  case SDLK_F8:
    return 0x42;
  case SDLK_F9:
    return 0x43;
  case SDLK_F10:
    return 0x44;
  case SDLK_KP_ENTER:
  case SDLK_RETURN:
    return 0x1C;
  case SDLK_TAB:
    return 0x0F;
  case SDLK_BACKSPACE:
    return 0x0E;
  case SDLK_ESCAPE:
    return 0x01;
  case SDLK_SPACE:
    return 0x39;
  default:
    return 0;
  }
}

#define KEYQ 64
static int keyq[KEYQ];
static int kh = 0, kt = 0; // head, tail
// PORT: tryb wpisywania tekstu (Write13h, src/image13h.cpp). Gdy wyłączony
// (gra), WASD jest mapowane na kody strzałek (przewijanie mapki); gdy
// włączony (ekran edycji), litery dochodzą jak zwykłe ASCII. Domyślnie
// wyłączony = zachowanie gry.
static int text_input_mode = 0;

void POL_SetTextInputMode(int on) { text_input_mode = on ? 1 : 0; }

void POL_PushKey(int c) {
  int next = (kt + 1) % KEYQ;
  if (next == kh)
    return; // pelna
  keyq[kt] = c;
  kt = next;
}

static int key_pop(void) {
  if (kh == kt)
    return -1;
  int c = keyq[kh];
  kh = (kh + 1) % KEYQ;
  return c;
}

// mysz w koordynatach int 33h (0..639, 0..199)
static int mouse_cx = 320, mouse_cy = 100, mouse_but = 0;
static int clicks_left = 0, clicks_right = 0;
// PORT: moment ostatniego DOWN kazdego przycisku (bezpiecznik lepkiego bitu)
static unsigned long long down_left_ms = 0, down_right_ms = 0;
#define STICKY_CLICK_MS 200
// PORT: kasowanie NIESKONSUMOWANEGO kliku starszego niz to (tylko przejscia
// ekranow/dlugie loady - np. klik "Dalej" z tekstow misji dozywalby bitwy jako
// fantom). W grze fazy bez odpytywania sa << 2 s (Decision()/fade'y), wiec
// liczniki zyja do odczytu jak int 33h; pod DOS-em licznik zyl w nieskonczonosc,
// ale tam tez kazdy ekran konczyl sie klikiem konsumowanym przez wlasna petle.
#define STALE_CLICK_MS 2000

// PORT: log diagnostyczny myszy (POL_MOUSE_DEBUG=1) - jeden przebieg usera
// ma pokazac, gdzie lamie sie sciezka klikniecia (zdarzenie SDL -> lepki
// bit -> konsumpcja w Ile() -> DispatchEvent -> SND -> glos)
static int mouse_dbg_on = -1;
static int mouse_dbg(void) {
  if (mouse_dbg_on < 0)
    mouse_dbg_on = getenv("POL_MOUSE_DEBUG") ? 1 : 0;
  return mouse_dbg_on;
}

static void mouse_from_game_coords(float gx, float gy) {
  int px = (int)gx, py = (int)gy;
  if (px < 0)
    px = 0;
  if (py < 0)
    py = 0;
  if (px > GW - 1)
    px = GW - 1;
  if (py > GH - 1)
    py = GH - 1;
  mouse_cx = px * 2; // jak int 33h: dwukrotna szerokosc (gra czyta cx>>1)
  mouse_cy = py;
}

void POL_PumpEvents(void) {
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    switch (e.type) {
    case SDL_EVENT_QUIT:
      quit_req = 1;
      // PORT: Esc do wewnetrznych petli gry, by mogly sie zakonczyc
      POL_PushKey(27);
      break;
    case SDL_EVENT_KEY_DOWN: {
      SDL_Keycode k = e.key.key;
      SDL_Keymod mod = e.key.mod;
      // PORT: WASD jako zamiennik strzałek (przewijanie mapki; decyzja usera
      // 2026-09-04). Te same kody gry co strzałki (bitwa czyta mouse.Key jako
      // 72/80/75/77). Tylko POZA trybem wpisywania tekstu (Write13h ustawia
      // POL_SetTextInputMode), żeby w polach tekstowych dalej dało się wpisać
      // 'w'/'a'/'s'/'d'. Uwaga: litery-te-klawisze przestają więc dochodzić
      // jako ASCII w bitwie (cheaty typu DOWY/SHOW/MAGIC) - świadomy koszt.
      if (!text_input_mode) {
        int scan = 0;
        switch (k) {
        case 'w':
        case 'W':
          scan = 0x48; // -> UP
          break;
        case 's':
        case 'S':
          scan = 0x50; // -> DOWN
          break;
        case 'a':
        case 'A':
          scan = 0x4B; // -> LEFT
          break;
        case 'd':
        case 'D':
          scan = 0x4D; // -> RIGHT
          break;
        }
        if (scan) {
          POL_PushKey(scan);
          break;
        }
      }
      if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
        POL_PushKey(13);
      } else if (k == SDLK_ESCAPE) {
        POL_PushKey(27);
      } else if (k == SDLK_BACKSPACE) {
        POL_PushKey(8);
      } else if (k == SDLK_TAB) {
        POL_PushKey(9);
      } else if (k >= ' ' && k <= 126) {
        int c = (int)k;
        if ((mod & SDL_KMOD_SHIFT) && c >= 'a' && c <= 'z')
          c -= 32;
        POL_PushKey(c);
      } else {
        int scan = sdl_to_dos_extended(k);
        if (scan)
          POL_PushKey(scan); // rozszerzony: gra robi if(!Key) Key=getch()
      }
      break;
    }
    case SDL_EVENT_MOUSE_MOTION:
      // PORT: GPU backend: brak renderera (logical presentation) - mysz
      // przychodzi w pikselach okna, dzielimy przez skale calkowita do
      // koordynatow gry (0..319); przy 1:1 to tozsame.
      if (video_is_gpu) {
        mouse_from_game_coords(e.motion.x / (float)scale,
                               e.motion.y / (float)scale);
      } else {
        SDL_ConvertEventToRenderCoordinates(ren, &e);
        mouse_from_game_coords(e.motion.x, e.motion.y);
      }
      break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
      if (video_is_gpu) { // PORT: GPU backend (jw.)
        mouse_from_game_coords(e.button.x / (float)scale,
                               e.button.y / (float)scale);
      } else {
        SDL_ConvertEventToRenderCoordinates(ren, &e);
        mouse_from_game_coords(e.button.x, e.button.y);
      }
      if (e.button.button == SDL_BUTTON_LEFT) {
        if (e.button.down) {
          mouse_but |= 1;
          clicks_left++;
          down_left_ms = SDL_GetTicks();
        } else
          mouse_but &= ~1;
      } else if (e.button.button == SDL_BUTTON_RIGHT) {
        if (e.button.down) {
          mouse_but |= 2;
          clicks_right++;
          down_right_ms = SDL_GetTicks();
        } else
          mouse_but &= ~2;
      }
      // PORT: diagnostyka - czy zdarzenie przycisku w ogole dociera do portu
      if (mouse_dbg())
        fprintf(stderr, "PORT: MSEV: %s przycisk %d px=%d py=%d -> L=%d R=%d "
                        "but=%d\n",
                e.button.down ? "DOWN" : "UP  ", (int)e.button.button,
                mouse_cx >> 1, mouse_cy, clicks_left, clicks_right, mouse_but);
      break;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
      window_refresh_metrics();
      break;
    }
  }
  // PORT: pod DOS-em ekran 0 (A0000) byl skanowany przez VGA w way ciagly -
  // kazdy zapis byl natychmiast widoczny. Menu/gra rysuja prosto do ekranu 0
  // i nigdzie nie wołaja ShowVirtualScreen, wiec prezentujemy bufor przy
  // kazdym pompowaniu zdarzen (ReadMouse13h/kbhit leca w petlach gry).
  // Bez tego Wayland nie zmapuje okna (brak pierwszej ramki).
  // PORT: ale petle gry pompuja 4-6 razy na iteracje (MousePos/Ile/kbhit),
  // a kazda prezentacja to pelna konwersja 64000 pikseli + UpdateTexture -
  // dlawimy do ~60 fps. Jawne POL_Present po rysowaniu (playfli, image13h)
  // zostaje natychmiastowe.
  {
    static unsigned long long last_pump_present_ms = 0;
    unsigned long long now = SDL_GetTicks();
    if (!last_pump_present_ms || now - last_pump_present_ms >= 15) {
      last_pump_present_ms = now;
      POL_Present();
    }
  }
}

int POL_QuitRequested(void) {
  POL_PumpEvents();
  return quit_req;
}

int POL_Kbhit(void) {
  POL_PumpEvents();
  return kh != kt;
}

int POL_Getch(void) {
  int c;
  while ((c = key_pop()) < 0) {
    POL_PumpEvents();
    if (quit_req)
      return 27; // zamkniecie okna = Esc
    POL_Delay(5);
  }
  return c;
}

void POL_MousePos(int *cx, int *cy, int *buttons) {
  POL_PumpEvents();
  if (cx)
    *cx = mouse_cx;
  if (cy)
    *cy = mouse_cy;
  if (buttons) {
    // PORT: tap PPM/LPM (DOWN+UP krotsze niz jedna iteracja petli gry)
    // przepadal miedzy odpytaniami - pod DOS-em petla odpytywala int 33h
    // mikrosekundowo, wiec przycisk zawsze byl przylopany w stanie
    // wcisnietym. Gdy przyciski sa juz puszczone, a licznik klikniec czeka
    // na skonsumowanie (Ile()), raportujemy lepki bit przycisku; bit gasnie
    // po konsumpcji licznika w POL_ClickCount albo po bezpieczniku czasowym
    // (petle typu GButtonUp kreca sie dopoki Button>0 i nie konsumuja
    // licznika).
    // PORT-POPRAWKA 2026-09-04: bezpiecznik NIE kasuje juz licznikow klikniec
    // po 200 ms (poprzednio: `clicks_left = clicks_right = 0`). Pod DOS-em
    // licznik int 33h (AX=0005h) zyl AZ DO ODCZYTU, bez limitu czasu - kazda
    // faza gry bez odpytywania dluzej niz 200 ms (Decision()/AI, ladowanie
    // planszy, typewriter tekstow, delay(300) w menu) gubila klik calkowicie.
    // Teraz licznik zyje do konsumpcji (jak w DOS); po przeterminowaniu lepkiego
    // bitu klik jest i tak dostarczony przez Ile() + DispatchEvent, ktory
    // odtwarza Button z licznikow (game/battle.cpp:1051-1054). Kasowanie
    // dotyczy tylko klikow starszych niz STALE_CLICK_MS (przejscia ekranow).
    int b = mouse_but;
    if (!b) {
      unsigned long long now = SDL_GetTicks();
      int l = clicks_left && now - down_left_ms < STICKY_CLICK_MS;
      int r = clicks_right && now - down_right_ms < STICKY_CLICK_MS;
      if (l)
        b |= 1;
      if (r)
        b |= 2;
      // PORT: klik starszy niz STALE_CLICK_MS = przejscie ekranu (np. klik
      // "Dalej" z tekstow, ktory inaczej dotarlby do bitwy jako fantom) -
      // kasujemy (per przycisk); mlodszy niz 2 s dozywa odczytu jak pod DOS-em.
      if (clicks_left && now - down_left_ms >= STALE_CLICK_MS) {
        if (mouse_dbg())
          fprintf(stderr, "PORT: MSEV: kasuje stary klik L (wiek=%dms)\n",
                  (int)(now - down_left_ms));
        clicks_left = 0;
      }
      if (clicks_right && now - down_right_ms >= STALE_CLICK_MS) {
        if (mouse_dbg())
          fprintf(stderr, "PORT: MSEV: kasuje stary klik R (wiek=%dms)\n",
                  (int)(now - down_right_ms));
        clicks_right = 0;
      }
      if (mouse_dbg() && (clicks_left || clicks_right) && !l && !r) {
        // PORT: lepki bit wygasl (<2 s), licznik zostaje - klik dozywa
        // odczytu w Ile() (semantyka int 33h); log raz na zmiane stanu
        static int last_logged = -1;
        int key = clicks_left * 4 + clicks_right;
        if (key != last_logged) {
          last_logged = key;
          fprintf(stderr, "PORT: MSEV: lepki bit wygasl, licznik zostaje "
                          "(L=%d wiek=%dms R=%d wiek=%dms)\n",
                  clicks_left, (int)(now - down_left_ms), clicks_right,
                  (int)(now - down_right_ms));
        }
      }
    }
    if (mouse_dbg() && !mouse_but) {
      // PORT: raport lepkiego bitu (tap miedzy odpytaniami) - fizyczne
      // przyciski sa puszczone, a raportujemy wcisniecie z licznika.
      // Dlawione: log tylko na pojawienie sie lepkiego bitu; zejscie (b=0)
      // resetuje, zeby nastepny tap tez sie zalogowal (petle typu GButtonUp
      // kreca sie bez konsumpcji - bez dlawienia log zalewalby stderr).
      static int last_sticky_logged = -1;
      if (!b) {
        last_sticky_logged = -1;
      } else if (b != last_sticky_logged) {
        last_sticky_logged = b;
        fprintf(stderr, "PORT: MSEV: lepki bit -> but=%d (L=%d R=%d)\n", b,
                clicks_left, clicks_right);
      }
    }
    *buttons = b;
  }
}

void POL_WarpMouse640(int cx, int cy) {
  mouse_cx = cx;
  mouse_cy = cy;
  if (!win)
    return;
  // koordynaty okna: logical presentation robi reszte (skala calkowita)
  float wx = (float)(cx / 2), wy = (float)cy;
  if (video_is_gpu) { // PORT: GPU backend: bez renderera - skala recznie
    wx = wx * (float)scale;
    wy = wy * (float)scale;
  } else if (ren) {
    SDL_RenderCoordinatesToWindow(ren, wx, wy, &wx, &wy);
  }
  SDL_WarpMouseInWindow(win, (int)wx, (int)wy);
}

int POL_ClickCount(int button) {
  POL_PumpEvents();
  int c = 0;
  // PORT: konsumpcja licznika gasi tez lepki bit raportowany w POL_MousePos
  if (button == 0) {
    c = clicks_left;
    clicks_left = 0;
  } else if (button == 1) {
    c = clicks_right;
    clicks_right = 0;
  }
  // PORT: diagnostyka - konsumpcja z wiekiem kliku (czy licznik dotrzywal
  // do odczytu, czy zjadl go bezpiecznik/faza bez odpytywania)
  if (mouse_dbg() && c) {
    unsigned long long now = SDL_GetTicks();
    fprintf(stderr,
            "PORT: MSEV: konsumpcja Ile(%d)=%d (wiek kliku %dms)\n", button, c,
            (int)(now - (button == 1 ? down_right_ms : down_left_ms)));
  }
  return c;
}

// PORT: odczyt licznika klikniec BEZ konsumpcji (odpowiednik int 33h AX=0005h
// w wariancie "podglad" - kwirk GetMsg13h musi wiedziec, czy PPM czeka, nie
// zjadajac licznika, ktory bitwa czyta w battle.cpp:422 `ile1 = mouse.Ile(1)`).
// Bez tego kwirk `Ile(1)` konsumowal PPM za wczesnie: lepki bit gasnie po
// 200 ms, kwirk zjadal licznik (Button=2) i `ile1` w bitwie czytal 0 ->
// rozkaz PPM nigdy nie przechodzil warunku `Button==2 && ile1`.
int POL_ClickPeek(int button) {
  POL_PumpEvents();
  if (button == 0)
    return clicks_left;
  if (button == 1)
    return clicks_right;
  return 0;
}

// ------------------------------------------------------------------ czas ---
void POL_Delay(int ms) { SDL_Delay(ms); }

unsigned long POL_GetTicks(void) { return (unsigned long)SDL_GetTicks(); }

// ----------------------------------------------------------------- audio ---
// PORT: audio przeniesione do port/port_audio.cpp (mikser: muzyka S3M przez
// libopenmpt + efekty WAV w jednym strumieniu SDL3). Interfejs: port_audio.h
// (wlaczany przez port.h); stara jednoglosowa sekcja 8-bit mono usunieta.