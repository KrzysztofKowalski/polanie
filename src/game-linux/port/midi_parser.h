// PORT: parser Standard MIDI File (SMF) dla toru MIDI+sfizz (dodatkowe utwory
// MIDI, katalog z POLANIE_MIDI). Czysty C++ (C++17), zero zaleznosci - moze byc
// linkowany w testach gtest bez SDL. Zakres zgodny z zadaniem: format 0/1,
// delta-time + running status, meta 0x51 (tempo) i 0x2F (koniec), sysex
// pomijany, pitch bend, control change (7/11/64/120/121 i pozostale - parser
// przekazuje wszystkie CC, selekcji robi silnik/sfizz).
//
// Wyjscie: MidiSong ze zdarzeniami posortowanymi po czasie absolutnym
// (tick) oraz z przeliczonym czasem w sekundach (tempo z meta 0x51; tempo
// domyslne 120 BPM przed pierwszym 0x51, jak w SMF). SMPTE (bit 15 dzielnika)
// obsluzone: sekundy na tick = 1/(fps*tpf), tempo meta ignorowane.
#ifndef POL_MIDI_PARSER_H
#define POL_MIDI_PARSER_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace polmidi {

enum MidiEvType {
  MIDIEV_NOTE_OFF = 0,
  MIDIEV_NOTE_ON = 1,
  MIDIEV_CC = 2,
  MIDIEV_PROGRAM = 3,
  MIDIEV_PITCH_BEND = 4,
};

struct MidiEvent {
  uint32_t tick;    // czas absolutny w tickach
  double seconds;   // czas absolutny w sekundach (wg mapy tempa)
  uint8_t type;     // MidiEvType
  uint8_t channel;  // 0-15 (10 = perkusja GM)
  uint8_t a;        // nuta / kontroler
  uint8_t b;        // velocity / wartosc CC
  int pitch;        // tylko PITCH_BEND: -8192..8191 (0 = srodek)
};

struct MidiSong {
  bool ok = false;
  std::string error;
  uint16_t format = 0;       // 0/1/2
  uint16_t division = 0;     // tickow na cwiercnute (albo SMPTE)
  bool smpte = false;        // dzielnik w formacie SMPTE
  int ticks_per_qn = 384;    // dla SMPTE: 0
  double length = 0.0;       // czas ostatniego zdarzenia [s]
  std::vector<MidiEvent> events; // posortowane po (tick, kolejnosc w pliku)
};

// Parsuje SMF z pamieci. Przy bledzie ok=false + opis w error (po polsku).
bool parse_smf(const unsigned char *data, size_t len, MidiSong &out);

// Varlen (VLQ SMF) - pomocnicze, publiczne dla testow. Zwraca liczbe
// zjedzonych bajtow albo 0 przy bledzie/przekroczeniu bufora.
size_t read_varlen(const unsigned char *d, size_t len, uint32_t *value);

} // namespace polmidi

#endif // POL_MIDI_PARSER_H