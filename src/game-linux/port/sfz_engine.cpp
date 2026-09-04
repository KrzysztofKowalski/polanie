// PORT: implementacja sfz_engine.h - patrz komentarze w naglowku.
// Silnik: jeden sfizz na kanal MIDI, ladowany .sfz wg kSfzMap (sfz_map.h),
// zdarzenia z parsera polmidi::parse_smf rozsylane w podblokach 256 probek
// (delay-ordered, jak wymaga API sfizz), render do bufora miksera.
#include "sfz_engine.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include <sfizz.h>

#include "midi_parser.h"
#include "mix_simd.h"
#include "sfz_map.h"

// Relacja glosnosci MIDI vs S3M (PORT-DECYZJA): syntezatory VSCO CE graja
// wyraznie glosniej niz rendering OPL libopenmpt; kazdy kanal sciszamy o
// 6 dB, a finalne wzmocnienie toru ustawia POL_SfzEngineSetGain z
// port_audio.cpp (wspoldzieli skale glosnosci muzyki 0-5). Stale do strojenia
// na sluch - patrz raport.
static const float kSfzSynthVolumeDb = -6.0f;

namespace {

struct SfzEv {
  int64_t sample; // czas w probkach 48 kHz
  uint8_t type;
  uint8_t channel;
  uint8_t a;
  uint8_t b;
  int pitch;
};

struct Engine {
  float rate = 48000.0f;
  bool initialized = false;
  bool active = false;
  int loop = 0;
  sfizz_synth_t *chan[16] = {};
  int chan_used[16] = {};    // 1 = syntezator zaladowany
  int chan_program[16] = {}; // zapamietany program (do logu zmiany w RT)
  std::vector<SfzEv> evs;
  size_t ev_idx = 0;
  int64_t cursor = 0;    // pozycja odtwarzania w probkach
  int64_t end = 0;       // ostatnie zdarzenie
  int64_t tail = 0;      // domkniecie ogonow po ostatnim zdarzeniu
  float gain = 1.0f;
  std::vector<float> sl, sr; // scratch render sfizz (tylko watk audio)
  std::string name;
  int prog_mismatch_logged = 0;
  // stan oczekujacy (Prepare -> Commit); callback go nie dotyka
  sfizz_synth_t *pend_chan[16] = {};
  int pend_used[16] = {};
  int pend_program[16] = {};
  std::vector<SfzEv> pend_evs;
  int64_t pend_end = 0;
  std::string pend_name;
  int pend_regions = 0;

  void free_chans(sfizz_synth_t **chans) {
    for (int c = 0; c < 16; c++) {
      if (chans[c]) {
        sfizz_free(chans[c]);
        chans[c] = NULL;
      }
    }
  }
  void reset_chans() {
    free_chans(chan);
    for (int c = 0; c < 16; c++) {
      chan_used[c] = 0;
      chan_program[c] = 0;
    }
  }
};

Engine g;

// normalizacja tekstu .sfz: '\\' -> '/' (VSCO ma default_path/sample po
// windowsowemu); reszta bez zmian. Wywolujemy sfizz_load_string z wirtualna
// sciezka w katalogu vsco, wiec sciezki wzgledne rozwiąza sie poprawnie.
std::string normalize_sfz(const std::string &in) {
  std::string out = in;
  for (auto &c : out)
    if (c == '\\')
      c = '/';
  return out;
}

int load_channel(sfizz_synth_t *s, const char *vsco_dir, const char *sfz_name,
                 const char *label) {
  std::string path = std::string(vsco_dir) + "/" + sfz_name;
  FILE *f = fopen(path.c_str(), "rb");
  if (!f) {
    fprintf(stderr, "PORT: sfizz: brak pliku '%s'\n", path.c_str());
    return -1;
  }
  std::string text;
  char buf[65536];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
    text.append(buf, n);
  fclose(f);
  // sciezka absolutna pliku dziala jako "wirtualna sciezka" sfizz - wzgledne
  // default_path/sample (juz z '/' po normalizacji) rozwiaza sie wzgledem vsco/
  if (!sfizz_load_string(s, path.c_str(), normalize_sfz(text).c_str())) {
    fprintf(stderr, "PORT: sfizz: nie mozna wczytac '%s' (%s)\n", path.c_str(),
            label);
    return -1;
  }
  return sfizz_get_num_regions(s);
}

} // namespace

extern "C" {

int POL_SfzEngineInit(float rate) {
  if (g.initialized && g.rate == rate)
    return 0;
  g.reset_chans();
  g.rate = rate;
  g.initialized = true;
  return 0;
}

void POL_SfzEngineShutdown(void) {
  g.active = false;
  g.evs.clear();
  g.reset_chans();
  g.free_chans(g.pend_chan);
  g.pend_evs.clear();
  g.initialized = false;
}

int POL_SfzLoadNormalizedFile(void *synth, const char *path) {
  sfizz_synth_t *s = (sfizz_synth_t *)synth;
  FILE *f = fopen(path, "rb");
  if (!f)
    return -1;
  std::string text;
  char buf[65536];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
    text.append(buf, n);
  fclose(f);
  if (!sfizz_load_string(s, path, normalize_sfz(text).c_str()))
    return -1;
  return sfizz_get_num_regions(s);
}

int POL_SfzEnginePrepare(const unsigned char *mid, size_t len,
                         const char *vsco_dir, const char *name) {
  polmidi::MidiSong song;
  if (!polmidi::parse_smf(mid, len, song)) {
    fprintf(stderr, "PORT: sfizz: blad parsowania '%s': %s\n", name,
            song.error.c_str());
    return -1;
  }
  if (song.events.empty()) {
    fprintf(stderr, "PORT: sfizz: '%s' nie ma zdarzen\n", name);
    return -1;
  }

  g.free_chans(g.pend_chan);
  for (int c = 0; c < 16; c++) {
    g.pend_used[c] = 0;
    g.pend_program[c] = 0;
  }
  g.pend_evs.clear();
  g.pend_evs.reserve(song.events.size());
  double spb = (double)g.rate;
  int64_t max_sample = 0;
  for (const auto &e : song.events) {
    SfzEv x;
    x.sample = (int64_t)llround(e.seconds * spb);
    x.type = e.type;
    x.channel = e.channel;
    x.a = e.a;
    x.b = e.b;
    x.pitch = e.pitch;
    if (x.sample > max_sample)
      max_sample = x.sample;
    g.pend_evs.push_back(x);
  }

  // program kazdego kanalu (pierwszy program change; fanowskie pliki maja
  // jeden na kanal). Kanal 10 = perkusja GM -> GM-StylePerc.sfz.
  int program[16];
  for (int c = 0; c < 16; c++)
    program[c] = -1;
  for (const auto &e : song.events) {
    if (e.type == polmidi::MIDIEV_PROGRAM && program[e.channel] < 0)
      program[e.channel] = e.a;
  }

  int total_regions = 0;
  int loaded_chans = 0;
  for (int c = 0; c < 16; c++) {
    bool perc = c == 9;
    bool has_notes = false;
    for (const auto &e : song.events) {
      if (e.channel == c && (e.type == polmidi::MIDIEV_NOTE_ON ||
                             e.type == polmidi::MIDIEV_NOTE_OFF)) {
        has_notes = true;
        break;
      }
    }
    if (!has_notes && program[c] < 0)
      continue; // kanal nieuzywany - bez syntezatora (CPU)
    const char *sfz_name = perc ? SFZ_PERC_FILE : NULL;
    if (!perc) {
      int prog = program[c] >= 0 ? program[c] : 0; // brak PC -> program 0
      const SfzMapEntry *me = find_sfz_for_gm(prog);
      if (!me || !me->sfz[0]) {
        fprintf(stderr, "PORT: sfizz: kanal %d: program GM %d - brak .sfz "
                        "(cisza)\n",
                c + 1, prog);
        continue;
      }
      sfz_name = me->sfz;
    }
    sfizz_synth_t *s = sfizz_create_synth();
    sfizz_set_sample_rate(s, g.rate);
    sfizz_set_samples_per_block(s, 1024);
    sfizz_set_preload_size(s, 262144); // cache probek (floaty) - VSCO 3,1 GB
    sfizz_set_num_voices(s, 24);
    sfizz_set_volume(s, kSfzSynthVolumeDb);
    int regions = load_channel(s, vsco_dir, sfz_name, name);
    if (regions < 0) {
      sfizz_free(s);
      continue;
    }
    g.pend_chan[c] = s;
    g.pend_used[c] = 1;
    g.pend_program[c] = program[c] >= 0 ? program[c] : 0;
    total_regions += regions;
    loaded_chans++;
    printf("PORT: sfizz: kanal %d -> %s (program GM %d, %d regionow)\n", c + 1,
           sfz_name, g.pend_program[c], regions);
  }
  if (!loaded_chans) {
    fprintf(stderr, "PORT: sfizz: '%s' - nie zaladowano zadnego kanalu\n",
            name);
    g.pend_evs.clear();
    return -1;
  }
  g.pend_end = max_sample;
  g.pend_name = name ? name : "";
  g.pend_regions = total_regions;
  (void)song.ticks_per_qn; // tempo juz przeliczone w parserze
  return total_regions;
}

void POL_SfzEngineCommit(int loop) {
  g.reset_chans(); // zwalnia stary utwor (caller trzyma mutex miksera)
  for (int c = 0; c < 16; c++) {
    g.chan[c] = g.pend_chan[c];
    g.chan_used[c] = g.pend_used[c];
    g.chan_program[c] = g.pend_program[c];
    g.pend_chan[c] = NULL;
    g.pend_used[c] = 0;
  }
  g.evs.swap(g.pend_evs);
  g.pend_evs.clear();
  g.end = g.pend_end;
  g.name = g.pend_name;
  g.loop = loop ? 1 : 0;
  g.cursor = 0;
  g.ev_idx = 0;
  g.tail = (int64_t)(2.0 * g.rate); // domkniecie ogonow (release) 2 s
  g.prog_mismatch_logged = 0;
  g.active = 1;
}

void POL_SfzEngineAbort(void) {
  g.free_chans(g.pend_chan);
  for (int c = 0; c < 16; c++)
    g.pend_used[c] = 0;
  g.pend_evs.clear();
}

void POL_SfzEngineStop(void) {
  g.active = 0;
  g.evs.clear();
  g.reset_chans();
}

int POL_SfzEnginePlaying(void) { return g.active; }

const char *POL_SfzEngineCurrentName(void) { return g.name.c_str(); }

void POL_SfzEngineSetGain(float gr) { g.gain = gr; }

static void dispatch(SfzEv &e, int delay) {
  sfizz_synth_t *s = g.chan[e.channel];
  if (!s)
    return;
  switch (e.type) {
  case polmidi::MIDIEV_NOTE_ON:
    sfizz_send_note_on(s, delay, e.a, e.b);
    break;
  case polmidi::MIDIEV_NOTE_OFF:
    sfizz_send_note_off(s, delay, e.a, e.b ? e.b : 64);
    break;
  case polmidi::MIDIEV_CC:
    // sfizz sam interpretuje 7 (volume), 11 (expression), 64 (sustain),
    // 120 (all sound off), 121 (reset controllers), 123 (all notes off)
    sfizz_send_cc(s, delay, e.a, e.b);
    break;
  case polmidi::MIDIEV_PITCH_BEND:
    sfizz_send_pitch_wheel(s, delay, e.pitch);
    break;
  case polmidi::MIDIEV_PROGRAM:
    // ladowanie .sfz nie jest RT-safe - programy ladujemy przed startem
    // (fanowskie pliki maja jeden program change na kanal, na ticku 0).
    if (e.a != g.chan_program[e.channel] && !g.prog_mismatch_logged) {
      g.prog_mismatch_logged = 1;
      fprintf(stderr, "PORT: sfizz: zmiana programu w trakcie utworu (kanal "
                      "%d, GM %d) - ignorowana (RT)\n",
              e.channel + 1, e.a);
    }
    break;
  }
}

size_t POL_SfzEngineRender(float *out, size_t frames) {
  if (!g.active || g.evs.empty())
    return 0;
  size_t done = 0;
  const size_t kSub = 256; // podblok: delay-ordered zdarzenia + render
  while (done < frames) {
    size_t n = frames - done;
    if (n > kSub)
      n = kSub;
    int64_t t0 = g.cursor;
    // zdarzenia nalezace do podbloku [t0, t0+n)
    while (g.ev_idx < g.evs.size() && g.evs[g.ev_idx].sample < t0 + (int64_t)n) {
      SfzEv &e = g.evs[g.ev_idx];
      int delay = (int)(e.sample - t0);
      if (delay < 0)
        delay = 0;
      if (delay >= (int)n)
        delay = (int)n - 1;
      dispatch(e, delay);
      g.ev_idx++;
    }
    // render kanalow i dodanie do miksu (planar -> interleaved)
    float *L, *R;
    if (g.sl.size() < n)
      g.sl.resize(n);
    if (g.sr.size() < n)
      g.sr.resize(n);
    L = g.sl.data();
    R = g.sr.data();
    for (int c = 0; c < 16; c++) {
      if (!g.chan_used[c] || !g.chan[c])
        continue;
      float *ptrs[2] = {L, R};
      sfizz_render_block(g.chan[c], ptrs, 2, (int)n);
      polmix::add_stereo_planar(out + done * 2, L, R, g.gain, n);
    }
    done += n;
    g.cursor += (int64_t)n;
    if (g.cursor >= g.end) {
      if (g.loop) {
        // ucisz wiszace nuty i od poczatku (jak zapetlanie plansz S3M)
        for (int c = 0; c < 16; c++) {
          if (!g.chan_used[c] || !g.chan[c])
            continue;
          sfizz_send_cc(g.chan[c], 0, 123, 0); // all notes off
          sfizz_send_cc(g.chan[c], 0, 120, 0); // all sound off
        }
        g.cursor = 0;
        g.ev_idx = 0;
      } else if (g.cursor >= g.end + g.tail) {
        g.active = 0; // utwor wygral sie do konca (z ogonem)
        return done;  // PORT-naprawa: reszta bufora NIE jest nasza - caller
                      // wyzeruje (mix_cb robi memset od `done`)
      }
    }
  }
  // PORT-naprawa (2026-09-03, retest usera 63/64): pelny render = wszystkie
  // klatki wypelnione; zwrotka 1 wypelnila by tylko klatke 0, a mix_cb
  // zeruje reszte bufora (memset od `done`) -> muzyka MIDI cichnie co blok.
  return done;
}

} // extern "C"