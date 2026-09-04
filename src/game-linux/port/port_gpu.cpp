// PORT: backend prezentacji na SDL3 GPU (Vulkan) - portu Polanie.
// Opcjonalny zamiennik renderera SDL z port_sdl.cpp, pod TYM SAMYM
// interfejsem POL_* wobec gry (gra rysuje do POL_FrameBuffer, ustawia palete
// POL_SetPalette, prezentuje POL_Present - nie widzi roznicy). Wybor w
// runtime: env POL_VIDEO=gpu; efekt CRT: env POL_CRT=1. Przy kazdym bledzie
// tworzenia urzadzenia/powierzchni zwracamy porazke, a port_sdl robi
// automatyczny fallback do zwyklego renderera (domyslna sciezka bez zmian).
//
// Architektura - wariant v2 (palette-exact, decyzja usera 2026-09-04:
// wyjscie ekranu = DOKLADNIE kolory 256-wpisowych palet, zero kolorow
// posrednich/ditheringu):
//  * tekstura ramki gry to surowe INDEKSY palety: R8_UNORM 320x200, upload
//    memcpy 64 KB co klatke (bez zadnej konwersji CPU);
//  * paleta -> kolor to osobna tekstura LUT B8G8R8A8_UNORM 256x1, upload
//    tylko przy POL_SetPalette; kolor piksela liczy shader (present.frag):
//    texelFetch(u_texIdx, floor(p/skala)) -> texelFetch(u_texLut, idx).
//    texelFetch nie interpoluje - kazdy piksel okna jest dokladnie jednym
//    z 256 wpisow palety DAC gry; (v<<2)|(v>>4) z port.h (POL_Dac6To8).
//  * quad fullscreen + jeden shader fragmentow (present.frag, SPIR-V z GLSL
//    w Makefile): tryb domyslny = nearest + integer scaling (pikselo-perfect,
//    wycentrowany prostokat jak SDL_LOGICAL_PRESENTATION_INTEGER_SCALE),
//    tryb CRT = scanlines + delikatna krzywizna + winieta;
//  * synchronizacja bez zmian: POL_Present wolany z port_sdl z tym samym
//    dlawieniem (pump co 15 ms, jawne po rysowaniach) - prezentacja ~60 Hz
//    przy vsync FIFO; gra sama rysuje klatki przy ~18 Hz. Zadnej petli
//    renderowania 1000 fps.
//  * minimize: SDL_AcquireGPUSwapchainTexture zwraca swapchain_texture==NULL
//    (nie blad) - klatka pomijana, czekamy na restore/resize.
//  * tryb renderera SDL (bez POL_VIDEO=gpu) zostaje na LUT CPU co klatke
//    (port_sdl.cpp) - ta sama konwersja POL_Dac6To8, upload ARGB8888
//    + SDL_SCALEMODE_NEAREST: rowniez pikselo-perfect, kolory z palety.
//
// Konwencja SDL_GPU dla SPIR-V (SDL_CreateGPUShader): fragment - samplery w
// set 2 (binding 0,1,... w kolejnosci SDL_BindGPUFragmentSamplers), uniform
// data w set 3 (binding = slot SDL_PushGPUFragmentUniformData). Vertex - nic
// (set 0/1 puste). Kompilacja: glslangValidator -V --target-env vulkan1.0
// (Makefile); bez kompilatora shaderow (POL_NO_SPV) backend sie nie
// uruchamia i port_sdl idzie w fallback.
#include "port_gpu.h"
#include "port.h" // POL_Dac6To8 (konwersja DAC 6->8 bitow, wspolny LUT)

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ------------------------------------------------------------- wymiary ---
#define GW 320
#define GH 200

// ------------------------------------------------------- parametry CRT ---
// Sily efektu (srednie wartosci "lightweight CRT"); dostrajanie przez edycje
// stalych ponizej (bez rekompilacji shadera - to uniformy).
static const float CRT_WARP = 0.045f; // krzywizna (0 = plaski ekran)
static const float CRT_SCAN = 0.55f;  // scanlines (0 = wylaczone)
static const float CRT_VIG = 0.30f;   // winieta (0 = wylaczone)

// ------------------------------------------------------------------ stan ---
static SDL_GPUDevice *g_dev = NULL;
static SDL_Window *g_win = NULL;
static SDL_GPUGraphicsPipeline *g_pipeline = NULL;
static SDL_GPUShader *g_shd_vert = NULL;
static SDL_GPUShader *g_shd_frag = NULL;
static SDL_GPUSampler *g_sampler = NULL;
static SDL_GPUBuffer *g_vbuf = NULL;
static SDL_GPUTexture *g_tex_idx = NULL; // ramka gry: surowe indeksy R8 320x200
static SDL_GPUTexture *g_tex_lut = NULL; // paleta 256x1 (B8G8R8A8), 1:1 wpisy
static SDL_GPUTransferBuffer *g_up_idx = NULL; // upload ramki (cyklowany)
static SDL_GPUTransferBuffer *g_up_lut = NULL; // upload palety (rzadki)
static SDL_GPUTextureFormat g_fmt =
    SDL_GPU_TEXTUREFORMAT_INVALID; // format swapchaina (pipeline musi go znac)
static int g_scale = 1;            // biezaca skala calkowita (ostatnia klatka)

// paleta gry i LUT -> ARGB8888 (tozsama konwersja co port_sdl.cpp: POL_Dac6To8)
static unsigned char g_pal[768];
static unsigned int g_lut[256];
static int g_lut_dirty = 1; // LUT na GPU niezsynchronizowany (upload w Present)

static int g_crt = -1; // env POL_CRT (raz)

// SPIR-V shaderow (generowane: build/spv_data.cpp z xxd -i). Symbole bez
// const - xxd emituje `unsigned char name[]` + `unsigned int name_len`.
#ifndef POL_NO_SPV
extern "C" {
extern unsigned char present_vert_spv[];
extern unsigned int present_vert_spv_len;
extern unsigned char present_frag_spv[];
extern unsigned int present_frag_spv_len;
}
#endif

// ------------------------------------------------------------ narzedzia ---
static int env_is(const char *name, const char *val) {
  const char *v = getenv(name);
  return v && strcmp(v, val) == 0;
}

int POL_GPU_Want(void) {
  static int want = -1;
  if (want < 0)
    want = env_is("POL_VIDEO", "gpu") ? 1 : 0;
  return want;
}

int POL_GPU_Crt(void) {
  if (g_crt < 0)
    g_crt = env_is("POL_CRT", "1") ? 1 : 0;
  return g_crt;
}

static void log_once_err(const char *what, const char *err) {
  static int logged = 0;
  if (!logged) {
    logged = 1;
    fprintf(stderr, "PORT: GPU: %s: %s\n", what, err ? err : "(brak)");
  }
}

static void palette_update_lut(void) {
  // DAC VGA trzyma 6 bitow (0-63) -> 8 bitow (POL_Dac6To8, port.h); slowo
  // ARGB -> w pamieci bajty B,G,R,A (B8G8R8A8_UNORM tekstury LUT).
  for (int i = 0; i < 256; i++) {
    unsigned r = POL_Dac6To8((unsigned char)g_pal[i * 3 + 0]);
    unsigned g = POL_Dac6To8((unsigned char)g_pal[i * 3 + 1]);
    unsigned b = POL_Dac6To8((unsigned char)g_pal[i * 3 + 2]);
    g_lut[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
  }
  g_lut_dirty = 1; // LUT na GPU odswiezy najblizszy POL_GPU_Present
}

// --------------------------------------------------- tworzenie zasobow ---
// Shadery SPIR-V wbudowane w binarke (build/spv_data.cpp). POL_NO_SPV =
// maszyna bez glslangValidator/glslc - backend nie startuje (fallback).
static SDL_GPUShader *gpu_shader(const unsigned char *code,
                                 unsigned int code_len,
                                 SDL_GPUShaderStage stage,
                                 Uint32 num_samplers,
                                 Uint32 num_uniform_buffers,
                                 const char *tag) {
#ifdef POL_NO_SPV
  (void)code;
  (void)code_len;
  (void)stage;
  (void)num_samplers;
  (void)num_uniform_buffers;
  fprintf(stderr,
          "PORT: GPU: brak SPIR-V (skompiluj przez make z glslangValidator/"
          "glslc) - backend %s wylaczony, fallback do renderera\n",
          tag);
  return NULL;
#else
  SDL_GPUShaderCreateInfo ci = {};
  ci.code_size = code_len;
  ci.code = code;
  ci.entrypoint = "main";
  ci.format = SDL_GPU_SHADERFORMAT_SPIRV;
  ci.stage = stage;
  ci.num_samplers = num_samplers;
  ci.num_uniform_buffers = num_uniform_buffers;
  SDL_GPUShader *s = SDL_CreateGPUShader(g_dev, &ci);
  if (!s)
    fprintf(stderr, "PORT: GPU: SDL_CreateGPUShader(%s): %s\n", tag,
            SDL_GetError());
  return s;
#endif
}

// Pipeline na format swapchaina; wołane po claimu okna i po zmianie formatu.
static int gpu_create_pipeline(SDL_GPUTextureFormat fmt) {
  if (g_pipeline) { // przebudowa po zmianie formatu swapchaina
    SDL_ReleaseGPUGraphicsPipeline(g_dev, g_pipeline);
    g_pipeline = NULL;
  }
  SDL_GPUGraphicsPipelineCreateInfo pci = {};
  pci.vertex_shader = g_shd_vert;
  pci.fragment_shader = g_shd_frag;
  // vertex input: slot 0, 2 x float2 (pos, uv), 16 B na wierzcholek
  static const SDL_GPUVertexBufferDescription vbd[1] = {
      {0, 16, SDL_GPU_VERTEXINPUTRATE_VERTEX, 0}};
  static const SDL_GPUVertexAttribute attrs[2] = {
      {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 0},
      {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 8}};
  pci.vertex_input_state.vertex_buffer_descriptions = vbd;
  pci.vertex_input_state.num_vertex_buffers = 1;
  pci.vertex_input_state.vertex_attributes = attrs;
  pci.vertex_input_state.num_vertex_attributes = 2;
  pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  pci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
  pci.rasterizer_state.enable_depth_clip = true;
  pci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
  pci.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
  pci.target_info.color_target_descriptions = NULL; // ustawiane nizej
  SDL_GPUColorTargetDescription ctd = {};
  ctd.format = fmt; // blend_state zerowy = bez blendowania
  pci.target_info.color_target_descriptions = &ctd;
  pci.target_info.num_color_targets = 1;
  pci.target_info.has_depth_stencil_target = false;
  g_pipeline = SDL_CreateGPUGraphicsPipeline(g_dev, &pci);
  if (!g_pipeline) {
    fprintf(stderr, "PORT: GPU: SDL_CreateGPUGraphicsPipeline: %s\n",
            SDL_GetError());
    return -1;
  }
  return 0;
}

static void gpu_destroy_resources(void) {
  if (g_dev) {
    SDL_WaitForGPUIdle(g_dev); // nic nie smiga w trakcie niszczenia
    if (g_pipeline) {
      SDL_ReleaseGPUGraphicsPipeline(g_dev, g_pipeline);
      g_pipeline = NULL;
    }
    if (g_shd_frag) {
      SDL_ReleaseGPUShader(g_dev, g_shd_frag);
      g_shd_frag = NULL;
    }
    if (g_shd_vert) {
      SDL_ReleaseGPUShader(g_dev, g_shd_vert);
      g_shd_vert = NULL;
    }
    if (g_sampler) {
      SDL_ReleaseGPUSampler(g_dev, g_sampler);
      g_sampler = NULL;
    }
    if (g_vbuf) {
      SDL_ReleaseGPUBuffer(g_dev, g_vbuf);
      g_vbuf = NULL;
    }
    if (g_tex_idx) {
      SDL_ReleaseGPUTexture(g_dev, g_tex_idx);
      g_tex_idx = NULL;
    }
    if (g_tex_lut) {
      SDL_ReleaseGPUTexture(g_dev, g_tex_lut);
      g_tex_lut = NULL;
    }
    if (g_up_idx) {
      SDL_ReleaseGPUTransferBuffer(g_dev, g_up_idx);
      g_up_idx = NULL;
    }
    if (g_up_lut) {
      SDL_ReleaseGPUTransferBuffer(g_dev, g_up_lut);
      g_up_lut = NULL;
    }
  }
}

// ---------------------------------------------------------------- wejscie ---
int POL_GPU_Init(SDL_Window *win) {
  if (g_dev)
    return 0; // juz zainicjowane
  g_win = win;
  g_crt = -1; // env POL_CRT czytane ponownie

  // 1) urzadzenie (Linux: Vulkan; SPIR-V to jedyny format, ktorego uzywamy)
  g_dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, NULL);
  if (!g_dev) {
    fprintf(stderr, "PORT: GPU: SDL_CreateGPUDevice: %s\n", SDL_GetError());
    return -1;
  }
  if (!SDL_ClaimWindowForGPUDevice(g_dev, g_win)) {
    fprintf(stderr, "PORT: GPU: SDL_ClaimWindowForGPUDevice: %s\n",
            SDL_GetError());
    SDL_DestroyGPUDevice(g_dev);
    g_dev = NULL;
    return -1;
  }
  // 2) swapchain: vsync FIFO (prezentacja dlawiona tez po stronie pump 15 ms;
  //    brak ryzyka petli 1000 fps). Blad tylko logujemy - lecimy dalej.
  if (!SDL_SetGPUSwapchainParameters(g_dev, g_win,
                                     SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                     SDL_GPU_PRESENTMODE_VSYNC))
    fprintf(stderr,
            "PORT: GPU: uwaga: SDL_SetGPUSwapchainParameters (vsync): %s\n",
            SDL_GetError());

  g_fmt = SDL_GetGPUSwapchainTextureFormat(g_dev, g_win);

  // 3) zasoby
  int bad = 0;
  g_shd_vert = gpu_shader(present_vert_spv, present_vert_spv_len,
                          SDL_GPU_SHADERSTAGE_VERTEX, 0, 0, "vertex");
  // v2: dwa samplery fragmentu - ramka indeksow (binding 0) + LUT (binding 1)
  g_shd_frag = gpu_shader(present_frag_spv, present_frag_spv_len,
                          SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 1, "fragment");
  if (!g_shd_vert || !g_shd_frag)
    bad = 1;
  if (!bad && gpu_create_pipeline(g_fmt))
    bad = 1;
  if (!bad) {
    SDL_GPUSamplerCreateInfo sci = {};
    sci.min_filter = SDL_GPU_FILTER_NEAREST; // pikselo-perfect
    sci.mag_filter = SDL_GPU_FILTER_NEAREST;
    sci.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sci.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sci.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sci.max_anisotropy = 1.0f;
    sci.min_lod = 0.0f;
    sci.max_lod = 0.0f;
    g_sampler = SDL_CreateGPUSampler(g_dev, &sci);
    if (!g_sampler) {
      fprintf(stderr, "PORT: GPU: SDL_CreateGPUSampler: %s\n", SDL_GetError());
      bad = 1;
    }
  }
  // 4) tekstury v2: ramka gry jako surowe indeksy R8 (upload 64 KB co klatke,
  //    bez konwersji CPU) + LUT palety 256x1 B8G8R8A8 (upload tylko przy
  //    zmianie palety); kolory liczy shader texelFetch (present.frag)
  if (!bad) {
    SDL_GPUTextureCreateInfo tci = {};
    tci.type = SDL_GPU_TEXTURETYPE_2D;
    tci.format = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
    tci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tci.width = GW;
    tci.height = GH;
    tci.layer_count_or_depth = 1;
    tci.num_levels = 1;
    tci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    g_tex_idx = SDL_CreateGPUTexture(g_dev, &tci);
    if (!g_tex_idx) {
      fprintf(stderr, "PORT: GPU: SDL_CreateGPUTexture (idx R8): %s\n",
              SDL_GetError());
      bad = 1;
    }
  }
  if (!bad) {
    SDL_GPUTextureCreateInfo tci = {};
    tci.type = SDL_GPU_TEXTURETYPE_2D;
    tci.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    tci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tci.width = 256;
    tci.height = 1;
    tci.layer_count_or_depth = 1;
    tci.num_levels = 1;
    tci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    g_tex_lut = SDL_CreateGPUTexture(g_dev, &tci);
    if (!g_tex_lut) {
      fprintf(stderr, "PORT: GPU: SDL_CreateGPUTexture (LUT 256x1): %s\n",
              SDL_GetError());
      bad = 1;
    }
  }
  if (!bad) {
    SDL_GPUTransferBufferCreateInfo tbi = {};
    tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbi.size = GW * GH; // 1 bajt na piksel (indeks palety)
    g_up_idx = SDL_CreateGPUTransferBuffer(g_dev, &tbi);
    if (!g_up_idx) {
      fprintf(stderr, "PORT: GPU: SDL_CreateGPUTransferBuffer (idx): %s\n",
              SDL_GetError());
      bad = 1;
    }
  }
  if (!bad) {
    SDL_GPUTransferBufferCreateInfo tbi = {};
    tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbi.size = 256 * 4; // cala LUT (1 wiersz, 256 wpisow ARGB)
    g_up_lut = SDL_CreateGPUTransferBuffer(g_dev, &tbi);
    if (!g_up_lut) {
      fprintf(stderr, "PORT: GPU: SDL_CreateGPUTransferBuffer (lut): %s\n",
              SDL_GetError());
      bad = 1;
    }
  }
  // 5) vertex buffer: fullscreen quad (2 trojkaty, 6 wierzcholkow pos+uv)
  if (!bad) {
    SDL_GPUBufferCreateInfo bci = {};
    bci.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    bci.size = 6 * 16;
    g_vbuf = SDL_CreateGPUBuffer(g_dev, &bci);
    if (!g_vbuf) {
      fprintf(stderr, "PORT: GPU: SDL_CreateGPUBuffer: %s\n", SDL_GetError());
      bad = 1;
    }
  }
  if (bad)
    goto fail;

  // 6) upload geometrii (raz, na starcie)
  {
    static const float quad[6][4] = { // x,y (clip), u,v
        {-1, -1, 0, 0}, {1, -1, 1, 0},  {-1, 1, 0, 1},
        {1, -1, 1, 0},  {1, 1, 1, 1},   {-1, 1, 0, 1}};
    SDL_GPUTransferBufferCreateInfo tbi = {};
    tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbi.size = sizeof(quad);
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(g_dev, &tbi);
    if (!tb) {
      fprintf(stderr, "PORT: GPU: vertex upload: %s\n", SDL_GetError());
      bad = 1;
    }
    if (!bad) {
      void *p = SDL_MapGPUTransferBuffer(g_dev, tb, false);
      if (!p) {
        fprintf(stderr, "PORT: GPU: SDL_MapGPUTransferBuffer: %s\n",
                SDL_GetError());
        bad = 1;
      } else {
        memcpy(p, quad, sizeof(quad));
        SDL_UnmapGPUTransferBuffer(g_dev, tb);
      }
    }
    if (!bad) {
      SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(g_dev);
      if (!cmd) {
        fprintf(stderr, "PORT: GPU: SDL_AcquireGPUCommandBuffer: %s\n",
                SDL_GetError());
        bad = 1;
      } else {
        SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
        if (!cp) {
          fprintf(stderr, "PORT: GPU: SDL_BeginGPUCopyPass: %s\n",
                  SDL_GetError());
          SDL_CancelGPUCommandBuffer(cmd);
          bad = 1;
        } else {
          SDL_GPUTransferBufferLocation src = {tb, 0};
          SDL_GPUBufferRegion dst = {g_vbuf, 0, sizeof(quad)};
          SDL_UploadToGPUBuffer(cp, &src, &dst, false);
          SDL_EndGPUCopyPass(cp);
          if (!SDL_SubmitGPUCommandBuffer(cmd))
            fprintf(stderr, "PORT: GPU: vertex submit: %s\n", SDL_GetError());
        }
      }
    }
    if (tb)
      SDL_ReleaseGPUTransferBuffer(g_dev, tb); // zwolnienie odroczone przez SDL
  }
  if (bad)
    goto fail;

  // 7) stan startowy (czarna ramka, jak port_sdl)
  memset(g_pal, 0, sizeof(g_pal));
  palette_update_lut();
  g_scale = POL_GPU_GetScale();
  return 0;

fail:
  fprintf(stderr,
          "PORT: GPU: tworzenie zasobow nieudane - fallback do renderera\n");
  gpu_destroy_resources();
  if (g_dev) {
    SDL_ReleaseWindowFromGPUDevice(g_dev, g_win);
    SDL_DestroyGPUDevice(g_dev);
    g_dev = NULL;
  }
  g_win = NULL;
  return -1;
}

void POL_GPU_Quit(void) {
  gpu_destroy_resources();
  if (g_dev) {
    if (g_win)
      SDL_ReleaseWindowFromGPUDevice(g_dev, g_win);
    SDL_DestroyGPUDevice(g_dev);
    g_dev = NULL;
  }
  g_win = NULL;
}

void POL_GPU_SetPalette(const unsigned char *pal768) {
  if (!pal768)
    return;
  memcpy(g_pal, pal768, 768);
  palette_update_lut();
}

int POL_GPU_GetScale(void) {
  if (!g_win)
    return g_scale > 0 ? g_scale : 1;
  int ww = 0, wh = 0;
  SDL_GetWindowSizeInPixels(g_win, &ww, &wh);
  int n = ww / GW;
  if (wh / GH < n)
    n = wh / GH;
  if (n < 1)
    n = 1;
  return n;
}

// ------------------------------------------------------------ prezentacja ---
void POL_GPU_Present(const unsigned char *fb) {
  if (!g_dev || !g_win || !g_pipeline || !fb)
    return;

  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(g_dev);
  if (!cmd) {
    log_once_err("SDL_AcquireGPUCommandBuffer", SDL_GetError());
    return;
  }
  SDL_GPUTexture *sw = NULL;
  Uint32 swW = 0, swH = 0;
  if (!SDL_AcquireGPUSwapchainTexture(cmd, g_win, &sw, &swW, &swH)) {
    // blad acquire - nie wolno zostawic command buffer; cancel jest legalny
    // dopoki swapchain texture nie zostala zdobyta
    SDL_CancelGPUCommandBuffer(cmd);
    log_once_err("SDL_AcquireGPUSwapchainTexture", SDL_GetError());
    return;
  }
  if (!sw || swW == 0 || swH == 0) {
    // okno zminimalizowane/0x0: swapchain zwraca NULL (to NIE jest blad) -
    // pomijamy klatke i czekamy na resize/restore (kolejny POL_Present)
    SDL_CancelGPUCommandBuffer(cmd);
    return;
  }

  // 0) zmiana formatu swapchaina (rzadkie: inny monitor/kompozytor) -
  //    pipeline ma wpisany format koloru, wiec przebudowa
  {
    SDL_GPUTextureFormat fmt = SDL_GetGPUSwapchainTextureFormat(g_dev, g_win);
    if (fmt != g_fmt) {
      SDL_WaitForGPUIdle(g_dev); // stary pipeline musi byc nieuzywany
      g_fmt = fmt;
      if (gpu_create_pipeline(g_fmt)) {
        // zostajemy bez pipeline: reszta klatek pomijana az do odzyskania
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
      }
    }
  }

  // 1) prostokat gry: najwieksza skala calkowita, wycentrowana (letterbox)
  int outW = (int)swW, outH = (int)swH;
  int n = outW / GW;
  if (outH / GH < n)
    n = outH / GH;
  if (n < 1) {
    // okno mniejsze niz gra: pokazujemy lewy gorny naroznik 1:1 (crop),
    // analogicznie do dzisiejszego window_refresh_metrics (scale=1)
    n = 1;
  }
  g_scale = n;
  int rw = GW * n, rh = GH * n;
  int rx = (outW - rw) / 2, ry = (outH - rh) / 2;

  // 2) uniformy: prostokat + parametry CRT (push, nie UBO - 32 B)
  float uni[8];
  uni[0] = (float)rx;
  uni[1] = (float)ry;
  uni[2] = (float)rw;
  uni[3] = (float)rh;
  if (POL_GPU_Crt()) {
    uni[4] = 1.0f;
    uni[5] = CRT_WARP;
    uni[6] = CRT_SCAN;
    uni[7] = CRT_VIG;
  } else {
    uni[4] = 0.0f;
    uni[5] = uni[6] = uni[7] = 0.0f;
  }
  SDL_PushGPUFragmentUniformData(cmd, 0, uni, sizeof(uni));

  // 3) upload ramki: surowe indeksy palety 1:1 (memcpy 64 KB, BEZ konwersji
  //    CPU) + LUT, gdy paleta sie zmienila od ostatniej klatki (rzadko).
  //    Bufor uploadu cyklowany (cycle=true): SDL sam podklada swiezy obszar,
  //    gdy poprzedni jest jeszcze uzywany przez GPU.
  {
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
    if (!cp) {
      log_once_err("SDL_BeginGPUCopyPass", SDL_GetError());
    } else {
      void *p = SDL_MapGPUTransferBuffer(g_dev, g_up_idx, true);
      if (!p) {
        log_once_err("SDL_MapGPUTransferBuffer (idx)", SDL_GetError());
      } else {
        memcpy(p, fb, GW * GH); // bajty ramki = indeksy palety
        SDL_UnmapGPUTransferBuffer(g_dev, g_up_idx);
        SDL_GPUTextureTransferInfo src = {};
        src.transfer_buffer = g_up_idx;
        src.offset = 0;
        src.pixels_per_row = GW;
        src.rows_per_layer = 0; // pelny obraz: wiersze gesto
        SDL_GPUTextureRegion dst = {};
        dst.texture = g_tex_idx;
        dst.mip_level = 0;
        dst.layer = 0;
        dst.x = dst.y = dst.z = 0;
        dst.w = GW;
        dst.h = GH;
        dst.d = 1;
        SDL_UploadToGPUTexture(cp, &src, &dst, false);
      }
      if (g_lut_dirty) {
        void *pl = SDL_MapGPUTransferBuffer(g_dev, g_up_lut, true);
        if (!pl) {
          log_once_err("SDL_MapGPUTransferBuffer (lut)", SDL_GetError());
        } else {
          memcpy(pl, g_lut, 256 * 4); // slowa ARGB = bajty B,G,R,A w pamieci
          SDL_UnmapGPUTransferBuffer(g_dev, g_up_lut);
          SDL_GPUTextureTransferInfo src = {};
          src.transfer_buffer = g_up_lut;
          src.offset = 0;
          src.pixels_per_row = 256;
          src.rows_per_layer = 0;
          SDL_GPUTextureRegion dst = {};
          dst.texture = g_tex_lut;
          dst.mip_level = 0;
          dst.layer = 0;
          dst.x = dst.y = dst.z = 0;
          dst.w = 256;
          dst.h = 1;
          dst.d = 1;
          SDL_UploadToGPUTexture(cp, &src, &dst, false);
          g_lut_dirty = 0; // zsynchronizowane z tekstura na GPU
        }
      }
      SDL_EndGPUCopyPass(cp);
    }
  }

  // 4) render pass na swapchain texture: clear czarny + quad ze shaderem
  //    (binding 0 = ramka indeksow, binding 1 = LUT palety)
  SDL_GPUColorTargetInfo ct = {};
  ct.texture = sw;
  ct.mip_level = 0;
  ct.layer_or_depth_plane = 0;
  ct.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
  ct.load_op = SDL_GPU_LOADOP_CLEAR;
  ct.store_op = SDL_GPU_STOREOP_STORE;
  SDL_GPURenderPass *rp = SDL_BeginGPURenderPass(cmd, &ct, 1, NULL);
  if (!rp) {
    log_once_err("SDL_BeginGPURenderPass", SDL_GetError());
  } else {
    SDL_GPUViewport vp = {0.0f, 0.0f, (float)outW, (float)outH, 0.0f, 1.0f};
    SDL_SetGPUViewport(rp, &vp);
    SDL_BindGPUGraphicsPipeline(rp, g_pipeline);
    SDL_GPUTextureSamplerBinding tsb[2] = {{g_tex_idx, g_sampler},
                                           {g_tex_lut, g_sampler}};
    SDL_BindGPUFragmentSamplers(rp, 0, tsb, 2);
    SDL_GPUBufferBinding vbb = {g_vbuf, 0};
    SDL_BindGPUVertexBuffers(rp, 0, &vbb, 1);
    SDL_DrawGPUPrimitives(rp, 6, 1, 0, 0);
    SDL_EndGPURenderPass(rp);
  }
  SDL_SubmitGPUCommandBuffer(cmd);
}
