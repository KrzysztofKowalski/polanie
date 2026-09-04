// PORT: warstwa audio portu Polanie (game-linux) - SDL3 + libopenmpt.
//
// Architektura (PORT-DECYZJA): JEDEN strumien SDL3 z wlasnym callbackiem
// miksera, zamiast osobnych strumieni podpietych do urzadzenia. W SDL3 mozna
// podpiac wiele SDL_AudioStream do jednego urzadzenia i system je sumuje, ale
// kazdy strumien konwertuje format osobno, a my tracimy wspolna kontrole nad
// glosnoscia muzyka/efekty i zapetlaniem utworow w jednym miejscu. Wlasny
// callback (SDL_OpenAudioDeviceStream z callbackiem) dostaje "ile bajtow
// potrzeba" od SDL, renderuje muzyke przez libopenmpt prosto do bufora
// F32 stereo i dodaje do niej glosy efektow WAV - jeden kod miksera, jeden
// mutex, przewidywalne uzycie CPU.
//
// - muzyka: moduly S3M z extracted/audio/muzyka/GRAF_*.s3m (zamiennik
//   CD-Audio z game/cd.cpp), renderowane na biezaco przez
//   openmpt_module_read_interleaved_float_stereo() do F32 stereo 48 kHz;
//   NIE przenosimy emulacji OPL/Adlib (plays3m/adlib/instrum = martwy kod).
// - efekty/mowa: RIFF WAV 8-bit mono 22050 Hz z extracted/audio/dzwieki/
//   (W001-W055.wav), dekodowane raz przez SDL_LoadWAV_IO (SDL3 czyta 8-bit
//   mono natywnie) i trzymane w cache; brak sciezki AAC/libav - decyzja
//   projektowa usera (2026-09-03): WAV zostaje.
#include "port_audio.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include <strings.h> // strcasecmp (stem_match)
#include <string>
#include <utility>
#include <vector>

#include "polshim.h" // fopen -> POL_fopen (normalizacja sciezek DOS)
#include "port.h"    // POL_DataDir()/POL_ExtractedDir()
#include "sound_dat.h" // PORT: kontener SOUND.DAT (glosy jednostek 1-183)

// PORT: tor MIDI+sfizz (fanowskie .mid + soundfonty VSCO CE) + dispatch SIMD
#include "audio_opts.h"  // POL_AUDIO_AUTO/S3M/SFZ (ten sam enum co tu)
#include "midi_map.h"    // tabela utworow CD -> .mid (fanowskie)
#include "midi_map_files.h" // szukanie plikow .mid + katalog vsco/
#include "mix_simd.h"    // gorace petle miksera (AVX2/scalar)
#include "sfz_engine.h"  // silnik MIDI+sfizz

// PORT: <SDL.h>/<string>/<vector> robia "#undef fopen" i zabijaja makro
// fopen->POL_fopen z polshim.h (jak w STATUS pkt 1 dla <cstdio>) - bez tego
// read_file() czytal glibc fopenem z CWD i efekty z "data/W0nn.dat" NIGDY
// nie siegaly do katalogu danych/ekstraktu (cichy brak dzwieku). Przywracamy.
#ifndef fopen
#define fopen POL_fopen
#endif

// ---------------------------------------------------------------- openmpt ---
#if __has_include(<libopenmpt/libopenmpt.h>)
#define POL_HAVE_OPENMPT 1
#include <libopenmpt/libopenmpt.h>
#endif

#define MIX_FREQ 48000
#define MIX_CH 2
#define MAXVOICES 8

// ------------------------------------------------- mapowanie utworow S3M ---
// PORT: tabela TrackMap/find_track wylaczona do port/track_map.h
// (tez dla testow jednostkowych) - semantyka i komentarze tam.
#include "track_map.h"

// PORT: skala glosnosci jak w oryginale (game/cd.cpp:485-504: 0/50/75/113/
// 170/255 z 255) - znormalizowana do wzmocnienia liniowego.
static float vol_gain(int v) {
  static const float g[6] = {0.0f, 0.20f, 0.30f, 0.44f, 0.67f, 1.0f};
  if (v < 0)
    v = 0;
  if (v > 5)
    v = 5;
  return g[v];
}

// PORT: glosnosc muzyki przez OPENMPT_MODULE_RENDER_MASTERGAIN_MILLIBEL
// (wzmocnienie w setnych decybela; 0 mB = 1.0). vol_gain -> decybely.
#ifdef POL_HAVE_OPENMPT
#include <math.h>
static int32_t vol_millibel(int v) {
  float g = vol_gain(v);
  if (g <= 0.0f)
    return -96000; // wyciszone
  return (int32_t)lround(2000.0f * log10f(g));
}
#endif

// ---------------------------------------------------------------- stan ----
static SDL_AudioStream *strm = NULL;
static SDL_Mutex *mx = NULL;

struct Voice {
  const float *pcm;  // probki (mono, float) z cache
  size_t len;
  size_t pos;        // indeks nastepnej probki zrodlowej
  double frac;       // ulamek pozycji miedzy probkami (resampling liniowy)
  double step;       // przyrost pozycji na klatke wyjscia = rate / MIX_FREQ
  int active;
};
static Voice voices[MAXVOICES];
static int voice_rr = 0; // rotacja slotow (najstarszy glos nadpisywany)

struct SfxEntry {
  std::string path;
  std::vector<float> pcm; // zdekodowany WAV jako float mono
  int rate;               // PORT: probkowanie zrodlowe WAV (resampling glosu)
};
// PORT: cache probek - rosnie do ~55 wpisow (kilka kB kazdy); glosy trzymaja
// wskaznik na dane wewnatrz wpisu, wiec cache nigdy nie czyscimy (wektory
// wewnetrzne przenosza sie przy realokacji outer, dane zostaja na heapie).
static std::vector<SfxEntry> sfx_cache;

#ifdef POL_HAVE_OPENMPT
static openmpt_module *mod = NULL; // pod mx
#endif
static int music_active = 0; // pod mx (jaki kolwiek tor muzyki)
static int music_loop = 0;   // pod mx (tor S3M)
static int cur_cd = 0;       // ostatnio zadany utwor CD
static int music_vol = 5;    // skala 0-5 (setVolume z cd.h)
static int sfx_vol = 5;
static int openmpt_missing_logged = 0;
// PORT: tor muzyki (CLI --audioType=...): 0=auto (.mid -> sfizz, inaczej S3M),
// 1=wymuszony S3M, 2=wymuszony MIDI+sfizz. Patrz port/audio_opts.h.
// PORT: decyzja usera 2026-09-04 — VSCO niestabilne, domyślnie S3M (jawne
// auto/sfz nadal działa). Bez flagi nie ruszamy .mid/sfizz (play_midi_track
// nie startuje, VSCO nie ładuje się).
static int audio_type = POL_AUDIO_S3M;

void POL_SetAudioType(int t) {
  if (t < POL_AUDIO_AUTO || t > POL_AUDIO_SFZ)
    t = POL_AUDIO_AUTO;
  audio_type = t;
  printf("PORT: tor muzyki: %s\n",
         t == POL_AUDIO_S3M ? "s3m (wymuszony, libopenmpt)"
         : t == POL_AUDIO_SFZ ? "sfz (wymuszony, sfizz+VSCO)"
                              : "auto (.mid gdzie jest, inaczej S3M)");
}

int POL_GetAudioType(void) { return audio_type; }

static int ensure_init(void) { return strm ? 0 : POL_MixerInit(); }

// --------------------------------------------------------- callback mixu ---
// Uruchamiany na watku audio SDL, gdy strumien potrzebuje danych.
static void SDLCALL mix_cb(void *userdata, SDL_AudioStream *s, int additional,
                           int total) {
  (void)userdata;
  (void)total;
  size_t frames = (size_t)additional / (MIX_CH * sizeof(float));
  if (!frames)
    return;
  static std::vector<float> buf; // tylko watek audio; rosnie wg potrzeb
  if (buf.size() < frames * MIX_CH)
    buf.resize(frames * MIX_CH);
  float *out = buf.data();

  SDL_LockMutex(mx);
  size_t done = 0; // klatki muzyki
  // PORT: tor MIDI+sfizz ma priorytet (gdy jest wlaczony utwor .mid);
  // POL_SfzEngineRender dodaje synteze do bufora (miks addytywny).
  int sfz_was = POL_SfzEnginePlaying();
  if (music_active && sfz_was) {
    // PORT-naprawa 2026-09-03: render moze skonczyc utwor w polowie bloku -
    // zwraca wtedy liczbe wypelnionych klatek, a reszta jest zerowana
    // (memset nizej); wczesniej `done = frames` wypychal do urzadzenia ogon
    // bufora z nieaktualnymi probkami (slyszalne "rozciagniecie"/echo nut).
    done = POL_SfzEngineRender(out, frames);
  }
#ifdef POL_HAVE_OPENMPT
  if (done < frames && mod && music_active) {
    done = openmpt_module_read_interleaved_float_stereo(mod, MIX_FREQ, frames,
                                                        out);
    if (done < frames && music_loop) {
      // koniec utworu -> od poczatku (zapetlenie plansz/menu)
      openmpt_module_set_position_seconds(mod, 0.0);
      done += openmpt_module_read_interleaved_float_stereo(
          mod, MIX_FREQ, frames - done, out + done * MIX_CH);
    }
    if (done < frames)
      music_active = 0; // utwor wygral sie do konca
  }
#else
  static int once = 0;
  if (!once) {
    once = 1;
    openmpt_missing_logged = 1;
    fprintf(stderr, "PORT: libopenmpt brak - muzyka S3M wylaczona\n");
  }
#endif
  // PORT: utwor .mid wygral sie do konca (nie zapetlony, z ogonem release) ->
  // czyscimy stan "gra muzyka" (CheckCD/POL_MusicPlaying)
  if (sfz_was && !POL_SfzEnginePlaying())
    music_active = 0;
  memset(out + done * MIX_CH, 0, (frames - done) * MIX_CH * sizeof(float));

  // glosy efektow: miks addytywny na wierzch muzyki. PORT: probki zrodlowe
  // sa 22050 Hz, wyjscie 48000 Hz - resampling liniowy. PORT: petla wynosi
  // do mix_simd (AVX2 z dispatchem runtime; sciezka identyczna bitowo w
  // granicach zaokraglen float - testowane w tests/test_sfz.cpp).
  float g = vol_gain(sfx_vol);
  for (int i = 0; i < MAXVOICES; i++) {
    Voice &v = voices[i];
    if (!v.active || !v.pcm)
      continue;
    size_t used = polmix::mix_voice_mono(out, frames, v.pcm, v.len, &v.pos,
                                         &v.frac, v.step, g);
    if (used < frames)
      v.active = 0; // probka skonczona w tym bloku
  }
  SDL_UnlockMutex(mx);

  // clamp przeciw clippingowi sumy muzyka+efekty (mix_simd: AVX2/scalar)
  polmix::clamp_f32(out, frames * MIX_CH);
  SDL_PutAudioStreamData(s, out, (int)(frames * MIX_CH * sizeof(float)));
}

// ------------------------------------------------- pliki / sciezki ----------
static std::vector<unsigned char> read_file(const char *path) {
  FILE *f = fopen(path, "rb"); // shim: POL_fopen
  if (!f)
    return {};
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return {};
  }
  long n = ftell(f);
  if (n <= 0) {
    fclose(f);
    return {};
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return {};
  }
  std::vector<unsigned char> d((size_t)n);
  size_t got = fread(d.data(), 1, d.size(), f);
  fclose(f);
  d.resize(got);
  return d;
}

static bool file_exists(const char *p) {
  FILE *f = fopen(p, "rb"); // shim: POL_fopen
  if (!f)
    return false;
  fclose(f);
  return true;
}

// PORT: modul S3M dla numeru utworu - najpierw ekstrakt (extracted/audio/
// muzyka/GRAF_0xx.s3m), potem surowa kopia danych (bd/GRAF/GRAF.0xx).
static std::string music_path_for(int module) {
  char name[1100];
  const char *ed = POL_ExtractedDir();
  if (ed[0]) {
    snprintf(name, sizeof(name), "%s/audio/muzyka/GRAF_%03d.s3m", ed, module);
    if (file_exists(name))
      return name;
  }
  const char *bd = POL_DataDir();
  if (bd[0]) {
    snprintf(name, sizeof(name), "%s/GRAF/GRAF.%03d", bd, module);
    if (file_exists(name))
      return name;
  }
  return std::string();
}

static std::string basename_of(const char *p) {
  const char *b = strrchr(p, '/');
  b = b ? b + 1 : p;
  return std::string(b);
}

// PORT: rozwiazanie sciezki efektu przeniesione do port_fopen.cpp jako
// POL_ResolveDataFile() - ta sama logika (katalog danych -> ekstrakt ->
// dane pelnej wersji CD), dostepna tez dla testow jednostkowych.
static std::string resolve_sfx_path(const char *dos) {
  const char *r = POL_ResolveDataFile(dos);
  return r ? std::string(r) : std::string();
}

// PORT: dekodowanie WAV -> float mono + cache. SDL3 czyta 8-bit mono 22050
// natywnie (SDL_LoadWAV_IO); konwersja do F32 przez SDL_ConvertAudioSamples
// (dla roznych formatow probek), stereo sprowadzamy srednia kanalow.
static const std::vector<float> *load_sfx(const char *resolved, int *rate) {
  for (auto &e : sfx_cache) {
    if (e.path == resolved) {
      if (rate)
        *rate = e.rate; // PORT: probkowanie z WAV (cache; patrz SfxEntry)
      return &e.pcm;
    }
  }

  std::vector<unsigned char> raw = read_file(resolved);
  if (raw.empty())
    return NULL;

  SDL_AudioSpec src;
  Uint8 *sdata = NULL;
  Uint32 slen = 0;
  SDL_IOStream *io = SDL_IOFromMem(raw.data(), (int)raw.size());
  if (!io)
    return NULL;
  if (!SDL_LoadWAV_IO(io, 1, &src, &sdata, &slen)) {
    fprintf(stderr, "PORT: SDL_LoadWAV_IO '%s': %s\n", resolved,
            SDL_GetError());
    return NULL;
  }

  SDL_AudioSpec dst = src;
  dst.format = SDL_AUDIO_F32;
  dst.channels = src.channels; // konwertujemy tylko format probki
  Uint8 *cdata = NULL;
  int clen = 0;
  if (!SDL_ConvertAudioSamples(&src, sdata, (int)slen, &dst, &cdata, &clen)) {
    SDL_free(sdata);
    fprintf(stderr, "PORT: konwersja WAV '%s': %s\n", resolved, SDL_GetError());
    return NULL;
  }
  SDL_free(sdata);

  std::vector<float> pcm;
  const float *f = (const float *)cdata;
  int n = clen / (int)sizeof(float);
  if (dst.channels == 1) {
    pcm.assign(f, f + n);
  } else {
    pcm.reserve(n / dst.channels);
    for (int i = 0; i + dst.channels <= n; i += dst.channels) {
      float s = 0;
      for (int c = 0; c < dst.channels; c++)
        s += f[i + c];
      pcm.push_back(s / dst.channels);
    }
  }
  SDL_free(cdata);

  // PORT: zapamietujemy tez czestotliwosc probek (resampling glosu w mix_cb)
  sfx_cache.push_back(SfxEntry{resolved, std::move(pcm), src.freq});
  return &sfx_cache.back().pcm;
}

// ------------------------------------------------------------- API ----------
int POL_MixerInit(void) {
  if (strm)
    return 0;
  // PORT: SND.Init (ladowanie) biegnie PRZED inicjacją SDL w main() - bez
  // tego strumien audio nie mowil sie otworzyc ("Audio subsystem is not
  // initialized") i czekal na pierwsze lazy POL_MusicPlay/POL_SfxPlay
  if (!SDL_WasInit(SDL_INIT_AUDIO))
    SDL_InitSubSystem(SDL_INIT_AUDIO);
  if (!mx)
    mx = SDL_CreateMutex();
  SDL_AudioSpec spec;
  spec.format = SDL_AUDIO_F32;
  spec.channels = MIX_CH;
  spec.freq = MIX_FREQ;
  strm = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                   mix_cb, NULL);
  if (!strm) {
    fprintf(stderr, "PORT: audio niedostepne: %s\n", SDL_GetError());
    return 1;
  }
  // PORT: ABI SDL3 zmienione - od 3.4.x SDL_ResumeAudioStreamDevice bierze
  // SDL_AudioStream*, a nie SDL_AudioDeviceID (stare SDL_ResumeAudioStreamDevice(
  // SDL_GetAudioStreamDevice(strm)) wpuszcialo device ID jako pointer ->
  // SIGSEGV w libSDL3 przy pierwszym wlaczeniu muzyki w OPCJACH, bo strumien
  // jest otwierany leniwie dopiero przy POL_MusicPlay/POL_SfxPlay).
  SDL_ResumeAudioStreamDevice(strm);
  // PORT: dispatch SIMD miksera (mix_simd.cpp) - log aktywnej sciezki
  printf("PORT: audio %d Hz F32 stereo (mikser: muzyka S3M/MIDI+sfizz + WAV; "
         "SIMD: %s)\n",
         MIX_FREQ, polmix::cpu_has_avx2() ? "AVX2" : "skalar (brak AVX2)");
  return 0;
}

void POL_MixerShutdown(void) {
  SDL_LockMutex(mx);
  music_active = 0;
  POL_SfzEngineStop(); // PORT: tor MIDI+sfizz (pod mx)
#ifdef POL_HAVE_OPENMPT
  if (mod) {
    openmpt_module_destroy(mod);
    mod = NULL;
  }
#endif
  for (int i = 0; i < MAXVOICES; i++)
    voices[i].active = 0;
  SDL_UnlockMutex(mx);
  if (strm) {
    SDL_DestroyAudioStream(strm);
    strm = NULL;
  }
}

// PORT: tor MIDI+sfizz - wczytanie .mid i przygotowanie syntezatorow VSCO.
// Wywolane bez mutexa (ladowanie VSCO trwa sekundy); pod mutexem tylko
// POL_SfzEngineCommit. Zwraca 1 = utwor wystartowal.
static int play_midi_track(int track, const char *mpath, int loop) {
  std::vector<unsigned char> data = read_file(mpath);
  if (data.empty()) {
    fprintf(stderr, "PORT: nie mozna wczytac '%s'\n", mpath);
    return 0;
  }
  const char *vsco = POL_VscoDir();
  if (!vsco[0]) {
    fprintf(stderr, "PORT: sfizz: nie znaleziono katalogu soundfontow VSCO "
                    "(vsco/ albo $POLANIE_VSCO) - .mid nie wystartuje\n");
    return 0;
  }
  if (POL_SfzEngineInit((float)MIX_FREQ))
    return 0;
  int regions =
      POL_SfzEnginePrepare(data.data(), data.size(), vsco, mpath);
  if (regions < 0)
    return 0;
  SDL_LockMutex(mx);
  POL_SfzEngineSetGain(vol_gain(music_vol)); // wspolna skala glosnosci muzyki
  POL_SfzEngineCommit(loop);
  music_active = 1;
  cur_cd = track;
  SDL_UnlockMutex(mx);
  printf("PORT: muzyka: utwor %d -> %s (sfizz, %d regionow)%s\n", track, mpath,
         regions, loop ? " (loop)" : "");
  return 1;
}

int POL_MusicPlay(int track) {
  if (ensure_init())
    return 0;
  const TrackMap *tm = find_track(track);
  if (!tm)
    return 0;
  // PORT: routing toru (auto/.mid -> sfizz, inaczej S3M). Wymuszony sfz nie
  // ma fallbacku (jawnie brakuje); wymuszony s3m pomija .mid w calosci.
  if (audio_type != POL_AUDIO_S3M) {
    const MidiTrackMap *mm = find_midi_track(track);
    if (mm) {
      char mpath[1100];
      if (POL_MidiFindFile(mm->dir, mm->needle, mpath, sizeof(mpath))) {
        if (play_midi_track(track, mpath, mm->loop))
          return 1;
        if (audio_type == POL_AUDIO_SFZ) {
          fprintf(stderr, "PORT: --audioType=sfz: wczytanie '%s' nie powiodlo "
                          "sie\n",
                  mpath);
          return 0;
        }
        fprintf(stderr, "PORT: tor MIDI nieudany dla utworu %d - fallback S3M "
                        "(GRAF_%03d)\n",
                track, tm->module);
      } else if (audio_type == POL_AUDIO_SFZ) {
        fprintf(stderr, "PORT: --audioType=sfz: brak pliku .mid dla utworu %d "
                        "(podkatalog %s, wzorzec '%s')\n",
                track, mm->dir, mm->needle);
        return 0;
      } else {
        fprintf(stderr, "PORT: brak pliku .mid dla utworu %d (wzorzec '%s' w "
                        "%s) - gra S3M\n",
                track, mm->needle, mm->dir);
      }
    } else if (audio_type == POL_AUDIO_SFZ) {
      fprintf(stderr, "PORT: --audioType=sfz: utwor %d nie ma przypisanego "
                      ".mid (port/midi_map.h)\n",
              track);
      return 0;
    }
  }
#ifdef POL_HAVE_OPENMPT
  std::string path = music_path_for(tm->module);
  if (path.empty()) {
    fprintf(stderr, "PORT: brak modulu S3M dla utworu %d (GRAF_%03d)\n", track,
            tm->module);
    return 0;
  }
  std::vector<unsigned char> data = read_file(path.c_str());
  if (data.empty()) {
    fprintf(stderr, "PORT: nie mozna wczytac '%s'\n", path.c_str());
    return 0;
  }
  openmpt_module *m = openmpt_module_create_from_memory(
      data.data(), data.size(), openmpt_log_func_silent, NULL, NULL);
  if (!m) {
    fprintf(stderr, "PORT: openmpt nie czyta '%s'\n", path.c_str());
    return 0;
  }
  openmpt_module_set_render_param(m, OPENMPT_MODULE_RENDER_MASTERGAIN_MILLIBEL,
                                  vol_millibel(music_vol));
  SDL_LockMutex(mx);
  if (mod)
    openmpt_module_destroy(mod);
  mod = m;
  music_active = 1;
  music_loop = tm->loop;
  cur_cd = track;
  SDL_UnlockMutex(mx);
  printf("PORT: muzyka: utwor %d -> %s%s\n", track, path.c_str(),
         tm->loop ? " (loop)" : "");
  return 1;
#else
  if (!openmpt_missing_logged) {
    openmpt_missing_logged = 1;
    fprintf(stderr, "PORT: libopenmpt brak - muzyka S3M wylaczona\n");
  }
  (void)track;
  return 0;
#endif
}

int POL_MusicStop(void) {
  SDL_LockMutex(mx);
  music_active = 0;
  POL_SfzEngineStop(); // PORT: tor MIDI+sfizz (pod mx)
#ifdef POL_HAVE_OPENMPT
  if (mod) {
    openmpt_module_destroy(mod);
    mod = NULL;
  }
#endif
  SDL_UnlockMutex(mx);
  return 0;
}

int POL_MusicPlaying(void) {
  SDL_LockMutex(mx);
  int r = music_active;
  SDL_UnlockMutex(mx);
  return r;
}

int POL_MusicCurrent(void) { return cur_cd; }

void POL_MusicSetVolume(int v) {
  if (v < 0)
    v = 0;
  if (v > 5)
    v = 5;
  music_vol = v;
  SDL_LockMutex(mx);
  // PORT: tor MIDI+sfizz wspoldzieli skale glosnosci muzyki (0-5)
  POL_SfzEngineSetGain(vol_gain(v));
#ifdef POL_HAVE_OPENMPT
  if (mod)
    openmpt_module_set_render_param(
        mod, OPENMPT_MODULE_RENDER_MASTERGAIN_MILLIBEL, vol_millibel(v));
#endif
  SDL_UnlockMutex(mx);
}

int POL_MusicGetVolume(void) { return music_vol; }

int POL_SfxPlay(const char *path) {
  if (!path || !path[0])
    return 0;
  if (ensure_init())
    return 0;
  std::string resolved = resolve_sfx_path(path);
  if (resolved.empty())
    return 0;
  int wav_rate = 0; // PORT: czestotliwosc probek z WAV (do resamplingu)
  const std::vector<float> *pcm = load_sfx(resolved.c_str(), &wav_rate);
  if (!pcm || pcm->empty())
    return 0;
  SDL_LockMutex(mx);
  Voice &v = voices[voice_rr];
  voice_rr = (voice_rr + 1) % MAXVOICES;
  v.pcm = pcm->data();
  v.len = pcm->size();
  v.pos = 0;
  v.frac = 0.0;
  v.step = (double)(wav_rate > 0 ? wav_rate : 22050) /
           MIX_FREQ; // PORT: resampling do czestotliwosci miksera
  v.active = 1;
  int slot = voice_rr;
  SDL_UnlockMutex(mx);
  // PORT: diagnostyka (POL_MOUSE_DEBUG=1) - start glosu z pliku (efekty W0nn)
  if (getenv("POL_MOUSE_DEBUG"))
    fprintf(stderr, "PORT: SND: glos start '%s' (%zu probek, %d Hz, slot %d)\n",
            resolved.c_str(), pcm->size(), wav_rate, slot);
  return 1;
}

void POL_SfxStopAll(void) {
  SDL_LockMutex(mx);
  for (int i = 0; i < MAXVOICES; i++)
    voices[i].active = 0;
  SDL_UnlockMutex(mx);
}

int POL_SfxPlaying(void) {
  SDL_LockMutex(mx);
  int r = 0;
  for (int i = 0; i < MAXVOICES; i++)
    if (voices[i].active)
      r = 1;
  SDL_UnlockMutex(mx);
  return r;
}

void POL_SfxSetVolume(int v) {
  if (v < 0)
    v = 0;
  if (v > 5)
    v = 5;
  sfx_vol = v;
}

int POL_SfxGetVolume(void) { return sfx_vol; }

// ------------------- globalny zbior probek (SOUND.DAT) ----------------------
// PORT: kontener z pelnej wersji gry ([PCM 8-bit][count x i32 LE na koncu])
// parsuje port/sound_dat.cpp (bez SDL - linkowalny przez testy); tu tylko
// delegacja do niego i zagranie wybranej probki jako glosu miksera.
int POL_SfxLoadGlobalSet(const char *path, int count) {
  return POL_SoundDatLoad(path, count);
}

int POL_SfxGlobalSetCount(void) { return POL_SoundDatCount(); }

int POL_SfxPlayGlobalSet(int index) {
  size_t len = 0;
  const float *pcm = POL_SoundDatSample(index, &len);
  if (!pcm || !len)
    return 0;
  return POL_SfxPlayPCM(pcm, len, 22050);
}

// PORT: glos z pamieci (probki globalne SOUND.DAT, w przyszlosci tez inne
// syntetyczne zrodla) - jak POL_SfxPlay, ale dane daje wywolujacy.
int POL_SfxPlayPCM(const float *pcm, size_t len, int rate) {
  if (!pcm || !len || rate <= 0)
    return 0;
  if (ensure_init())
    return 0;
  SDL_LockMutex(mx);
  Voice &v = voices[voice_rr];
  voice_rr = (voice_rr + 1) % MAXVOICES;
  v.pcm = pcm;
  v.len = len;
  v.pos = 0;
  v.frac = 0.0;
  v.step = (double)rate / MIX_FREQ;
  v.active = 1;
  int slot = voice_rr;
  SDL_UnlockMutex(mx);
  // PORT: diagnostyka (POL_MOUSE_DEBUG=1) - start glosu z pamieci (SOUND.DAT)
  if (getenv("POL_MOUSE_DEBUG"))
    fprintf(stderr, "PORT: SND: glos start (PCM %zu probek, %d Hz, slot %d)\n",
            len, rate, slot);
  return 1;
}

// PORT: diagnostyka/testy - rozwiazanie sciezki efektu bez grania (ta sama
// logika co w POL_SfxPlay). Zwraca statyczny bufor albo NULL.
const char *POL_SfxResolve(const char *dos) { return POL_ResolveDataFile(dos); }

// ------------------------- kompatybilnosc (stare API port.h) ---------------
int POL_AudioInit(void) { return POL_MixerInit(); }
void POL_AudioShutdown(void) { POL_MixerShutdown(); }
int POL_AudioPlayFile(const char *path) { return POL_SfxPlay(path); }
void POL_AudioStop(void) { POL_SfxStopAll(); }
int POL_AudioPlaying(void) { return POL_SfxPlaying(); }