// PORT: mapa instrumentow S3M (Adlib/OPL) -> General MIDI -> pliki .sfz (VSCO CE).
// Czysta tabela danych dla toru MIDI+sfizz; niczego nie includuje z port_audio.*,
// nie zmienia zachowania istniejacego kodu (tor S3M dalej gra przez libopenmpt).
//
// Skad dane: naglowki modulow extracted/audio/muzyka/GRAF_001-018.s3m.
// To wlasy wariant S3M Polan (nie pelny standard): OrdNum/InsNum/PatNum u16
// @0x20/0x22/0x24 (game/plays3m.cpp:76-79), ordery @0x60, wskaznik i-tego
// instrumentu u16 @0x60+OrdNum+i*2, rekord 80B na para*16 (standardowy naglowek
// S3M: typ @+0, nazwa pliku 12B @+1, nazwa instrumentu 28B @+48, sygnatura
// "SCRI" @+76 zamiast "SCRS"). Wszystkie 100 instrumentow w 18 modulach ma
// typ=2 (OPL melodic) - ZADNEGO sampla PCM, wiec "instrumenty S3M" to patche
// Adliba opisane nazwami; do sfz mapujemy PO NAZWACH (nie da sie odtworzyc
// rejestrow OPL jako sampli 1:1).
//
// VSCO-2-CE (Creative Commons biblioteka sampli, pobierana i instalowana
// osobno - sciezka przez env POLANIE_VSCO, patrz README): 75 plikow .sfz,
// zadny nie mapi numerow programow GM (to solo-patche, brak opcode'ow prog/bank).
// Pliki -KS sa wersjami z keyswitchami (sw_lokey..sw_hikey); uzywamy zwyklych
// wariantow bez -KS (domyslna artykulacja = plik z sus/vib/spic/pizz w nazwie),
// dzieki czemu nie trzeba obslugiwac keyswitchy. default_path w .sfz ma
// backslashe w stylu Windows ("Strings\Violin Section\susVib\") - silnik sfz
// musi je normalizowac na '/' wzgledem katalogu vsco/.
//
// Perkusja: kanał 10 MIDI -> GM-StylePerc.sfz (220 regionow, klucze ~32-94 w
// ukladzie zblizonym do GM; braki: 43/44/45, 57, 58 - cisza). Dla instrumentow
// S3M perkusyjnych pole perc_note = numer nuty GM w tym mapie (0 = nie perkusja).
#ifndef POL_SFZ_MAP_H
#define POL_SFZ_MAP_H

#include <cstddef> // NULL, size_t (header samodzielny)

// ---------------------------------------------------------------------------
// (a) Program GM 0-127 -> plik .sfz (sciezka wzgledem katalogu vsco/).
//     Braki VSCO (gitary, saxofony, syntezatory, chorki, etniczne) - fallback
//     na najblizszy timbre, oznaczony "/* fallback */"; te wpisy i tak sa
//     "cisza z logiem" jesli ktos bedzie chcial podmienic.
struct SfzMapEntry {
  int gm;         // program GM 0-127 (melodiczny)
  const char *sfz;   // plik w katalogu vsco/ ("" = cisza + log)
  const char *info;  // komentarz / uzasadnienie
};

static const SfzMapEntry kSfzMap[128] = {
    {0, "UprightPiano.sfz", "fallback: brak grand piano"},
    {1, "UprightPiano.sfz", "fallback"},
    {2, "UprightPiano.sfz", "fallback"},
    {3, "UprightPiano.sfz", "fallback"},
    {4, "UprightPiano.sfz", "fallback"},
    {5, "UprightPiano.sfz", "fallback"},
    {6, "UprightPiano.sfz", "klawesyn (uzywane: wymarsz na wojne ch4/5) - fallback"},
    {7, "UprightPiano.sfz", "fallback"},
    {8, "Glockenspiel.sfz", "fallback"},
    {9, "Glockenspiel.sfz", ""},
    {10, "Glockenspiel.sfz", "fallback"},
    {11, "Marimba.sfz", "fallback: brak vibrafonu"},
    {12, "Marimba.sfz", ""},
    {13, "Xylophone.sfz", ""},
    {14, "TubularBells.sfz", ""},
    {15, "Harp.sfz", "fallback"},
    {16, "OrganLoud.sfz", ""},
    {17, "OrganLoud.sfz", ""},
    {18, "OrganLoud.sfz", ""},
    {19, "OrganLoudPedal.sfz", ""},
    {20, "OrganQuiet.sfz", ""},
    {21, "OrganQuiet.sfz", "fallback"},
    {22, "ClarinetSus.sfz", "fallback"},
    {23, "OrganQuiet.sfz", "fallback"},
    {24, "Harp.sfz", "fallback: brak gitar"},
    {25, "Harp.sfz", "fallback"},
    {26, "Harp.sfz", "fallback"},
    {27, "Harp.sfz", "fallback"},
    {28, "Harp.sfz", "fallback"},
    {29, "OrganLoud.sfz", "fallback (overdriven)"},
    {30, "OrganLoud.sfz", "fallback (distortion): uzywane przez S3M ZynDist.Full"},
    {31, "Harp.sfz", "fallback"},
    {32, "ContrabassPizz.sfz", "fallback: brak basu akustycznego"},
    {33, "ContrabassPizz.sfz", "fallback: bas elektryczny -> smyczkowy pizz"},
    {34, "ContrabassPizz.sfz", "fallback"},
    {35, "ContrabassSusVB.sfz", "fallback (fretless = sustain)"},
    {36, "ContrabassSpic.sfz", "fallback (slap -> spiccato)"},
    {37, "ContrabassSpic.sfz", "fallback"},
    {38, "ContrabassSusNV.sfz", "fallback (synth bass -> non-vibrato)"},
    {39, "ContrabassSusNV.sfz", "fallback"},
    {40, "SViolinVib.sfz", "solo violin"},
    {41, "ViolaEnsSusVib.sfz", ""},
    {42, "CelloEnsSusVib.sfz", "uzywane: wszystkie 5 plikow fanowskich MIDI"},
    {43, "ContrabassSusVB.sfz", "uzywane: wszystkie pliki fanowskie MIDI"},
    {44, "ViolinEnsTrem.sfz", ""},
    {45, "ViolinEnsPizz.sfz", ""},
    {46, "Harp.sfz", "uzywane: marzenia/las/wymarsz ch4 (orkiestrowa harfa)"},
    {47, "Timpani.sfz", "uzywane: marzenia/las/wymarsz ch3 (kociol)"},
    {48, "ViolinEnsSusVib.sfz", "uzywane: LUZYCE.MID ch2 (skrzypce)"},
    {49, "ViolinEnsSusVib.sfz", "fallback"},
    {50, "ViolinEnsSusVib.sfz", "fallback"},
    {51, "ViolinEnsSusVib.sfz", "fallback"},
    {52, "ViolinEnsSusVib-Quiet.sfz", "fallback (chorki -> miekkie smyczki)"},
    {53, "ViolinEnsSusVib-Quiet.sfz", "fallback"},
    {54, "ViolinEnsSusVib-Quiet.sfz", "fallback"},
    {55, "ViolinEnsSpic.sfz", "fallback (orchestra hit -> krotki spiccato)"},
    {56, "TrumpetSusVib.sfz", ""},
    {57, "TromboneVib.sfz", ""},
    {58, "TubaSus.sfz", ""},
    {59, "TrumpetStraightMuteSus.sfz", "uzywane: LUZYCE.MID ch1 („trabka” jako muted)"},
    {60, "FHornSus.sfz", "S3M: horn (HORN1.ADL)"},
    {61, "TrumpetSus.sfz", "fallback: brak sekcji brass (combo jest tylko w .ariax)"},
    {62, "TrumpetSus.sfz", "fallback"},
    {63, "TromboneSus.sfz", "fallback"},
    {64, "ClarinetSus.sfz", "fallback: brak saxofonow"},
    {65, "ClarinetSus.sfz", "fallback"},
    {66, "BassoonSus.sfz", "fallback (tenor/baryton nizej)"},
    {67, "BassoonSus.sfz", "fallback"},
    {68, "OboeSusVib.sfz", ""},
    {69, "OboeSusNV.sfz", ""},
    {70, "BassoonVib.sfz", ""},
    {71, "ClarinetSus.sfz", ""},
    {72, "PiccoloSus.sfz", ""},
    {73, "FluteSusVib.sfz", "S3M: PsiFlute"},
    {74, "FluteSusNV.sfz", ""},
    {75, "FluteSusVib.sfz", "fallback"},
    {76, "FluteSusVib.sfz", "fallback"},
    {77, "FluteSusNV.sfz", "fallback"},
    {78, "FluteSusVib.sfz", "fallback (whistle ~ flet)"},
    {79, "FluteSusNV.sfz", "uzywane: wymarsz ch1, LUZYCE2 ch1 (piszczalka/ocarina)"},
    {80, "FluteSusVib.sfz", "ocarina (uzywane: marzenia/las ch1) - fallback na flet"},
    {81, "SViolinVib.sfz", "fallback: brak leadow syntezatorowych; S3M: AttackLead"},
    {82, "OrganQuiet.sfz", "fallback"},
    {83, "OrganLoud.sfz", "fallback"},
    {84, "OrganQuiet.sfz", "fallback"},
    {85, "OrganQuiet.sfz", "fallback"},
    {86, "OrganQuiet.sfz", "fallback"},
    {87, "OrganQuiet.sfz", "fallback"},
    {88, "ViolinEnsSusVib-Quiet.sfz", "fallback (pady -> wolny sustain smyczkow)"},
    {89, "ViolinEnsSusVib-Quiet.sfz", "fallback (pad 2 warm)"},
    {90, "ViolinEnsSusVib-Quiet.sfz", "fallback"},
    {91, "ViolinEnsSusVib-Quiet.sfz", "fallback"},
    {92, "ViolinEnsSusVib-Quiet.sfz", "fallback"},
    {93, "ViolinEnsSusVib-Quiet.sfz", "fallback"},
    {94, "ViolinEnsSusVib-Quiet.sfz", "fallback"},
    {95, "ViolinEnsSusVib-Quiet.sfz", "fallback"},
    {96, "OrganQuiet.sfz", "fallback (FX)"},
    {97, "OrganQuiet.sfz", "fallback"},
    {98, "Glockenspiel.sfz", "fallback"},
    {99, "ViolinEnsSusVib-Quiet.sfz", "fallback"},
    {100, "ViolinEnsTrem.sfz", "fallback"},
    {101, "OrganQuiet.sfz", "fallback"},
    {102, "Glockenspiel.sfz", "fallback"},
    {103, "OrganQuiet.sfz", "fallback"},
    {104, "Harp.sfz", "fallback (sitar)"},
    {105, "Harp.sfz", "fallback (banjo)"},
    {106, "Harp.sfz", "fallback"},
    {107, "Harp.sfz", "fallback (koto)"},
    {108, "Marimba.sfz", "fallback (kalimba)"},
    {109, "FHornSus.sfz", "fallback (bagpipe)"},
    {110, "SViolinVib.sfz", "fiddle ~ solo violin"},
    {111, "FluteSusVib.sfz", "fallback (shanai)"},
    {112, "Glockenspiel.sfz", "fallback"},
    {113, "GM-StylePerc.sfz", "agogo -> cowbell (key 56)"},
    {114, "Marimba.sfz", "fallback"},
    {115, "GM-StylePerc.sfz", "woodblock -> LogDrum (key 76/77)"},
    {116, "Timpani.sfz", "fallback (taiko)"},
    {117, "Timpani.sfz", "fallback (melodic tom)"},
    {118, "GM-StylePerc.sfz", "fallback"},
    {119, "GM-StylePerc.sfz", "reverse cymbal -> crash (key 49)"},
    {120, "Glockenspiel.sfz", "fallback"},
    {121, "FluteSusNV.sfz", "fallback (breath)"},
    {122, "GM-StylePerc.sfz", "fallback (seashore -> bell tree, key 82)"},
    {123, "PiccoloStac.sfz", "fallback (bird tweet)"},
    {124, "GM-StylePerc.sfz", "fallback (telephone)"},
    {125, "GM-StylePerc.sfz", "fallback"},
    {126, "OrganQuiet.sfz", "fallback (applause) - praktycznie cisza"},
    {127, "GM-StylePerc.sfz", "fallback (gunshot)"},
};

// ---------------------------------------------------------------------------
// (b) Kazdy instrument kazdego modulu S3M: indeks -> nazwa (z naglowka) ->
//     program GM (lub nuta perkusyjna na ch10) -> wpis VSCO.
//     niepewna=1: nazwa patcha nie pozwala na jednoznaczne przypisanie
//     (synth lead/pad/fm/dist albo perkusja bez konkretnej nuty GM).
struct S3mInsMap {
  int modul;         // 1-18 (GRAF_0xx.s3m)
  int indeks;        // 1-based, kolejnosc w naglowku modulu
  const char *nazwa; // 28-bajtowa nazwa z rekordu instrumentu (offset +48)
  int gm;            // program GM melodiczny 0-127 (dla perc_note=0)
  int perc_note;     // nuta perkusyjna GM na kanale 10 (0 = nie perkusja)
  int niepewna;      // 1 = mapowanie po znaczeniu nazwy, do weryfikacji na sluch
  const char *sfz;   // plik vsco/ (perc_note>0 => zawsze GM-StylePerc.sfz)
};

static const S3mInsMap kS3mInstrumentMap[] = {
    // GRAF_001 "Victoria"
    {1, 1, "horn", 60, 0, 0, "FHornSus.sfz"},
    {1, 2, "snare", 0, 38, 0, "GM-StylePerc.sfz"},
    {1, 3, "SlowAttackLead.2", 89, 0, 1, "ViolinEnsSusVib-Quiet.sfz"},
    {1, 4, "AttackLead", 81, 0, 1, "SViolinVib.sfz"},
    {1, 5, "DoumBass", 32, 0, 1, "ContrabassPizz.sfz"}, // alternatywnie kick ch10/36
    {1, 6, "PsiFlute", 73, 0, 0, "FluteSusVib.sfz"},
    // GRAF_002 "menu - end ver."
    {2, 1, "bass short 01", 33, 0, 0, "ContrabassPizz.sfz"},
    {2, 2, "long synth 01", 89, 0, 1, "ViolinEnsSusVib-Quiet.sfz"},
    {2, 3, "Strings", 48, 0, 0, "ViolinEnsSusVib.sfz"},
    // GRAF_003 "intro - end ver."
    {3, 1, "bass 2 b l", 33, 0, 0, "ContrabassPizz.sfz"},
    {3, 2, "Strings", 48, 0, 0, "ViolinEnsSusVib.sfz"},
    {3, 3, "bass 1 l", 33, 0, 0, "ContrabassPizz.sfz"},
    {3, 4, "PsiFlute", 73, 0, 0, "FluteSusVib.sfz"},
    {3, 5, "TomDrum", 0, 47, 0, "GM-StylePerc.sfz"},
    {3, 6, "ShortViolin", 40, 0, 0, "SViolinSpic.sfz"},
    {3, 7, "BloppStick", 0, 37, 0, "GM-StylePerc.sfz"},
    // GRAF_004 (bez tytulu)
    {4, 1, "A      (", 89, 0, 1, "ViolinEnsSusVib-Quiet.sfz"}, // nazwa nieczytelna
    {4, 2, "bass 2 c l", 33, 0, 0, "ContrabassPizz.sfz"},
    {4, 3, "ZynDist.Full", 30, 0, 1, "OrganLoud.sfz"}, // dist lead -> fallback
    {4, 4, "fm1", 89, 0, 1, "ViolinEnsSusVib-Quiet.sfz"}, // patch nieopisany
    {4, 5, "Strings", 48, 0, 0, "ViolinEnsSusVib.sfz"},
    // GRAF_005 "history - end ver."
    {5, 1, "HitString", 48, 0, 0, "ViolinEnsSpic.sfz"},
    {5, 2, "SlowAttackLead.2", 89, 0, 1, "ViolinEnsSusVib-Quiet.sfz"},
    {5, 3, "Strings", 48, 0, 0, "ViolinEnsSusVib.sfz"},
    {5, 4, "AttackLead", 81, 0, 1, "SViolinVib.sfz"},
    {5, 5, "HeavyGummiBass", 36, 0, 1, "ContrabassSpic.sfz"},
    // GRAF_006 "wiktor - end ver."
    {6, 1, "DoumBass", 32, 0, 1, "ContrabassPizz.sfz"},
    {6, 2, "snare", 0, 38, 0, "GM-StylePerc.sfz"},
    {6, 3, "Strings", 48, 0, 0, "ViolinEnsSusVib.sfz"}, // STRINGS2.ADl
    {6, 4, "SlowAttackStrings", 48, 0, 0, "ViolinEnsSusVib.sfz"},
    {6, 5, "bass 2 a l", 33, 0, 0, "ContrabassPizz.sfz"},
    {6, 6, "Strings", 48, 0, 0, "ViolinEnsSusVib.sfz"}, // STRINGS2.ADL
    // GRAF_007 "dark death - end ver." (= utwor CD 4, porazka)
    {7, 1, "bass 2 c l", 33, 0, 0, "ContrabassPizz.sfz"},
    {7, 2, "AttackLead", 81, 0, 1, "SViolinVib.sfz"},
    {7, 3, "bass 1 l", 33, 0, 0, "ContrabassPizz.sfz"},
    {7, 4, "TomDrum", 0, 47, 0, "GM-StylePerc.sfz"},
    // GRAF_008 "intro - md"
    {8, 1, "bass 2 c l", 33, 0, 0, "ContrabassPizz.sfz"},
    {8, 2, "AttackLead", 81, 0, 1, "SViolinVib.sfz"},
    {8, 3, "bass 1 l", 33, 0, 0, "ContrabassPizz.sfz"},
    {8, 4, "TomDrum", 0, 47, 0, "GM-StylePerc.sfz"},
    // GRAF_009 "dark death - end ver."
    {9, 1, "bass 2 c l", 33, 0, 0, "ContrabassPizz.sfz"},
    {9, 2, "AttackLead", 81, 0, 1, "SViolinVib.sfz"},
    {9, 3, "bass 1 l", 33, 0, 0, "ContrabassPizz.sfz"},
    {9, 4, "TomDrum", 0, 47, 0, "GM-StylePerc.sfz"},
    // GRAF_010 "dark death - end ver."
    {10, 1, "bass 2 c l", 33, 0, 0, "ContrabassPizz.sfz"},
    {10, 2, "AttackLead", 81, 0, 1, "SViolinVib.sfz"},
    {10, 3, "bass 1 l", 33, 0, 0, "ContrabassPizz.sfz"},
    {10, 4, "TomDrum", 0, 47, 0, "GM-StylePerc.sfz"},
    // GRAF_011 (bez tytulu)
    {11, 1, "bass 1 l", 33, 0, 0, "ContrabassPizz.sfz"},
    {11, 2, "bass 2 a l", 33, 0, 0, "ContrabassPizz.sfz"},
    {11, 3, "hathyb2", 0, 46, 0, "GM-StylePerc.sfz"},
    {11, 4, "horn", 60, 0, 0, "FHornSus.sfz"},
    {11, 5, "sup bass", 33, 0, 0, "ContrabassPizz.sfz"},
    {11, 6, "PsiFlute", 73, 0, 0, "FluteSusVib.sfz"},
    // GRAF_012 "plansza 2 - end version"
    {12, 1, "Little Percussion", 0, 115, 1, "GM-StylePerc.sfz"}, // woodblock?
    {12, 2, "BloppStick", 0, 37, 0, "GM-StylePerc.sfz"},
    {12, 3, "snare", 0, 38, 0, "GM-StylePerc.sfz"},
    {12, 4, "newkick", 0, 36, 0, "GM-StylePerc.sfz"},
    {12, 5, "StipPerco", 0, 54, 1, "GM-StylePerc.sfz"}, // klucz do dobrania
    {12, 6, "hathyb2", 0, 46, 0, "GM-StylePerc.sfz"},
    {12, 7, "Tsunk", 0, 75, 1, "GM-StylePerc.sfz"},     // klucz do dobrania
    {12, 8, "AttackLead", 81, 0, 1, "SViolinVib.sfz"},
    {12, 9, "HitString", 48, 0, 0, "ViolinEnsSpic.sfz"},
    {12, 10, "fm1", 89, 0, 1, "ViolinEnsSusVib-Quiet.sfz"},
    // GRAF_013 "plansza3 - end ver."
    {13, 1, "bass 2 d l", 33, 0, 0, "ContrabassPizz.sfz"},
    {13, 2, "ZynDist.Full", 30, 0, 1, "OrganLoud.sfz"},
    {13, 3, "HeavyGummiBass", 36, 0, 1, "ContrabassSpic.sfz"},
    {13, 4, "newkick", 0, 36, 0, "GM-StylePerc.sfz"},
    {13, 5, "snare", 0, 38, 0, "GM-StylePerc.sfz"},
    {13, 6, "SlowAttackStrings", 48, 0, 0, "ViolinEnsSusVib.sfz"},
    // GRAF_014 "plansza 2 - end version" (druga wersja)
    {14, 1, "Little Percussion", 0, 115, 1, "GM-StylePerc.sfz"},
    {14, 2, "BloppStick", 0, 37, 0, "GM-StylePerc.sfz"},
    {14, 3, "snare", 0, 38, 0, "GM-StylePerc.sfz"},
    {14, 4, "newkick", 0, 36, 0, "GM-StylePerc.sfz"},
    {14, 5, "StipPerco", 0, 54, 1, "GM-StylePerc.sfz"},
    {14, 6, "hathyb2", 0, 46, 0, "GM-StylePerc.sfz"},
    {14, 7, "Tsunk", 0, 75, 1, "GM-StylePerc.sfz"},
    {14, 8, "AttackLead", 81, 0, 1, "SViolinVib.sfz"},
    {14, 9, "HitString", 48, 0, 0, "ViolinEnsSpic.sfz"},
    {14, 10, "fm1", 89, 0, 1, "ViolinEnsSusVib-Quiet.sfz"},
    // GRAF_015 (bez tytulu) = GRAF_011
    {15, 1, "bass 1 l", 33, 0, 0, "ContrabassPizz.sfz"},
    {15, 2, "bass 2 a l", 33, 0, 0, "ContrabassPizz.sfz"},
    {15, 3, "hathyb2", 0, 46, 0, "GM-StylePerc.sfz"},
    {15, 4, "horn", 60, 0, 0, "FHornSus.sfz"},
    {15, 5, "sup bass", 33, 0, 0, "ContrabassPizz.sfz"},
    {15, 6, "PsiFlute", 73, 0, 0, "FluteSusVib.sfz"},
    // GRAF_016 "plansza6 - end ver."
    {16, 1, "bass 1 l", 33, 0, 0, "ContrabassPizz.sfz"},
    {16, 2, "AttackLead", 81, 0, 1, "SViolinVib.sfz"},
    {16, 3, "DoumBass", 32, 0, 1, "ContrabassPizz.sfz"},
    {16, 4, "SlowAttacksound.5", 89, 0, 1, "ViolinEnsSusVib-Quiet.sfz"},
    {16, 5, "AttackLead 2", 81, 0, 1, "SViolinVib.sfz"},
    // GRAF_017 "plansza 7 - end ver"
    {17, 1, "A      (", 89, 0, 1, "ViolinEnsSusVib-Quiet.sfz"},
    {17, 2, "SlowAttackStrings", 48, 0, 0, "ViolinEnsSusVib.sfz"},
    {17, 3, "AttackLead", 81, 0, 1, "SViolinVib.sfz"},
    {17, 4, "Little Percussion", 0, 115, 1, "GM-StylePerc.sfz"},
    {17, 5, "DoumBass", 32, 0, 1, "ContrabassPizz.sfz"},
    // GRAF_018 "plansza goralska"
    {18, 1, "snare", 0, 38, 0, "GM-StylePerc.sfz"},
    {18, 2, "ShortViolin", 40, 0, 0, "SViolinSpic.sfz"},
    {18, 3, "Strings", 48, 0, 0, "ViolinEnsSusVib.sfz"},
    {18, 4, "DoumBass", 32, 0, 1, "ContrabassPizz.sfz"},
};

// Wyszukiwanie: program GM -> wpis (NULL gdy poza 0-127).
static const SfzMapEntry *find_sfz_for_gm(int gm) {
  if (gm < 0 || gm > 127)
    return NULL;
  return &kSfzMap[gm];
}

// Wszystkie instrumenty danego modulu (liczba wpisow).
static int count_s3m_instruments(int modul) {
  int n = 0;
  for (size_t i = 0; i < sizeof(kS3mInstrumentMap) / sizeof(kS3mInstrumentMap[0]); i++)
    if (kS3mInstrumentMap[i].modul == modul)
      n++;
  return n;
}

// Kanał 10 MIDI (perkusja GM) - jeden plik, mapowanie kluczy wewnatrz sfz.
#define SFZ_PERC_FILE "GM-StylePerc.sfz"

#endif // POL_SFZ_MAP_H