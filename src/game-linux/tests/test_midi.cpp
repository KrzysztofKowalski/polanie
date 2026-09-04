// PORT: testy toru MIDI+sfizz - parser SMF (port/midi_parser.{h,cpp}),
// mapa utworow CD -> .mid (port/midi_map.h), szukanie plikow .mid
// (port/midi_map_files.{h,cpp}, przez $POLANIE_MIDI) i parsowanie
// --audioType (port/audio_opts.h).
#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "audio_opts.h"
#include "midi_map.h"
#include "midi_map_files.h"
#include "midi_parser.h"
#include "test_util.h"

using polmidi::MidiEvent;
using polmidi::MidiSong;

namespace {

// ---------- budowanie testowych SMF ----------
void push_be32(std::vector<unsigned char> &v, uint32_t x) {
  v.push_back((x >> 24) & 0xFF);
  v.push_back((x >> 16) & 0xFF);
  v.push_back((x >> 8) & 0xFF);
  v.push_back(x & 0xFF);
}
void push_be16(std::vector<unsigned char> &v, uint16_t x) {
  v.push_back((x >> 8) & 0xFF);
  v.push_back(x & 0xFF);
}
void push_varlen(std::vector<unsigned char> &v, uint32_t x) {
  // najprostszy wariant: < 0x80*4 (testy nie sa subtelne)
  if (x >= 0x200000) {
    v.push_back(0x80 | ((x >> 21) & 0x7F));
    x &= 0x1FFFFF;
  }
  if (x >= 0x4000) {
    v.push_back(0x80 | ((x >> 14) & 0x7F));
    x &= 0x3FFF;
  }
  if (x >= 0x80) {
    v.push_back(0x80 | ((x >> 7) & 0x7F));
    x &= 0x7F;
  }
  v.push_back((unsigned char)x);
}
void push_chunk(std::vector<unsigned char> &v, const char *id,
                const std::vector<unsigned char> &body) {
  v.insert(v.end(), id, id + 4);
  push_be32(v, (uint32_t)body.size());
  v.insert(v.end(), body.begin(), body.end());
}

std::vector<unsigned char> make_header(uint16_t format, uint16_t ntrks,
                                       uint16_t division) {
  std::vector<unsigned char> v;
  push_chunk(v, "MThd", {0, 0, (unsigned char)(format),
                         (unsigned char)(ntrks >> 8), (unsigned char)ntrks,
                         (unsigned char)(division >> 8),
                         (unsigned char)division});
  // prostsze: recznie (chunk z 6 bajtami)
  v.clear();
  v.insert(v.end(), {'M', 'T', 'h', 'd'});
  push_be32(v, 6);
  push_be16(v, format);
  push_be16(v, ntrks);
  push_be16(v, division);
  return v;
}

} // namespace

// ---------------------------------------------------------------- parser ---
TEST(ParserSMF, Format0_Podstawowy) {
  std::vector<unsigned char> m = make_header(0, 1, 384);
  std::vector<unsigned char> tr;
  push_varlen(tr, 0);
  tr.insert(tr.end(), {0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20}); // 500000 us = 120 BPM
  push_varlen(tr, 0);
  tr.insert(tr.end(), {0xC0, 42});              // program 42 na kanal 1
  push_varlen(tr, 0);
  tr.insert(tr.end(), {0x90, 60, 100});         // note on
  push_varlen(tr, 384);                          // 0,5 s pozniej (120 BPM)
  tr.insert(tr.end(), {0x91, 64, 90});          // inny kanal (running off)
  push_varlen(tr, 384);
  tr.insert(tr.end(), {0x80, 60, 64});          // note off
  push_varlen(tr, 192);
  tr.insert(tr.end(), {0xB0, 7, 100});          // CC7
  push_varlen(tr, 0);
  tr.insert(tr.end(), {0xE0, 0, 96});           // pitch bend lekko w gore
  push_varlen(tr, 0);
  tr.insert(tr.end(), {0xFF, 0x2F, 0x00});      // koniec
  push_chunk(m, "MTrk", tr);

  MidiSong s;
  ASSERT_TRUE(polmidi::parse_smf(m.data(), m.size(), s)) << s.error;
  EXPECT_EQ(s.ticks_per_qn, 384);
  // zdarzenia: program, 2x note on, note off, CC, pitch bend (note on z
  // velocity 0 nie wystepuje; note off velocity>0)
  ASSERT_EQ(s.events.size(), 6u);
  EXPECT_EQ(s.events[0].type, polmidi::MIDIEV_PROGRAM);
  EXPECT_EQ(s.events[0].a, 42);
  EXPECT_EQ(s.events[1].type, polmidi::MIDIEV_NOTE_ON);
  EXPECT_EQ(s.events[1].channel, 0);
  EXPECT_EQ(s.events[1].a, 60);
  EXPECT_EQ(s.events[1].tick, 0u);
  // 384 ticki @ 120 BPM = 0,5 s
  EXPECT_NEAR(s.events[2].seconds, 0.5, 1e-9);
  EXPECT_EQ(s.events[2].channel, 1);
  EXPECT_NEAR(s.events[3].seconds, 1.0, 1e-9);
  EXPECT_EQ(s.events[3].type, polmidi::MIDIEV_NOTE_OFF);
  EXPECT_EQ(s.events[4].type, polmidi::MIDIEV_CC);
  EXPECT_EQ(s.events[4].a, 7);
  EXPECT_EQ(s.events[4].tick, 960u);
  EXPECT_EQ(s.events[5].type, polmidi::MIDIEV_PITCH_BEND);
  EXPECT_EQ(s.events[5].pitch, (96 << 7) - 8192); // 4096 (lekko w gore)
  EXPECT_NEAR(s.length, 1.25, 1e-9);
}

TEST(ParserSMF, RunningStatus) {
  std::vector<unsigned char> m = make_header(0, 1, 96);
  std::vector<unsigned char> tr;
  push_varlen(tr, 0);
  tr.insert(tr.end(), {0x90, 60, 64}); // status
  push_varlen(tr, 10);
  tr.push_back(62); // running status: note on bez powtorzonego 0x90
  tr.push_back(70);
  push_varlen(tr, 10);
  tr.push_back(64);
  tr.push_back(80);
  push_varlen(tr, 0);
  tr.insert(tr.end(), {0xFF, 0x2F, 0x00});
  push_chunk(m, "MTrk", tr);

  MidiSong s;
  ASSERT_TRUE(polmidi::parse_smf(m.data(), m.size(), s)) << s.error;
  ASSERT_EQ(s.events.size(), 3u);
  EXPECT_EQ(s.events[0].a, 60);
  EXPECT_EQ(s.events[1].a, 62);
  EXPECT_EQ(s.events[2].a, 64);
  EXPECT_EQ(s.events[1].tick, 10u);
  EXPECT_EQ(s.events[2].tick, 20u);
}

TEST(ParserSMF, NoteOnVelocity0_JestNoteOff) {
  std::vector<unsigned char> m = make_header(0, 1, 96);
  std::vector<unsigned char> tr;
  push_varlen(tr, 0);
  tr.insert(tr.end(), {0x90, 60, 64});
  push_varlen(tr, 5);
  tr.insert(tr.end(), {0x90, 60, 0}); // konwencja SMF: note off
  push_varlen(tr, 0);
  tr.insert(tr.end(), {0xFF, 0x2F, 0x00});
  push_chunk(m, "MTrk", tr);

  MidiSong s;
  ASSERT_TRUE(polmidi::parse_smf(m.data(), m.size(), s));
  ASSERT_EQ(s.events.size(), 2u);
  EXPECT_EQ(s.events[1].type, polmidi::MIDIEV_NOTE_OFF);
}

TEST(ParserSMF, ZmianaTempa_WielePunktow) {
  std::vector<unsigned char> m = make_header(0, 1, 384);
  std::vector<unsigned char> tr;
  push_varlen(tr, 0);
  tr.insert(tr.end(), {0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20}); // 120 BPM
  push_varlen(tr, 0);
  tr.insert(tr.end(), {0x90, 60, 64});  // t = 0
  push_varlen(tr, 384);                  // 0,5 s przy 120 BPM
  tr.insert(tr.end(), {0xFF, 0x51, 0x03, 0x0F, 0x42, 0x40}); // 1000000 us = 60 BPM
  push_varlen(tr, 384);                  // 1,0 s przy 60 BPM
  tr.insert(tr.end(), {0x80, 60, 64});
  push_varlen(tr, 0);
  tr.insert(tr.end(), {0xFF, 0x2F, 0x00});
  push_chunk(m, "MTrk", tr);

  MidiSong s;
  ASSERT_TRUE(polmidi::parse_smf(m.data(), m.size(), s)) << s.error;
  ASSERT_EQ(s.events.size(), 2u);
  EXPECT_NEAR(s.events[0].seconds, 0.0, 1e-9);
  // od ticku 384 tempo 60 BPM: 384 tickow = 1,0 s
  EXPECT_NEAR(s.events[1].seconds, 1.5, 1e-9);
  EXPECT_NEAR(s.length, 1.5, 1e-9);
}

TEST(ParserSMF, Format1_DwieSciezki) {
  std::vector<unsigned char> m = make_header(1, 2, 384);
  std::vector<unsigned char> t0;
  push_varlen(t0, 0);
  t0.insert(t0.end(), {0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20}); // 120 BPM
  push_varlen(t0, 0);
  t0.insert(t0.end(), {0xFF, 0x2F, 0x00});
  push_chunk(m, "MTrk", t0);
  std::vector<unsigned char> t1;
  push_varlen(t1, 0);
  t1.insert(t1.end(), {0xC1, 48});
  push_varlen(t1, 192); // 0,25 s
  t1.insert(t1.end(), {0x91, 57, 64});
  push_varlen(t1, 0);
  t1.insert(t1.end(), {0xFF, 0x2F, 0x00});
  push_chunk(m, "MTrk", t1);

  MidiSong s;
  ASSERT_TRUE(polmidi::parse_smf(m.data(), m.size(), s)) << s.error;
  EXPECT_EQ(s.format, 1);
  ASSERT_EQ(s.events.size(), 2u);
  EXPECT_NEAR(s.events[1].seconds, 0.25, 1e-9);
}

TEST(ParserSMF, UszkodzonePliki) {
  MidiSong s;
  // brak naglowka
  const unsigned char junk[] = {'R', 'I', 'F', 'F', 0, 0, 0, 0};
  EXPECT_FALSE(polmidi::parse_smf(junk, sizeof(junk), s));
  EXPECT_FALSE(s.error.empty());
  // ucięty track
  std::vector<unsigned char> m = make_header(0, 1, 384);
  std::vector<unsigned char> tr;
  push_varlen(tr, 0);
  tr.insert(tr.end(), {0x90, 60}); // brak drugiego bajtu danych
  push_chunk(m, "MTrk", tr);
  EXPECT_FALSE(polmidi::parse_smf(m.data(), m.size(), s));
  // pusty bufor
  EXPECT_FALSE(polmidi::parse_smf(nullptr, 0, s));
}

TEST(ParserSMF, Varlen) {
  uint32_t v = 0;
  const unsigned char a[] = {0x7F};
  EXPECT_EQ(polmidi::read_varlen(a, 1, &v), 1u);
  EXPECT_EQ(v, 127u);
  const unsigned char b[] = {0x81, 0x00};
  EXPECT_EQ(polmidi::read_varlen(b, 2, &v), 2u);
  EXPECT_EQ(v, 128u);
  const unsigned char c[] = {0xFF, 0xFF, 0xFF, 0x7F};
  EXPECT_EQ(polmidi::read_varlen(c, 4, &v), 4u);
  EXPECT_EQ(v, 0x0FFFFFFFu);
}

// ------------------------------------------------------- mapa utworow ------
TEST(MapowanieMidi, UtworyFabularneIMenu) {
  const MidiTrackMap *menu = find_midi_track(2);
  ASSERT_NE(menu, nullptr);
  EXPECT_STREQ(menu->dir, "luzyce");
  EXPECT_EQ(menu->loop, 1); // menu zapetla (jak S3M)

  const MidiTrackMap *txt = find_midi_track(3);
  ASSERT_NE(txt, nullptr);
  EXPECT_STREQ(txt->needle, "marzenia");
  EXPECT_EQ(txt->loop, 0); // teksty graja raz
}

TEST(MapowanieMidi, Plansze6_14_Zapetlane) {
  for (int cd = 6; cd <= 14; cd++) {
    const MidiTrackMap *mm = find_midi_track(cd);
    ASSERT_NE(mm, nullptr) << "utwor " << cd;
    EXPECT_EQ(mm->loop, 1) << "plansza " << cd;
    EXPECT_NE(mm->needle, nullptr);
  }
}

TEST(MapowanieMidi, PorażkaZwyciestwo_FallbackS3M) {
  // brak wpisu = tor S3M (fallback) - te utwory celowo zostaja na modulach
  for (int cd : {0, 1, 4, 5, 15, 99})
    EXPECT_EQ(find_midi_track(cd), nullptr) << "utwor " << cd;
}

TEST(MapowanieMidi, WzorceASCII_NaKodowanychNazwach) {
  // nazwy fanowskich plikow maja mieszane kodowanie - dopasowanie po ASCII
  EXPECT_TRUE(ascii_nocase_find("wymarsz na wojn\xA6.mid", "wojn"));
  EXPECT_TRUE(ascii_nocase_find("muzyka z lasu p\xC9yn\xCAca.mid", "lasu"));
  EXPECT_TRUE(ascii_nocase_find("marzenia.mid", "marzenia"));
  EXPECT_TRUE(ascii_nocase_find("LUZYCE.MID", "luzyce."));
  // "luzyce." NIE moze lapac LUZYCE2.MID (kropka po nazwie)
  EXPECT_FALSE(ascii_nocase_find("LUZYCE2.MID", "luzyce."));
  EXPECT_TRUE(ascii_nocase_find("LUZYCE2.MID", "luzyce2."));
  EXPECT_FALSE(ascii_nocase_find("marzenia.mid", "wojn"));
}

// --------------------------------------------------- szukanie plikow -------
TEST(SzukanieMidi, POLANIE_MIDI_I_Wzorzec) {
  // korzen fanowskich utworow zbudowany w katalogu testowym ($POLANIE_MIDI)
  std::string root = POL_TEST_TMP + "/fanroot";
  system(("mkdir -p '" + root + "/utwory_mid' '" + root + "/luzyce'").c_str());
  system(("touch '" + root + "/utwory_mid/marzenia.mid'").c_str());
  system(("touch '" + root + "/utwory_mid/wymarsz na wojn\xA6.mid'").c_str());
  system(("touch '" + root + "/luzyce/LUZYCE.MID'").c_str());
  setenv("POLANIE_MIDI", root.c_str(), 1);

  char out[1100];
  EXPECT_EQ(POL_MidiFindFile("luzyce", "luzyce.", out, sizeof(out)), 1);
  EXPECT_NE(std::string(out).find("LUZYCE.MID"), std::string::npos);

  EXPECT_EQ(POL_MidiFindFile("utwory_mid", "marzenia", out, sizeof(out)), 1);
  EXPECT_NE(std::string(out).find("marzenia.mid"), std::string::npos);

  EXPECT_EQ(POL_MidiFindFile("utwory_mid", "wojn", out, sizeof(out)), 1);

  // nieobecny wzorzec / podkatalog
  EXPECT_EQ(POL_MidiFindFile("utwory_mid", "nie-ma-takiego", out, sizeof(out)),
            0);
  EXPECT_EQ(POL_MidiFindFile("brak_katalogu", "marzenia", out, sizeof(out)), 0);

  unsetenv("POLANIE_MIDI");
}

// ------------------------------------------------------ parsowanie CLI -----
TEST(AudioOpts, ParsowanieAudioType) {
  EXPECT_EQ(pol_parse_audio_type_arg("--audioType=s3m"), POL_AUDIO_S3M);
  EXPECT_EQ(pol_parse_audio_type_arg("--audioType=sfz"), POL_AUDIO_SFZ);
  EXPECT_EQ(pol_parse_audio_type_arg("--audioType=auto"), POL_AUDIO_AUTO);
  EXPECT_EQ(pol_parse_audio_type_arg("--audioType=midi"), POL_AUDIO_AUTO);
  EXPECT_EQ(pol_parse_audio_type_arg("--audioType"), POL_AUDIO_AUTO);
  EXPECT_EQ(pol_parse_audio_type_arg("--AUDIOTYPE=SFZ"), POL_AUDIO_SFZ);
  // bledna wartosc -> sygnal bledu (main wypisze uzycie)
  EXPECT_EQ(pol_parse_audio_type_arg("--audioType=xyz"), -2);
  EXPECT_EQ(pol_parse_audio_type_arg("--audioType="), -2);
  // obce argumenty -> pominiecie (skala itd.)
  EXPECT_EQ(pol_parse_audio_type_arg("8"), -1);
  EXPECT_EQ(pol_parse_audio_type_arg("-s"), -1);
  EXPECT_EQ(pol_parse_audio_type_arg("--audioTypex=1"), -1);
  EXPECT_EQ(pol_parse_audio_type_arg(nullptr), -1);
}