// polanie_extract.cpp — ekstraktor zasobów gry "Polanie" (1997).
//
// Formaty odtworzone z kodu źródłowego gry (game/image13h.cpp, game/playfli.cpp):
//
//  GRAF.DAT  bloki po 33000 B (domyślnie 30; dopuszczalne inne wielokrotności).
//            Blok = [u16 size][u16 w=319][u16 h=100] + 100 wierszy po 319 B.
//            Blok n = górna połowa ekranu (y 0..99), blok n+15 = dolna połowa
//            (y 100..199). 15 par => 15 ekranów 320x200 (pary wg ShowPicture).
//  PAL.DAT   14 palet po 768 B; wartości 0..255, DAC VGA przyjmuje 0..63
//            (SetExtendedPalette robi >>2, image13h.cpp:857).
//  PIC.DAT / SETUP.DAT   rekordy po 64768 B: [768 B palety 0..255][64000 B obraz
//            320x200 8bpp] (game/graphics.cpp ShowPicture2, builder mk_mix.cpp:
//            paleta + surowy VirtualScreen z tloNN.pcx). Liczba obrazów = rozmiar
//            / 64768 (demo/polanie_cd mają 35, SETUP.DAT = 1 obraz).
//  FONT.DAT / SETUP1.DAT / SETUP2.DAT — pojedyncze arkusze image13h, wiersze 319 B.
//  SWIAT.DAT FLIC (0xAF12), 371 klatek 320x200 8bpp, ale chunk 0xF100 (prefiks)
//            na starcie — trzeba go pominąć. Semantyka chunków wg playfli.cpp:
//            11=palieta, 12=FLI_LC(różnice), 13=czyszczenie, 15=FLI_BRUN, 16=KOPIA.
//  DATA/W*.DAT   RIFF WAV 8-bit mono 22050 Hz (dźwięki)
//  GRAF/GRAF.0xx S3M ("SCRM") — muzyka modułowa
//  GRAF/LEVEL.DAT / LEVEL.INI — tekst (CRLF, rekordy '$', wiersze '!'), polskie
//            litery we własnym kodowaniu fontu gry (tabela Transform13h),
//            NIE CP852 — patrz komentarz przy dumpLevelDat/dumpLevelIni
//
// Użycie: polanie-extract <katalog_gry> <katalog_wyjsciowy>
//         (LEVEL.DAT/INI szukane w GRAF/, LEVELS/ i katalogu głównym)
//
// Budowa: g++ -std=c++20 -O2 -o polanie-extract polanie_extract.cpp

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ------------------------------------------------------------- narzędzia --

static std::vector<uint8_t> readAll(const fs::path &p) {
  FILE *f = fopen(p.string().c_str(), "rb");
  if (!f)
    return {};
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  std::vector<uint8_t> d(n > 0 ? (size_t)n : 0);
  if (n > 0 && fread(d.data(), 1, (size_t)n, f) != (size_t)n)
    d.clear();
  fclose(f);
  return d;
}

// ---------------------------------------------------------------- PNG ----

static uint32_t crcTab[256];

static void initCrc() {
  for (uint32_t n = 0; n < 256; n++) {
    uint32_t c = n;
    for (int k = 0; k < 8; k++)
      c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    crcTab[n] = c;
  }
}

static uint32_t crc32(const uint8_t *d, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++)
    c = crcTab[(c ^ d[i]) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

static void put32(uint8_t *p, uint32_t v) {
  // PNG: wszystkie pola wielobajtowe big-endian
  p[0] = (uint8_t)(v >> 24);
  p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);
  p[3] = (uint8_t)v;
}

static void writeChunk(FILE *f, const char *type, const std::vector<uint8_t> &data) {
  uint8_t len[4];
  put32(len, (uint32_t)data.size());
  fwrite(len, 1, 4, f);
  std::vector<uint8_t> td;
  td.reserve(data.size() + 4);
  td.insert(td.end(), type, type + 4);
  td.insert(td.end(), data.begin(), data.end());
  uint8_t c[4];
  put32(c, crc32(td.data(), td.size()));
  fwrite(td.data(), 1, td.size(), f);
  fwrite(c, 1, 4, f);
}

// PNG zapisany "stored" deflate (bez kompresji) — czytelny wszędzie.
static void writePNG(const fs::path &path, int w, int h, const uint8_t *idx,
                     const uint8_t *pal8 /* 768 B, 0..255 */) {
  std::vector<uint8_t> raw((size_t)h * (1 + (size_t)w * 3));
  for (int y = 0; y < h; y++) {
    uint8_t *r = raw.data() + (size_t)y * (1 + (size_t)w * 3);
    r[0] = 0; // filtr: brak
    for (int x = 0; x < w; x++) {
      uint8_t c = idx[(size_t)y * w + x];
      r[1 + x * 3 + 0] = pal8[c * 3 + 0];
      r[1 + x * 3 + 1] = pal8[c * 3 + 1];
      r[1 + x * 3 + 2] = pal8[c * 3 + 2];
    }
  }

  std::vector<uint8_t> idat;
  idat.push_back(0x78); // zlib: compression level 1, bez predykcji
  idat.push_back(0x01);
  size_t pos = 0;
  while (pos < raw.size()) {
    size_t chunk = raw.size() - pos;
    if (chunk > 65535)
      chunk = 65535;
    bool last = (pos + chunk == raw.size());
    idat.push_back(last ? 1 : 0); // BFINAL+BTYPE=00 (stored)
    idat.push_back((uint8_t)(chunk & 0xFF));
    idat.push_back((uint8_t)(chunk >> 8));
    idat.push_back((uint8_t)(~chunk & 0xFF));
    idat.push_back((uint8_t)((~chunk >> 8) & 0xFF));
    idat.insert(idat.end(), raw.begin() + (long)pos, raw.begin() + (long)(pos + chunk));
    pos += chunk;
  }
  uint32_t a = 1, b = 0; // adler32
  for (uint8_t byte : raw) {
    a = (a + byte) % 65521;
    b = (b + a) % 65521;
  }
  uint8_t ad[4];
  put32(ad, (b << 16) | a);
  idat.insert(idat.end(), ad, ad + 4);

  FILE *f = fopen(path.string().c_str(), "wb");
  if (!f) {
    fprintf(stderr, "BLAD: nie moge zapisac %s\n", path.string().c_str());
    return;
  }
  static const uint8_t sig[8] = {137, 'P', 'N', 'G', 13, 10, 26, 10};
  fwrite(sig, 1, 8, f);
  std::vector<uint8_t> ihdr;
  uint8_t t[4];
  put32(t, (uint32_t)w);
  ihdr.insert(ihdr.end(), t, t + 4);
  put32(t, (uint32_t)h);
  ihdr.insert(ihdr.end(), t, t + 4);
  ihdr.push_back(8); // bit depth
  ihdr.push_back(2); // color type: RGB
  ihdr.push_back(0); // compression
  ihdr.push_back(0); // filter
  ihdr.push_back(0); // interlace
  writeChunk(f, "IHDR", ihdr);
  writeChunk(f, "IDAT", idat);
  writeChunk(f, "IEND", {});
  fclose(f);
}

// ------------------------------------------------------------- palety ----

// PAL.DAT / SETUP.PAL: wartości 0..255, DAC VGA przyjmuje 0..63 (gra robi >>2).
static void palFrom8bit(const uint8_t *src, uint8_t *dst) {
  for (int i = 0; i < 256; i++)
    for (int k = 0; k < 3; k++) {
      uint32_t v = src[i * 3 + k] >> 2; // 0..63
      dst[i * 3 + k] = (uint8_t)(v * 255 / 63);
    }
}

// Paleta z FLIC: wartości już 0..63 (gra pisze je wprost do DAC 0x3C9).
static void palFrom6bit(const uint8_t *src, uint8_t *dst) {
  for (int i = 0; i < 256; i++)
    for (int k = 0; k < 3; k++)
      dst[i * 3 + k] = (uint8_t)((uint32_t)src[i * 3 + k] * 255 / 63);
}

// Paleta FLIC 0..255 (chunk 4 w SWIAT.DAT ma wartości w pełnym zakresie 0..255,
// np. 14 18 0c / 54 68 8c / ac... — więcej niż 63).
static void palFrom8bitDirect(const uint8_t *src, uint8_t *dst) {
  memcpy(dst, src, 768);
}

// -------------------------------------------------------------- GRAF.DAT --

// PORT: tabela PAIRY wylaczona do wspolnego naglowka (tez dla testow portu)
#include "pairy.h"

static void dumpGraf(const fs::path &in, const fs::path &out) {
  auto graf = readAll(in / "GRAF.DAT");
  auto pal = readAll(in / "PAL.DAT");
  if (graf.empty()) {
    fprintf(stderr, "UWAGA: brak GRAF.DAT - pomijam\n");
    return;
  }
  if (graf.size() % 33000u != 0) {
    fprintf(stderr,
            "UWAGA: GRAF.DAT rozmiar %zu nie jest wielokrotnoscia 33000 - pomijam\n",
            graf.size());
    return;
  }
  size_t nblo = graf.size() / 33000u;
  if (nblo < 2) {
    fprintf(stderr, "UWAGA: GRAF.DAT ma %zu blokow - za malo, pomijam\n", nblo);
    return;
  }
  int pairs = (int)(nblo / 2);
  if (pairs > 15)
    pairs = 15;
  if (pal.size() < 14u * 768u) {
    fprintf(stderr, "UWAGA: PAL.DAT za krotki (%zu) - pomijam\n", pal.size());
    return;
  }

  // próbniki palet (16x16 komórek po 16 px)
  for (int p = 0; p < 14; p++) {
    uint8_t pal8[768];
    palFrom8bit(pal.data() + p * 768, pal8);
    std::vector<uint8_t> idx((size_t)256 * 256, 0);
    for (int c = 0; c < 256; c++) {
      int cx = c % 16, cy = c / 16;
      for (int dy = 0; dy < 16; dy++)
        for (int dx = 0; dx < 16; dx++)
          idx[(size_t)(cy * 16 + dy) * 256 + cx * 16 + dx] = (uint8_t)c;
    }
    char nm[64];
    snprintf(nm, 64, "paleta_%02d.png", p);
    writePNG(out / nm, 256, 256, idx.data(), pal8);
  }

  // ekrany 320x200 (górna + dolna połowa, wiersze 319 B)
  for (int t = 0; t < pairs; t++) {
    int top, bottom, palIdx;
    if (nblo >= 30) {
      top = PAIRY[t].top;
      bottom = PAIRY[t].bottom;
      palIdx = PAIRY[t].pal;
    } else { // nietypowa liczba bloków: parowanie n / n+pairs
      top = t;
      bottom = t + pairs;
      palIdx = t % 14;
    }
    uint8_t pal8[768];
    palFrom8bit(pal.data() + palIdx * 768, pal8);
    std::vector<uint8_t> scr((size_t)320 * 200, 0);
    for (int half = 0; half < 2; half++) {
      size_t off = (size_t)(half ? bottom : top) * 33000;
      for (int row = 0; row < 100; row++)
        memcpy(scr.data() + (size_t)(half * 100 + row) * 320,
               graf.data() + off + 6 + (size_t)row * 319, 319);
    }
    char nm[64];
    snprintf(nm, 64, "ekran_%02d.png", t);
    writePNG(out / nm, 320, 200, scr.data(), pal8);
  }
  printf("GRAF.DAT : %d ekranow (%zu blokow) -> %s/ekran_00..%02d.png + 14 palet\n",
         pairs, nblo, out.c_str(), pairs - 1);
}

// Pojedyncze arkusze image13h (FONT.DAT, SETUP1/2.DAT): [u16 size][u16 w][u16 h]
// + dane, wiersze po 319 B (pole size jest zawyżone; faktyczna szerokość 319).
static void dumpSheet(const fs::path &file, const fs::path &outPng,
                      const uint8_t *pal8) {
  auto d = readAll(file);
  if (d.size() < 7)
    return;
  uint32_t size = d[0] | (d[1] << 8);
  uint32_t w = d[2] | (d[3] << 8);
  uint32_t h = d[4] | (d[5] << 8);
  (void)h;
  size_t dataLen = d.size() - 6;
  if (dataLen > (size_t)size - 6)
    dataLen = (size_t)size - 6;
  int rows = (int)(dataLen / 319);
  if (rows < 1)
    return;
  if (rows > 200)
    rows = 200;
  std::vector<uint8_t> idx((size_t)319 * rows, 0);
  for (int row = 0; row < rows; row++)
    memcpy(idx.data() + (size_t)row * 319, d.data() + 6 + (size_t)row * 319, 319);
  writePNG(outPng, 319, rows, idx.data(), pal8);
  printf("%-12s -> %s (%d wierszy)\n", file.filename().string().c_str(),
         outPng.string().c_str(), rows);
}

// PIC.DAT / SETUP.DAT: rekordy po 64768 B = [768 B palety 0..255][64000 B obraz
// 320x200 8bpp]. Gra czyta: fseek(nr*64768); fread(pal,768); fread(scr,64000)
// (game/graphics.cpp ShowPicture2); paleta przez SetExtendedPalette => >>2.
// Liczba obrazów wyliczana z rozmiaru pliku (demo/polanie_cd: 35, SETUP.DAT: 1).
static void dumpPic(const fs::path &file, const fs::path &outDir) {
  const size_t REC = 768 + 64000;
  auto d = readAll(file);
  if (d.empty())
    return;
  if (d.size() < REC || d.size() % REC != 0) {
    fprintf(stderr,
            "UWAGA: %s rozmiar %zu nie jest wielokrotnoscia 64768 B - pomijam "
            "(to nie bank obrazow PIC; moze byc plikiem EXE)\n",
            file.filename().string().c_str(), d.size());
    return;
  }
  fs::create_directories(outDir);
  size_t n = d.size() / REC;
  for (size_t i = 0; i < n; i++) {
    uint8_t pal8[768];
    palFrom8bit(d.data() + i * REC, pal8);
    char nm[64];
    snprintf(nm, 64, "obraz_%02zu.png", i);
    writePNG(outDir / nm, 320, 200, d.data() + i * REC + 768, pal8);
  }
  printf("%-12s -> %s/obraz_00..%02zu.png (%zu obrazow 320x200 + wlasne palety)\n",
         file.filename().string().c_str(), outDir.string().c_str(), n - 1, n);
}

// ------------------------------------------------------- SWIAT.DAT FLIC --

static void dumpFlic(const fs::path &in, const fs::path &out) {
  auto f = readAll(in / "SWIAT.DAT");
  if (f.size() < 16) {
    fprintf(stderr, "UWAGA: brak SWIAT.DAT - pomijam\n");
    return;
  }
  auto u16 = [&](size_t p) -> uint32_t {
    return f[p] | (f[p + 1] << 8);
  };
  auto u32 = [&](size_t p) -> uint32_t {
    return f[p] | (f[p + 1] << 8) | (f[p + 2] << 16) | ((uint32_t)f[p + 3] << 24);
  };

  uint32_t frames = u16(6);
  uint8_t pal[768] = {0}, pal8[768] = {0};
  int palKind = 4; // rodzaj ostatnio wczytanej palety (4 = wartości 0..255)
  std::vector<uint8_t> scr((size_t)320 * 200, 0);

  size_t pos = 128; // po nagłówku FLIC
  // niestandardowy chunk-prefiks 0xF100 (zawiera oryginalną ścieżkę pliku .FLC)
  if (pos + 6 <= f.size() && u16(pos + 4) == 0xF100)
    pos += u32(pos);

  int n = 0;
  for (uint32_t fr = 0; fr < frames && pos + 16 <= f.size(); fr++) {
    uint32_t fsize = u32(pos);
    uint32_t chunks = u16(pos + 6);
    if (fsize < 16)
      break;
    pos += 16;
    for (uint32_t c = 0; c < chunks && pos + 6 <= f.size(); c++) {
      uint32_t csize = u32(pos);
      uint32_t ctype = u16(pos + 4);
      if (csize < 6 || pos + csize > f.size())
        break;
      const uint8_t *d = f.data() + pos + 6;
      size_t dl = csize - 6;
      size_t p = 0;
      if (ctype == 11 || ctype == 4) { // paleta (4 = FLI_COLOR256)
        palKind = (int)ctype;
        uint32_t packets = u16(pos + 6);
        p = 2;
        int colStart = 0;
        for (uint32_t k = 0; k < packets && p + 2 <= dl; k++) {
          int skip = d[p], len = d[p + 1];
          p += 2;
          colStart += skip * 3;
          size_t colLen = (size_t)len * 3;
          if (colLen == 0)
            colLen = 0x300; // 0 = pełne 256 kolorów
          if (p + colLen <= dl && colStart + (int)colLen <= 0x300) {
            memcpy(pal + colStart, d + p, colLen);
            p += colLen;
            colStart += (int)colLen;
          } else
            break;
        }
      } else if (ctype == 7) { // FLI_SS2 — różnice wyrównane do słowa
        p = 0;
        int y = 0;
        while (y < 200 && p < dl) {
          int packets = d[p++];
          if (packets == 0) { // 0 = następne u16: ile linii pominąć
            if (p + 2 > dl)
              break;
            y += d[p] | (d[p + 1] << 8);
            p += 2;
            continue;
          }
          int addr = y * 320, col = 0;
          for (int k = 0; k < packets && p < dl; k++) {
            col += d[p++];
            int cnt = (int8_t)d[p++];
            if (cnt >= 0) { // kopiuj cnt bajtów
              if (p + cnt <= dl && addr + col + cnt <= 64000)
                memcpy(&scr[addr + col], d + p, (size_t)cnt);
              p += cnt;
              col += cnt;
              if (cnt & 1)
                p++; // nieparzysta liczba bajtów = 1 bajt wyrównania
            } else { // powtórz następny bajt -cnt razy
              int nn = -cnt;
              uint8_t val = (p < dl) ? d[p++] : 0;
              if (addr + col + nn <= 64000)
                memset(&scr[addr + col], val, (size_t)nn);
              col += nn;
            }
          }
          y++;
        }
      } else if (ctype == 12) { // FLI_LC: różnice
        if (dl >= 4) {
          int startLine = u16(pos + 6);
          int nLines = u16(pos + 8);
          p = 4;
          for (int l = 0; l < nLines && p < dl; l++) {
            int addr = (startLine + l) * 320;
            int packets = d[p++];
            for (int k = 0; k < packets && p < dl; k++) {
              int skip = d[p++];
              int sz = d[p++];
              addr += skip;
              if (!(sz & 0x80)) { // kopiuj
                if (p + (size_t)sz <= dl && addr + sz <= 64000) {
                  memcpy(&scr[addr], d + p, (size_t)sz);
                  p += sz;
                }
                addr += sz;
              } else { // wypełnij
                int cnt = (sz ^ 0xFF) + 1;
                uint8_t val = d[p++];
                if (addr + cnt <= 64000)
                  memset(&scr[addr], val, (size_t)cnt);
                addr += cnt;
              }
            }
          }
        }
      } else if (ctype == 13) { // wyczyść ekran
        memset(scr.data(), 0, 64000);
      } else if (ctype == 15) { // FLI_BRUN: 200 wierszy, pełny obraz
        p = 0;
        for (int y = 0; y < 200 && p < dl; y++) {
          int addr = y * 320;
          int packets = d[p++];
          for (int k = 0; k < packets && p < dl; k++) {
            int sz = d[p++];
            if (sz & 0x80) { // kopiuj (semantyka wg playfli.cpp)
              int cnt = (sz ^ 0xFF) + 1;
              if (p + (size_t)cnt <= dl && addr + cnt <= 64000) {
                memcpy(&scr[addr], d + p, (size_t)cnt);
                p += cnt;
              }
              addr += cnt;
            } else { // wypełnij
              uint8_t val = d[p++];
              if (addr + sz <= 64000)
                memset(&scr[addr], val, (size_t)sz);
              addr += sz;
            }
          }
        }
      } else if (ctype == 16) { // kopia całego ekranu
        if (dl >= 64000)
          memcpy(scr.data(), d, 64000);
      }
      pos += csize;
    }
    if (palKind == 4)
      palFrom8bitDirect(pal, pal8);
    else
      palFrom6bit(pal, pal8);
    char nm[64];
    snprintf(nm, 64, "klatka_%03d.png", n);
    writePNG(out / nm, 320, 200, scr.data(), pal8);
    n++;
    if (pos + 16 > f.size())
      break;
  }
  printf("SWIAT.DAT: %d klatek -> %s/klatka_000..png\n", n, out.c_str());
}

// --------------------------------------------------------------- audio ---

static void dumpAudio(const fs::path &in, const fs::path &out) {
  fs::create_directories(out / "audio" / "dzwieki");
  fs::create_directories(out / "audio" / "muzyka");

  int nw = 0, nm = 0;
  char src[32], dst[64];
  for (int i = 1; i <= 200; i++) {
    snprintf(src, 32, "DATA/W%03d.DAT", i);
    fs::path p = in / src;
    if (!fs::exists(p))
      continue;
    auto d = readAll(p);
    bool wav = d.size() > 12 && !memcmp(d.data(), "RIFF", 4) &&
               !memcmp(d.data() + 8, "WAVE", 4);
    snprintf(dst, 64, "audio/dzwieki/W%03d.%s", i, wav ? "wav" : "dat");
    fs::copy_file(p, out / dst, fs::copy_options::overwrite_existing);
    if (wav)
      nw++;
  }
  for (int i = 1; i <= 100; i++) {
    snprintf(src, 32, "GRAF/GRAF.%03d", i);
    fs::path p = in / src;
    if (!fs::exists(p))
      continue;
    auto d = readAll(p);
    bool s3m = d.size() > 0x30 && !memcmp(d.data() + 0x2C, "SCRM", 4);
    snprintf(dst, 64, "audio/muzyka/GRAF_%03d.%s", i, s3m ? "s3m" : "dat");
    fs::copy_file(p, out / dst, fs::copy_options::overwrite_existing);
    if (s3m)
      nm++;
  }
  // DATA/SOUND.DAT — bank instrumentów (LoadGlobalData, ladowani.cpp:63);
  // format własny miksera (opiswava/operwav) — kopiujemy surowo, bez dekodowania.
  {
    fs::path sd = in / "DATA" / "SOUND.DAT";
    if (fs::exists(sd))
      fs::copy_file(sd, out / "audio" / "SOUND.dat",
                    fs::copy_options::overwrite_existing);
  }
  // DATA/I0xx.DAT — pojedyncze WAV-y próbek (SND.PlayWav("data\\i001.dat"))
  for (int i = 1; i <= 20; i++) {
    snprintf(src, 32, "DATA/I%03d.DAT", i);
    fs::path p = in / src;
    if (!fs::exists(p))
      continue;
    auto d = readAll(p);
    bool wav = d.size() > 12 && !memcmp(d.data(), "RIFF", 4) &&
               !memcmp(d.data() + 8, "WAVE", 4);
    snprintf(dst, 64, "audio/dzwieki/I%03d.%s", i, wav ? "wav" : "dat");
    fs::copy_file(p, out / dst, fs::copy_options::overwrite_existing);
  }
  printf("Audio    : %d WAV (DATA/W*.DAT) + %d S3M (GRAF/GRAF.0*) -> %s/audio\n",
         nw, nm, (out / "audio").c_str());
}

// ------------------------------------------------- teksty: LEVEL.DAT/INI --
//
// FORMAT (odtworzony z game/battle.cpp:2078, game/graphics.cpp:1349,
// editor/edit.cpp:590, editor/graphics.cpp:1293):
//   * pliki tekstowe DOS (CRLF), rekordy oddzielone '$', znak '@' = koniec;
//   * wiersze tekstu/mapy zaczynają się '!', treść wiersza kończy '%',
//     blok tekstu kończy wiersz z '~';
//   * LEVEL.DAT — definicje poziomów: rekord = wiersz parametrów
//     (np. "$D2 E1 T07 M0 G P8 *Helwig Wiking* lev1 \"hel\"") + wiersze mapy
//     '!...' (MaxX znaków na kafelek). Bajty 0xB3-0xDA to kody kafelków
//     skał/murów — gra porównuje je jako znaki (editor/edit.cpp, battle.cpp);
//     w pliku jest 28 rekordów + wiersz "legendy" przed pierwszym '$'.
//   * LEVEL.INI — teksty z ekranów plansz; gra czyta rekord nr
//     k = level + t*30 (t=0 wstęp, t=1 zwycięstwo, t=2 porażka):
//     rekordy 1-30 = wstępy, 31-60 = zwycięstwa, 61-90 = porażki,
//     91-95 = początek gry / zakończenie / autorzy.
//
// KODOWANIE: to NIE jest CP852. Polskie litery zapisano we własnym kodowaniu
// fontu gry — definitywną tabelę bajt→glif podaje Transform13h()
// (game/image13h.cpp:575, identycznie editor/lida13h.cpp:605):
//   ą=0x86 Ą=0x8F  ć=0x8D Ć=0x95  ę=0x91 Ę=0x90  ł=0x92 Ł=0x9C
//   ń=0xA4 Ń=0xA5  ó=0xA2 Ó=0xA3  ś=0x9E Ś=0x98  ź=0xA6 Ź=0xA0  ż=0xA7 Ż=0xA1
// (z CP852 pokrywają się tylko ć=0x86 i ó=0xA2 — stąd błędna konwersja iconv).
// Kafle mapy leżą na pozycjach CP437 box-drawing (font tekstowy ich nie ma).

static void appendUtf8(std::string &s, uint32_t cp) {
  if (cp < 0x80) {
    s.push_back((char)cp);
  } else if (cp < 0x800) {
    s.push_back((char)(0xC0 | (cp >> 6)));
    s.push_back((char)(0x80 | (cp & 0x3F)));
  } else {
    s.push_back((char)(0xE0 | (cp >> 12)));
    s.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
    s.push_back((char)(0x80 | (cp & 0x3F)));
  }
}

// polskie litery wg Transform13h()
static const struct {
  uint8_t b;
  uint32_t cp;
} TAB_POLSKIE[] = {
    {0x86, 0x0105}, {0x8F, 0x0104}, // ą Ą
    {0x8D, 0x0107}, {0x95, 0x0106}, // ć Ć
    {0x91, 0x0119}, {0x90, 0x0118}, // ę Ę
    {0x92, 0x0142}, {0x9C, 0x0141}, // ł Ł
    {0xA4, 0x0144}, {0xA5, 0x0143}, // ń Ń
    {0xA2, 0x00F3}, {0xA3, 0x00D3}, // ó Ó
    {0x9E, 0x015B}, {0x98, 0x015A}, // ś Ś
    {0xA6, 0x017A}, {0xA0, 0x0179}, // ź Ź
    {0xA7, 0x017C}, {0xA1, 0x017B}, // ż Ż
};

// kody kafelków mapy w LEVEL.DAT (pozycje CP437 box-drawing)
static const struct {
  uint8_t b;
  uint32_t cp;
} TAB_KAFLE[] = {
    {0xB3, 0x2502}, {0xB7, 0x2556}, {0xB8, 0x2555}, {0xBA, 0x2551},
    {0xBC, 0x255D}, {0xBD, 0x255C}, {0xC4, 0x2500}, {0xC8, 0x255A},
    {0xC9, 0x2554}, {0xCD, 0x2550}, {0xD3, 0x2559}, {0xD4, 0x2558},
    {0xD5, 0x2552}, {0xD9, 0x2518}, {0xDA, 0x250C},
};

template <class T, size_t N> static bool tabFind(const T (&tab)[N], uint8_t b, uint32_t &cp) {
  for (size_t i = 0; i < N; i++)
    if (tab[i].b == b) {
      cp = tab[i].cp;
      return true;
    }
  return false;
}

// bajty gry → UTF-8; nieznane bajty wysokie jako <XX>, żeby nie gubić danych
static std::string polToUtf8(const std::vector<uint8_t> &d, bool kafle) {
  std::string s;
  s.reserve(d.size() + d.size() / 16 + 16);
  for (uint8_t c : d) {
    if (c == '\r')
      continue; // CRLF → LF
    if (c < 0x80) {
      s.push_back((char)c);
      continue;
    }
    uint32_t cp;
    if (tabFind(TAB_POLSKIE, c, cp))
      appendUtf8(s, cp);
    else if (kafle && tabFind(TAB_KAFLE, c, cp))
      appendUtf8(s, cp);
    else {
      char b[8];
      snprintf(b, 8, "<%02X>", c);
      s += b;
    }
  }
  return s;
}

static bool writeFileUtf8(const fs::path &p, const std::string &s) {
  FILE *f = fopen(p.string().c_str(), "wb");
  if (!f) {
    fprintf(stderr, "BLAD: nie moge zapisac %s\n", p.string().c_str());
    return false;
  }
  fwrite(s.data(), 1, s.size(), f);
  fclose(f);
  return true;
}

// podział na rekordy oddzielone '$'; '@' = koniec pliku (pomijamy)
static std::vector<std::string> splitRecords(const std::string &s) {
  std::vector<std::string> v;
  std::string cur;
  for (char ch : s) {
    if (ch == '$') {
      v.push_back(cur);
      cur.clear();
    } else if (ch != '@') {
      cur.push_back(ch);
    }
  }
  if (!cur.empty())
    v.push_back(cur);
  return v;
}

// treść rekordu do końca bloku (wiersz zawierający '~')
static std::string blockToTilde(const std::string &rec) {
  std::string out;
  size_t pos = 0;
  while (pos <= rec.size()) {
    size_t eol = rec.find('\n', pos);
    if (eol == std::string::npos)
      eol = rec.size();
    bool stop = rec.find('~', pos) != std::string::npos && rec.find('~', pos) < eol;
    out.append(rec, pos, eol - pos);
    out.push_back('\n');
    if (stop)
      break;
    pos = eol + 1;
  }
  return out;
}

static std::string trim(const std::string &s) {
  size_t a = s.find_first_not_of(" \t\n");
  if (a == std::string::npos)
    return "";
  size_t b = s.find_last_not_of(" \t\n");
  return s.substr(a, b - a + 1);
}

// nazwa poziomu z wiersza parametrów: *...*
static std::string poziomNazwa(const std::string &rec) {
  size_t a = rec.find('*');
  if (a == std::string::npos)
    return "";
  size_t b = rec.find('*', a + 1);
  if (b == std::string::npos)
    return "";
  return rec.substr(a + 1, b - a - 1);
}

// LEVEL.DAT/INI mogą leżeć w GRAF/ (pełna wersja), LEVELS/ (demo, polanie_cd)
// albo bezpośrednio w katalogu gry (pakiet misji).
static fs::path findResource(const fs::path &in, const char *name) {
  static const char *DIRS[] = {"", "GRAF", "LEVELS"};
  for (const char *dir : DIRS) {
    fs::path p = dir[0] ? in / dir / name : in / name;
    std::error_code ec;
    if (fs::exists(p, ec))
      return p;
  }
  return {};
}

static void dumpLevelDat(const fs::path &in, const fs::path &out) {
  fs::path p = findResource(in, "LEVEL.DAT");
  if (p.empty()) {
    fprintf(stderr, "UWAGA: brak LEVEL.DAT (GRAF/|LEVELS/|.) - pomijam\n");
    return;
  }
  auto d = readAll(p);
  if (d.empty()) {
    fprintf(stderr, "UWAGA: LEVEL.DAT pusty - pomijam\n");
    return;
  }
  std::string all = polToUtf8(d, true);
  std::string hdr =
      "# LEVEL.DAT - definicje poziomow gry Polanie (rekordy oddzielone '$',\n"
      "# wiersze mapy '!...', kody kafelkow = znaki kreskowe CP437).\n"
      "# Polskie litery wg Transform13h (font gry), CRLF->LF. Wygenerowane\n"
      "# przez tools/polanie_extract.cpp - plik zrodlowy: GRAF/LEVEL.DAT.\n\n";
  writeFileUtf8(out / "teksty" / "LEVEL.txt", hdr + all);

  auto rec = splitRecords(all);
  printf("LEVEL.DAT (%s): %zu rekordow + legenda -> %s/teksty/LEVEL.txt + "
         "%s/teksty/poziomy/\n",
         p.string().c_str(), rec.size() - 1, out.c_str(), out.c_str());

  fs::create_directories(out / "teksty" / "poziomy");
  writeFileUtf8(out / "teksty" / "poziomy" / "00_legenda.txt",
                "# Wiersz legendy sprzed pierwszego rekordu (znaki kafelkow).\n" + rec[0]);
  for (size_t i = 1; i < rec.size(); i++) {
    char nm[80];
    snprintf(nm, 80, "poziom_%02zu.txt", i);
    std::string nazwa = trim(poziomNazwa(rec[i]));
    std::string body =
        "== Poziom " + std::to_string(i) +
        (nazwa.empty() ? " ==" : ": " + nazwa + " ==") + "\n\n" + rec[i];
    writeFileUtf8(out / "teksty" / "poziomy" / nm, body);
  }
}

static void dumpLevelIni(const fs::path &in, const fs::path &out) {
  fs::path p = findResource(in, "LEVEL.INI");
  if (p.empty()) {
    fprintf(stderr, "UWAGA: brak LEVEL.INI (GRAF/|LEVELS/|.) - pomijam\n");
    return;
  }
  auto d = readAll(p);
  if (d.empty()) {
    fprintf(stderr, "UWAGA: LEVEL.INI pusty - pomijam\n");
    return;
  }
  std::string all = polToUtf8(d, false);
  std::string hdr =
      "# LEVEL.INI - teksty z ekranow plansz (wstep/zwyciestwo/porazka).\n"
      "# Rekordy oddzielone '$'; gra czyta rekord k = level + t*30\n"
      "# (t=0 wstep, t=1 zwyciestwo, t=2 porazka), '~' konczy blok.\n"
      "# Polskie litery wg Transform13h (font gry), CRLF->LF. Wygenerowane\n"
      "# przez tools/polanie_extract.cpp - plik zrodlowy: GRAF/LEVEL.INI.\n\n";
  writeFileUtf8(out / "teksty" / "LEVEL_INI.txt", hdr + all);

  auto rec = splitRecords(all);
  printf("LEVEL.INI (%s): %zu rekordow -> %s/teksty/LEVEL_INI.txt + "
         "%s/teksty/levelini/\n",
         p.string().c_str(), rec.size() - 1, out.c_str(), out.c_str());

  fs::create_directories(out / "teksty" / "levelini");
  int n = 0;
  for (int lvl = 1; lvl <= 30; lvl++) {
    static const char *SEK[] = {"wstep", "zwyciestwo", "porazka"};
    std::string body = "== Poziom " + std::to_string(lvl) + " ==\n";
    bool any = false;
    for (int t = 0; t < 3; t++) {
      size_t idx = (size_t)lvl + 30 * t; // rec[k] = splitRecords[k]
      if (idx >= rec.size())
        continue;
      if (rec[idx].find('!') == std::string::npos)
        continue; // rekord-separator (np. naglowek sekcji)
      body += "\n--- " + std::string(SEK[t]) + " ---\n" + blockToTilde(rec[idx]);
      any = true;
      n++;
    }
    if (!any)
      continue;
    char nm[80];
    snprintf(nm, 80, "poziom_%02d.txt", lvl);
    writeFileUtf8(out / "teksty" / "levelini" / nm, body);
  }

  // rekordy 91+: początek gry, zakończenie, autorzy
  std::string konc = "== Zakonczenie i napisy ==\n";
  for (size_t idx = 91; idx < rec.size(); idx++) {
    if (rec[idx].find('!') == std::string::npos)
      continue;
    size_t eol = rec[idx].find('\n');
    std::string tytul = trim(eol == std::string::npos ? rec[idx] : rec[idx].substr(0, eol));
    konc += "\n--- " + (tytul.empty() ? std::string("(bez tytulu)") : tytul) + " ---\n" +
            blockToTilde(rec[idx]);
  }
  if (konc.find('!') != std::string::npos)
    writeFileUtf8(out / "teksty" / "levelini" / "99_zakonczenie.txt", konc);
  printf("           tekstow plansz: %d (wstep/zwyciestwo/porazka)\n", n);
}

// LEVEL2.INI — teksty z nowszej wersji (demo/polanie_cd, 40 kB); to samo
// kodowanie co LEVEL.INI; zrzucamy w całości (rekordy '$' zostają w pliku).
static void dumpLevel2Ini(const fs::path &in, const fs::path &out) {
  fs::path p = findResource(in, "LEVEL2.INI");
  if (p.empty())
    return;
  auto d = readAll(p);
  if (d.empty())
    return;
  std::string hdr =
      "# LEVEL2.INI - teksty z ekranow plansz (nowsza wersja gry).\n"
      "# Kodowanie jak LEVEL.INI (Transform13h), CRLF->LF. Zrodlo: ";
  hdr += p.string() + "\n\n";
  writeFileUtf8(out / "teksty" / "LEVEL2.txt", hdr + polToUtf8(d, false));
  printf("LEVEL2.INI (%s): %zu B -> %s/teksty/LEVEL2.txt\n", p.string().c_str(),
         d.size(), out.c_str());
}

// ----------------------------------------------------------------- main --

int main(int argc, char **argv) {
  if (argc < 3) {
    printf("Uzycie: polanie-extract <katalog_gry> <katalog_wyjsciowy>\n");
    printf("  np. polanie-extract dysk/GRY/POLANIE extracted\n");
    return 1;
  }
  initCrc();
  fs::path in = argv[1], out = argv[2];
  fs::create_directories(out / "grafika");
  fs::create_directories(out / "swiat");
  fs::create_directories(out / "teksty");

  dumpGraf(in, out / "grafika");

  // arkusze SETUP — paleta SETUP.PAL, a jeśli jej nie ma, to paleta 0 z PAL.DAT
  {
    uint8_t pal8[768] = {0}, palInst[768] = {0};
    auto sp = readAll(in / "SETUP.PAL");
    auto pd = readAll(in / "PAL.DAT");
    if (sp.size() >= 768)
      palFrom8bit(sp.data(), pal8);
    else if (pd.size() >= 768)
      palFrom8bit(pd.data(), pal8);
    else { // brak jakiejkolwiek palety (np. same kody zrodlowe) — skala szarości
      for (int i = 0; i < 256; i++)
        pal8[i * 3] = pal8[i * 3 + 1] = pal8[i * 3 + 2] = (uint8_t)i;
    }
    dumpSheet(in / "SETUP1.DAT", out / "grafika" / "setup1.png", pal8);
    dumpSheet(in / "SETUP2.DAT", out / "grafika" / "setup2.png", pal8);
    dumpSheet(in / "FONT.DAT", out / "grafika" / "font.png", pal8);

    // INSTALL1/2.DAT (ekran instalatora) — paleta INSTALL.PAL, fallback jak wyżej
    auto ip = readAll(in / "INSTALL.PAL");
    if (ip.size() >= 768)
      palFrom8bit(ip.data(), palInst);
    else
      memcpy(palInst, pal8, 768);
    dumpSheet(in / "INSTALL1.DAT", out / "grafika" / "install1.png", palInst);
    dumpSheet(in / "INSTALL2.DAT", out / "grafika" / "install2.png", palInst);

    // font edytora (format 13h, ten sam nagłówek co FONT.DAT)
    dumpSheet(in / "FONTS1.13H", out / "grafika" / "fonts13h.png", pal8);
  }

  // banki obrazów: PIC.DAT (tła z mk_mix.cpp) i SETUP.DAT (pojedynczy obraz)
  dumpPic(in / "PIC.DAT", out / "grafika" / "pic");
  dumpPic(in / "SETUP.DAT", out / "grafika" / "setup");

  dumpFlic(in, out / "swiat");
  dumpAudio(in, out);
  dumpLevelDat(in, out);
  dumpLevelIni(in, out);
  dumpLevel2Ini(in, out);
  return 0;
}