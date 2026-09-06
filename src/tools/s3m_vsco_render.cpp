// s3m_vsco_render.cpp — tor "VSCO-mapped" dla 18 modułów S3M Polan.
//
// Parser wariantu S3M Polan + symulator oryginalnego odtwarzacza
// (game/plays3m.cpp: LoadPaterns + PlayOneLine) -> eventy -> sfizz (VSCO CE)
// -> WAV 16-bit stereo.
//
// Semantyka JEDNA-KO-JEDNA z game/plays3m.cpp (oryginał gry ma priorytet nad
// schismtrackerem):
//   * nagłówek: ordnum/insnum/patnum u16 @0x20/0x22/0x24, Gvol/speed/tempo/Mvol
//     bajty @0x30..0x33, ordery @0x60, wskaźniki instrumentów u16
//     @0x60+ordnum+i*2 (rekord na para*16+0x10), wskaźniki patternów u16
//     @0x60+ordnum+insnum*2+i*2 (dane na para*16, u16 długość przed danymi).
//   * packing patternów = standardowy packed S3M (potwierdzone w
//     thirdparty/schismtracker fmt/s3m.c:415-470): bajt 0 = koniec LINII,
//     bity maski: 32=note+inst, 64=vol, 128=ef+par, kanał = bajt & 31.
//     Wariant Polanie ma dokładnie ten sam packing — różnicą jest tylko
//     wczytywanie instrumentów (OPL zamiast sampli) i 9-kanałowy odtwarzacz.
//   * nuta S3M: oktawa = note>>4, półton = note&15; F-num freq[note&15] przy
//     okt 0: F-num 343 = 343*49716/2^20 = 16.26 Hz = C0 (taka konwersja jest
//     w schism player/snd_fm.c, OPLRATEBASE=49716), czyli
//     MIDI = oct*12 + sem + 12 (schism fmt/s3m.c robi wewn. +13 = MIDI +12).
//   * timing: timer 50 Hz (SetTimerProc(0x32), game/timer.cpp: 1193180/freq),
//     linia co `speed` ticków = speed/50 s. Tempo @0x32 oryginał IGNORUJE.
//     Moduły mają tempo 125 (116 dla 013) => standardowy tick S3M (0.4*tempo)
//     = 50 Hz — timing oryginału i standardu się zgadza.
//   * efekty (PlayOneLine:181-228): 1 = set speed, 2 = jump do order `par`
//     (par-1, po czym ++ => par), 3 = pattern break do row 0 następnego
//     ordera (par ignorowany), 4 = volume slide down o par*8 RAZ na linię
//     (nie co tick jak Dxx w standardzie). Inne efekty oryginał ignoruje.
//   * głośność: Vol[kanał] = S3M.vol[inst-1] (efektywnie z +28 rekordu, bo
//     LoadPaterns:95-96 czyta vol podwójnie i drugi odczyt nadpisuje),
//     eventowy vol nadpisuje; vvv = max(1, Vol - MVol); MVol = 8 (global
//     plays3m.cpp); velocity MIDI = vvv * 127/63 (SetVolume: reg40 = 63-vvv).
//   * nuty grają tylko przy MVol < 64 (oryginał: "note < 254 && MVol < 64");
//     note==254 => StopNote (note off); vol==0 => KillNote w tym samym
//     przerwaniu 50 Hz => słuchowo cisza => pomijamy nutę, ucina aktywną.
//   * Vol[inst-1] poza vol[40] wchodzi w sąsiednie pola struct S3MStruct
//     (dsk, Hz) — global S3M jest zero-inicjalizowany, vol[-1] == S3M.insN;
//     odtwarzamy tę "wirtualną tablicę" (patrz VolPamiec).
//   * kanały: oryginał gra 9 kanałów Adlib (Vol[9], KillNote 0..8) — kanały
//     >=9 ignorowane z licznikiem w logu (zapis Vol[9..] byłby UB).
//   * wskP startowe = paternT[0] (LoadPaterns:127); kolejnosc[0] jest używane
//     dopiero przy pętli po 0xff — gdy kolejnosc[0] != 0, logujemy różnicę.
//   * pętla utworu (kolejnosc==0xff -> rozkaz=0) jest nieskończona w grze;
//     render kończy się po --maxpass "przejściach" listy orderów (default 1).
//
// Mapowanie instrumentów: game-linux/port/sfz_map.h (kS3mInstrumentMap, 100
// wpisów). perc_note>0 => GM-StylePerc.sfz na STAŁEJ nucie GM; melodyczne =>
// sfz z mapy; niepewna=1 => flaga UNCERTAIN w logu (przypisanie z mapy).
// Sfizz: 9 instancji (jedna na kanał OPL), więc note_off jednego kanału nie
// zabija nut sąsiednich; zmiana instrumentu = sfizz_load_string (po
// all_sound_off — oryginał też ucina kanał StopNote przed SetInstrum).
// default_path .sfz ma backslashe Windowsowe — normalizujemy '\\' -> '/'.
//
// Kompilacja:
//   g++ -std=c++20 -O2 -Wall -I game-linux tools/s3m_vsco_render.cpp -o game-linux/build/tools/s3m_vsco_render -lsfizz
// (game-linux/tools.mk ma gotowy target: make -f game-linux/tools.mk tools)
//
// Użycie:
//   s3m_vsco_render --dump GRAF_001.s3m        # statystyka bez sfizz
//   s3m_vsco_render GRAF_001.s3m -o out.wav    # render toru VSCO
// Opcje: --maxpass N (przejścia listy orderów, default 1), --tail S
// (doświecenie, default 3), --mvol N (default 8), --vol-gain dB (default 0),
// --vsco DIR (katalog .sfz, default vsco/ przy korzeniu repo).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <sfizz.h>

#include "port/sfz_map.h"

namespace {

constexpr int kMaxChannels = 9;       // Vol[9] w PlayOneLine — 9 kanałów Adlib
constexpr int kPaternBufSize = 13000; // rozmiar bufora w GetPaternMemory
constexpr int kSampleRate = 48000;
constexpr int kTickSamples = kSampleRate / 50; // 960 — tick 20 ms (timer 50 Hz)

uint16_t rd16(const uint8_t *p) { return uint16_t(p[0]) | uint16_t(p[1] << 8); }

// ---------------------------------------------------------------------------
// (1) Parser wariantu S3M Polan (dokładnie wg LoadPaterns, plays3m.cpp:53-131)
// ---------------------------------------------------------------------------

struct S3mIns {
  int typ = 0;
  std::string plik;  // 12 B @+1
  std::string nazwa; // 28 B @+48
  uint8_t opl[11] = {};
  int vol = 0;   // @+28 — efektywnie wg LoadPaterns (podwójny fread nadpisuje)
  int vol27 = 0; // @+27 — pierwszy odczyt vol (nadpisany w oryginale, do logu)
  int dsk = 0;   // @+29
  int hz = 0;    // @+32..33 — efektywnie wg LoadPaterns
  int hz30 = 0;  // @+30..31 — pierwszy odczyt Hz (nadpisany, do logu)
  char sig[5] = {0, 0, 0, 0, 0};
};

struct S3mMod {
  std::string tytul;
  int ordnum = 0, insnum = 0, patnum = 0;
  int gvol = 0, speed = 6, tempo = 125, mvol = 0;
  bool scrm = false;
  std::vector<uint8_t> kolejnosc;
  std::vector<S3mIns> ins;
  std::vector<uint16_t> patpar;
  std::vector<size_t> patoff;   // offset danych i-tego patternu w patdata
  std::vector<uint16_t> patlen; // Length z nagłówka patternu
  std::vector<uint8_t> patdata; // konkatenacja (jak bufor S3M.paterns)
  std::vector<std::string> uwagi; // ostrzeżenia parsera
};

std::string przycina(const char *p, int n) {
  std::string s(p, size_t(n));
  while (!s.empty() && (s.back() == ' ' || s.back() == 0)) s.pop_back();
  return s;
}

bool load_s3m(const std::string &path, S3mMod &m) {
  FILE *f = std::fopen(path.c_str(), "rb");
  if (!f) {
    m.uwagi.push_back("nie można otworzyć pliku");
    return false;
  }
  std::fseek(f, 0, SEEK_END);
  long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  std::vector<uint8_t> buf((size_t)size);
  if (std::fread(buf.data(), 1, buf.size(), f) != buf.size()) {
    m.uwagi.push_back("skrócony odczyt pliku");
    std::fclose(f);
    return false;
  }
  std::fclose(f);

  auto rd = [&](long off, long len) -> const uint8_t * {
    if (off < 0 || off + len > (long)buf.size()) {
      m.uwagi.push_back("odczyt poza plikiem @" + std::to_string(off));
      return nullptr;
    }
    return buf.data() + off;
  };

  if (const uint8_t *s = rd(0x2C, 4))
    m.scrm = 0 == std::memcmp(s, "SCRM", 4);
  if (const uint8_t *t = rd(0, 28)) m.tytul = przycina((const char *)t, 28);
  if (const uint8_t *h30 = rd(0x30, 4)) {
    m.gvol = h30[0];
    m.speed = h30[1];
    m.tempo = h30[2];
    m.mvol = h30[3];
  }
  if (m.speed == 0) {
    m.speed = 6;
    m.uwagi.push_back("speed=0 w nagłówku -> 6 (jak w schism fmt/s3m.c)");
  }
  const uint8_t *h20 = rd(0x20, 6);
  if (!h20) return false;
  m.ordnum = rd16(h20);
  m.insnum = rd16(h20 + 2);
  m.patnum = rd16(h20 + 4);
  if (m.ordnum > 100)
    m.uwagi.push_back("ordnum " + std::to_string(m.ordnum) +
                      " > 100 (kolejnosc[100] przepełniona w oryginale)");
  if (m.insnum > 40)
    m.uwagi.push_back("insnum " + std::to_string(m.insnum) +
                      " > 40 (instT[40]/vol[40] przepełnione w oryginale)");
  if (m.patnum > 25)
    m.uwagi.push_back("patnum " + std::to_string(m.patnum) +
                      " > 25 (paternT[25] przepełnione w oryginale)");

  const uint8_t *k = rd(0x60, m.ordnum);
  if (!k) return false;
  m.kolejnosc.assign(k, k + m.ordnum);
  if (m.ordnum > 0 && m.kolejnosc[0] != 0)
    m.uwagi.push_back("kolejnosc[0]=" + std::to_string(m.kolejnosc[0]) +
                      "; oryginał jednak startuje z paternT[0] (LoadPaterns:127)");

  for (int i = 0; i < m.insnum; i++) {
    const uint8_t *pw = rd(0x60 + m.ordnum + i * 2, 2);
    if (!pw) return false;
    uint16_t para = rd16(pw);
    long index = long(para) * 16 + 0x10; // dokładnie jak LoadPaterns:89-93
    S3mIns in;
    if (para == 0)
      m.uwagi.push_back("instrument " + std::to_string(i + 1) +
                        ": wskaźnik 0 (oryginał czytałby śmieci)");
    const uint8_t *r = rd(index, 80);
    if (!r) return false;
    in.typ = r[0];
    in.plik = przycina((const char *)(r + 1), 12);
    std::memcpy(in.opl, r + 16, 11);
    in.vol27 = r[27];
    in.vol = r[28];
    in.dsk = r[29];
    in.hz30 = rd16(r + 30);
    in.hz = rd16(r + 32);
    std::memcpy(in.sig, r + 76, 4);
    in.nazwa = przycina((const char *)(r + 48), 28);
    m.ins.push_back(in);
    if (in.typ != 2)
      m.uwagi.push_back("instrument " + std::to_string(i + 1) + ": typ=" +
                        std::to_string(in.typ) + " (oryginał zakłada Adlib)");
    if (std::memcmp(in.sig, "SCRI", 4) != 0)
      m.uwagi.push_back("instrument " + std::to_string(i + 1) + ": sygnatura \"" +
                        std::string(in.sig, 4) + "\" != \"SCRI\"");
  }

  size_t off = 0;
  for (int i = 0; i < m.patnum; i++) {
    const uint8_t *pw = rd(0x60 + m.ordnum + m.insnum * 2 + i * 2, 2);
    if (!pw) return false;
    uint16_t para = rd16(pw);
    m.patpar.push_back(para);
    if (para == 0) {
      m.uwagi.push_back("pattern " + std::to_string(i) +
                        ": wskaźnik 0 (pomijam; oryginał czytałby nagłówek " +
                        "pliku jako pattern)");
      m.patoff.push_back(off);
      m.patlen.push_back(0);
      continue;
    }
    const uint8_t *ph = rd(long(para) * 16, 2);
    if (!ph) return false;
    uint16_t len = rd16(ph);
    const uint8_t *pd = rd(long(para) * 16 + 2, len);
    if (!pd) return false;
    m.patoff.push_back(off);
    m.patlen.push_back(len);
    m.patdata.insert(m.patdata.end(), pd, pd + len);
    off += len;
  }
  if ((long)m.patdata.size() > kPaternBufSize)
    m.uwagi.push_back("łączne dane patternów " + std::to_string(m.patdata.size()) +
                      " B > " + std::to_string(kPaternBufSize) +
                      " B bufora oryginału (przepełnienie)");
  return true;
}

// ---------------------------------------------------------------------------
// (2) Symulator PlayOneLine (plays3m.cpp:133-250)
// ---------------------------------------------------------------------------

struct Sink {
  virtual void noteOn(int ch, int midi, int vel, int inst1) = 0;
  virtual void noteOff(int ch, bool kill) = 0;
  virtual void volumeOnly(int ch, int vel) = 0;
  virtual ~Sink() = default;
};

struct SimStat {
  long noteOn = 0, noteOff254 = 0, volOnly = 0, killVol0 = 0, volUnderflow = 0;
  long ef1 = 0, ef2 = 0, ef3 = 0, ef4 = 0, efInne = 0;
  long chIgnored = 0, instPozaZakres = 0, instZero = 0, semPonad11 = 0;
  long orderPozaZakres = 0, skoki = 0, tickCount = 0, przecieki = 0;
  int maxCh = -1, maxInsIdx = 0, maxSem = -1;
  std::map<int, long> efHist; // nieobsłużone efekty -> liczba
  std::map<int, long> chUse;  // kanał -> liczba nut
  std::map<int, long> insUse; // instrument (1-based) -> liczba nut
};

// Wirtualna tablica "vol[-1] ... vol[239]" — odczyty S3M.vol[inst-1] poza
// vol[40] w oryginale wchodzą w sąsiednie pola struct S3MStruct (dsk, Hz).
// Global S3M jest zero-inicjalizowany; vol[-1] == S3M.insN (char przed vol).
struct VolPamiec {
  const S3mMod &m;
  uint8_t operator[](int idx) const { // idx = inst-1 (może być -1)
    if (idx < 0) return uint8_t(m.insnum);
    if (idx < 40) {
      if (idx < m.insnum) return uint8_t(m.ins[idx].vol);
      return 0; // LoadPaterns nie zapisywał (zakładamy pierwsze załadowanie)
    }
    if (idx < 80) { // dsk[0..39]
      int i = idx - 40;
      if (i < m.insnum) return uint8_t(m.ins[i].dsk);
      return 0;
    }
    if (idx < 240) { // Hz[0..39] jako bajty little-endian
      int i = idx - 80;
      if (i / 4 < m.insnum)
        return uint8_t((m.ins[i / 4].hz >> (8 * (i % 4))) & 0xFF);
      return 0;
    }
    return 0;
  }
};

struct Sim {
  const S3mMod &m;
  Sink &sink;
  int MVol = 8;
  int speed = 6;
  int linia = 0, rozkaz = 0, czekaj = 0;
  size_t wskP = 0;
  long rows = 0, maxRows = 0;
  bool end = false, limitHit = false;
  SimStat st;
  uint8_t volTab[kMaxChannels] = {63, 63, 63, 63, 63, 63, 63, 63, 63};
  int kon = 0, nrRozkazu = 255;
  uint8_t inst = 0, note = 255, vol = 255, ef = 0, par = 0, chanel = 0;

  Sim(const S3mMod &mod, Sink &sn, int mvol, int maxpass)
      : m(mod), sink(sn), MVol(mvol), speed(mod.speed) {
    maxRows = long(m.ordnum) * 64 * (maxpass > 0 ? maxpass : 1);
    if (m.patnum > 0) wskP = m.patoff[0]; // LoadPaterns:127 — start z paternT[0]
  }

  static int velocity(int vvv) {
    long v = std::lround(vvv * 127.0 / 63.0);
    if (v < 1) v = 1;
    return int(std::min<long>(v, 127));
  }
  int volKanalu(int ch) const { return volTab[std::min(ch, kMaxChannels - 1)]; }
  void ustawVol(int ch, int v) {
    volTab[std::min(ch, kMaxChannels - 1)] = uint8_t(std::max(0, v));
  }
  void efekt4(int ch) { // Vol -= par<<3; oryginał wrappuje u8 — my clamp + log
    int par8 = par << 3;
    if (volKanalu(ch) - par8 < 0) st.volUnderflow++;
    ustawVol(ch, volKanalu(ch) - par8);
    st.ef4++;
  }
  int vvv(int ch) const {
    int v = volKanalu(ch);
    return v > MVol ? v - MVol : 1;
  }

  // jeden tick zegara 50 Hz; po nim sprawdź sim.end
  void tick() {
    if (end) return;
    st.tickCount++;
    if (czekaj > 0) { // oryginał: czekaj--, return
      czekaj--;
      return;
    }
    playOneLine();
    czekaj = speed - 1; // oryginał: na końcu PlayOneLine
  }

  void playOneLine() {
    // PORT (limit iteracji — NAPRAWA 2026-09-06): maxRows (ordnum*64*maxpass)
    // był liczony w konstruktorze i drukowany w statystykach, lecz NIGDY nie
    // sprawdzany — sim kończył się tylko przez rozkaz>=ordnum / kolejnosc==255.
    // Moduły ze skokiem ef2 na końcu zapętlają ordery w nieskończoność
    // (GRAF_002: ostatnia linia patternu 7 = maska 0x80, ef=2, par=2 -> skok
    // do orderu 2; to pętla menu, w oryginale gry wieczna) -> pętla
    // while (!sim.end) wisiała na 100% CPU. Limit z --maxpass działa realnie:
    if (maxRows > 0 && rows >= maxRows) {
      end = true;
      limitHit = true;
      return;
    }
    rows++;
    bool endLine = false;
    while (!endLine) {
      if (wskP >= m.patdata.size()) {
        // oryginał czytałby dalej z bufora 13000 B (przeciek do kolejnych
        // patternów w pamięci) — tu: koniec patternu, zliczone w log
        st.przecieki++;
        endLine = true;
        break;
      }
      uint8_t bajt = m.patdata[wskP++];
      if (!bajt) {
        endLine = true; // oryginał: endLine=1
        break;
      }
      chanel = bajt;
      note = 255;
      vol = 255;
      ef = 0;
      par = 0;
      st.maxCh = std::max<int>(st.maxCh, int(chanel & 31));
      // PORT: zabezpieczenie odczytów klauzuli — wcześniej m.patdata[wskP++]
      // czytało POZA końcem wektora (UB, śmieci jako ef/par -> mogły
      // fabrykować skoki ef2 z losowego bajtu); teraz: koniec linii + count
      if (chanel & 32) {
        if (wskP + 1 >= m.patdata.size()) {
          st.przecieki++;
          endLine = true;
          break;
        }
        note = m.patdata[wskP++];
        inst = m.patdata[wskP++];
      }
      if (chanel & 64) {
        if (wskP >= m.patdata.size()) {
          st.przecieki++;
          endLine = true;
          break;
        }
        vol = m.patdata[wskP++];
      }
      if (chanel & 128) {
        if (wskP + 1 >= m.patdata.size()) {
          st.przecieki++;
          endLine = true;
          break;
        }
        ef = m.patdata[wskP++];
        par = m.patdata[wskP++];
      }
      chanel = uint8_t(chanel & 31);
      // ---- graj (oryginał :181-228) ----
      if (ef == 1) {
        st.ef1++;
        if (par > 0) speed = par;
      }
      if (note == 254) {
        st.noteOff254++;
        sink.noteOff(int(chanel), false);
      }
      if (note < 254 && MVol < 64) {
        if (chanel >= kMaxChannels) {
          st.chIgnored++; // zapis Vol[9..] byłby poza tablicą (UB w oryginale)
        } else {
          int inst1 = inst;
          if (inst1 > st.maxInsIdx) st.maxInsIdx = inst1;
          if (!inst1)
            st.instZero++;
          else if (inst1 > m.insnum)
            st.instPozaZakres++;
          else {
            st.insUse[inst1]++;
            st.chUse[int(chanel)]++;
          }
          // Vol[chanel] = S3M.vol[inst-1] — bezwarunkowo, jak w oryginale
          ustawVol(int(chanel), VolPamiec{m}[int(inst) - 1]);
          if (vol != 255) ustawVol(int(chanel), vol);
          // ef4 w tym samym evencie ustawia reg40 przed słyszeniem nuty
          if (ef == 4) efekt4(int(chanel));
          if (vol == 0) {
            // PlayNoteFreq + KillNote w tym samym przerwaniu => cisza
            st.killVol0++;
            sink.noteOff(int(chanel), true);
          } else {
            st.noteOn++;
            if ((note & 15) > 11) st.semPonad11++;
            sink.noteOn(int(chanel), (note >> 4) * 12 + (note & 15) + 12,
                        velocity(vvv(int(chanel))), inst1);
          }
        }
      } else if (vol != 255) {
        // note==255: tylko zmiana głośności trwającej nuty
        st.volOnly++;
        if (chanel < kMaxChannels) {
          ustawVol(int(chanel), vol);
          if (ef == 4) efekt4(int(chanel));
          sink.volumeOnly(int(chanel), velocity(vvv(int(chanel))));
        }
      } else if (chanel < kMaxChannels) {
        // sam efekt bez nuty i bez volume (np. sama maska 128 z ef4)
        if (ef == 4) {
          efekt4(int(chanel));
          sink.volumeOnly(int(chanel), velocity(vvv(int(chanel))));
        }
      }
      // efekty wspólny blok (oryginał :213-228)
      if (ef == 2) {
        st.ef2++;
        st.skoki++;
        kon = 1;
        nrRozkazu = int(par);
      }
      if (ef == 3) {
        st.ef3++;
        kon = 1;
        nrRozkazu = 255;
      }
      if (ef > 4) {
        st.efInne++;
        st.efHist[ef]++;
      }
    }
    // oryginał :232-249
    if (kon) {
      linia = 63;
      if (nrRozkazu < 255) rozkaz = nrRozkazu - 1;
      nrRozkazu = 255;
      kon = 0;
    }
    linia++;
    if (linia == 64) {
      linia = 0;
      rozkaz++;
      if (rozkaz >= m.ordnum) {
        st.orderPozaZakres++;
        end = true;
        return;
      }
      if (m.kolejnosc[rozkaz] == 255) {
        // oryginał: rozkaz=0 (pętla nieskończona); tu: koniec po maxpass
        end = true;
        return;
      }
      int pat = m.kolejnosc[rozkaz];
      if (pat >= m.patnum) {
        st.orderPozaZakres++;
        end = true;
        return;
      }
      wskP = m.patoff[pat];
    }
  }
};

// ---------------------------------------------------------------------------
// (3) Render sfizz -> WAV
// ---------------------------------------------------------------------------

struct WavWriter {
  FILE *f = nullptr;
  uint32_t frames = 0;
  bool ok = false;
  float peak = 0.f; // maksimum bezwzgledne (do raportu dBFS po renderze)
  bool open(const std::string &path) {
    f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    uint8_t hdr[44];
    auto u32 = [&](int off, uint32_t v) {
      hdr[off] = uint8_t(v & 255);
      hdr[off + 1] = uint8_t((v >> 8) & 255);
      hdr[off + 2] = uint8_t((v >> 16) & 255);
      hdr[off + 3] = uint8_t((v >> 24) & 255);
    };
    auto u16 = [&](int off, uint32_t v) {
      hdr[off] = uint8_t(v & 255);
      hdr[off + 1] = uint8_t((v >> 8) & 255);
    };
    std::memcpy(hdr, "RIFF", 4);
    u32(4, 36);
    std::memcpy(hdr + 8, "WAVE", 4);
    std::memcpy(hdr + 12, "fmt ", 4);
    u32(16, 16);
    u16(20, 1);
    u16(22, 2);
    u32(24, kSampleRate);
    u32(28, kSampleRate * 4);
    u16(32, 4);
    u16(34, 16);
    std::memcpy(hdr + 36, "data", 4);
    u32(40, 0);
    ok = std::fwrite(hdr, 1, 44, f) == 44;
    return ok;
  }
  void write(const float *L, const float *R, int n) {
    if (!ok) return;
    auto clip = [](float x) { return x < -1.f ? -1.f : (x > 1.f ? 1.f : x); };
    float mx = 0.f;
    for (int i = 0; i < n; i++) {
      float al = fabsf(L[i]);
      float ar = fabsf(R[i]);
      if (al > mx) mx = al;
      if (ar > mx) mx = ar;
      int16_t l = int16_t(std::lround(clip(L[i]) * 32767.0f));
      int16_t r = int16_t(std::lround(clip(R[i]) * 32767.0f));
      uint8_t b[4];
      b[0] = uint8_t(uint16_t(l) & 255);
      b[1] = uint8_t((uint16_t(l) >> 8) & 255);
      if (mx > peak) peak = mx;
      b[2] = uint8_t(uint16_t(r) & 255);
      b[3] = uint8_t((uint16_t(r) >> 8) & 255);
      ok = std::fwrite(b, 1, 4, f) == 4;
      if (!ok) return;
      frames++;
    }
  }
  void close() {
    if (!f) return;
    if (ok) {
      uint32_t dataBytes = frames * 4;
      auto u32 = [&](long off, uint32_t v) {
        uint8_t b[4] = {uint8_t(v & 255), uint8_t((v >> 8) & 255),
                        uint8_t((v >> 16) & 255), uint8_t((v >> 24) & 255)};
        std::fseek(f, off, SEEK_SET);
        std::fwrite(b, 1, 4, f);
      };
      u32(4, 36 + dataBytes);
      u32(40, dataBytes);
    }
    std::fclose(f);
    f = nullptr;
  }
  ~WavWriter() { close(); }
};

// default_path VSCO ma backslashe Windowsowe — normalizujemy cały tekst
std::string wczytaj_sfz(const std::string &path) {
  FILE *f = std::fopen(path.c_str(), "rb");
  if (!f) return {};
  std::string t;
  char buf[4096];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) t.append(buf, n);
  std::fclose(f);
  for (auto &c : t)
    if (c == '\\') c = '/';
  return t;
}

struct ChVoice {
  sfizz_synth_t *syn = nullptr;
  std::string loaded;  // ścieżka załadowanego sfz ("" = nic)
  int curInst = 0;     // bieżący instrument kanału (1-based, 0 = nieznany)
  bool active = false;
  int activeNote = -1; // MIDI aktywnej nuty (lub perc_note)
};

struct Renderer : Sink {
  std::string dirVsco;
  int modulNr = 0;
  std::set<std::string> sfzUzyte;
  long volRetrig = 0;
  long loadCount = 0;
  ChVoice ch[kMaxChannels];

  Renderer(const std::string &vscoDir, int modul, double gainDb)
      : dirVsco(vscoDir), modulNr(modul) {
    for (auto &c : ch) {
      c.syn = sfizz_create_synth();
      sfizz_set_sample_rate(c.syn, kSampleRate);
      sfizz_set_samples_per_block(c.syn, kTickSamples);
      sfizz_set_num_voices(c.syn, 24);
      sfizz_enable_freewheeling(c.syn); // offline render
      sfizz_set_volume(c.syn, float(gainDb));
    }
  }
  ~Renderer() override {
    for (auto &c : ch)
      if (c.syn) sfizz_free(c.syn);
  }

  int percNote(int inst1) const {
    if (inst1 < 1) return 0;
    for (size_t i = 0; i < sizeof(kS3mInstrumentMap) / sizeof(kS3mInstrumentMap[0]); i++)
      if (kS3mInstrumentMap[i].modul == modulNr &&
          kS3mInstrumentMap[i].indeks == inst1)
        return kS3mInstrumentMap[i].perc_note;
    return 0;
  }
  // PERKUSJA NATURALNA: wpis mapy z GM-StylePerc BEZ ustalonego klucza
  // (perc_note=0) -> instrument ma grac originalne nuty na kanale 10
  // (np. "hathyb2" GRAF_011/015: nuty 76/79 = woodblock/shaker w
  // GM-StylePerc; ku temu istniało "szaleństwo" z kluczem 46 hi-hatu)
  bool percNatural(int inst1) const {
    if (inst1 < 1) return false;
    for (size_t i = 0; i < sizeof(kS3mInstrumentMap) / sizeof(kS3mInstrumentMap[0]); i++)
      if (kS3mInstrumentMap[i].modul == modulNr &&
          kS3mInstrumentMap[i].indeks == inst1)
        return kS3mInstrumentMap[i].perc_note == 0 &&
               std::strcmp(kS3mInstrumentMap[i].sfz, SFZ_PERC_FILE) == 0;
    return false;
  }
  const char *sfzPlik(int inst1) const {
    if (inst1 < 1) return nullptr;
    for (size_t i = 0; i < sizeof(kS3mInstrumentMap) / sizeof(kS3mInstrumentMap[0]); i++)
      if (kS3mInstrumentMap[i].modul == modulNr &&
          kS3mInstrumentMap[i].indeks == inst1)
        return kS3mInstrumentMap[i].sfz;
    return nullptr;
  }

  void noteOn(int c, int midi, int vel, int inst1) override {
    if (c < 0 || c >= kMaxChannels) return;
    ChVoice &v = ch[c];
    if (inst1 >= 1 && inst1 <= 100) v.curInst = inst1;
    bool perc = percNote(v.curInst) > 0 || percNatural(v.curInst);
    if (v.curInst >= 1) {
      const char *sfz = perc ? SFZ_PERC_FILE : sfzPlik(v.curInst);
      if (sfz) {
        std::string sciezka = dirVsco + "/" + sfz;
        if (v.loaded != sciezka) {
          std::string tekst = wczytaj_sfz(sciezka);
          loadCount++;
          if (tekst.empty()) {
            std::fprintf(stderr, "  [sfizz] kanał %d: nie można wczytać %s\n", c,
                         sciezka.c_str());
            v.loaded = sciezka; // nie próbuj ponownie
          } else {
            sfizz_all_sound_off(v.syn); // oryginał ucina kanał przed SetInstrum
            if (!sfizz_load_string(v.syn, sciezka.c_str(), tekst.c_str()))
              std::fprintf(stderr, "  [sfizz] kanał %d: błąd ładowania %s\n", c,
                           sciezka.c_str());
            v.loaded = sciezka;
            v.active = false;
            v.activeNote = -1;
            sfzUzyte.insert(sciezka);
          }
        }
      } else if (v.loaded.empty()) {
        std::fprintf(stderr, "  [sfizz] kanał %d: instrument %d bez wpisu mapy "
                             "(nuta cicha)\n", c, inst1);
      }
    }
    // oryginał: StopNote(chanel) przed każdą nową nutą
    if (v.active) {
      sfizz_send_note_off(v.syn, 0, v.activeNote, 0);
      v.active = false;
      v.activeNote = -1;
    }
    int nuta = perc ? (percNote(v.curInst) > 0 ? percNote(v.curInst) : midi)
                    : midi; // naturalna: originalna nuta z modulu
    if (nuta < 0 || nuta > 127) return;
    sfizz_send_note_on(v.syn, 0, nuta, vel);
    v.active = true;
    v.activeNote = nuta;
  }

  void noteOff(int c, bool) override {
    if (c < 0 || c >= kMaxChannels) return;
    ChVoice &v = ch[c];
    if (v.active) {
      sfizz_send_note_off(v.syn, 0, v.activeNote, 0);
      v.active = false;
      v.activeNote = -1;
    }
  }

  void volumeOnly(int c, int vel) override {
    if (c < 0 || c >= kMaxChannels) return;
    ChVoice &v = ch[c];
    // sfizz C API nie ma per-voice gain — przybliżenie: retrigger tej samej
    // nuty z nową velocity (stary voice przechodzi w release); zliczane
    if (v.active) {
      sfizz_send_note_off(v.syn, 0, v.activeNote, 0);
      sfizz_send_note_on(v.syn, 0, v.activeNote, vel);
      volRetrig++;
    }
  }

  void cisz() {
    for (auto &v : ch) {
      if (v.active) sfizz_send_note_off(v.syn, 0, v.activeNote, 0);
      v.active = false;
      v.activeNote = -1;
    }
  }

  void render(Sim &sim, WavWriter &wav, int tailTicks) {
    std::vector<float> L(kTickSamples), R(kTickSamples);
    float tmp[2][kTickSamples];
    auto blok = [&]() {
      std::fill(L.begin(), L.end(), 0.f);
      std::fill(R.begin(), R.end(), 0.f);
      for (auto &v : ch) {
        if (v.loaded.empty() || !v.syn) continue;
        std::fill(tmp[0], tmp[0] + kTickSamples, 0.f);
        std::fill(tmp[1], tmp[1] + kTickSamples, 0.f);
        float *pt[2] = {tmp[0], tmp[1]};
        sfizz_render_block(v.syn, pt, 2, kTickSamples);
        for (int i = 0; i < kTickSamples; i++) {
          L[i] += tmp[0][i];
          R[i] += tmp[1][i];
        }
      }
      wav.write(L.data(), R.data(), kTickSamples);
      if (!wav.ok) std::fprintf(stderr, "  [wav] błąd zapisu\n");
    };
    while (!sim.end) {
      sim.tick(); // eventy tej linii wysłane z delay=0 bloku
      blok();
      if (!wav.ok) return;
    }
    cisz();
    for (int t = 0; t < tailTicks && wav.ok; t++) blok();
  }
};

// ---------------------------------------------------------------------------
// (2b) Dump eventów symulatora do Standard MIDI File (format 1) — odsłuch MT32
// ---------------------------------------------------------------------------
// Ten sam strumień eventów co tor VSCO (Sink -> Sim), zapisany jako SMF:
//   * 9 kanałów OPL (0-8) -> kanały MIDI 0-8 (perkusja -> kanał 9, bank 128);
//   * nuta: MIDI = oktawa*12 + półton + 12 (identyczna konwersja jak w
//     Sim::playOneLine noteOn, linia 413);
//   * czas: 1 tick symulatora = 20 ms (timer 50 Hz, linia co `speed` ticków);
//     SMF: PPQ = 24, tempo 480000 us/kwarta (kwadrans 480 ms) =>
//     1 miara MIDI = dokładnie 20 ms (bez zaokrągleń tempo);
//   * program GM wg kS3mInstrumentMap (game-linux/port/sfz_map.h):
//     melodyczny -> CC0 bank 0 + Program Change gm; perkusyjny -> kanał 9
//     z nutą perc_note (jak Renderer::noteOn); brak wpisu mapy -> PC 0 + log.
// Renderer (sfizz) zostaje nietknięty — to dostawkowa classa Sink, wywoływana
// osobnym przebiegiem Sim w main() (--dump-midi).
struct MidiEv {
  uint32_t tick = 0; // bezwzględny tick 50 Hz (20 ms)
  uint8_t n = 0;     // liczba bajtów zdarzenia (bez czasu trwania)
  uint8_t b[4] = {};
  MidiEv(uint32_t t, uint8_t nn, uint8_t b0 = 0, uint8_t b1 = 0,
         uint8_t b2 = 0, uint8_t b3 = 0) {
    tick = t;
    n = nn;
    b[0] = b0;
    b[1] = b1;
    b[2] = b2;
    b[3] = b3;
  }
};

struct MidiSink : Sink {
  int modulNr = 0;
  uint32_t tick = 0; // ustawiane przez wywołującego przed każdym sim.tick()
  std::vector<MidiEv> evs;
  int prog[16];  // wydany program GM na kanale MIDI (0-9); -1 = jeszcze nie
  int lastCh[9]; // kanał MIDI ostatniej nuty kanału OPL (9 = perkusja)
  int lastNote[9];
  bool have[9]; // trwa nuta na kanale OPL?
  long nProg = 0, nOn = 0, nOff = 0, nCc = 0, nMissing = 0;

  explicit MidiSink(int modul) : modulNr(modul) {
    for (int i = 0; i < 16; i++)
      prog[i] = -1;
    for (int i = 0; i < 9; i++) {
      lastCh[i] = i;
      lastNote[i] = 0;
      have[i] = false;
    }
  }

  void setTick(uint32_t t) { tick = t; }

  const S3mInsMap *findIns(int inst1) const {
    if (inst1 < 1) return nullptr;
    for (size_t i = 0; i < sizeof(kS3mInstrumentMap) / sizeof(kS3mInstrumentMap[0]); i++)
      if (kS3mInstrumentMap[i].modul == modulNr &&
          kS3mInstrumentMap[i].indeks == inst1)
        return &kS3mInstrumentMap[i];
    return nullptr;
  }

  void programChange(int ch, int gm) {
    if (prog[ch] == gm)
      return;
    if (prog[ch] < 0)
      evs.emplace_back(tick, 3, uint8_t(0xB0 | ch), 0, 0); // CC0: bank 0
    evs.emplace_back(tick, 2, uint8_t(0xC0 | ch), uint8_t(gm & 127), 0);
    prog[ch] = gm;
    nProg++;
  }

  void noteOn(int c, int midi, int vel, int inst1) override {
    if (c < 0 || c >= kMaxChannels)
      return;
    int note = midi;
    int tch = c;
    if (inst1 >= 1) {
      const S3mInsMap *e = findIns(inst1);
      if (e) {
        if (e->perc_note > 0) {
          tch = 9; // perkusja GM -> kanał 10 (0-based 9), nuta perc_note
          note = e->perc_note;
        } else if (e->gm >= 0) {
          programChange(c, e->gm);
        }
      } else {
        nMissing++;
        std::fprintf(stderr, "  [midi] instrument %d bez wpisu mapy (kanał %d) "
                             "- PC 0 (dźwięk może nie pasować)\n",
                     inst1, c);
        programChange(c, 0);
      }
    } else if (prog[c] < 0) {
      programChange(c, 0); // inst1==0 (niewykryte) -> domyślny program
    }
    if (note < 0 || note > 127)
      return;
    // PORT (naprawa wiszących nut): oryginał gra MONO na kanał — przed każdą
    // nową nutą wykonuje StopNote(chanel) (tak robi Renderer::noteOn ->
    // sfizz_send_note_off). SMF był tego pozbawiony: GM jest poliglota, nuty
    // są rozdzielne, więc poprzednia nuta WISAŁA do note-off 254/vol==0 lub
    // aż do EOF (fluidsynth: "not all notes have received a note off" ->
    // słyszalne zawieszki w ogonie). Wysyłamy note-off przed nową nutą.
    if (have[c]) {
      evs.emplace_back(tick, 3, uint8_t(0x80 | lastCh[c]), uint8_t(lastNote[c]),
                       0);
      have[c] = false;
      nOff++;
    }
    // PORT (moc nut): oryginał skalował głośność OPL (vol 0..63, MVol=8),
    // a surowe vvv po konwersji dawało nuty 1..30 -> -30..-60 dB = "sparse
    // cicho, jeden instrument głośny". Band 30..127: dynamika zostaje,
    // dół nie pada do ciszy.
    int vv = 30 + (vel * 97) / 127;
    if (vv < 1) vv = 1;
    if (vv > 127) vv = 127;
    evs.emplace_back(tick, 3, uint8_t(0x90 | tch), uint8_t(note), uint8_t(vv));
    lastCh[c] = tch;
    lastNote[c] = note;
    have[c] = true;
    nOn++;
  }

  void noteOff(int c, bool) override {
    if (c < 0 || c >= kMaxChannels || !have[c])
      return;
    evs.emplace_back(tick, 3, uint8_t(0x80 | lastCh[c]), uint8_t(lastNote[c]),
                     0);
    have[c] = false;
    nOff++;
  }

  void volumeOnly(int c, int vel) override {
    if (c < 0 || c >= kMaxChannels)
      return;
    // PORT (moc nut, cz. 2): CC7 = surowe vvv dawało kanały 1..30 (fluid
    // interpretuje CC7 40log10(v/127): 1..30 = ok. -52..-25 dB -> "większość
    // cicho"). Band 50..90: najniższy kanał -8 dB (słyszalny), najwyższy
    // -3 dB; dynamika między kanałami zostaje.
    int cc = 50 + (vel * 40) / 127;
    evs.emplace_back(tick, 3, uint8_t(0xB0 | c), 7, uint8_t(cc));
    nCc++;
  }

  void close(const std::string &path, uint32_t endTick) {
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) {
      std::fprintf(stderr, "nie można otworzyć %s do zapisu\n", path.c_str());
      return;
    }
    auto wr = [&](const void *d, size_t n) { std::fwrite(d, 1, n, f); };
    auto be32 = [&](uint32_t v) {
      uint8_t b[4] = {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8),
                      uint8_t(v)};
      wr(b, 4);
    };
    auto be16 = [&](uint16_t v) {
      uint8_t b[2] = {uint8_t(v >> 8), uint8_t(v)};
      wr(b, 2);
    };
    // nagłówek: format 1, 2 ścieżki, 24 PPQ (1 miara = 20 ms przy tempie 480000)
    wr("MThd", 4);
    be32(6);
    be16(1);
    be16(2);
    be16(24);
    // ścieżka 0: tempo 480000 us/kwarta (ćwierćnuta = 480 ms), 4/4, EOT
    static const uint8_t t0[] = {
        0x00, 0xFF, 0x51, 0x03, 0x07, 0x53, 0x00, // tempo meta
        0x00, 0xFF, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08, // sygnatura 4/4
        0x00, 0xFF, 0x2F, 0x00};                   // end of track
    wr("MTrk", 4);
    be32(uint32_t(sizeof(t0)));
    wr(t0, sizeof(t0));
    // ścieżka 1: eventy (delta w miarach MIDI = tick 50 Hz)
    std::vector<uint8_t> body;
    uint32_t last = 0;
    for (auto &e : evs) {
      uint32_t d = e.tick >= last ? e.tick - last : 0;
      // VLQ na body
      if (d == 0)
        body.push_back(0);
      else {
        uint8_t b[4] = {};
        int n = 0;
        while (d) {
          b[n++] = uint8_t(d & 127);
          d >>= 7;
        }
        for (int i = n - 1; i >= 0; i--)
          body.push_back(i ? (b[i] | 0x80) : b[i]);
      }
      last = e.tick;
      for (int i = 0; i < e.n; i++)
        body.push_back(e.b[i]);
    }
    // PORT (naprawa wiszących nut, cz. 2): utwór się kończy, a nuty jeszcze
    // wiszą (oryginał gra mono — ostatni noteOn nie dostał noteOff; utwory z
    // pętlami/pattern-breakami typowo urywają się nutą). Dopisujemy note-off
    // na TYM SAMYM ticku co ostatnie zdarzenie — zamiast oddawać koniec do
    // auto-OFF w playerze (fluidsynth ostrzega "not all notes have received a
    // note off" o wiszące nuty i tnie je EOT; dzięki temu 2-sekundowy tail
    // robi naturalny ring-out release, jak w torze sfizz).
    for (int c = 0; c < 9; c++) {
      if (!have[c])
        continue;
      body.push_back(0); // delta 0 (ten sam tick co ostatnie zdarzenie)
      body.push_back(uint8_t(0x80 | lastCh[c]));
      body.push_back(uint8_t(lastNote[c]));
      body.push_back(0x00); // velocity note-off: nieużywane
      have[c] = false;
      nOff++;
    }
    uint32_t tail = endTick > last ? endTick - last : 0;
    if (tail == 0)
      body.push_back(0);
    else {
      uint8_t b[4] = {};
      int n = 0;
      while (tail) {
        b[n++] = uint8_t(tail & 127);
        tail >>= 7;
      }
      for (int i = n - 1; i >= 0; i--)
        body.push_back(i ? (b[i] | 0x80) : b[i]);
    }
    body.push_back(0xFF);
    body.push_back(0x2F);
    body.push_back(0x00);
    wr("MTrk", 4);
    be32(uint32_t(body.size()));
    wr(body.data(), body.size());
    std::fclose(f);
  }
};

} // namespace

// ---------------------------------------------------------------------------
// main — log + tryby
// ---------------------------------------------------------------------------

static void wypiszUwagi(const S3mMod &m) {
  for (auto &u : m.uwagi) std::printf("  [parser] %s\n", u.c_str());
}

int main(int argc, char **argv) {
  std::string plik, out, vscoDir = "/home/k/Projects/polanie-src/vsco";
  std::string midiOut; // --dump-midi out.mid: SMF zamiast renderu sfizz
  bool dump = false;
  int maxpass = 1, tailS = 3, mvol = 8;
  double gainDb = 0.0;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto nast = [&](int &v) { v = std::atoi(argv[++i]); };
    if (a == "--dump") dump = true;
    else if (a == "--dump-midi" && i + 1 < argc) midiOut = argv[++i];
    else if (a == "-o" && i + 1 < argc) out = argv[++i];
    else if (a == "--maxpass") nast(maxpass);
    else if (a == "--tail") nast(tailS);
    else if (a == "--mvol") nast(mvol);
    else if (a == "--vol-gain" || a == "--gain-db")
      gainDb = std::atof(argv[++i]); // aliasy: ta sama wartosc w dB
    else if (a == "--vsco" && i + 1 < argc) vscoDir = argv[++i];
    else if (a == "-h" || a == "--help") {
      std::printf("s3m_vsco_render [--dump] [--dump-midi out.mid] plik.s3m "
                  "[-o out.wav] [--maxpass N] [--tail S] [--mvol N] "
                  "[--vol-gain dB] [--vsco DIR]\n"
                  "  --vol-gain / --gain-db: wzmocnienie toru VSCO w dB "
                  "(ujemne = ciszej; do zrownywania trzech torow\n"
                  "  VSCO/MT32/OPL w skryptach scripts/audio/)\n");
      return 0;
    } else plik = a;
  }
  if (plik.empty()) {
    std::fprintf(stderr, "brak pliku .s3m (s3m_vsco_render --help)\n");
    return 2;
  }

  int modulNr = 0;
  {
    auto pos = plik.find("GRAF_");
    if (pos != std::string::npos && plik.size() >= pos + 8)
      modulNr = std::atoi(plik.substr(pos + 5, 3).c_str());
  }

  S3mMod m;
  if (!load_s3m(plik, m)) {
    std::fprintf(stderr, "błąd wczytywania %s\n", plik.c_str());
    wypiszUwagi(m);
    return 1;
  }
  std::printf("== %s \"%s\": ord=%d ins=%d pat=%d | gvol=%d speed=%d tempo=%d "
              "mvol=%d | scrm=%d | dane paternów %zu B\n",
              plik.c_str(), m.tytul.c_str(), m.ordnum, m.insnum, m.patnum,
              m.gvol, m.speed, m.tempo, m.mvol, int(m.scrm), m.patdata.size());
  wypiszUwagi(m);

  // PORT: moduły NIE-OPL (instrumenty ty: typ!=2 / brak "SCRI", np. GRAF_002
  // „menu - end ver." z próbkami PCM) — sekwencja (ordery/patterny) jest NADAL
  // standardowym S3M, więc symulacja i render przez soundfont działają; tylko
  // brzmienie wyprowadzone z próbek soundfontu zamiast chipu. Ostrzegamy, NIE
  // pomijamy (to nie błąd — do odsłuchu toru vsco/mt32 dokładnie o to chodzi).
  {
    bool okl = true;
    for (auto &in : m.ins)
      if (in.typ != 2 || std::memcmp(in.sig, "SCRI", 4) != 0)
        okl = false;
    if (!okl)
      std::printf("  [parser] moduł z instrumentami NIE-OPL (typ!=2 / brak "
                  "SCRI): render przez soundfont po mapie nazw — symulacja "
                  "sekwencji przebiega normalnie\n");
  }

  struct NullSink : Sink {
    void noteOn(int, int, int, int) override {}
    void noteOff(int, bool) override {}
    void volumeOnly(int, int) override {}
  };
  NullSink nullSink;
  Sim simLog(m, nullSink, mvol, maxpass);
  while (!simLog.end) simLog.tick();
  const SimStat &st = simLog.st;
  double czas = double(st.tickCount) / 50.0;
  std::printf("  symulacja: %.2f s | wierszy %ld | note_on %ld, off254 %ld, "
              "vol-only %ld, kill(vol0) %ld | ef1 %ld ef2 %ld ef3 %ld ef4 %ld "
              "inne %ld | kanał max %d ignorowane %ld | inst>insN %ld inst=0 "
              "%ld | sem>11 %ld | vol-underflow %ld | skoki %ld | przecieki "
              "bufora %ld | limit %s\n",
              czas, simLog.rows, st.noteOn, st.noteOff254, st.volOnly, st.killVol0,
              st.ef1, st.ef2, st.ef3, st.ef4, st.efInne, st.maxCh, st.chIgnored,
              st.instPozaZakres, st.instZero, st.semPonad11, st.volUnderflow,
              st.skoki, st.przecieki, simLog.limitHit ? "TAK" : "nie");
  if (simLog.limitHit)
    std::printf("  [limit] zapętlenie orderów (ef2-skok) — symulacja odcięta po "
                "%ld liniach (--maxpass %d; zwiększ maxpass żeby wyrenderować "
                "więcej pasa)\n",
                simLog.rows, maxpass);
  if (!st.efHist.empty()) {
    std::printf("  [parser] nieobsłużone efekty (oryginał je ignoruje):");
    for (auto &e : st.efHist) std::printf(" ef%d=%ld", e.first, e.second);
    std::printf("\n");
  }
  std::printf("  kanały użyte:");
  for (auto &c : st.chUse) std::printf(" ch%d=%ld", c.first, c.second);
  std::printf("\n  instrumenty użyte:");
  for (auto &i : st.insUse) std::printf(" ins%d=%ld", i.first, i.second);
  std::printf("\n");

  // rozpiska mapowania instrumentów (pełna lista z nagłówka modułu)
  std::printf("  mapa: modul=%d, wpisów mapy dla modułu=%d\n", modulNr,
              count_s3m_instruments(modulNr));
  for (int i = 0; i < m.insnum; i++) {
    const S3mIns &in = m.ins[i];
    const char *sfz = nullptr;
    int percNote = 0, niepewna = -1;
    for (size_t j = 0; j < sizeof(kS3mInstrumentMap) / sizeof(kS3mInstrumentMap[0]); j++) {
      if (kS3mInstrumentMap[j].modul == modulNr &&
          kS3mInstrumentMap[j].indeks == i + 1) {
        sfz = kS3mInstrumentMap[j].sfz;
        percNote = kS3mInstrumentMap[j].perc_note;
        niepewna = kS3mInstrumentMap[j].niepewna;
        break;
      }
    }
    std::string percStr = percNote > 0 ? " (perc GM " + std::to_string(percNote) + ")"
                                       : std::string();
    std::printf("    ins %2d: \"%s\" (plik \"%s\", vol %d@+28/%d@+27, dsk %d, "
                "hz %d@+32/%d@+30, sig %.4s) -> %s%s\n",
                i + 1, in.nazwa.c_str(), in.plik.c_str(), in.vol, in.vol27,
                in.dsk, in.hz, in.hz30, in.sig, sfz ? sfz : "[BRAK W MAPIE]",
                percStr.c_str());
    if (niepewna == 1)
      std::printf("      UNCERTAIN: mapowanie po znaczeniu nazwy — do weryfikacji na słuch\n");
  }

  if (!midiOut.empty()) {
    // świeży przebieg symulatora z MidiSink (identyczny strumień eventów,
    // który poszedłby do sfizz; Renderer nie jest budowany/ładowany)
    MidiSink msink(modulNr);
    Sim simM(m, msink, mvol, maxpass);
    long tk = 0;
    while (!simM.end) {
      msink.setTick(uint32_t(tk)); // 1 tick = 20 ms (timer 50 Hz jak w grze)
      simM.tick();
      tk++;
    }
    msink.close(midiOut, uint32_t(tk) + uint32_t(tailS * 50));
    std::printf("  --dump-midi: %s | %zu eventów (prog %ld, on %ld, off %ld, "
                "cc %ld, bez-wpisu %ld) | %.2f s + %d s ogonu | SMF1, PPQ 24, "
                "1 miara = 20 ms\n",
                midiOut.c_str(), msink.evs.size(), msink.nProg, msink.nOn,
                msink.nOff, msink.nCc, msink.nMissing,
                double(tk) / 50.0, tailS);
    return 0;
  }

  if (dump) {
    std::printf("  tryb --dump: render pominięty\n");
    return 0;
  }

  Renderer rend(vscoDir, modulNr, gainDb);
  if (out.empty()) {
    auto pos = plik.rfind('/');
    std::string base = pos == std::string::npos ? plik : plik.substr(pos + 1);
    base = base.substr(0, base.find('.'));
    out = base + ".vsco.wav";
  }
  WavWriter wav;
  if (!wav.open(out)) {
    std::fprintf(stderr, "nie można otworzyć %s do zapisu\n", out.c_str());
    return 1;
  }
  Sim simR(m, rend, mvol, maxpass); // świeży przebieg z eventami do sfizz
  rend.render(simR, wav, tailS * 50);
  wav.close();
  std::printf("  render: %s | %.2f s | sfz załadowane: %zu (loadów %ld) | "
              "retrig volume %ld\n",
              out.c_str(), double(wav.frames) / kSampleRate, rend.sfzUzyte.size(),
              rend.loadCount, rend.volRetrig);
  // peak (dBFS) do zrównania trzech torów w skryptach (VSCO/MT32/OPL):
  // wartościa porównywalną jest 20*log10(peak) - nie mocna subiektywna
  {
    float p = wav.peak > 0.f ? wav.peak : 1e-9f;
    std::printf("  peak: %+.1f dBFS (maks %.3f)\n",
                20.0 * std::log10((double)p), (double)p);
  }
  if (simR.limitHit)
    std::printf("  [render] WAV przycięty na limicie --maxpass %d (zapętlenie "
                "orderów — patrz log symulacji)\n", maxpass);
  for (auto &s : rend.sfzUzyte) std::printf("    sfz: %s\n", s.c_str());
  return 0;
}