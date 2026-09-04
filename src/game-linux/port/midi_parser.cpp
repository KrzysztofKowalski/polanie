// PORT: implementacja midi_parser.h - patrz komentarze w naglowku.
#include "midi_parser.h"

#include <algorithm>
#include <cstring>

namespace polmidi {

namespace {

inline uint16_t be16(const unsigned char *p) {
  return (uint16_t)((p[0] << 8) | p[1]);
}
inline uint32_t be32(const unsigned char *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | p[3];
}

// Jednoeventowy status kanalowy (0x80..0xEF): ile bajtow danych.
inline int data_bytes(uint8_t status) {
  switch (status & 0xF0) {
  case 0xC0:
  case 0xD0:
    return 1;
  default:
    return 2;
  }
}

struct TempoPoint {
  uint32_t tick;
  double us_per_qn;
};

} // namespace

size_t read_varlen(const unsigned char *d, size_t len, uint32_t *value) {
  uint32_t v = 0;
  size_t i = 0;
  for (; i < len && i < 4; i++) {
    v = (v << 7) | (d[i] & 0x7F);
    if (!(d[i] & 0x80)) {
      *value = v;
      return i + 1;
    }
  }
  return 0; // varlen > 4 bajty albo koniec bufora
}

bool parse_smf(const unsigned char *data, size_t len, MidiSong &out) {
  out = MidiSong();
  if (!data || len < 14) {
    out.error = "plik za krotki na naglowek MThd";
    return false;
  }
  if (memcmp(data, "MThd", 4) != 0) {
    out.error = "brak sygnatury MThd (to nie jest plik .mid)";
    return false;
  }
  uint32_t hdr_len = be32(data + 4);
  if (hdr_len < 6 || 8 + hdr_len > len) {
    out.error = "uszkodzony naglowek MThd";
    return false;
  }
  out.format = be16(data + 8);
  uint16_t ntrks = be16(data + 10);
  out.division = be16(data + 12);
  if (out.format > 2) {
    out.error = "nieobslugiwany format SMF (>2)";
    return false;
  }

  double sec_per_tick = 1.0 / (384.0 * 2.0); // default 120 BPM (SMPTE)
  if (out.division & 0x8000) {
    // SMPTE: wysoki bajt = ujemne fps (komplement dwojkowy), niski = tick/klatke
    int fps = 256 - (int)(out.division >> 8); // np. 0xE7 -> 25
    int tpf = out.division & 0xFF;
    if (fps <= 0 || tpf == 0) {
      out.error = "bledny dzielnik SMPTE";
      return false;
    }
    out.smpte = true;
    out.ticks_per_qn = 0;
    sec_per_tick = 1.0 / ((double)fps * (double)tpf);
  } else {
    if (out.division == 0) {
      out.error = "dzielnik 0 (ticks per quarter note)";
      return false;
    }
    out.ticks_per_qn = out.division;
  }

  // --- przejscie po chunkach ---
  std::vector<MidiEvent> evs;
  evs.reserve(1024);
  std::vector<TempoPoint> tempos; // meta 0x51 (do przeliczenia na sekundy)
  size_t off = 8 + hdr_len;
  int tracks = 0;

  while (off + 8 <= len && tracks < ntrks) {
    uint32_t chunk_len = be32(data + off + 4);
    if (memcmp(data + off, "MTrk", 4) != 0) {
      // nieznany chunk - SMF na to pozwala, pomijamy
      off += 8 + (size_t)chunk_len;
      continue;
    }
    off += 8;
    if (off + chunk_len > len) {
      out.error = "uciecie pliku w srodku sciezki MTrk";
      return false;
    }
    const unsigned char *t = data + off;
    size_t tlen = chunk_len;
    size_t p = 0;
    uint32_t tick = 0;
    uint8_t running = 0;
    tracks++;

    while (p < tlen) {
      uint32_t delta;
      size_t n = read_varlen(t + p, tlen - p, &delta);
      if (!n) {
        out.error = "bledny delta-time w sciezce";
        return false;
      }
      p += n;
      tick += delta;
      if (p >= tlen) {
        out.error = "koniec sciezki po delta-time";
        return false;
      }
      uint8_t status = t[p];
      if (!(status & 0x80)) {
        if (!running) {
          out.error = "dane bez statusu (brak running status)";
          return false;
        }
        status = running;
      } else {
        p++;
        if (status < 0xF0)
          running = status;
        else
          running = 0; // sysex/meta kasuja running status
      }

      if (status == 0xFF) { // meta
        if (p >= tlen) {
          out.error = "uciete meta-event";
          return false;
        }
        uint8_t type = t[p++];
        uint32_t mlen;
        n = read_varlen(t + p, tlen - p, &mlen);
        if (!n || p + n + mlen > tlen) {
          out.error = "bledna dlugosc meta-event";
          return false;
        }
        p += n;
        if (type == 0x51 && mlen == 3 && !out.smpte) {
          double us = ((uint32_t)t[p] << 16) | ((uint32_t)t[p + 1] << 8) |
                      t[p + 2];
          tempos.push_back({tick, us > 0 ? us : 500000.0});
        }
        // 0x2F (koniec sciezki) i reszta meta - pomijane
        p += mlen;
        continue;
      }
      if (status == 0xF0 || status == 0xF7) { // sysex - pomijamy
        uint32_t slen;
        n = read_varlen(t + p, tlen - p, &slen);
        if (!n || p + n + slen > tlen) {
          out.error = "bledny sysex";
          return false;
        }
        p += n + slen;
        continue;
      }
      if (status < 0x80) {
        out.error = "niepoprawny bajt statusu";
        return false;
      }

      int nd = data_bytes(status);
      if (p + (size_t)nd > tlen) {
        out.error = "uciete dane eventu";
        return false;
      }
      MidiEvent e;
      e.tick = tick;
      e.seconds = 0.0; // wypelniamy po zebraniu mapy tempa
      e.channel = status & 0x0F;
      e.pitch = 0;
      uint8_t a = (uint8_t)(t[p] & 0x7F);
      uint8_t b = (uint8_t)(nd == 2 ? (t[p + 1] & 0x7F) : 0);
      p += (size_t)nd;
      switch (status & 0xF0) {
      case 0x80:
        e.type = MIDIEV_NOTE_OFF;
        e.a = a;
        e.b = b;
        break;
      case 0x90:
        // NOTE_ON z velocity 0 = NOTE_OFF (konwencja SMF)
        e.type = b == 0 ? MIDIEV_NOTE_OFF : MIDIEV_NOTE_ON;
        e.a = a;
        e.b = b;
        break;
      case 0xA0: // poly aftertouch - ignorowany
        continue;
      case 0xB0:
        e.type = MIDIEV_CC;
        e.a = a;
        e.b = b;
        break;
      case 0xC0:
        e.type = MIDIEV_PROGRAM;
        e.a = a;
        e.b = 0;
        break;
      case 0xD0: // channel pressure - ignorowany
        continue;
      default: // 0xE0 pitch bend
        e.type = MIDIEV_PITCH_BEND;
        e.pitch = ((int)b << 7 | (int)a) - 8192; // -8192..8191, 0 = srodek
        break;
      }
      evs.push_back(e);
    }
    off += chunk_len;
  }
  if (tracks != ntrks) {
    out.error = "za malo sciezek MTrk (naglowek obiecuje wiecej)";
    return false;
  }

  // --- mapa tempa -> sekundy (zdarzenia posortowane po ticku) ---
  std::sort(evs.begin(), evs.end(),
            [](const MidiEvent &x, const MidiEvent &y) { return x.tick < y.tick; });
  if (!out.smpte) {
    std::sort(tempos.begin(), tempos.end(),
              [](const TempoPoint &x, const TempoPoint &y) {
                return x.tick < y.tick;
              });
    // przed pierwszym 0x51 obowiazuje default 120 BPM (500000 us / cwiercnute)
    if (tempos.empty() || tempos.front().tick > 0)
      tempos.insert(tempos.begin(), {0, 500000.0});
    // segmentowo: kazdy punkt tempa wylacza sie W SWOIM ticku (interwal
    // [tick_i, tick_{i+1}) liczymy tempem tempos[i])
    size_t tp = 0;
    uint32_t tick_prev = 0;
    double sec_prev = 0.0;
    for (auto &e : evs) {
      while (tp + 1 < tempos.size() && tempos[tp + 1].tick <= e.tick) {
        double spq_seg =
            tempos[tp].us_per_qn / 1e6 / (double)out.division;
        sec_prev += (double)(tempos[tp + 1].tick - tick_prev) * spq_seg;
        tick_prev = tempos[tp + 1].tick;
        tp++;
      }
      double spq = tempos[tp].us_per_qn / 1e6 / (double)out.division;
      sec_prev += (double)(e.tick - tick_prev) * spq;
      tick_prev = e.tick;
      e.seconds = sec_prev;
    }
  } else {
    for (auto &e : evs)
      e.seconds = (double)e.tick * sec_per_tick;
  }

  out.events = std::move(evs);
  out.length = out.events.empty() ? 0.0 : out.events.back().seconds;
  out.ok = true;
  return true;
}

} // namespace polmidi