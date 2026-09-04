// PORT: gorące pętle miksera (port_audio.cpp) wydzielone do osobnego pliku,
// żeby (a) można je było testować w gtest bez SDL i bez urządzenia audio,
// (b) ścieżkę AVX2 kompilować przez __attribute__((target("avx2"))) bez
// włączania -mavx2 dla całego projektu (binarka musi działać też na CPU
// bez AVX2 - dispatch runtime __builtin_cpu_supports("avx2")).
//
// Rozkład odpowiedzialności:
//   - polmix::cpu_has_avx2()     - jednorazowy dispatch (logowane raz).
//   - polmix::clamp_f32()        - clamp sumy muzyka+efekty do [-1,1].
//   - polmix::add_stereo_planar()  - dodanie planarnego stereo (L,R) * gain
//                                  do bufora interleaved F32 (wyjście sfizz).
//   - polmix::mix_voice_mono()   - resampling liniowy głosu mono (efekty WAV)
//                                  + dodanie do interleaved stereo.
// Kazda funkcja ma wariant *_scalar() - wzorzec dla testow rownowaznosci
// AVX2<->scalar i fallback dla CPU bez AVX2.
#ifndef POL_MIX_SIMD_H
#define POL_MIX_SIMD_H

#include <cstddef>

namespace polmix {

// 1 gdy CPU wspiera AVX2 (wynik zapamietywany; pierwszy wywolanie dispatchuje).
int cpu_has_avx2(void);

// --- clamp -----------------------------------------------------------------
void clamp_f32(float *v, size_t n);
void clamp_f32_scalar(float *v, size_t n);
void clamp_f32_avx2(float *v, size_t n); // wywolywac tylko gdy cpu_has_avx2()

// --- dst[i] += srcL[i]*g; dst interleaved stereo (dst[2i], dst[2i+1]) ------
void add_stereo_planar(float *dst, const float *l, const float *r, float g,
                       size_t frames);
void add_stereo_planar_scalar(float *dst, const float *l, const float *r,
                              float g, size_t frames);
void add_stereo_planar_avx2(float *dst, const float *l, const float *r,
                            float g, size_t frames);

// --- glos mono -> interleaved stereo z resamplingiem liniowym --------------
// Stan głosu (pos, frac) przekazywany przez wskaźnik (mikser go trzyma).
// Zwraca liczbe wypelnionych klatek wyjsciowych (mniejsza od frames, gdy
// probka sie skonczyla; wtedy *pos ustawiane na koniec). Glos dodawany
// jest do dst z wzmocnieniem g (miks addytywny, jak w port_audio.cpp).
size_t mix_voice_mono(float *dst, size_t frames, const float *pcm,
                      size_t len, size_t *pos, double *frac, double step,
                      float g);
size_t mix_voice_mono_scalar(float *dst, size_t frames, const float *pcm,
                             size_t len, size_t *pos, double *frac,
                             double step, float g);
size_t mix_voice_mono_avx2(float *dst, size_t frames, const float *pcm,
                           size_t len, size_t *pos, double *frac, double step,
                           float g); // tylko przy cpu_has_avx2()

} // namespace polmix

#endif // POL_MIX_SIMD_H