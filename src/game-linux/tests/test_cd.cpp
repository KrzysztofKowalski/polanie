// PORT: testy warstwy CD/audio (src/cd.cpp) bez urzadzenia i bez SDL -
// POL_MusicPlay/POL_MusicStop/POL_MusicSetVolume sa ze stubow
// (tests/test_stubs.cpp), ktore zapisuja wywolania. Mapowanie numer -> plik
// S3M testuje osobno tests/test_audio.cpp (port/track_map.h).
#include <gtest/gtest.h>

#include "cd.h" // -I../game

// liczniki ze stubow
extern int POL_test_music_calls;
extern int POL_test_music_last;
extern int POL_test_music_ret;
extern int POL_test_music_vol;
// globals z cd.cpp (nie ma ich w cd.h)
extern int CD_MaxTrack;
extern int CD_CurrentTrack;
extern int CD_AudioOn;
extern int CD_Volume;

// is_playing jest static w cd.cpp - obserwujemy stan przez GetCurrentTrack();
// pomocnik tylko czytelnie zaznacza "brak grajacego utworu"

class CD : public ::testing::Test {
protected:
  void SetUp() override {
    CD_AudioOn = 0;
    CD_MaxTrack = 14;
    CD_CurrentTrack = 0;
    CD_Volume = 5;
    POL_test_music_calls = 0;
    POL_test_music_last = -1;
    POL_test_music_ret = 1;
    POL_test_music_vol = -1;
  }
};

TEST_F(CD, InitCD_Ok) { EXPECT_EQ(InitCD(), 0); }

TEST_F(CD, PlayTrack_AudioWylaczone_NieGra) {
  EXPECT_EQ(PlayTrack(2), 0);
  EXPECT_EQ(POL_test_music_calls, 0);   // mikser nie dostal zadania
  EXPECT_EQ(CD_CurrentTrack, 0);
  OffCDAudio();
  EXPECT_EQ(PlayTrack(5), 0);
  EXPECT_EQ(POL_test_music_calls, 0);
}

TEST_F(CD, PlayTrack_UstawiaNumerUTworu) {
  OnCDAudio();
  EXPECT_EQ(PlayTrack(5), 0);
  EXPECT_EQ(POL_test_music_calls, 1);
  EXPECT_EQ(POL_test_music_last, 5);
  EXPECT_EQ(GetCurrentTrack(), 5);
}

TEST_F(CD, PlayTrack_ObciecieDoMaxTrack) {
  // jak game/cd.cpp:531 - numer > ostatniego utworu plyty jest przycinany
  OnCDAudio();
  CD_MaxTrack = 14;
  PlayTrack(99);
  EXPECT_EQ(POL_test_music_last, 14);
  EXPECT_EQ(CD_CurrentTrack, 14);
}

TEST_F(CD, PlayTrack_BrakModulu_NieZmieniaUtworu) {
  // POL_MusicPlay zwraca 0 (np. utwor poza tabela) - stan bez zmian
  OnCDAudio();
  POL_test_music_ret = 0;
  PlayTrack(9);
  EXPECT_EQ(CD_CurrentTrack, 0);
}

TEST_F(CD, StopPlaying_WylaczaGra) {
  OnCDAudio();
  PlayTrack(2);
  EXPECT_EQ(StopPlaying(), 0);
  OffCDAudio(); // przy wylaczonym audio StopPlaying nie woła miksera
  EXPECT_EQ(StopPlaying(), 0);
}

TEST_F(CD, PlayNext_PlayPrevious_Zakres) {
  OnCDAudio();
  CD_MaxTrack = 14;
  PlayTrack(5); // zwyciestwo
  PlayNext();   // 5 -> 6
  EXPECT_EQ(GetCurrentTrack(), 6);
  PlayPrevious(); // 6 -> 5
  EXPECT_EQ(GetCurrentTrack(), 5);
  CD_CurrentTrack = 6;
  PlayPrevious(); // 6 -> 5
  EXPECT_EQ(GetCurrentTrack(), 5);
  CD_CurrentTrack = 5;
  PlayPrevious(); // ponizej 5 -> ostatni utwor (CD_MaxTrack)
  EXPECT_EQ(GetCurrentTrack(), 14);
}

TEST_F(CD, Volume_OgraniczonyDo0_5_IPrzekazanyDoMiksera) {
  setVolume(3);
  EXPECT_EQ(getVolume(), 3);
  EXPECT_EQ(POL_test_music_vol, 3);
  setVolume(-3);
  EXPECT_EQ(getVolume(), 0);
  setVolume(9);
  EXPECT_EQ(getVolume(), 5);
}

TEST_F(CD, CDAudioOnOffIDeInit) {
  OffCDAudio();
  OnCDAudio();
  BigOnCDAudio();
  BigOffCDAudio();
  DeInitCD(); // nie powinno crashowac bez CD
}

// is_playing jest static w cd.cpp - zamiast tego sprawdzamy obserwowalny stan
