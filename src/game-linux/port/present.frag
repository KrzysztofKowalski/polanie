#version 450
// PORT: shader fragmentow backendu SDL3 GPU (port_gpu.cpp) - prezentacja
// ramki 320x200 gry. Dwa tryby w jednym shaderze, przelacznik w uniformie:
//   u_crt.x < 0.5  -> tryb domyslny: nearest + integer scaling
//                     (pikselo-perfect, odpowiednik renderera SDL3
//                     z SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
//   u_crt.x >= 0.5 -> tryb CRT "a'la crt" (POL_CRT=1): scanlines co 2
//                     wiersze rastra, delikatna krzywizna (barrel), winieta.
// Wzorowany na "Lightweight CRT Effect" (godotshaders.com; ta sama technika
// w testach SDL: test/testgpurender_effects_CRT.frag).
//
// WARIANT v2 (palette-exact): tekstura ramki gry to surowe INDEKSY palety
// (R8_UNORM, 320x200), a paleta -> kolor to osobna tekstura LUT 256x1
// (B8G8R8A8_UNORM) odswiezana tylko przy POL_SetPalette. Kolor piksela
// czytamy texelFetch (bez samplera/interpolacji): indeks -> wpis LUT.
// Zaden piksel okna nie powstaje przez mieszanie kolorow - kazdy jest
// D O K L A D N I E jednym z 256 wpisow palety DAC gry. (Konwersja CPU
// LUT co klatke znika; upload ramki to 64 KB zamiast 256 KB.)
layout(location = 0) in vec2 vUv;

layout(location = 0) out vec4 outColor;

// set 2 = samplery fragmentu (konwencja SDL_GPU dla SPIR-V)
layout(set = 2, binding = 0) uniform sampler2D u_texIdx; // indeksy 320x200 R8
layout(set = 2, binding = 1) uniform sampler2D u_texLut; // 256x1 BGRA paleta

// set 3 = uniformy fragmentu (32 B, push co klatke z port_gpu.cpp)
layout(set = 3, binding = 0) uniform u_cfg {
  vec4 u_rect; // x,y,w,h = prostokat gry w pikselach okna (wycentrowany
               // integer scale; poza nim letterbox)
  vec4 u_crt;  // x: 0=zwykly tryb, 1=CRT; y: sila krzywizny;
               // z: sila scanlines; w: sila winiiety
};

// kolor gry w pikselu okna pp (wspolrzedne wewnatrz prostokata gry):
// teksel ramki = floor(pp / skala), kolor = LUT[teksel] - zero interpolacji.
vec4 gameColor(vec2 pp) {
  vec2 scale = u_rect.zw / vec2(320.0, 200.0); // calkowite (integer scale)
  ivec2 g = ivec2(pp / scale);                 // floor -> teksel ramki gry
  g = clamp(g, ivec2(0), ivec2(319, 199));
  float idxf = texelFetch(u_texIdx, g, 0).r * 255.0 + 0.5; // R8 -> 0..255
  int li = clamp(int(idxf), 0, 255);
  return texelFetch(u_texLut, ivec2(li, 0), 0);
}

void main() {
  vec2 p = gl_FragCoord.xy - u_rect.xy; // piksele wewnatrz prostokata gry
  vec4 col = vec4(0.0);                 // poza obszarem gry: czarny (letterbox)

  if (p.x >= 0.0 && p.y >= 0.0 && p.x < u_rect.z && p.y < u_rect.w) {
    if (u_crt.x < 0.5) {
      // ---- tryb domyslny: pikselo-perfect (nearest, skala calkowita) ----
      // floor(pp/scale) daje dokladnie jeden teksel gry na piksel okna.
      col = gameColor(p);
    } else {
      // ---- tryb CRT ----
      // 1) krzywizna: srodek prostokata gry w pikselach okna
      vec2 c = u_rect.zw * 0.5;
      vec2 d = (p - c) / c; // -1..1
      float r2 = dot(d, d);
      vec2 wp = d * (1.0 + u_crt.y * r2) * c + c; // barrel
      if (wp.x >= 0.0 && wp.y >= 0.0 && wp.x < u_rect.z &&
          wp.y < u_rect.w) {
        col = gameColor(wp);
        // 2) scanlines: co 2 wiersze fizycznego rastra okna (sin(pi*(k+0.5))
        //    daje +/-1 na zmiane); u_crt.z=0 wylacza
        float scan = 0.5 - 0.5 * sin(3.14159265 * p.y);
        col.rgb *= 1.0 - u_crt.z * scan;
        // 3) winieta (wygaszenie naroznikow) + delikatna kompensacja sredniej
        //    ciemnosci scanlines (wiersze ciemne srednio o polowe u_crt.z)
        vec2 vv = (wp / u_rect.zw) * (1.0 - wp / u_rect.zw);
        float vig = clamp(vv.x * vv.y * 15.0, 0.0, 1.0);
        col.rgb *= (1.0 - u_crt.w * (1.0 - vig)) * (1.0 + 0.25 * u_crt.z);
      }
      // 4) poza wygiety ekran: zostaje czarny (col = 0)
    }
  }
  outColor = col;
}
