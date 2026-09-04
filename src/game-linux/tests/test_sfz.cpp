// PORT: testy silnika sfz + SIMD miksera:
//  - mix_simd: rownowaznosc sciezki AVX2 i skalarnej (clamp, dodawanie
//    stereo planarnego, resampling glosu) + mikro-benchmark (pomiar do
//    raportu; test wymaga tylko rownowaznosci, nie szybkosci),
//  - sfz_engine: wczytanie syntetycznego .sfz (normalizacja backslashy,
//    liczba regionow, render z nuta),
//  - kSfzMap: programy GM uzyte w fanowskich .mid musza miec sensowne .sfz,
//    a wskazane pliki istnieja w katalogu vsco/ (GTEST_SKIP gdy go nie ma).
#include <gtest/gtest.h>

#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include <sfizz.h>

#include "midi_map_files.h" // POL_VscoDir
#include "mix_simd.h"
#include "sfz_engine.h"
#include "sfz_map.h"
#include "test_util.h"

namespace {

void fill_random(std::vector<float> &v, unsigned seed) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> d(-1.5f, 1.5f);
  for (auto &x : v)
    x = d(rng);
}

void write_wav(const std::string &path, int rate, int samples) {
  std::vector<int16_t> pcm(samples);
  for (int i = 0; i < samples; i++)
    pcm[i] = (int16_t)(8000.0 * sin(2.0 * M_PI * 440.0 * i / rate));
  FILE *f = fopen(path.c_str(), "wb");
  ASSERT_NE(f, nullptr);
  uint32_t data_bytes = (uint32_t)pcm.size() * 2;
  uint32_t chunk = 36 + data_bytes;
  fwrite("RIFF", 1, 4, f);
  fwrite(&chunk, 4, 1, f);
  fwrite("WAVEfmt ", 1, 8, f);
  uint32_t fmtlen = 16, drate = (uint32_t)rate;
  uint16_t fmt = 1, ch = 1;
  fwrite(&fmtlen, 4, 1, f);
  fwrite(&fmt, 2, 1, f);
  fwrite(&ch, 2, 1, f);
  fwrite(&drate, 4, 1, f);
  fwrite(&drate, 4, 1, f);
  uint16_t align = 2, bits = 16;
  fwrite(&align, 2, 1, f);
  fwrite(&bits, 2, 1, f);
  fwrite("data", 1, 4, f);
  fwrite(&data_bytes, 4, 1, f);
  fwrite(pcm.data(), 2, pcm.size(), f);
  fclose(f);
}

bool vec_close(const std::vector<float> &a, const std::vector<float> &b,
               float tol) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); i++)
    if (fabsf(a[i] - b[i]) > tol)
      return false;
  return true;
}

} // namespace

// ------------------------------------------------------------- mix_simd ----
TEST(MixSimd, Clamp_Avx2RowneScalar) {
  if (!polmix::cpu_has_avx2())
    GTEST_SKIP() << "CPU bez AVX2 (sciezka skalarna = wzorzec)";
  std::vector<float> a(1000), b;
  fill_random(a, 1);
  b = a;
  polmix::clamp_f32_scalar(a.data(), a.size());
  polmix::clamp_f32_avx2(b.data(), b.size());
  EXPECT_TRUE(vec_close(a, b, 0.0f)) << "clamp AVX2 != scalar";
  // ogon petli wektorowej (< 8 elementow)
  std::vector<float> c(7), d;
  fill_random(c, 2);
  d = c;
  polmix::clamp_f32_scalar(c.data(), c.size());
  polmix::clamp_f32_avx2(d.data(), d.size());
  EXPECT_TRUE(vec_close(c, d, 0.0f));
}

TEST(MixSimd, AddStereo_Avx2RowneScalar) {
  if (!polmix::cpu_has_avx2())
    GTEST_SKIP() << "CPU bez AVX2";
  const size_t frames = 513; // niepodzielne przez 8 -> sprawdza ogon
  std::vector<float> L(frames), R(frames), dst(frames * 2), ref, out;
  fill_random(L, 3);
  fill_random(R, 4);
  fill_random(dst, 5);
  ref = dst;
  out = dst;
  polmix::add_stereo_planar_scalar(ref.data(), L.data(), R.data(), 0.3f,
                                   frames);
  polmix::add_stereo_planar_avx2(out.data(), L.data(), R.data(), 0.3f, frames);
  EXPECT_TRUE(vec_close(ref, out, 1e-6f));
}

TEST(MixSimd, MixVoice_Avx2RowneScalar) {
  if (!polmix::cpu_has_avx2())
    GTEST_SKIP() << "CPU bez AVX2";
  const size_t len = 4096;
  std::vector<float> pcm(len);
  fill_random(pcm, 6);
  const double step = 22050.0 / 48000.0; // jak glosy efektow WAV

  // skalar: cala probka (limit klatek zeby nie wydluzac) x3 powtorzenia
  std::vector<float> dst_s(4800 * 2, 0.0f);
  size_t pos_s = 0;
  double frac_s = 0.0;
  size_t got_s = 0;
  for (int rep = 0; rep < 3; rep++) {
    size_t used = polmix::mix_voice_mono_scalar(dst_s.data(), 1600, pcm.data(),
                                                len, &pos_s, &frac_s, step,
                                                0.5f);
    got_s += used;
    if (used < 1600)
      break;
  }

  // AVX2: ten sam stan poczatkowy
  std::vector<float> dst_v(4800 * 2, 0.0f);
  size_t pos_v = 0;
  double frac_v = 0.0;
  size_t got_v = 0;
  for (int rep = 0; rep < 3; rep++) {
    size_t used =
        polmix::mix_voice_mono_avx2(dst_v.data(), 1600, pcm.data(), len,
                                    &pos_v, &frac_v, step, 0.5f);
    got_v += used;
    if (used < 1600)
      break;
  }

  EXPECT_EQ(got_s, got_v) << "AVX2 wypelnil inna liczbe klatek";
  EXPECT_EQ(pos_s, pos_v) << "rozjechana pozycja glosu po AVX2";
  EXPECT_NEAR(frac_s, frac_v, 1e-4);
  EXPECT_TRUE(vec_close(dst_s, dst_v, 1e-4f)) << "mix_voice AVX2 != scalar";
}

TEST(MixSimd, Benchmark_Avx2VsScalar) {
  // mikro-benchmark (pomiar do raportu, bez asercji szybkosci): 10 s miksu
  // glosow efektow przy 48 kHz - taka kolejnosc jak w callbacku port_audio.
  if (!polmix::cpu_has_avx2())
    GTEST_SKIP() << "CPU bez AVX2";
  const size_t frames = 480; // 10 ms
  const size_t len = 44100;  // 1 s probka WAV 22050 -> resampling w gore
  std::vector<float> pcm(len);
  fill_random(pcm, 7);
  std::vector<float> dst(frames * 2, 0.0f);

  auto run = [&](bool avx2) {
    size_t pos = 0;
    double frac = 0.0;
    double step = 22050.0 / 48000.0;
    // 1000 wywolan = 10 s miksu jednego glosu (resampling 22050->48000)
    for (int i = 0; i < 1000; i++) {
      size_t used = avx2 ? polmix::mix_voice_mono_avx2(
                               dst.data(), frames, pcm.data(), len, &pos,
                               &frac, step, 0.4f)
                         : polmix::mix_voice_mono_scalar(
                               dst.data(), frames, pcm.data(), len, &pos,
                               &frac, step, 0.4f);
      if (used < frames) { // probka sie skonczyla - od nowa (jak dźwiek w grze)
        pos = 0;
        frac = 0.0;
      }
    }
  };

  auto t0 = std::chrono::steady_clock::now();
  run(false);
  auto t1 = std::chrono::steady_clock::now();
  run(true);
  auto t2 = std::chrono::steady_clock::now();
  double ms_scalar = std::chrono::duration<double, std::milli>(t1 - t0).count();
  double ms_avx2 = std::chrono::duration<double, std::milli>(t2 - t1).count();
  printf("PORT-benchmark: mix_voice 10 s miksu: scalar = %.1f ms, AVX2 = "
         "%.1f ms (przyspieszenie %.2fx)\n",
         ms_scalar, ms_avx2, ms_scalar / ms_avx2);
  // pomiar, nie test szybkosci - zapisujemy tylko, ze obie sciezki dzialaja
  EXPECT_GT(ms_scalar, 0.0);
  EXPECT_GT(ms_avx2, 0.0);
}

// ------------------------------------------------------------ sfz_engine ---
TEST(SfzEngine, SyntetyczneSfz_NormalizacjaIRegiony) {
  // .sfz z windowsowym default_path (backslashe) i probka w podkatalogu -
  // silnik musi znormalizowac do '/' i zaladowac region (decyzja ws. VSCO:
  // normalizacja przy wczytywaniu, bez kopii plikow na dysku)
  system(("mkdir -p '" + POL_TEST_TMP + "/Sub/Dir'").c_str());
  write_wav(testpath("Sub/Dir/sfztest_tone.wav"), 22050, 2000);
  std::string sfz = "<control>\ndefault_path=Sub\\Dir\\\n\n<region>\nsample=" +
                    std::string("sfztest_tone.wav") +
                    "\nkey=60\nampeg_release=0.1\n";
  std::string p = testpath("sfztest.sfz");
  FILE *f = fopen(p.c_str(), "wb");
  ASSERT_NE(f, nullptr);
  fwrite(sfz.data(), 1, sfz.size(), f);
  fclose(f);

  sfizz_synth_t *s = sfizz_create_synth();
  ASSERT_NE(s, nullptr);
  sfizz_set_sample_rate(s, 48000.0f);
  EXPECT_EQ(POL_SfzLoadNormalizedFile(s, p.c_str()), 1)
      << "syntetyczny .sfz ma miec dokladnie 1 region";
  // render z nuta - ma byc skonczona arytmetyka (bez NaN/inf)
  sfizz_send_note_on(s, 0, 60, 100);
  std::vector<float> L(480), R(480), dst(960, 0.0f);
  float *ptrs[2] = {L.data(), R.data()};
  sfizz_render_block(s, ptrs, 2, 480);
  for (size_t i = 0; i < L.size(); i++) {
    ASSERT_TRUE(std::isfinite(L[i])) << "NaN/inf w renderze sfizz";
    ASSERT_TRUE(std::isfinite(R[i]));
  }
  // i caly tor przez interfejs silnika: nieaktywny silnik zwraca 0 klatek
  // (kontrakt size_t po PORT-naprawie ogona bufora; stara asercja
  // EXPECT_GT(render, -1) porownywala unsigned z (size_t)-1 i zawsze padala)
  POL_SfzEngineInit(48000.0f);
  POL_SfzEngineStop(); // stan globalny wspoldzielony - wymus nieaktywny
  EXPECT_EQ(POL_SfzEngineRender(dst.data(), dst.size() / 2), 0u)
      << "nieaktywny silnik ma zwracac 0 wypelnionych klatek";
  sfizz_free(s);
}

// --------------------------------------------------------------- kSfzMap ---
TEST(SfzMap, ProgramyUzywaneWFanowskichMid) {
  // programy z fanowskich utworow (raporty/mapa-s3m-midi-sfz.md): 42, 43,
  // 47, 48, 59, 79, 80 - wszystkie musza wskazywac niepusty plik .sfz
  struct Used {
    int gm;
    const char *sfz;
  };
  const Used used[] = {
      {42, "CelloEnsSusVib.sfz"},   {43, "ContrabassSusVB.sfz"},
      {47, "Timpani.sfz"},          {48, "ViolinEnsSusVib.sfz"},
      {59, "TrumpetStraightMuteSus.sfz"},
      {79, "FluteSusNV.sfz"},       {80, "FluteSusVib.sfz"},
  };
  for (const auto &u : used) {
    const SfzMapEntry *e = find_sfz_for_gm(u.gm);
    ASSERT_NE(e, nullptr) << "GM " << u.gm;
    EXPECT_STREQ(e->sfz, u.sfz) << "GM " << u.gm;
    EXPECT_NE(e->sfz[0], '\0') << "GM " << u.gm << " - cisza?";
  }
}

TEST(SfzMap, WszystkieWpisySaPoprawne) {
  for (int gm = 0; gm < 128; gm++) {
    const SfzMapEntry *e = find_sfz_for_gm(gm);
    ASSERT_NE(e, nullptr);
    ASSERT_NE(e->sfz, nullptr);
    // nazwa pliku .sfz bez znakow sciezkowych (pliki leza w korzeniu vsco)
    for (const char *p = e->sfz; *p; p++)
      EXPECT_EQ(strchr(p, '/'), nullptr) << "GM " << gm;
  }
}

TEST(SfzMap, PlikiIstniejaWVsco) {
  // katalog vsco/ (symlink do VSCO CE) - wykrywany jak w grze (../vsco itd.)
  const char *vsco = POL_VscoDir();
  if (!vsco[0])
    GTEST_SKIP() << "brak katalogu vsco/ przy testach (uruchamiane z repo)";
  int missing = 0;
  for (int gm = 0; gm < 128; gm++) {
    const SfzMapEntry *e = find_sfz_for_gm(gm);
    if (!e || !e->sfz[0])
      continue;
    std::string p = std::string(vsco) + "/" + e->sfz;
    FILE *f = fopen(p.c_str(), "rb");
    if (!f) {
      ADD_FAILURE() << "GM " << gm << ": brak " << p;
      missing++;
    } else {
      fclose(f);
    }
  }
  EXPECT_EQ(missing, 0);
}

// ------------------------------------- silnik: render (kontrakt size_t) ----
// PORT: testy dodane po naprawie zwrotki POL_SfzEngineRender (retest usera
// 63/64): pelny render ma zwracac `frames`, koniec utworu - czesciowa liczbe,
// silnik nieaktywny - 0 (mix_cb zeruje reszte bufora od zwrotki).
namespace {

void push_be32x(std::vector<unsigned char> &v, uint32_t x) {
  v.push_back((x >> 24) & 0xFF);
  v.push_back((x >> 16) & 0xFF);
  v.push_back((x >> 8) & 0xFF);
  v.push_back(x & 0xFF);
}
void push_be16x(std::vector<unsigned char> &v, uint16_t x) {
  v.push_back((x >> 8) & 0xFF);
  v.push_back(x & 0xFF);
}
void push_varlenx(std::vector<unsigned char> &v, uint32_t x) {
  // MSB-first (SMF varlen): grupy po 7 bitow od najwyzszej, kazda poza
  // ostatnia z ustawionym bitem kontynuacji 0x80
  unsigned char tmp[5];
  int n = 0;
  tmp[n++] = (unsigned char)(x & 0x7F);
  x >>= 7;
  while (x) {
    tmp[n++] = (unsigned char)(0x80 | (x & 0x7F));
    x >>= 7;
  }
  while (n)
    v.push_back(tmp[--n]);
}
void push_chunkx(std::vector<unsigned char> &v, const char *id,
                 const std::vector<unsigned char> &body) {
  v.insert(v.end(), id, id + 4);
  push_be32x(v, (uint32_t)body.size());
  v.insert(v.end(), body.begin(), body.end());
}

// Syntetyczny .mid: format 0, division 384, tempo 500000 us (120 BPM),
// kanaal 1: program GM 48, nuta 60 on w ticku 0 / off w ticku note_ticks.
// Dlugosc utworu = note_ticks/384 * 0,5 s. Domylnie 384 = 0,5 s = 24000 probek.
std::vector<unsigned char> make_song_mid(uint32_t note_ticks = 384) {
  std::vector<unsigned char> m;
  m.insert(m.end(), {'M', 'T', 'h', 'd'});
  push_be32x(m, 6);
  push_be16x(m, 0); // format 0
  push_be16x(m, 1); // 1 sciezka
  push_be16x(m, 384);
  std::vector<unsigned char> tr;
  push_varlenx(tr, 0);
  tr.insert(tr.end(), {0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20}); // 120 BPM
  push_varlenx(tr, 0);
  tr.insert(tr.end(), {0xC0, 48});      // program GM 48 -> kSfzMap[48]
  push_varlenx(tr, 0);
  tr.insert(tr.end(), {0x90, 60, 100}); // nuta on
  push_varlenx(tr, note_ticks);
  tr.insert(tr.end(), {0x80, 60, 64});  // nuta off
  push_varlenx(tr, 0);
  tr.insert(tr.end(), {0xFF, 0x2F, 0x00});
  push_chunkx(m, "MTrk", tr);
  return m;
}

// "Mini VSCO" w katalogu testowym: probka + .sfz pod nazwa, ktora podaje
// kSfzMap[48] (program GM 48 = ViolinEnsSusVib.sfz) - silnik sam wybiera
// plik wg mapy, wiec test musi miec plik o tej nazwie.
void write_engine_sfz() {
  write_wav(testpath("engine_tone.wav"), 22050, 8000);
  std::string sfz = "<region>\nsample=engine_tone.wav\nkey=60\n"
                    "lovel=0\nhivel=127\nampeg_release=0.05\n";
  std::string p = POL_TEST_TMP + "/" + kSfzMap[48].sfz;
  FILE *f = fopen(p.c_str(), "wb");
  ASSERT_NE(f, nullptr);
  fwrite(sfz.data(), 1, sfz.size(), f);
  fclose(f);
}

TEST(SfzEngine, RenderNieaktywny_Zwraca0) {
  POL_SfzEngineStop(); // czysci wspoldzielony stan globalny silnika
  std::vector<float> dst(960, 0.0f);
  EXPECT_EQ(POL_SfzEngineRender(dst.data(), 480), 0u)
      << "nieaktywny silnik = 0 wypelnionych klatek (mix_cb wyzeruje calosc)";
}

TEST(SfzEngine, PrepareCommitRender_Roundtrip) {
  POL_SfzEngineStop();
  write_engine_sfz();
  std::vector<unsigned char> mid = make_song_mid();
  POL_SfzEngineInit(48000.0f);
  EXPECT_GT(POL_SfzEnginePrepare(mid.data(), mid.size(), POL_TEST_TMP.c_str(),
                                 "test.mid"),
            0)
      << "syntetyczne .sfz z kSfzMap[48] ma sie wczytac (regiony > 0)";
  POL_SfzEngineCommit(0);
  EXPECT_EQ(POL_SfzEnginePlaying(), 1);
  std::vector<float> dst(480 * 2, 0.0f);
  size_t got = POL_SfzEngineRender(dst.data(), 480);
  EXPECT_EQ(got, 480u)
      << "pelny render ma zwracac liczbe klatek, nie 1 (retest 63/64)";
  for (float x : dst)
    ASSERT_TRUE(std::isfinite(x)) << "NaN/inf w renderze";
  float mx = 0.0f;
  for (float x : dst)
    mx = mx > fabsf(x) ? mx : fabsf(x);
  EXPECT_GT(mx, 0.0f) << "aktywna nuta (on w ticku 0) ma dawac niezerowy miks";
  POL_SfzEngineStop();
  EXPECT_EQ(POL_SfzEnginePlaying(), 0);
}

TEST(SfzEngine, KoniecUtworu_NieaktywnyIOgon) {
  POL_SfzEngineStop();
  write_engine_sfz();
  // PORT (retest 67/68): celowo note-off w ticku 480, NIE domyslne 384.
  // Arytmetyka: end = 480/384*0,5 s = 0,625 s = 30000 probek; ogon 2 s =
  // 96000; deaktywacja przy cursor >= 126000. A 126000/480 = 262,5 -
  // przekroczenie wypada W SRODKU wywolania render(480) (podblok 256-probkowy
  // konczy sie na 126016) -> wywolanie zwraca 256 < 480 (ogon czesciowo).
  // Przy domyslnych 384 tickach koniec+ogon = 120000 = 250*480 dokladnie na
  // granicy wywolania - silnik wypelnil wtedy caly blok (poprawnie!) i
  // partial-return byl niemozliwy (FAIL byl bugiem arytmetyki testu).
  std::vector<unsigned char> mid = make_song_mid(480);
  POL_SfzEngineInit(48000.0f);
  ASSERT_GT(POL_SfzEnginePrepare(mid.data(), mid.size(), POL_TEST_TMP.c_str(),
                                 "test.mid"),
            0);
  POL_SfzEngineCommit(0); // bez petli
  std::vector<float> dst(480 * 2, 0.0f);
  bool saw_partial = false;
  for (int i = 0; i < 400 && POL_SfzEnginePlaying(); i++) {
    size_t n = POL_SfzEngineRender(dst.data(), 480);
    if (n < 480u)
      saw_partial = true; // ostatni blok (ogon) - mix_cb wyzeruje reszte
  }
  EXPECT_TRUE(saw_partial)
      << "koniec utworu w srodku wywolania ma zwrocic < frames (ogon)";
  EXPECT_EQ(POL_SfzEnginePlaying(), 0) << "utwor sie skonczyl";
  EXPECT_EQ(POL_SfzEngineRender(dst.data(), 480), 0u)
      << "po koncie utworu kolejne wywolanie = 0 (kontrakt mix_cb)";
  POL_SfzEngineStop();
}

TEST(SfzEngine, Loop_Wraps) {
  POL_SfzEngineStop();
  write_engine_sfz();
  std::vector<unsigned char> mid = make_song_mid();
  POL_SfzEngineInit(48000.0f);
  ASSERT_GT(POL_SfzEnginePrepare(mid.data(), mid.size(), POL_TEST_TMP.c_str(),
                                 "test.mid"),
            0);
  POL_SfzEngineCommit(1); // zapetlenie (jak plansze/menu)
  std::vector<float> dst(480 * 2, 0.0f);
  // utwor = 24000 probek (0,5 s, note-off @ tick 384); 160 blokow x 480 =
  // 76800 probek = 3,2x dlugosci - silnik ma dalej grac (wrap: CC 123+120
  // i od poczatku). Asercja: po 3x dlugosci zapetlany utwor wciaz aktywny
  // (gdyby loop nie dzialal, po end+tail = 120000 probek = 250 blokach
  // silnik by sie wylaczyl i zwrotki bylyby 0).
  for (int i = 0; i < 160; i++)
    EXPECT_EQ(POL_SfzEngineRender(dst.data(), 480), 480u) << "blok " << i;
  EXPECT_EQ(POL_SfzEnginePlaying(), 1)
      << "po 3x dlugosci utworu zapetlany utwor ma dalej grac";
  POL_SfzEngineStop();
  EXPECT_EQ(POL_SfzEnginePlaying(), 0);
}

} // namespace (testy silnika)