// PORT: implementacja mix_simd.h - patrz komentarze w naglowku.
// Sciezka AVX2 przez __attribute__((target("avx2"))): kompilator generuje
// instrukcje AVX2 tylko w tych funkcjach, reszta TU zostaje czysta SSE2,
// wiec modul (i cala gra) uruchamia sie na CPU bez AVX2 (dispatch runtime).
#include "mix_simd.h"

#include <immintrin.h>

namespace polmix {

int cpu_has_avx2(void) {
  static int cached = -1;
  if (cached < 0)
    cached = __builtin_cpu_supports("avx2") ? 1 : 0;
  return cached;
}

// ------------------------------------------------------------- clamp ------
void clamp_f32_scalar(float *v, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (v[i] > 1.0f)
      v[i] = 1.0f;
    else if (v[i] < -1.0f)
      v[i] = -1.0f;
  }
}

__attribute__((target("avx2"))) void clamp_f32_avx2(float *v, size_t n) {
  const __m256 lo = _mm256_set1_ps(-1.0f), hi = _mm256_set1_ps(1.0f);
  size_t i = 0;
  for (; i + 8 <= n; i += 8) {
    __m256 x = _mm256_loadu_ps(v + i);
    x = _mm256_max_ps(_mm256_min_ps(x, hi), lo);
    _mm256_storeu_ps(v + i, x);
  }
  for (; i < n; i++)
    clamp_f32_scalar(v + i, 1);
}

void clamp_f32(float *v, size_t n) {
  if (cpu_has_avx2())
    clamp_f32_avx2(v, n);
  else
    clamp_f32_scalar(v, n);
}

// ------------------------------------------------- add_stereo_planar ------
// dst interleaved: dst[2i]=L[i]*g, dst[2i+1]=R[i]*g (miks addytywny).
void add_stereo_planar_scalar(float *dst, const float *l, const float *r,
                              float g, size_t frames) {
  for (size_t i = 0; i < frames; i++) {
    dst[i * 2] += l[i] * g;
    dst[i * 2 + 1] += r[i] * g;
  }
}

// Interleave 8 probek L/R do 2 wektorow (kolejnosc pamieci dst):
//   lo = unpacklo(l,r) = [l0,r0,l1,r1 | l4,r4,l5,r5]
//   hi = unpackhi(l,r) = [l2,r2,l3,r3 | l6,r6,l7,r7]
//   v0 = permute2f128(lo,hi,0x20) = [l0,r0,l1,r1,l2,r2,l3,r3]
//   v1 = permute2f128(lo,hi,0x31) = [l4,r4,l5,r5,l6,r6,l7,r7]
__attribute__((target("avx2"))) static inline void interleave8(__m256 l,
                                                               __m256 r,
                                                               __m256 *v0,
                                                               __m256 *v1) {
  __m256 lo = _mm256_unpacklo_ps(l, r);
  __m256 hi = _mm256_unpackhi_ps(l, r);
  *v0 = _mm256_permute2f128_ps(lo, hi, 0x20);
  *v1 = _mm256_permute2f128_ps(lo, hi, 0x31);
}

__attribute__((target("avx2"))) void add_stereo_planar_avx2(
    float *dst, const float *l, const float *r, float g, size_t frames) {
  const __m256 vg = _mm256_set1_ps(g);
  size_t i = 0;
  for (; i + 8 <= frames; i += 8) {
    __m256 vl = _mm256_mul_ps(_mm256_loadu_ps(l + i), vg);
    __m256 vr = _mm256_mul_ps(_mm256_loadu_ps(r + i), vg);
    __m256 v0, v1;
    interleave8(vl, vr, &v0, &v1);
    _mm256_storeu_ps(dst + i * 2,
                     _mm256_add_ps(_mm256_loadu_ps(dst + i * 2), v0));
    _mm256_storeu_ps(dst + i * 2 + 8,
                     _mm256_add_ps(_mm256_loadu_ps(dst + i * 2 + 8), v1));
  }
  for (; i < frames; i++) {
    dst[i * 2] += l[i] * g;
    dst[i * 2 + 1] += r[i] * g;
  }
}

void add_stereo_planar(float *dst, const float *l, const float *r, float g,
                       size_t frames) {
  if (cpu_has_avx2())
    add_stereo_planar_avx2(dst, l, r, g, frames);
  else
    add_stereo_planar_scalar(dst, l, r, g, frames);
}

// ----------------------------------------------------- mix_voice_mono -----
// Zwrotka: ile klatek wyjscia wypelniono (reszta = probka skonczona).
// Stan wyjsciowy *pos/*frac zaktualizowany tak, by kolejne wywolanie
// kontynuowalo probke (identyczna umowa jak stara petla w port_audio.cpp).
size_t mix_voice_mono_scalar(float *dst, size_t frames, const float *pcm,
                             size_t len, size_t *pos, double *frac,
                             double step, float g) {
  size_t pos_l = *pos;
  double frac_l = *frac;
  for (size_t k = 0; k < frames; k++) {
    size_t idx = pos_l;
    if (idx + 1 >= len) { // koniec probki (idx = ostatni bez pary)
      *pos = pos_l;
      *frac = frac_l;
      return k;
    }
    float s = (float)(pcm[idx] * (1.0 - frac_l) + pcm[idx + 1] * frac_l);
    dst[k * 2] += s * g;
    dst[k * 2 + 1] += s * g;
    frac_l += step;
    pos_l += (size_t)frac_l;
    frac_l -= (size_t)frac_l;
  }
  *pos = pos_l;
  *frac = frac_l;
  return frames;
}

// Krok wektorowy: 8 klatek na raz. Indeks probki kazdej warstwi:
//   idx_j = pos + floor(f0 + j*step), waga = frac(f0 + j*step)
// (f0 w [0,1), step < 1 -> identyczny wynik jak petla skalarnej: suma
// pojedynczych przekroczen 1.0 = floor(f0 + 8*step) - floor(f0), a floor(f0)=0).
// Przed kazdym krokiem sprawdzamy najgorszy indeks; gdy probka sie konczy,
// reszte dopelnia petla skalarna.
__attribute__((target("avx2"))) size_t mix_voice_mono_avx2(
    float *dst, size_t frames, const float *pcm, size_t len, size_t *pos,
    double *frac, double step, float g) {
  const __m256 vg = _mm256_set1_ps(g);
  const __m256 vi = _mm256_setr_ps(0, 1, 2, 3, 4, 5, 6, 7);
  size_t k = 0;
  size_t pos_l = *pos;
  double frac_l = *frac;

  while (k + 8 <= frames) {
    double last = frac_l + 7.0 * step;
    size_t maxidx = pos_l + (size_t)last;
    if (maxidx + 1 >= len) // probka skonczy sie w tym bloku -> skalar reszta
      break;
    __m256 vf = _mm256_add_ps(_mm256_set1_ps((float)frac_l),
                              _mm256_mul_ps(vi, _mm256_set1_ps((float)step)));
    __m256 ifr = _mm256_floor_ps(vf);
    __m256 f = _mm256_sub_ps(vf, ifr);
    __m256i idx = _mm256_add_epi32(
        _mm256_cvtps_epi32(ifr), _mm256_set1_epi32((int)pos_l));
    __m256 s0 = _mm256_i32gather_ps(pcm, idx, 4);
    __m256 s1 = _mm256_i32gather_ps(pcm, _mm256_add_epi32(
                                           idx, _mm256_set1_epi32(1)), 4);
    // lerp bez FMA (dispatch sprawdzamy tylko po AVX2; FMA nic tu nie daje)
    __m256 s = _mm256_add_ps(
        s0, _mm256_mul_ps(_mm256_sub_ps(s1, s0), f));
    s = _mm256_mul_ps(s, vg);
    __m256 v0, v1;
    interleave8(s, s, &v0, &v1); // mono -> oba kanaly
    _mm256_storeu_ps(dst + k * 2,
                     _mm256_add_ps(_mm256_loadu_ps(dst + k * 2), v0));
    _mm256_storeu_ps(dst + k * 2 + 8,
                     _mm256_add_ps(_mm256_loadu_ps(dst + k * 2 + 8), v1));
    double nf = frac_l + 8.0 * step;
    double cross = (double)(size_t)nf; // floor dla nf >= 0
    pos_l += (size_t)cross;
    frac_l = nf - cross;
    k += 8;
  }
  // resztka (albo calosc przy braku AVX2-dlugiego biegu) skalarnie
  size_t done = mix_voice_mono_scalar(dst + k * 2, frames - k, pcm, len, &pos_l,
                                      &frac_l, step, g);
  *pos = pos_l;
  *frac = frac_l;
  return k + done;
}

size_t mix_voice_mono(float *dst, size_t frames, const float *pcm, size_t len,
                      size_t *pos, double *frac, double step, float g) {
  if (cpu_has_avx2())
    return mix_voice_mono_avx2(dst, frames, pcm, len, pos, frac, step, g);
  return mix_voice_mono_scalar(dst, frames, pcm, len, pos, frac, step, g);
}

} // namespace polmix