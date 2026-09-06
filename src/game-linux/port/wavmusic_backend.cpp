// PORT: implementacja GENERYCZNEGO BACKENDU MUZYKI WAV (audio_backend.h) —
// zamiennik czytania PCM z CD-DA (oryginał grał z płyty; u nas gotowe pliki
// WAV z exportu) ORAZ trybu MT32/MIDI przez FluidSynth (soundfont
// MT-32 Hedsound, gdy nie ma WAV). Wzorowana na sfz_engine.cpp (ta sama
// dwufazowa architektura Prepare/Commit/Abort), ale z waznym rozniacym
// założeniem:
//
//   PELNY PRE-RENDER W PREPARE(). FluidSynth NIE dziala w callbacku audio:
//   caly utwor (SMF -> eventy -> synteza przez FLUID_Synth -> F32 stereo)
//   jest wyliczany offline w POL_MusicPlay (jak render CLI przez
//   "fluidsynth -a file") i zapisany w buforze pcm. Callback tylko kopiuje
//   probki do miksu (z zapetleniem). Dzieki temu w watku audio nie ma: JIT
//   ladowania probek SF2, ciezkiego moksu 16-kanalowego synth, ani dysku -
//   czysta kopia = zero artefaktow RT (klasyka „stutter" i „pops" przy
//   kreceniu synth bezposrednio w callbacku).
//
// Konsekwencje:
//   - RAM: bufor pre-render ~23 MB/min (48 kHz stereo float); plus SF2
//     (~0,6-1 GB przy sfload) - patrz wavmusic_backend.h;
//   - lagiem przy zmianie utworu (prerender trwa sekundy przy dlugim utworze),
//     co jest i tak tuz po sfload (10+ s);
//   - brzmienie w grze = brzmienie CLI (zweryfikowane na sluch: 11,8 MB WAV).
//
// Kluczowy fragment: GM REMAPA (gm2preset[128]) z pliku *GM.txt obok SF2,
// nakladana w wysylce zdarzen (program GM -> preset banku 0 wg tabeli autora
// SF2, bo bank 0 ma porzadek MT-32, nie GM).
#include "wavmusic_backend.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h> // atoi
#include <string.h>
#include <string>
#include <vector>

#include "midi_parser.h"
#include "mix_simd.h"

#ifdef POL_HAVE_MT32
#include <fluidsynth.h>
#define POL_MT32_FLUID 1
#else
#define POL_MT32_FLUID 0
#endif

namespace {
// limit czasu pre-renderu (1 utwor): 300 s = ~230 MB bufora F32 stereo.
// Wyzej - ucinamy z ostrzezeniem (utwory Polan sa krotkie).
const int64_t kMaxPrerenderSec = 300;

// zdarzenie MIDI (to samo co SfzEv w sfz_engine.cpp; kanal 0-15, a/b =
// note/vel albo cc/val, pitch = pitch bend 0-16383)
struct MtEv {
  int64_t sample; // czas w probkach miksera
  uint8_t type;
  uint8_t channel;
  uint8_t a;
  uint8_t b;
  int pitch;
};
} // namespace

struct WavMusicBackend::Impl {
  float rate = 48000.0f;
#if POL_MT32_FLUID
  fluid_settings_t *settings = NULL;
  fluid_synth_t *syn = NULL;
#endif
  bool sf2Loaded = false; // sfload wykonane i OK (raz; bez powtorek)
  bool active = false;
  int loop = 0;
  int64_t cursor = 0;     // pozycja odtwarzania w probkach bufora
  int64_t loopEnd = 0;    // dlugosc jednego przejscia (bez ogona; zapetlenie)
  int64_t pcmFrames = 0;  // dlugosc bufora z ogonem (koniec utworu)
  float gain = 1.0f;
  int gm2preset[128]; // GM program -> preset banku 0 (wg GM.txt; fallback id)
  int chanProgram[16];
  bool chanBank0[16];
  std::vector<float> pcm;      // pre-render: F32 interleaved stereo
  std::vector<float> sl, sr;   // scratch offline synth (tylko prepare)
  std::string name;
  bool missingLogged = false;

  void resetChans() {
    for (int c = 0; c < 16; c++) {
      chanProgram[c] = -1;
      chanBank0[c] = false;
    }
  }

  // jednorazowe utworzenie synth (offline; zadnych driverow audio nie
  // podpinamy - dzieki temu nie ma konfliktu z mikserem SDL)
  bool ensureSynth(float ratev) {
#if POL_MT32_FLUID
    if (syn)
      return true;
    settings = new_fluid_settings();
    if (!settings) {
      fprintf(stderr, "PORT: mt32: new_fluid_settings() nieudane\n");
      return false;
    }
    fluid_settings_setnum(settings, "synth.sample-rate", ratev);
    fluid_settings_setnum(settings, "synth.polyphony", 128);
    // Wbudowane efekty MT-32: hardware Rolandu MIAL tylko REVERB (19 modów,
    // ~2-4 s, ciemny); chorus to dopiero SC-55. Próbki SF2 nagrane "dry"
    // (konieczne do soundfontow), wiec mokrej przestrzeni u nas NIE MA.
    // FluidSynth ma WŁASNY reverb/chorus (nie kopie Rolanda), ale przy
    // subtelnych wartosciach daje zbliżony "wet". Domyslnie OFF (czysty
    // synth — potwierdzone brzmienie); POLANIE_MT32FX=reverb|chorus|both
    // wlacza (te same ustawienia subtelne co --fx w skryptach audio).
    fluid_settings_setnum(settings, "synth.chorus.active", 0);
    fluid_settings_setnum(settings, "synth.reverb.active", 0);
    {
      const char *fx = getenv("POLANIE_MT32FX");
      if (fx && (strcmp(fx, "reverb") == 0 || strcmp(fx, "both") == 0)) {
        fluid_settings_setnum(settings, "synth.reverb.active", 1);
        fluid_settings_setnum(settings, "synth.reverb.roomsize", 0.35);
        fluid_settings_setnum(settings, "synth.reverb.damp", 0.45);
        fluid_settings_setnum(settings, "synth.reverb.level", 0.35);
        fluid_settings_setnum(settings, "synth.reverb.width", 0.5);
      }
      if (fx && (strcmp(fx, "chorus") == 0 || strcmp(fx, "both") == 0))
        fluid_settings_setnum(settings, "synth.chorus.active", 1);
    }
    syn = new_fluid_synth(settings);
    if (!syn) {
      fprintf(stderr, "PORT: mt32: new_fluid_synth() nieudane\n");
      return false;
    }
    // domyslny gain FluidSynth to 0.2; podnosimy do 0.5 (-6 dB jak
    // kSfzSynthVolumeDb przy torze sfizz, dokumentowane w sfz_engine.cpp)
    fluid_synth_set_gain(syn, 0.5f);
    return true;
#else
    (void)ratev;
    return false;
#endif
  }

  // jednorazowe ladowanie SF2 (447 MiB - trwa kilkanaście sekund;
  // Prepare pelni sie poza mutexem miksera, jak ladowanie VSCO w sfizz)
  bool ensureSf2(const char *sf2path) {
#if POL_MT32_FLUID
    if (sf2Loaded)
      return true;
    if (!syn)
      return false;
    int pid = fluid_synth_sfload(syn, sf2path, 1);
    if (pid < 0) { // FLUID_FAILED = -1
      fprintf(stderr, "PORT: mt32: nie mozna wczytac soundfontu '%s'\n",
              sf2path);
      return false;
    }
    sf2Loaded = true;
    loadGmMap(sf2path);
    return true;
#else
    (void)sf2path;
    return false;
#endif
  }

  // --- tryb WAV („zamiast MIDI WAV leci jako muzyka"): gotowy pre-render
  // z exportu (GRAF_NNN.mt32.wav, PCM16/24, mono/stereo) -> bufor F32
  // interleaved @ mix rate (resampling liniowy, jak glosy efektow) — bez
  // ladowania SF2/fluid i bez czekania na synteze.
  bool loadWavF32(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
      return false;
    std::vector<uint8_t> d;
    uint8_t b[65536];
    size_t n;
    while ((n = fread(b, 1, sizeof(b), f)) > 0)
      d.insert(d.end(), b, b + n);
    fclose(f);
    // diagnostyka: kazde odrzucenie pliku podaje konkrretny powod (log usera
    // nie ma zgadywac, co odrzucilo WAV); zmienne DEKLAROWANE PRZED lambda
    // (w C++ lambda nie widzi nazw zadeklarowanych ponizej w tym samym bloku)
    int fmt = 0, ch = 0, srcRate = 0, bits = 0;
    size_t dataOff = 0, dataLen = 0;
    auto fail = [&](const char *why) {
      fprintf(stderr, "PORT: mt32: WAV '%s': ODRZUCONY - %s (razem %zu B, fmt %d, "
                      "ch %d, rate %d, bits %d, dataOff %zu, dataLen %zu)\n",
              path, why, d.size(), fmt, ch, srcRate, bits, dataOff, dataLen);
      return false;
    };
    if (d.size() < 44 || memcmp(d.data(), "RIFF", 4) != 0)
      return fail("nie RIFF/za maly plik");
    size_t i = 12;
    while (i + 8 <= d.size()) {
      uint32_t sz = d[i + 4] | (d[i + 5] << 8) | (d[i + 6] << 16) |
                    (d[i + 7] << 24);
      if (memcmp(d.data() + i, "fmt ", 4) == 0 && i + 8 + sz <= d.size()) {
        fmt = d[i + 8] | (d[i + 9] << 8);
        ch = d[i + 10] | (d[i + 11] << 8);
        srcRate = (int)((uint32_t)d[i + 12] | ((uint32_t)d[i + 13] << 8) |
                        ((uint32_t)d[i + 14] << 16) |
                        ((uint32_t)d[i + 15] << 24));
        // pola fmt od i+8: format(2) ch(2) rate(4) byterate(4) blockAlign(2)
        // BITS(2) => bits przy i+8+14 = i+22 (wczesniej i+18 czytało
        // blockAlign -> "bits 2/4" zamiast 16/24 -> kazdy WAV odrzucany)
        bits = d[i + 22] | (d[i + 23] << 8);
      } else if (memcmp(d.data() + i, "data", 4) == 0) {
        dataOff = i + 8;
        dataLen = sz;
        break;
      }
      i += 8 + sz + (sz & 1);
    }
    if (fmt != 1 || (ch != 1 && ch != 2) || (bits != 16 && bits != 24) ||
        srcRate <= 0 || dataOff + dataLen > d.size())
      return fail("nieprawidlowy format/kontener (PCM16/24 mono|stereo, rate>0)");
    const int bytesPer = bits / 8;
    const int64_t srcFrames =
        (int64_t)(dataLen / (size_t)bytesPer / (size_t)ch);
    if (srcFrames < 1)
      return fail("brak danych audio");
    // resampling liniowy do mix rate (jak glosy efektow w port_audio)
    const double step = (double)srcRate / (double)this->rate;
    const int64_t dstFrames = (int64_t)((double)srcFrames / step);
    if (dstFrames < 1)
      return fail("brak klatek po resamplingu");
    pcm.assign((size_t)dstFrames * 2, 0.0f);
    auto at = [&](int64_t fr, int c) -> float {
      if (fr < 0)
        fr = 0;
      if (fr >= srcFrames)
        fr = srcFrames - 1;
      const uint8_t *q = d.data() + dataOff +
                         (size_t)fr * (size_t)bytesPer * (size_t)ch +
                         (size_t)c * (size_t)bytesPer;
      if (bits == 16) {
        int32_t v = q[0] | (q[1] << 8);
        if (v >= 32768)
          v -= 65536;
        return v / 32768.0f;
      }
      int32_t v = q[0] | (q[1] << 8) | (q[2] << 16);
      if (v >= 8388608)
        v -= 16777216;
      return v / 8388608.0f;
    };
    for (int64_t fr = 0; fr < dstFrames; fr++) {
      double pos = (double)fr * step;
      int64_t i0 = (int64_t)pos;
      float t = (float)(pos - (double)i0);
      float l = at(i0, 0) * (1.0f - t) + at(i0 + 1, 0) * t;
      float r = (ch == 2) ? (at(i0, 1) * (1.0f - t) + at(i0 + 1, 1) * t) : l;
      pcm[(size_t)fr * 2] = l;
      pcm[(size_t)fr * 2 + 1] = r;
    }
    pcmFrames = (int64_t)pcm.size() / 2;
    // export konczy sie ~2 s ogona (tail); zapetlenie skacze przed ogon
    loopEnd = pcmFrames - (int64_t)(2.0 * (double)this->rate);
    if (loopEnd < 1)
      loopEnd = pcmFrames;
    return pcmFrames > 0;
  }


  // --- remapa GM: '000:054 = Cello 1 -> 042' -> gm2preset[042] = 054 ---
  void loadGmMap(const char *sf2path) {
#if POL_MT32_FLUID
    for (int i = 0; i < 128; i++)
      gm2preset[i] = i; // fallback: identycznosc gdy pliku brak
    std::string t(sf2path);
    size_t dot = t.rfind('.');
    if (dot != std::string::npos)
      t.erase(dot);
    t += ".txt";
    FILE *f = fopen(t.c_str(), "r");
    if (!f) {
      printf("PORT: mt32: brak '%s' - remapa GM nieaktywna (identycznosc)\n",
             t.c_str());
      return;
    }
    char ln[192];
    int mapped = 0;
    while (fgets(ln, sizeof(ln), f)) {
      // format: '000:054 = Cello 1 -> 042'; lewa strona = bank:preset SF2,
      // prawa = odpowiadajacy program GM (mapa autora sf2)
      char *eq = strchr(ln, '=');
      char *ar = strstr(ln, "->");
      if (!eq || !ar)
        continue;
      int bank = -1, preset = -1;
      if (sscanf(ln, "%d:%d", &bank, &preset) != 2)
        continue;
      int gm = atoi(ar + 2);
      if (gm < 0 || gm > 127)
        continue;
      if (bank != 0 || preset < 0 || preset > 127)
        continue;
      gm2preset[gm] = preset;
      mapped++;
    }
    fclose(f);
    printf("PORT: mt32: remapa GM wczytana (%d programow, '%s')\n", mapped,
           t.c_str());
#else
    (void)sf2path;
#endif
  }

};

// ---------------------------------------------------------------------------
// interfejs (audio_backend.h) - przy braku libfluidsynth cala klasa jest
// stubem: available()=0 i prepare()=-2, reszta bezpieczna
// ---------------------------------------------------------------------------

WavMusicBackend::WavMusicBackend() : imp_(new Impl()) {}

WavMusicBackend::~WavMusicBackend() {
#if POL_MT32_FLUID
  if (imp_->syn)
    delete_fluid_synth(imp_->syn);
  if (imp_->settings)
    delete_fluid_settings(imp_->settings);
#endif
}

int WavMusicBackend::available() const {
#if POL_MT32_FLUID
  return 1;
#else
  return 0;
#endif
}

int WavMusicBackend::init(float rate) {
  imp_->rate = rate;
  return 0; // ciezkie kroki (synth+sfload) czekaja na pierwszy prepare()
}

void WavMusicBackend::dispatch(int type, int channel, int a, int b, int pitch,
                           int) {
#if POL_MT32_FLUID
  fluid_synth_t *s = imp_->syn;
  if (!s)
    return;
  switch (type) {
  case polmidi::MIDIEV_NOTE_ON:
    fluid_synth_noteon(s, channel, a, b);
    break;
  case polmidi::MIDIEV_NOTE_OFF:
    fluid_synth_noteoff(s, channel, a);
    break;
  case polmidi::MIDIEV_CC:
    // fluid sam interpretuje 7/11/64/120/121/123 (volume/expression/sustain
    // /all sound off/reset controllers/all notes off)
    fluid_synth_cc(s, channel, a, b);
    break;
  case polmidi::MIDIEV_PITCH_BEND:
    fluid_synth_pitch_bend(s, channel, pitch);
    break;
  case polmidi::MIDIEV_PROGRAM:
    if (channel == 9) {
      // perkusja GM: fluid sam przestawia na bank 128 (synth.drums-channel);
      // nie przepuszczamy remapy (presety perkusyjne tam sa)
      if (imp_->chanProgram[channel] != a) {
        fluid_synth_program_change(s, channel, a & 127);
        imp_->chanProgram[channel] = a;
      }
      break;
    }
    if (imp_->chanProgram[channel] == (a & 127) && imp_->chanBank0[channel])
      break;
    // GM REMAPA: program GM z pliku -> preset banku 0 wg GM.txt (identycznosc
    // w fallbacku). Fanowskie MIDI: GM 42/43/47/48/59/79/80; identycznosc
    // brzmi dobrze tylko dla 48 (bank 0 SF2 ma porzadek MT-32).
    {
      int gm = a & 127;
      int preset = imp_->gm2preset[gm];
      if (!imp_->chanBank0[channel]) {
        fluid_synth_bank_select(s, channel, 0);
        imp_->chanBank0[channel] = true;
      }
      fluid_synth_program_change(s, channel, preset);
      imp_->chanProgram[channel] = gm;
    }
    break;
  }
#else
  (void)a;
  (void)b;
  (void)pitch;
#endif
}

int WavMusicBackend::prepare(const unsigned char *mid, size_t len,
                         const char *sf2_path, const char *name) {
  if (!available()) {
    if (!imp_->missingLogged) {
      imp_->missingLogged = true;
      fprintf(stderr, "PORT: mt32: niedostepne (brak libfluidsynth w buildzie) "
                      "- wczytanie nie wystartuje\n");
    }
    return -2;
  }
  // tryb WAV — „zamiast MIDI WAV leci jako muzyka": bank_path konczy sie na
  // ".wav" (POLANIE_MT32_WAVDIR + GRAF_NNN.mt32.wav, gotowy pre-render z
  // exportu) → wczytujemy WAV do bufora pcm; zero ladowania SF2/fluid i
  // zero czekania na synteze (szybki start, jak torrent gotowych plikow).
  {
    size_t bl = strlen(sf2_path ? sf2_path : "");
    if (bl > 4 && strcmp(sf2_path + bl - 4, ".wav") == 0) {
      if (!imp_->loadWavF32(sf2_path)) {
        fprintf(stderr, "PORT: mt32: nie mozna wczytac WAV '%s'\n", sf2_path);
        return -1;
      }
      imp_->name = name ? name : "";
      printf("PORT: mt32: WAV %s (%.2f s, %zu probek) - tryb gotowych plikow\n",
             sf2_path, double(imp_->pcmFrames) / (double)imp_->rate,
             (size_t)imp_->pcmFrames);
      return 1;
    }
  }
  if (!imp_->ensureSynth(imp_->rate))
    return -1;
  if (!imp_->ensureSf2(sf2_path))
    return -1;

  polmidi::MidiSong song;
  if (!polmidi::parse_smf(mid, len, song)) {
    fprintf(stderr, "PORT: mt32: blad parsowania '%s': %s\n",
            name ? name : "", song.error.c_str());
    return -1;
  }
  if (song.events.empty()) {
    fprintf(stderr, "PORT: mt32: '%s' nie ma zdarzen\n", name ? name : "");
    return -1;
  }

  // (1) eventy -> probki (szybkie odtworzenie w offline renderze nizej)
  std::vector<MtEv> evs;
  evs.reserve(song.events.size());
  double spb = (double)imp_->rate;
  int64_t max_sample = 0;
  for (const auto &e : song.events) {
    MtEv x;
    x.sample = (int64_t)llround(e.seconds * spb);
    x.type = e.type;
    x.channel = e.channel;
    x.a = e.a;
    x.b = e.b;
    x.pitch = e.pitch;
    if (x.sample > max_sample)
      max_sample = x.sample;
    evs.push_back(x);
  }
  const int64_t tailS = (int64_t)(2.0 * imp_->rate); // domkniecie ogonow 2 s
  int64_t total = max_sample + tailS;
  if (total > kMaxPrerenderSec * (int64_t)imp_->rate) {
    total = kMaxPrerenderSec * (int64_t)imp_->rate;
    fprintf(stderr, "PORT: mt32: utwor dluższy niz %d s - pre-render uciety "
                    "(%lld s)\n", int(kMaxPrerenderSec),
            (long long)(total / (int64_t)imp_->rate));
  }
  if (imp_->rate <= 0.0f)
    return -1;

  // (2) OFFLINE pre-render calego utworu do bufora pcm (bez watku audio)
  imp_->active = 0;
  imp_->pcm.clear();
  imp_->pcm.resize((size_t)total * 2, 0.0f);
  imp_->sl.resize(256);
  imp_->sr.resize(256);
#if POL_MT32_FLUID
  fluid_synth_system_reset(imp_->syn);
#endif
  imp_->resetChans();
  auto fsWrite = [&](size_t pos, size_t cnt) {
#if POL_MT32_FLUID
    if (!cnt || imp_->pcm.empty() || pos >= (size_t)total)
      return;
    if (cnt > 256)
      cnt = 256; // write_float w kawałkach (scratch 256)
    fluid_synth_write_float(imp_->syn, (int)cnt, imp_->sl.data(), 0, 1,
                            imp_->sr.data(), 0, 1);
    float *dst = imp_->pcm.data() + pos * 2;
    for (size_t i = 0; i < cnt; i++) {
      dst[i * 2] = imp_->sl[i];
      dst[i * 2 + 1] = imp_->sr[i];
    }
#else
    (void)pos;
    (void)cnt;
#endif
  };
  size_t cur = 0, idx = 0;
  size_t nEv = evs.size();
  while (idx < nEv && evs[idx].sample < total) {
    size_t at = (size_t)evs[idx].sample;
    if (at > cur) {
      for (size_t p = cur; p < at; p += 256)
        fsWrite(p, at - p);
      cur = at;
    }
    // zdarzenia w tej samej probce: wysylka przed metrologia - odtwarzane
    // w kolejnosci (program -> CC -> nuta), jak w pliku SMF
    while (idx < nEv && (size_t)evs[idx].sample == at) {
      MtEv &e = evs[idx];
      dispatch(e.type, e.channel, e.a, e.b, e.pitch, 0);
      idx++;
    }
  }
  while (cur < (size_t)total) {
    size_t n = (size_t)total - cur;
    if (n > 256)
      n = 256;
    fsWrite(cur, n);
    cur += n;
  }

  // (3) instalujemy stan oczekujacy (old track gra dalej; commit pod mx)
  imp_->name = name ? name : "";
  imp_->loopEnd = max_sample; // zapetlenie: wrap po danych (bez ogona)
  imp_->pcmFrames = (int64_t)imp_->pcm.size() / 2;
  if (imp_->pcmFrames <= 0)
    return -1;

  // ile kanalow obsadzonych (do logu; 0 = nic nie zagra - blad)
  bool has[16] = {};
  for (const auto &e : song.events)
    if (e.channel >= 0 && e.channel < 16)
      has[e.channel] = true;
  int chans = 0;
  for (int c = 0; c < 16; c++)
    if (has[c])
      chans++;
  if (!chans) {
    fprintf(stderr, "PORT: mt32: '%s' nie ma nut na zadnym kanale\n",
            name ? name : "");
    return -1;
  }
  printf("PORT: mt32: pre-render %s: %.2f s (%zu zdarzen, %d kanalow) "
         "-> %zu probek stereo\n",
         name ? name : "", double(total) / imp_->rate, nEv, chans,
         (size_t)imp_->pcmFrames);
  return chans;
}

void WavMusicBackend::commit(int loop) {
  imp_->cursor = 0;
  imp_->loop = loop ? 1 : 0;
  imp_->active = imp_->pcmFrames > 0 ? 1 : 0;
}

void WavMusicBackend::abortPrepare() { imp_->pcm.clear(); imp_->pcmFrames = 0; }

void WavMusicBackend::stop() {
  imp_->active = 0;
  imp_->cursor = 0;
  imp_->pcm.clear();
  imp_->pcmFrames = 0;
}

int WavMusicBackend::playing() const { return imp_->active; }

const char *WavMusicBackend::currentName() const { return imp_->name.c_str(); }

void WavMusicBackend::setGain(float g) { imp_->gain = g; }

size_t WavMusicBackend::render(float *out, size_t frames) {
  if (!imp_->active)
    return 0;
  // callack audio: tylko kopia z bufora pre-renderu + wzmocnienie toru
  // (gain rozbicza miksera jak przy torze sfizz); zero synth/JIT w RT.
  size_t done = 0;
  while (done < frames) {
    if (imp_->cursor >= imp_->pcmFrames) {
      if (!imp_->loop)
        break; // koniec utworu (z ogonem); caller wyzeruje reszte
      imp_->cursor = 0;
    }
    // zapetlenie skacze do poczatku (jak sfz), nie ogarnia ogona
    if (imp_->loop && imp_->cursor >= imp_->loopEnd && imp_->loopEnd > 0)
      imp_->cursor = 0;
    size_t avail = (size_t)(imp_->pcmFrames - imp_->cursor);
    if (avail > frames - done)
      avail = frames - done;
    const float *src = imp_->pcm.data() + (size_t)imp_->cursor * 2;
    float *dst = out + done * 2;
    float g = imp_->gain;
    for (size_t i = 0; i < avail * 2; i++)
      dst[i] += src[i] * g;
    imp_->cursor += (int64_t)avail;
    done += avail;
  }
  if (imp_->cursor >= imp_->pcmFrames && !imp_->loop)
    imp_->active = 0; // wysylka kolejnych blokow zwroci 0 (jak sfz)
  return done;
}
