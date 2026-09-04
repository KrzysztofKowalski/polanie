// PORT: testy mapowania PlayTrack(n) -> modul S3M (port/track_map.h,
// uzyte przez port_audio.cpp). Tabela bez SDL/libopenmpt - testy linkuja
// sam naglowek. Kotwice: 2=menu, 3=teksty, 4=porazka, 5=zwyciestwo,
// 6-14=plansze (zapetlane), poza zakresem = brak modulu (cicho).
#include <gtest/gtest.h>

#include "track_map.h" // -Iport

TEST(MapowanieUtworow, KotwiceFabuly) {
  // TRACK_MENU / TRACK_TXT / TRACK_DEFEAT / TRACK_VICTORY (battle.cpp:20-23)
  const TrackMap *menu = find_track(2);
  ASSERT_NE(menu, nullptr);
  EXPECT_EQ(menu->module, 2);  // GRAF_002 "menu"
  EXPECT_EQ(menu->loop, 1);    // menu zapetla

  const TrackMap *txt = find_track(3);
  ASSERT_NE(txt, nullptr);
  EXPECT_EQ(txt->module, 3);   // GRAF_003 "intro"
  EXPECT_EQ(txt->loop, 0);     // teksty graja raz

  const TrackMap *defeat = find_track(4);
  ASSERT_NE(defeat, nullptr);
  EXPECT_EQ(defeat->module, 7); // "dark death"
  EXPECT_EQ(defeat->loop, 0);

  const TrackMap *victory = find_track(5);
  ASSERT_NE(victory, nullptr);
  EXPECT_EQ(victory->module, 6); // "wiktor"
  EXPECT_EQ(victory->loop, 0);
}

TEST(MapowanieUtworow, Plansze6_14_Zapetlane) {
  for (int cd = 6; cd <= 14; cd++) {
    const TrackMap *tm = find_track(cd);
    ASSERT_NE(tm, nullptr) << "utwor " << cd;
    EXPECT_GE(tm->module, 1);
    EXPECT_LE(tm->module, 18); // GRAF_001-018
    EXPECT_EQ(tm->loop, 1) << "plansza " << cd << " ma zapetlac";
  }
  // plansze nie moga wskakiwac na moduly fabuly (2=menu, 3=txt, 7=defeat,
  // 6=victory) - oprocz celowego 12->4/13->11 (wolne moduly tla)
  for (int cd = 6; cd <= 14; cd++) {
    const TrackMap *tm = find_track(cd);
    ASSERT_NE(tm, nullptr);
    if (tm->module == 2 || tm->module == 3)
      ADD_FAILURE() << "plansza " << cd << " na module menu/tekstow";
  }
}

TEST(MapowanieUtworow, PozaZakresem_BrakModulu) {
  for (int cd : {-1, 0, 1, 15, 52, 99}) {
    EXPECT_EQ(find_track(cd), nullptr) << "utwor " << cd;
  }
}

TEST(MapowanieUtworow, UnikalneNumeryUtworow) {
  // kazdy numer CD moze byc w tabeli tylko raz (pierwsze trafienie wygrywa)
  for (size_t i = 0; i < sizeof(kTrackMap) / sizeof(kTrackMap[0]); i++)
    for (size_t j = i + 1; j < sizeof(kTrackMap) / sizeof(kTrackMap[0]); j++)
      EXPECT_NE(kTrackMap[i].cd, kTrackMap[j].cd)
          << "duplikat cd=" << kTrackMap[i].cd;
}