# PORT Polanie → Linux — status i notatki

Stan na 2026-09-04. Praca podagentami.

## Architektura

- `game/` — oryginalne źródła (Watcom/DOS) kompilowane **bez zmian** przez
  `-include include/polshim.h` (shim Watcom→g++/SDL3).
- `src/` — portowane kopie DOS-owych plików (`// PORT:`), m.in. main, image13h,
  mouse, menegdma (SB→SDL audio), cd (CD-Audio → moduły S3M/libopenmpt).
- `port/` — warstwa `port.h`/`port_sdl.cpp`/`port_fopen.cpp`/`port_shims.cpp`.
- Wideo: **SDL3 natywnie** (decyzja projektowa). Port renderuje **natywne
  320×200**; **skalowanie do ekranu robi gamescope** (nearest, 2560×1600).
  Bez gamescope: `./pol2 [n]` = 1 px gry = n×n px okna (integer scale w SDL3).
- Dane gry: silnik czyta oryginalne DAT-y (`dysk/GRY/POLANIE`, wykrywane auto,
  `POLANIE_DATA`). Migracja na `extracted/` (PNG/WAV/S3M) — docelowo.
- `POL_fopen` normalizuje DOS-owe ścieżki: `\`→`/`, zrzucanie `X:`, match bez
  rozróżniania wielkości liter, katalog bazowy + **synteza SETUP.INI** (7 B
  domyślnej konfiguracji SB) gdy pliku nie ma — „gotowa instalacja", gra nigdy
  nie każe uruchamiać SETUP.EXE.

## Naprawione błędy

1. **`#define fopen POL_fopen` znikało**: libstdc++ `<cstdio>` robi `#undef
   fopen` → shim `include/iostream.h` przywraca makro na końcu.
2. **Makefile nie przebudowywał** plików po zmianie shimów → `-MMD -MP`;
   `-include *.d` musi być NA KOŃCU Makefile (w przeciwnym razie pierwszy cel
   z pliku .d staje się celem domyślnym make).
3. **FONT.DAT** (i arkusze image13h z dysku): nagłówek `[u16 size][u16 w][u16
   h]`, pole size zawyżone. Oryginalny `LoadImage13h` brał size za szerokość
   → memcpy 32326 B/wiersz → segfault. Port rozpoznaje 3 pola i buduje
   nagłówek `[w][h]` dla `PutImage13h`. (Font to arkusz 319×100 — stąd
   „519 forgotten pixels": arkusze mają 319 px szerokości.)
4. **Wiszenie przed oknem (100% CPU)**: znane problemy SDL/Wayland+Hyprland
   (busy-wait przy init/swapchain, FULLSCREEN_DESKTOP+CreateRenderer). Środki:
   natywne okno 320×200 bez fullscreen, SDL3 (>= 3.2.18 z poprawką xdg_surface),
   gamescope (nested compositor), awaryjnie `SDL_VIDEODRIVER=x11`.
   Diagnostyka: yama blokuje ptrace-attach → `timeout -s SEGV 10 ./pol2`
   i `coredumpctl gdb`.
5. **Okno się nie mapuje po inicjacji**: pod DOS-em zapis do ekranu 0 (A0000)
   był widoczny od razu (VGA skanuje pamięć w sposób ciągły); port rysuje do
   framebuffera SDL, a `POL_Present` wołany był tylko w `ShowVirtualScreen`
   (bitwa). Menu rysuje na ekran 0 i nigdy nie prezentuje → Wayland nie mapuje
   okna bez pierwszej ramki. Fix: `POL_PumpEvents()` kończy się `POL_Present()`
   (odwzorowanie ciągłego skanowania; pompy zdarzeń lecą w pętlach myszy/klawiatury).
6. **Ciche `exit(0)` po kliknięciu „Nowa Gra"**: `InitBattle` otwiera
   `levels\level.dat` (`battle.cpp:2078`), a w tej kopii danych nie ma
   katalogu `levels/` — pliki kampanii leżą w `GRAF/LEVEL.DAT|LEVEL.INI`.
   Bez pliku gra robiła `Close13h(); exit(0)` bez komunikatu. Fix:
   `POL_fopen` ma fallback `levels/X` → `<dane>/GRAF/X`. Zabezpieczenia z
   ery DOS nie przeszkadzają: `Haslo()` (hasło z instrukcji, plansze
   7/14/24) to w źródłach pusty stub, a `zabezset.cpp`
   (`odswiez_Polan*`) = samoodszyfrowanie POLANIE.EXE, nie wywoływane.
7. **Crash na ekranie wstępu misji**: `ShowPicture2` (`graphics.cpp:1488`)
   robi `fopen("pic.dat")` i nie sprawdza NULL → `fseek(NULL)` = crash,
   okno zamiera w losowym stanie („zepsuty ekran po Nowej Grze"). `pic.dat`
   (podkłady tekstów: `[768 B palety][64000 B ekran]` × obraz, indeksy 0–35)
   nie istnieje nigdzie w tej kopii. Fix: synteza w `POL_fopen` (jak
   SETUP.INI): 64 czarne ekrany + paleta z czytelnymi kolorami tekstu
   (1 = cien, 255 = litery). Teksty misji działają na czerni, do czasu
   znalezienia oryginalnego pic.dat (np. z pełnej instalacji).
8. **Ekran wyboru kampanii „zepsuty" (palisada + oczy)**: GRAF.DAT w tej
   kopii pochodzi z wersji demo (30 bloków × 33000 B = 15 ekranów; ekran
   i = blok i góra + blok i+15 dół — tak skleja je ekstraktor, patrz
   tools/polanie_extract.cpp). Kod gry (wersja pełna) rysuje bloki 10+24
   jako mapę kampanii — w demo to inne rysunki. Oczy (czerwona kropka +
   czarne punkty) to działający wybór trudności (PokazOczy) — ekran jest
   klikalny „w ciemno". Fix tymczasowy: NewGame() w src/main.cpp rysuje
   szkielet przycisków w tych samych współrzędnych co regiony klikania
   (// PORT), a klik kampanii 2–6 sprawdza IsFile(levels/level.N) zamiast
   cichego exit(0). Prawdziwe rozwiązanie: pełny GRAF.DAT + levels/level.N
   + pic.dat z pełnego wydania gry (linki w polanie-resources / README).

9. **`get_base_dir` not declared** (port_fopen.cpp:71, 2026-09-03, późny
   wieczór): funkcja zdefiniowana niżej, niż użyta w `get_extracted_dir()`
   → fix: deklaracja forward przed pierwszym użyciem (port_fopen.cpp:34-37).
   Build przechodzi, link czysty.
10. **Ciche wyjście po drugim ekranie fabuły** (2026-09-03, późny wieczór):
    `InitBattle` otwiera `levels\level.dat` przez `sprintf("%slevels\\...", drive)`
    z `drive = "./"` (game/battle.cpp:2078-2088) → znormalizowana ścieżka
    `./levels/level.dat` omijała fallback `levels/` → `GRAF/` (łapał tylko
    czysty prefiks) → NULL → `Close13h()+exit(0)`. Teksty działały, bo
    `level.ini` otwierane bez prefiksu. Fix: zdejmowanie wiodącego `./`
    (i powtórzonych `././`) w normalizacji `POL_fopen`
    (port_fopen.cpp:188-198). Diagnoza potwierdzona diagnostyką (11).
    Pełna analiza: `raporty/raport-ciche-wyjscie-po-tekstach.md`.
12. **Crash „enable audio" w opcjach (2026-09-03, noc, agent): rozjazd ABI SDL3.**
    Klik „MusikOn" → pierwszy w sesji `POL_MusicPlay` → leniwe `POL_MixerInit`
    → `SDL_ResumeAudioStreamDevice(SDL_GetAudioStreamDevice(strm))`. Binarka
    była zbudowana przeciw starym nagłówkom (funkcja brała `SDL_AudioDeviceID`);
    systemowy SDL 3.4.14 bierze `SDL_AudioStream*` — do funkcji oczekującej
    pointera poszedł numer urządzenia 0x25 → SIGSEGV (potwierdzone coredumpem
    + dezasemblacją). Fix: `port_audio.cpp:333-340` — `SDL_ResumeAudioStreamDevice(strm)`
    bezpośrednio. Bonus: (a) `POL_ListFiles(dir,…)` — enumeracja katalogu
    POSIX readdir (`port_fopen.cpp:373-404`, deklaracja `polshim.h:31-35`);
    (b) sondy slotów `save.001–004` logowane RAZ jako „sonda slotu zapisu —
    to nie błąd" (`is_save_slot_probe`, `port_fopen.cpp:29-59`) — koniec
    spamu fopen FAIL; (c) `resolve_sfx_path` zamiast ślepych sond robi
    jednorazową enumerację katalogu z matchem stempla W0nn (dowolne
    rozszerzenie, case-insensitive). Testy: **39/39 PASS** (dołożone
    regresje: POL_ListFiles, sondy slotów). Test usera: OPCJE → MusikOn
    (ma być `PORT: audio 48000 Hz F32 stereo` + `PORT: muzyka: utwor 2`).
12b. **Efekty dźwiękowe niesłyszalne — diagnoza + fix (2026-09-03, noc, agent).**
     Zgłoszenie: „muzyka gra (MusikOn), głosy jednostek nie". Hipoteza o
     syntetycznym SETUP.INI (bajt[1]=0 = „brak SB") **nie potwierdziła się**
     jako przyczyna: `SND.Init` w ladowani.cpp:44 wołany jest bezwarunkowo,
     portowany `MENEGERDMA::Init` startuje audio zawsze, a realny SETUP.INI
     w `dysk/GRY/POLANIE` (bajty `00 00 0D 20 02 83 00`) i tak ma pierwszeństwo
     przed syntetyzowanym. Prawdziwe przyczyny, wszystkie naprawione:
     - **Killer: `port_audio.o` wołał glibc `fopen` zamiast `POL_fopen`** —
       nagłówki systemowe (`<SDL.h>`, `<string>`) robią `#undef fopen` i
       zabijały makro shim (jak STATUS pkt 1); `read_file()` czytał więc
       surowym fopenem z CWD i efekty z `data/W0nn.dat` **nigdy** nie sięgały
       do katalogu danych/ekstraktu — cicha cisza. Fix: przywrócenie makra
       `fopen`→`POL_fopen` po include'ach (`port_audio.cpp`, `sound_dat.cpp`
       — sprawdzone przez `nm`: `U POL_fopen`, brak `U fopen`).
     - **Głosy jednostek żyją w SOUND.DAT, którego nie ma w kopii demo**: gra
       prosi o dźwięki 1–183 (wybór `34+type*11`, ruch `28+type*11`, grupa
       `168/177`), a `dysk/GRY/POLANIE/DATA` ma tylko W001–W055. Pełna
       wersja CD (`jprok_pliki/rozpakowane/polanie_cd/DATA/SOUND.DAT`,
       3,68 MB) trzyma wszystkie 183 próbki w kontenerze
       `[surowe PCM 8-bit 22050 Hz][183 × i32 LE długości na końcu]`
       (zweryfikowane: suma długości = rozmiar PCM). Fix: realny
       `LoadGlobalData` w porcie — parser `port/sound_dat.cpp` (bez SDL,
       linkowany przez testy), `MENEGERDMA::operator()` gra teraz próbkę nr
       ze zbioru, z fallbackiem na pliki W0nn; loader sam znajduje SOUND.DAT
       (katalog danych → ekstrakt → archiwum jprok). Wczytanie widać w logu:
       `PORT: SOUND.DAT: 183 probek glosow (z 183) wczytane`. Zalecane dla
       usera: `cp jprok_pliki/rozpakowane/polanie_cd/DATA/SOUND.DAT
       dysk/GRY/POLANIE/DATA/` (port i tak znajdzie je w archiwum — kopiowanie
       jest tylko czystsze).
     - **Głosy odtwarzane ~2,2× za szybko** (chipmunk): próbki 22050 Hz były
       konsumowane 1 probka/klatkę wyjścia 48000 Hz. Fix: resampling liniowy
       w mikserze (`Voice.step/frac`, `port_audio.cpp`).
     - **`POL_ListFiles`/`ci_fopen` nie otwierały katalogów o innej
       wielkości liter** (dane mają `DATA/`, gra prosi o `data/`) — fix:
       rekurencyjne dopasowanie komponentów katalogu (`ci_opendir`,
       `port_fopen.cpp`); wcześniej `POL_ListFiles("data") = -1`. Uwaga:
       pośrednia, błędna wersja `ci_fopen` dawała nieskończoną rekurencję
       (coredump 22:32, stack overflow w snprintf) — naprawione, testy zielone.
     - Syntetyczny SETUP.INI: bajt[1] = **1** (port zawsze ma własny mikser =
       „karta dźwiękowa"); bajt[0] = 0 (bez CD — muzyka startuje MusikOn, jak
       instalacja bez napędu). Port nie gateuje na tym audio.
     - Mixer startuje od razu (`POL_MixerInit` robi `SDL_InitSubSystem
       (SDL_INIT_AUDIO)`) — wcześniej `SND.Init` biegł przed `SDL_Init` i
       drukował „audio niedostepne".
     - Intro i001–i003 rozwiązywane teraz do pełnej wersji CD (kandydat nr 3
       w `POL_ResolveDataFile`); `s000–s003` to FLIC-y wideo — ich nigdzie
       nie ma (luka danych, osobny tor, nieszkodliwe fopen FAIL w logu).
     - Flaky test `POLFopen.SlotZapisuZPlikiemDzialaNormalnie` (czytał realny
       `save.001` z CWD repo) — CWD testu przełączane na katalog tymczasowy.
     Testy: **43/43 PASS** (nowe: `test_sfx.cpp` — resolve W0nn, case-
     insensitive katalogi, parser SOUND.DAT; zaktualizowany SetupIniSynteza).
     Test usera: `bash run_port.sh` → misja → zaznaczenie jednostki i ruch —
     słychać głosy (W028/W034…; komendy grupy 168/177 też, bo SOUND.DAT).
     UWAGA: „crash" pol2 z coredumpów 22:40–22:47 miał si_code=SI_USER —
     to SIGSEGV wysłany zewnętrznie przez `timeout -s SEGV` (metoda diagnozy),
     nie błąd portu; uruchamiać grę przez `bash run_port.sh` bez timeout.
13. **Diagnostyka portu — włączona na stałe** (2026-09-03, późny wieczór):
    przechwyt `exit()` z plik:linia (`polshim.h:46-47` `#define exit` →
    `POL_exit` w port_shims.cpp:43-61, drukuje też ostatnie nieudane fopen)
    + log nieudanych fopen w `POL_fopen` (`PORT: fopen FAIL: 'DOS' -> 'znorm'
    (baza, próba #N, errno)`, limit spamu: 3 próby/potem co 200 na ścieżkę)
    + line-buffering stdout w konstruktorze (port_shims.cpp:57-61). Każdy
    cichy exit pokazuje teraz przyczynę w stdout run_port.sh.

14. **Retest zegara OK + „glitche kolorów" — wariant A palette-exact**
    (2026-09-04, user): po fixie ABI timera (sygnatura SDL3 `(userdata, timerID,
    interval)` + `return interval`) user potwierdził „teraz klikanie działa i
    animacje", ale zgłosił „dziwny dithering i artefakty... róż gdzie zieleń"
    (tryb domyślny renderera). Audyt pełnego łańcucha (port_sdl/port_gpu/
    image13h/playfli; kontrakt `POL_SetPalette` = 256×3 B DAC VGA 0–63):
    **łańcuch portu jest 1:1 z DOS-em — w rendererze nie ma kroku
    produkującego przebarwienia**. PAL.DAT jest 0–255; `SetExtendedPalette`
    robi `>>2` dokładnie jak oryginał (DOS wypalał tylko górne 6 bitów DAC),
    palety FLIC (intro) idą surowo 0–63 (standard FLIC) — dwa kontrakty, oba
    kończą jako DAC 0–63; LUT był jednym wzorcem (patrz niżej), byte-order
    ARGB/BGRA zgodny, nigdzie LINEAR, rysowanie atomowe (RealVirtualScreen +
    blit), a `delay()` nie pompuje zdarzeń → fade nie bywa przerywany
    prezentacją. Najbardziej prawdopodobny korzeń „różu zamiast zieleni"
    leży w WARSTWIE DANYCH/substytutów, nie w rendererze: ta kopia danych to
    rewizja dyskietkowa (GRAF.DAT/PAL.DAT unikalne, ≠ CD w 244/256 wpisach
    palety), ekran kampanii = szkielet przycisków 254/255 (src/main.cpp
    workaround), teksty misji = syntetyczny PIC.DAT zamiast rycin, a kolory UI
    bitwy czytane są z pikseli arkusza GRAF.DAT (na dyskietce brązy/czernie,
    nie paska kolorów). Namacalny mechanizm: **indeks 173 = oliwkowa zieleń
    w palecie mapy świata (6), a różowo-rdzawy w palecie bitwy** — piksele
    rysowane na mapie (173) wyświetlone pod paletą bitwy są różowe. Fix
    wariantu A (dokładnie 256 kolorów z palety, zero kolorów pośrednich):
    - `POL_Dac6To8` w port.h — konwersja 6→8 bitów jako `(v*255+31)/63` ==
      round == `(c*259+33)>>6` z DOSBox Staging (wzorzec; bit-replikacja
      `(v<<2)|(v>>4)` różni się o 1 LSB na 10 wpisach); jedyne źródło
      konwersji w porcie; renderer (port_sdl.cpp) LUT przez `POL_Dac6To8`;
    - **GPU v2** (port_gpu.cpp + present.frag): tekstura ramki = surowe
      INDEKSY R8_UNORM (upload 64 KB/klatkę bez konwersji CPU) + paleta jako
      osobna tekstura LUT 256×1 (B8G8R8A8) odświeżana tylko przy
      `POL_SetPalette`; kolor piksela = `texelFetch(idx)` → `texelFetch(LUT)`
      — bez samplera/interpolacji: każdy piksel okna = dokładnie 1 z 256
      wpisów palety (tryb zwykły i CRT);
    - testy: `tests/test_palette_lut.cpp` (0→0, 63→255, ścisła monotoniczność,
      tożsamość z round) + brakujący stub `POL_SetTextInputMode` w
      `tests/test_stubs.cpp` (link testów był zepsuty — naprawiony).
    `make`: 0 błędów; nic nie uruchamiane (zasada). Analiza:
    `raporty/rendering-kolory-256.md`. Retest usera: wskazać NA KTÓRYM
    ekranie (wybór kampanii / mapa świata / bitwa / teksty misji) róż
    zniknął — to rozstrzygnie hipotezę danych (patrz sekcja niżej).

15. **Podwójny kursor** (2026-09-04): gra rysuje własny sprite kursora (jak
    int 33h pod DOS-em), a systemowy kursor okna zostawał widoczny — dwa
    kursory naraz. Fix: `port_sdl.cpp` — `hide_system_cursor()` =
    `SDL_HideCursor()` + **całkiem przezroczysty kursor** `SDL_CreateCursor`
    (SDL3 3.4.14: sygnatura bez `depth`) ustawiany `SDL_SetCursor` jako
    fallback (część kompozytorów/gamescope ignoruje samo hide), wołane raz
    w `POL_VideoInit` po `SDL_CreateWindow` — obejmuje też backend GPU
    (współdzieli okno). `SDL_DestroyCursor` w `POL_VideoQuit`; nigdzie
    `SDL_ShowCursor`.
16. **Muzyka domyślnie włączona** (2026-09-04): `src/main.cpp` robił na
    sztywno `BigOffCDAudio`/`OffCDAudio` (jak instalacja bez CD) — muzyka
    startowała dopiero z opcji. Po fixie gałąź jak oryginalna instalacja
    z CD: oryginał `game/main.cpp:95-103` czytał bajt[0] `SETUP.INI`
    (litera napędu) — jest napęd CD → `BigOnCDAudio()`+`OnCDAudio()` i
    muzyka gra od wejścia. Port ma zawsze własny mikser, więc traktuje
    instalację jako „z CD". Wspólny gate `src/cd.cpp` obejmuje menu, teksty
    misji, bitwę i zwycięstwo/porażkę; **MusikOff w OPCJACH dalej wyłącza**
    (z powrotem `OffCDAudio`). Intro FLIC bez muzyki jak w oryginale (tylko
    narracja WAV). `port_fopen.cpp`: wyłącznie komentarz przy syntezie
    `SETUP.INI` (bajt[0] zostaje 0 — gra nie czyta go do decyzji o muzyce).
17. **Testy: sonda slotu zapisu izolowana od CWD** (2026-09-04): test
    `POLFopen.SondaSlotuZapisuNieLadujeSieJakoFail` robił `POL_fopen`
    z CWD uruchomienia, a w korzeniu repo leżą REALNE save'i usera
    (`save.001..004` — gra zapisuje do CWD) → flaky FAIL
    (`EXPECT_EQ(f, nullptr)` dostawał `FILE*`). Fix jak w teście slotu
    z plikiem: `chdir(POL_TEST_TMP)` na czas testu + przywrócenie CWD
    (`tests/test_fopen.cpp`). Testy: **72/72 PASS**.

## Przebadane — nie bug (2026-09-04, diagnostyka zamyka zgłoszenia usera)

18. **Panel bitwy: „rozjechane" ikonki / puste gniazda + „brak portretów"**:
    panel odbudowy (`case 20..25`, `game/graphics.cpp:1181-1228`) rysuje ikony
    **warunkowo od poziomu mleka** — progi 250/450/650/850/1050/1250; przy
    mleku 450–1249 (`case 21`) widoczna jest wyłącznie krowa na 5. gnieździe
    (275,99), pastuch dopiero od mleka ≥1250 **i** poziomu >26 — puste gniazda
    to stan z definicji oryginału. Górne pole (274,18) to listwa tła z arkusza
    3 + naklejka pustego gniazda — **portretu gra nigdy nie rysowała**
    (`face[16]` martwy kod; dane twarzy w GRAF.DAT, arkusze 4/5, używa
    wyłącznie edytor poziomów). Pasek mleka i gniazda pikselowo zgodne 1:1.
    W kodzie portu nic nie zmieniano. Raport:
    `raporty/panel-ikonki-budynki-portret.md`.
19. **Naprawa budynków „w nieskończoność"** — mechanika oryginału: rozkaz 8
    (napraw) wystawiany **zawsze** przy PPM na własnym budynku, bez
    sprawdzenia hp (`game/battle.cpp:1849-1870`); robotnik macha młotem
    (+2 hp/uderzenie, znaczniki placeN>219 zbiera budynek,
    `game/mover1.cpp:2384-2404`) tylko gdy `hp < maxhp`; ruina (`exist=2`)
    nie ma warunku stopu — macha bez efektu; ogień nie blokuje naprawy.
    `Rebuild()` to pusty stub w źródłach DOS — ruin nie da się odbudować.
    Port kompiluje `mover1/world/mapa` z `game/` bez zmian (1:1); jedyna
    zmiana tej rundy: diagnostyka opt-in **`POL_REPAIR_DEBUG=1`**
    (`src/battle.cpp` — log rozkazu 8: cel + exist/hp/maxhp budynku, oraz
    stan budynków i wybranej jednostki co ~2 s). Raport:
    `raporty/naprawa-nieskonczona.md`.
20. **Testy: +4 regresje łańcucha sprite'ów panelu** (`tests/test_image13h.cpp`):
    wycięcie 16×14 (jak ikona krowy z (16,14) arkusza 4) i gniazda 18×16
    (tło gniazda z (274,58) arkusza 3) przez `GetImage13h`, wklejenie
    piksel-w-piksel `PutImage13h`, nagłówek `[w][h][1][0]`, pitch wiersza
    (nic poza prostokątem nadpisane), przezroczystość koloru 0 przy how=1 vs
    pełne kopiowanie przy how=0. Do uruchomienia przez usera:
    `bash run_tests.sh`.

## Nowe zasoby (2026-09-03, fanowska strona jprokulewicz.github.io)

Pobrane wgetem (na polecenie usera, z opcją `--mirror`) i rozpakowane do
`../jprok_pliki/rozpakowane/` — 56 zipów, wszystkie OK (`jprok_pliki/RAPORT.md`
= raport agenta analizy). Kluczowe:

- **`polanie_cd/`** — dane wersji CD **z `PIC.DAT` i `LEVELS/LEVEL.26–48`** —
  dokładnie to, czego brakuje (kampanie 2–6, podkłady tekstów misji).
  Do weryfikacji nagłówków i ewentualnego użycia w porcie (zamiast
  syntetyzowanych plików z `POL_fopen`).
- **`polanie/`** — pełna wersja instalacyjna (`DATA.000–009` + `INSTALUJ.EXE`).
  **ODRZUCONE (user, 2026-09-03):** DATA.ARJ pod Linuksem wywala błędy
  dekompresji (działa tylko INSTALUJ.EXE w DOSBox), a `dysk/GRY/POLANIE/`
  i tak to już wypakowane pliki — ARJ to tylko kontener kompresji, bez
  wpływu na logikę. Nie analizować.
- `victory/` (niemiecka), `kodpol/`, `editpol/` (kody źródłowe CD/edytora),
  `misje/`, `poprawki/`, 25+ save'ów, `demo/` (nasze znane demo).

Uwaga: **GRAF.DAT naszej kopii jest już wypakowany do `extracted/grafika/`**
(ekran_00–14.png, font.png, paleta_00–13.png, setup1/setup2.png) — nie
powtarzać ekstrakcji; ewentualne ponowne uruchomienie ekstraktora tylko na
obcych kopiach (jprok_pliki) i wyłącznie do osobnych katalogów porównawczych
(`jprok_pliki/ekstrakcja/<kopia>/`).

Uwaga 2: **mapy poziomów to ASCII art** — plan planszy to zwykły tekst
(wiersz parametrów `D/E/T/M/G/P/N/*nazwa*` + siatka znaków po `!`); gotowe
w `extracted/teksty/poziomy/poziom_01..28.txt` (+ `00_legenda.txt`). Mapy
można czytać/edytować bez grafiki — to też format wejściowy edytora
(`editor/`).

Uwaga 3 (porównanie md5 z ekstrakcją `jprok_pliki/ekstrakcja/`, 2026-09-03):

- **Nasza kopia = pełna wersja detaliczna „Powrót Mirka"** (nasz DATA.000 =
  `polanie/DATA.000`, md5 zgodny), a jej GRAF.DAT/PAL.DAT to **unikalna
  rewizja** — nie „wersja demo", jak sądzono wcześniej.
- Fanowski GRAF.DAT (demo = polanie_cd, identyczne) ma w rekordzie 10
  **prawdziwy ekran menu/wyboru kampanii** (przyciski: Powrót Mirka /
  Przyjaciele / Porwanie / Wojna Magów / Wschodnia Pożoga / Południe
  w ogniu) — kandydat do podmiany zamiast rysowania przycisków
  w `src/main.cpp` (do weryfikacji: dolny blok 24 + palety).
- **PIC.DAT (demo = polanie_cd) podłożyć wprost jako `pic.dat`**: format
  zgodny co do bajta (`nr*64768`; 768 B palety + 64000 B ekran; indeksy
  0–34), realne podkłady ściśle lepsze niż syntetyczne czarne ekrany.

## Decyzje migracji zasobów (user, 2026-09-03, wieczór)

- **Wariant A — PNG w silniku 8-bit**: blit zostaje 8-bitowy; ładowanie
  obrazów (font, arkusze) z `extracted/` (PNG + wbudowana paleta) zamiast
  oryginalnych DAT-ów.
- **Intro**: jednorazowa konwersja SWIAT.DAT → **H.264** (ffmpeg, odpala
  user); w porcie dekodowanie klatek w locie przez libav do bufora pikseli
  (bez plików PNG), pamięć zwalniana po odtworzeniu.
- **Efekty/mowa (ZMIANA, noc): zostają w WAV** — bez konwersji na AAC;
  SDL3 dekoduje WAV natywnie. Muzyka zostaje S3M (libopenmpt).

## Raport ekranów gry + stan dekompilacji (2026-09-03, wieczór)

- **`raporty/ekrany-gry.html`** — gotowy raport agenta analizy kodu (czarne
  tło/złoto, Cinzel + IBM Plex, odwołania plik:linia). Publikacja przez
  Artifact **zablokowana** (sesja autoryzuje się tokenem API, a nie kontem
  usługi publikacji) — raport dostępny jako plik lokalny; otwarcie np.
  `xdg-open raporty/ekrany-gry.html`.
  Kluczowe ustalenia: 12 ekranów/etapów; 13 typów jednostek
  (`Udata[13][7]`, game/mover1.cpp:32) + 6 budynków; 55 misji (kampania 1 =
  28 rekordów LEVEL.DAT, kampanie 2–6 = LEVEL.26–52, granice
  30/35/41/46/52, game/battle.cpp:479); kampania 1 zaczyna się od **misji 15**
  (NewGame, game/main.cpp:418) i to mapa podboju 25 prowincji; kafel
  **16×14 px** (`x=(mouse.X-11)>>4, y=(mouse.Y-8)/14`, game/battle.cpp:747);
  kody literowe K-O-D-Y → DOSW/MAGIC/MILK/KILL/TREE/ENDV/ENDL/COUNT/SHOW
  (game/battle.cpp:1055-1250); AI losuje typ wroga z `mouse.X & 7`
  (decision.cpp), fale co `150 − diff·50` ticków.
- **Dekompilacja POLANIE.EXE — agent przerwany (429 limit)** po znacznej
  pracy; w `game-linux/dekompilacja/` pozostało: `odszyfruj.cpp` →
  `POLANIE_odszyfrowane.EXE` (odszyfrowanie zabezset), `emul8086.cpp`
  (własny emulator 8086 w C++ do trace'owania) → `POLANIE_rozpakowane.bin`
  (180 992 B, rozpakowany EXE), `klucze.cpp`, `trace.txt`–`trace6.txt`
  (trace6 = 363 MB), `FEEDBACK.md` (dyrektywy usera). **Brak RAPORT.md** —
  agent zginął przy weryfikacji „czy każdy refill cofa strumień dokładnie
  o 0x400" (rozpakowywacz EXE). Dyrektywy usera z FEEDBACK.md: nie analizować
  plików z instalatora (tylko katalog gry), EXE to kod nie assety, nie
  parsować .dat, punkt odniesienia stringów = game/ i editor/.

## Do zrobienia

- [x] **Panel bitwy: „tarcza lekko za wysoko" + biały prostokąt w ramce
      (screen usera, 2026-09-04) — przebadane, NIE BUG, nic nie zmieniano.**
      Tarcza (rozkaz STOP) na (275,19) — pozycja oryginalna: siedzi na 1.
      gnieździe nakładanym na belkę panelu (pierwsze gniazdo tła jest dopiero
      na y=38; region kliku STOP `mouse.MWindow(274,20,295,35)`,
      `game/battle.cpp:1581`). Biały prostokąt = **pasek mleka pełny na
      starcie** (`milk = maxmilk`, `game/battle.cpp:2672-2673`; kolor 255 =
      biel w palecie 3), nie brakujący portret — **portretu w grze nigdy nie
      było** (`face[16]` martwy kod, panel z portretem rysuje tylko edytor
      poziomów). Szczegóły: `raporty/panel-bitwy-tarcza-portret.md`.
- [ ] **Ekstrakcja porównawcza jprok_pliki — UKOŃCZONA (2026-09-03, 21:18)**
      przez usera: 487 plików, `jprok_pliki/ekstrakcja/MD5SUMS.txt` +
      `ROZNICE.txt` (log: `jprok_pliki/ekstrakcja.log`). Wyniki już
      przeanalizowane i wpisane do „Uwaga 3" wyżej; do ewentualnej
      dalszej eksploracji plików z ROZNICE.txt.
- [ ] **Test usera po fix 10/11 — WYKONANY (2026-09-03, noc)**: fix 10
      potwierdzony — gra weszła w bitwę (misja 15), cichego exitu ani
      `fopen FAIL` na level.dat już nie ma. Nieszkodliwy szum w logu:
      `save.001..004` (sprawdzanie slotów) i „Compositor released us"
      z gamescope. Nowe objawy do naprawy: (1) **biały „poświt" pikseli
      po naciśnięciu WYJŚCIA z gry** — gra nie wychodzi (w logu brak
      `PORT: exit(...)`), okno zostaje z białymi pikselami (doprecyzowanie
      usera: bitwa prawdopodobnie renderuje się OK, problem jest w ścieżce
      wyjścia); (2) **fonty w menu wyboru kampanii nadal się nie ładują**
      (przyciski = białe prostokąty z białym tekstem zamiast glifów, choć
      teksty fabuły renderują się dobrze); (3) dźwięk brak (audio = stub,
      znane). **Diagnozy ukończone (noc):**
      - **Fonty menu: FONT.DAT zdrowy** — „białe prostokąty" to workaround
        szkieletu przycisków w `NewGame` (`src/main.cpp:409-424`), który rysuje
        kolorami 254/255 w założeniu „pal10: 255=czerwony, 254=ciemny";
        w PAL.DAT tej kopii `pal10[254..255]` = czysta biel (nawet w
        polanie_cd 255 = jasny beż — założenie fałszywe w każdej rewizji).
        Fix: nadpisać wpisy DAC 255→czerwony, 254→ciemny między
        `LoadExtendedPalette(10)` a `RisePalette(1)` (przy okazji „oczy"
        z `PokazOczy` odzyskają kolory); zaktualizować komentarz.
      - **Biały ekran przy wyjściu**: „Koniec gry" z bitwy nie kończy programu
        (`quitLevel=1` → menu główne, jak na DOS-ie); okno „Koniec gry? Tak/Nie"
        czeka TYLKO na klik w 2 prostokąty (`battle.cpp:3024-3039`) — Escape
        ignorowany; biel = scena × paleta 1 (indeksy ~48–255 białe), po „Tak"
        paleta bitwy nie przywracana (`battle.cpp:1356`); zamknięcie okna (X)
        w bitwie nie kończy gry (`POL_QuitRequested()` sprawdzane tylko
        w pętli menu, `src/main.cpp:241-242`). Fix: (1) `Key==27` → „Nie";
        (2) `POL_QuitRequested()` też w bitwie (np. w `GetMsg13h`);
        (3) przy `quitLevel` przywrócić paletę bitwy.
      Workflow `diag:bitwa-bialy-ekran`+`diag:menu-fonty` UBITY (za długo
      działał, na życzenie usera) — z journala uratowana diagnoza fontów;
      agent ścieżki wyjścia ukończony (raport powyżej).
- [ ] **Test usera (2): jednostki nie poruszają się po mapie** — „tak jakby
      klikanie nie działało" (bitwa, misja 15; klik w menu działa).
      **Diagnoza UKOŃCZONA — znaleziona przyczyna (agent klik→ruch):**
      tapnięcie PPM (gładzik: DOWN+UP w jednej pompie) **przepada między
      odpytaniami pętli bitwy**. Kwirk `GetMsg13h` (`src/mouse.cpp:49-51`):
      `if (!Button) Button = Ile(1);` — gdy przycisk już puszczony, zużywa
      licznik kliknięć PPM i ustawia `Button = 1` (wartość licznika = jakby
      LEWY, nie 2!); potem `battle.cpp:422` czyta `ile1 = Ile(1)` = 0, lepka
      `if (ile1) Button = 2` (`battle.cpp:1053-1054`) nie działa i warunek
      ruchu `battle.cpp:1685` (`Button==2 && ile1`) nigdy nie przechodzi.
      Pod DOS-em nie wychodziło (pętla odpytywała mikrosekundowo, klik
      zawsze przyłapany z `Button==2`); w porcie iteracja jest rozdmuchana
      (4–6 pomp × `POL_Present` = LUT 64k + upload 256 KB, tempowane przez
      Wayland/gamescope) → DOWN+UP lądują między odpytaniami. LPM działa
      (lepka `ile0`), PPM nie ma skutecznej lepki — dokładnie objaw.
      **Naprawa:** (1) `port/port_sdl.cpp:333` (`POL_MousePos`): lepki bit
      przycisku dopóki licznik nie skonsumowany (`if (!buttons &&
      clicks_right) buttons |= 2; if (!buttons && clicks_left) buttons |= 1;`),
      bit zdjąć przy konsumpcji w `POL_ClickCount` + bezpiecznik ~200 ms
      (żeby `GButtonUp`, `src/mouse.cpp:24-27`, się nie zawiesił);
      (2) `src/mouse.cpp:49-51` — `if (!Button) { if (Ile(1)) Button = 2; }`;
      (3) opcjonalnie: nie prezentować w każdej pompie (`port_sdl.cpp:309`);
      (4) jednorazowy log w `DispatchEvent` dla ostatecznego rozstrzygnięcia
      (jeśli `ile1==1, Button==2, mouseCommand!=10` → hipoteza ticków
      `licznik`).
      **Naprawa WDROŻONA (2026-09-03, noc, agent) — czeka retest usera.**
      (1) `port/port_sdl.cpp`: statyczne `down_left_ms`/`down_right_ms` +
      `STICKY_CLICK_MS 200`; `POL_MousePos` raportuje lepki bit (PPM→2,
      LPM→1) gdy przyciski fizycznie puszczone, a licznik klika czeka;
      bit gaśnie przy konsumpcji w `POL_ClickCount` albo po bezpieczniku
      200 ms od DOWN (bezpiecznik gasi też przeterminowane kliki —
      `GButtonUp` się nie zawiesi). (2) `src/mouse.cpp:55-60` (kwirk):
      `if (!Button && Ile(1)) Button = 2;` (`// PORT:`) — PPM nie jest już
      podmieniany na lewy. (3) present w `POL_PumpEvents` dławiony do
      ~60 fps (próg 15 ms) — iteracja pętli bitwy przestaje być
      rozdmuchana (korzeń kwirku); jawne `POL_Present` po rysowaniu i
      pierwsza ramka natychmiastowe. (4) Log diagnostyczny w
      `src/mouse.cpp` (`MOUSE: Button=…` / `MOUSE: Ile(1)=…`) pod
      `POL_MOUSE_DEBUG=1`, domyślnie wyłączony. Kompilacja czysta,
      testy 39/39. **Retest usera:** `POL_MOUSE_DEBUG=1 bash run_port.sh`,
      misja 15 — zaznacz LPM (tap i przeciągnięcie), rozkaz ruchu PPM
      (jeden tap gładzika i fizyczny przycisk); przy PPM oczekiwane
      `MOUSE: Button=2 …` + `MOUSE: Ile(1)=1 …`.
- [x] **Audio + testy jednostkowe — UKOŃCZONE (2026-09-03, noc, podagenci)**.
      **Audio** (`port/port_audio.{h,cpp}`): jeden strumień SDL3 (F32 stereo
      48 kHz) z własnym callbackiem miksera; muzyka S3M renderowana na bieżąco
      przez libopenmpt 0.8.8 (loop w callbacku, głośność
      MASTERGAIN_MILLIBEL); efekty WAV 8-bit mono 22050 dekodowane raz
      (`SDL_LoadWAV_IO`+konwersja do float, cache ~55), do 8 głosów, miks
      addytywny + clamp (bez AAC — decyzja usera). `src/cd.cpp` →
      `POL_MusicPlay/Stop/SetVolume`, `PlayNext/Previous`, `CheckCD` no-op;
      `src/menegdma.cpp` gra efekty jako głosy, zegar 18,2 Hz czyści
      `jest_odtwarzany` (semantyka ISR DMA, `battle.cpp:391`); `POL_AudioInit`
      zawsze, niezależnie od flagi SB. Mapowanie PlayTrack(n)→S3M w
      `port/track_map.h` (2=menu, 3=intro, 4=porażka→GRAF_007, 5=zwycięstwo→
      GRAF_006, plansze 6–14→012/013/014/016/017/018/004/011/015; **hipoteza
      wg tytułów modułów — weryfikacja na słuch**). **Testy** (gtest 1.17.0,
      `game-linux/tests/`, `make test`, 35/35 PASS): normalizacja ścieżek
      (8), POL_fopen+syntezy (7), image13h (6), CD (9), mapowanie utworów (4,
      `port/track_map.h`), PAIRY (2, `tools/pairy.h` wydzielone). Skrypt
      użytkownika **`run_tests.sh` W KATALOGU GŁÓWNYM** (obok `run_port.sh`):
      build+run (kod wyjścia = wynik), `-c`/`--compile`, `-h`,
      `-- --gtest_filter=...` — agent napisał, **do odpalenia przez usera**:
      `bash /home/k/Projects/polanie-src/run_tests.sh`.
      **Bonus: test wykrył realny bug** — `SetExtendedPalette`
      (`src/image13h.cpp:886-890`): `char` w g++ jest ze znakiem, więc
      `0xFF >> 2` dawało 255 zamiast 63 w DAC (wszystkie palety z wartościami
      >127 przygaszały źle); naprawione rzutowaniem na `unsigned char`
      (test `Image13h.SetExtendedPalette_DacDzielenieNa4`).
      Zostało: mikser bez testów jednostkowych (wymaga słuchu usera),
      warstwa wideo `port_sdl.cpp` bez testów.
- [x] **Efekty WAV: cisza — NAPRAWIONE (2026-09-03, noc, agent; 43/43 PASS,
      build czysty).** Hipoteza SETUP.INI/jest_SB nietrafiona (SND.Init wołany
      bezwarunkowo, mikser zawsze startuje; SETUP.INI istnieje na dysku).
      Prawdziwe przyczyny: (1) **killer** — `port_audio.o` wołał glibc `fopen`
      zamiast `POL_fopen` (nagłówki systemowe robiły `#undef fopen`) → efekty
      czytały z CWD, nigdy nie sięgały do danych; (2) **głosy jednostek 1–183
      żyją w `DATA/SOUND.DAT` pełnej wersji CD** (kontener: PCM 8-bit 22050 +
      183×i32 LE długości na końcu), nieobecny w kopii (tylko W001–W055) —
      nowy loader `port/sound_dat.{h,cpp}` ładuje go też z
      `jprok_pliki/rozpakowane/polanie_cd/DATA/SOUND.DAT`
      (log: `PORT: SOUND.DAT: 183 probek glosow wczytane`); (3) głosy leciały
      2,2× za szybkie — dodany resampling liniowy w mikserze; (4)
      case-insensitive katalogi w `POL_ListFiles`/`ci_fopen` (pośrednia wersja
      fixa miała nieskończoną rekurencję — to był segfault testów usera);
      (5) `POL_MixerInit` inicjuje `SDL_INIT_AUDIO` (wcześniej start przed
      SDL_Init). `s000–s003` w logach usera = FLIC-y wideo (luka, nieszkodliwa).
      „Crash" z coredumpów 22:40–22:47 = SIGSEGV z `timeout -s SEGV` (metoda
      diagnozy, nie błąd portu). **Retest usera:** `bash run_port.sh` → misja →
      głosy jednostek; opcjonalnie
      `cp jprok_pliki/rozpakowane/polanie_cd/DATA/SOUND.DAT dysk/GRY/POLANIE/DATA/`;
      przy okazji retest tap PPM z `POL_MOUSE_DEBUG=1`. Szczegóły: STATUS.md
      punkt 12b.
- [x] **Mapa S3M→GM→SFZ (VSCO) — UKOŃCZONA (2026-09-03, noc, agent)**.
      Katalog `vsco/` = VSCO Community Edition: 75 .sfz (3,1 GB, 3167 WAV);
      `.ariax` = sesje silnika Aria (XML, absolutne ścieżki Windows —
      **bezużyteczne dla sfizz**); `default_path` w SFZ ma backslashe —
      warstwa wczytująca musi normalizować. **Wszystkie 100 instrumentów w
      GRAF_001–018.s3m to patche Adlib/OPL (typ=2, sygnatura „SCRI"), zero
      sampli PCM** — tor S3M zostaje na libopenmpt (openmpt sam syntetyzuje
      OPL, soundfont mu zbędny); mapa S3M→GM prowadzona po nazwach patchy.
      **Zapisane:** `game-linux/port/sfz_map.h` — `kSfzMap[128]` (pełny GM
      0–127 → .sfz z fallbackami), `kS3mInstrumentMap` (100 wpisów:
      moduł→indeks→nazwa→GM/perc-note→sfz; **64 pewne / 36 UNCERTAIN**,
      pole `niepewna`), helpery `find_sfz_for_gm()` /
      `count_s3m_instruments()`; header niczego nie includuje (walidacja
      samodzielna g++ C++17/C++20). Dokument: `raporty/mapa-s3m-midi-sfz.md`
      (pełne tabele + założenia). Narzędzia: /tmp/s3mdump.cpp,
      /tmp/middump.cpp, /tmp/check_sfz_map.cpp (C++, parsery wg
      `game/plays3m.cpp:53-121` i SMF). **Fanowskie MIDI**
      (`jprok_pliki/rozpakowane/utwory_mid/*.mid` — marzenia 99 BPM,
      muzyka z lasu 92, wymarsz na wojnę 92; `luzyce*/LUZYCE.MID` 55 BPM;
      autor Krzysztof Fornalski): SMF 1, jeden program-change na kanał,
      **brak kanału 10**; programy GM: **42/43/47/48/59/79/80 — wszystkie
      pokryte VSCO** (80 Ocarina→FluteSusVib). Perkusja: `GM-StylePerc.sfz`
      (220 regionów, ~mapa GM; braki 43/44/45/57/58 → cisza).
      **Decyzje usera (2026-09-03, noc):** silnik SFZ = **sfizz**
      (`sfizz-lib` zainstalowane przez usera); integracja = **auto-fallback
      (MIDI gdy istnieje plik .mid, inaczej S3M) + parametr CLI
      `--audioType=s3m|sfz`**; dodatkowo user zażądał **optymalizacji +
      AVX2 w miksie** (runtime dispatch `__builtin_cpu_supports`).
      **[x] Tor MIDI+SFZ — UKOŃCZONY (2026-09-03, noc, agent; retest usera
      68/68 PASS):** odtwarzacz MIDI (SMF, `port/midi_parser.*`) → sfizz
      (VSCO wg `kSfzMap`, kanał 10 → GM-StylePerc, `port/sfz_engine.*`) →
      głosy miksera; auto-fallback + `--audioType=s3m|sfz|auto`
      (przez `run_port.sh`); mapowanie utworów `port/midi_map.h`
      (2=menu→LUZYCE.MID 55 BPM loop, 3=teksty→marzenia.mid raz,
      6/8/10/14=las, 7/9/11/12/13=wymarsz; 4/5=bez .mid→S3M); AVX2 w miksie
      (`port/mix_simd.*`, dispatch runtime, ≈1,34–1,68× na resamplingu);
      backslashe `default_path` normalizowane przy wczytaniu. Naprawy
      podczas oddania: ogon bufora przy końcu niezapętlonego .mid
      (kontrakt zwrotki `POL_SfzEngineRender` udokumentowany w
      `port/sfz_engine.h`), stara asercja testu, arytmetyka testu
      `KoniecUtworu` (end+tail = 126000 = 262,5×480 → deterministyczny
      partial-return; `make` po edycji źródła, bo wyścig edycja-vs-build
      dał stary bin). **`run_tests.sh`: przy FAIL same dopisują na końcu
      sekcję „2b/2 Detale zepsutych testów"** (pełny przebieg przez tee,
      filtr z linii `[ FAILED ]`). Retesty usera: `run_tests.sh` (68/68
      ✅), `run_port.sh` (auto) vs `--audioType=s3m`; **rendery A/B S3M:
      `./run_rendery.sh`** (tor OPL przez openmpt123 + tor VSCO przez
      `tools/s3m_vsco_render`, 18×2 WAV do `game-linux/rendery/`,
      raport: `raporty/s3m-rendery.md`; packing wzorców = standardowy
      S3M, odchyłki tylko w instrumentach). **Podagent „myszka na
      jednostki + WAV w misji" UKOŃCZONY (2026-09-04) — pełna diagnoza
      i poprawki niżej (bullet „Myszka na jednostki nie działa...").**
- **Decyzje usera (2026-09-04):** (1) **VSCO WYCOFANE** („nie uzywaj
  vsco, unstable") — tor muzyki portu = **S3M** (oryginalna muzyka);
  `run_port.sh` od teraz domyślnie podaje `--audioType=s3m` (tor sfz
  tylko na jawny parametr); tor 3 S3M→VSCO **SHELVED** — nie integrować,
  narzędzia (`tools/s3m_vsco_render.cpp`, `run_rendery.sh`) zostają jako
  artefakt RE; docelowo też default `auto` w binarce = S3M (zmiana po
  podagencie mysz+WAV, wspólny plik `port_audio.cpp`). **Default `auto`
  w binarce = S3M — WDROŻONE (batch 2, `port_audio.cpp:127`; jawne
  `--audioType=auto|sfz` nadal działa).** (2) **WASD jako zamiennik
  strzałek do przewijania mapki — WDROŻONE (batch 2, 2026-09-04; user
  potwierdził: „wsad dziala")**: port nie ma SDL_TEXTINPUT — jedyny
  ekran wpisywania tekstu to `Write13h` (`src/image13h.cpp:734`, imię
  w bitwie) → nowe `POL_SetTextInputMode` (`port.h:45`): poza trybem
  tekstu W/A/S/D pushują kody strzałek DOS (`port_sdl.cpp:283-313`);
  w bitwie litery W/A/S/D nie dochodzą jako ASCII (sekwencje
  DOWY/SHOW/MAGIC tracą pierwszą literę — koszt zaakceptowany decyzją
  usera).
- **Batche 1+2 zacommitowane (2026-09-04, agenci; 12 commitów
  `c5acaa4`–`7168d9f`).** Batch 1: tor MIDI+sfizz (port/include/main),
  testy + `run_tests.sh` (detale FAIL na końcu), default S3M w
  `run_port.sh`, narzędzie renderek + raporty, dekompilacja (źródła) +
  `.gitignore` (trace*.txt, *.bin, *.EXE, rendery/), STATUS. Batch 2:
  naprawy mysz/klik (kwirk `POL_ClickPeek`, liczniki żyją do odczytu
  2 s), WASD z trybem tekstu, instrumentacja bitwy (`src/battle.cpp` =
  kopia 1:1 + logi, Makefile), log startu głosu + default S3M w
  binarce, resztki (`port_shims.cpp`, `src/cd.cpp`, `src/dpmi.cpp`,
  `src/playfli.cpp`, `src/image13h.cpp`, `tools/pairy.h`,
  `tools/polanie_extract.cpp`, raporty HTML, dekompilacja
  `analiza.cpp`/`probe.cpp`/`DumpStub.java`) — **repo buduje się z
  czystego checkoutu**. Pominięte na batch 3: `raporty/mysz-na-jednostki-wav.md`,
  `raporty/mapa-s3m-midi-sfz.md`, `raporty/s3m-rendery.md`;
  `run_rendery.sh` NIE istnieje (VSCO shelved — bez znaczenia). **Batch 3
  (po retestach zegara i GPU/CRT):** fix zegara (src/menegdma.cpp,
  src/battle.cpp), backend GPU (port/port_gpu.*, port/present.*,
  port_sdl.cpp, Makefile), raporty (architektura-cpp23.md,
  backend-wideo-vulkan-2d.md, diagnoza-zegar-timer-sdl3.md + 3 czekające
  wyżej), edycje STATUS.md.
- **NOWE SYMPTOMY (retest usera, 2026-09-04):** „wsad dziala", ALE:
  (1) **chodzenie i atak jednostkami nie działa — NAWET przeciwnicy
  i krowy stoją** (krowy błąkają się same → NIE ścieżka rozkazów;
  niemal pewny korzeń: **globalny zegar/tick pętli bitwy nie jest
  inkrementowany w porcie** — podejrzani: int 8h/1Ch, `game/timer.cpp`,
  `Timer`/`Krok`/`Czas`); (2) **brak wszystkich WAV w misji** (muzyka
  gra) — głosy wyzwalane z pętli bitwy (`Msg.dzwiek`, battle.cpp:391);
  zamrożona pętla = komendy nieobrabiane = cisza; klik działa (mysz
  odpytywana osobno); (3) „coś jakby misja 1 nie startuje" (możliwa
  regresja: liczniki klików żyją teraz 2 s — fantom kliku „Dalej" może
  być zjadany przez zły ekran); (4) **ikonka przesunięta wobec paska**
  (osobny bug rysowania: pasek vs ikonka — slicing arkusza albo
  kursor/hotspot). **DIAGNOZA UKOŃCZONA + FIX WDROŻONY (2026-09-04, agenci;
  make 0 błędów, nic nie uruchamiane):** jedyny timer portu = callback
  `src/menegdma.cpp:39` w sygnaturze **SDL2** `(Uint32 interval, void*)`,
  a SDL3 (3.4.14) woła `(void *userdata, SDL_TimerID timerID, Uint32
  interval)` → ABI x86-64: funkcja czyta interval z userdata=NULL=0,
  `licznik++` wykonuje się RAZ (~55 ms), `return 0` każe SDL3 **skasować
  timer** → licznik zamrożony na 1 (`-w -fpermissive` tłumił niezgodność
  typów — Makefile:8). Skutek: pętla bitwy `while (licznik - licznik2 <
  speed)` (src/battle.cpp:463) kręci pętlę wewnętrzną (kliki/rozkazy żyją —
  stąd „wsad dziala"), a pętla zewnętrzna (castle[].Run/Prepare, AI/krowy,
  bramka `Msg.dzwiek`→WAV, EndLevel) nie wykonuje się NIGDY → symptomy 1–3
  to JEDEN korzeń; (4) ikonka = ten sam korzeń (UI mrożone w 1. klatce:
  panel + marker `Msg.count` rysowane raz; rewizja GRAF.DAT ODRZUCONA
  z dowodem pikselowym — układ paska identyczny w 4 kopiach; do oceny po
  fixie zostają hipotezy B3 hotspot kursora i D2 fantom kliku). Fix:
  sygnatura SDL3 + `return interval` (menegdma.cpp:39, komentarze // PORT:),
  logi `PORT: TIMER: zainstalowano id=%u 18.2 Hz` oraz `PORT: BATTLE: tick
  licznik=… decisionFaza=…` (~1/s) pod POL_MOUSE_DEBUG (battle.cpp:472).
  **Retest usera:** `POL_MOUSE_DEBUG=1 bash run_port.sh` → `PORT: TIMER:`,
  tick ~18/s, ruch/atak/krowy/WAV/koniec misji. Pełny raport:
  `raporty/diagnoza-zegar-timer-sdl3.md`.
- **Analiza „co dać z C++23" — raport: `raporty/architektura-cpp23.md`**
  (agent, read-only, 2026-09-04). Top 5: A1 zegar+DI (to był właśnie bug
  sygnatury — patrz wyżej), D1/D2 Makefile (C++23 dla portu i testów,
  `-Werror=permissive` — bug timera byłby błędem kompilacji; `-w` tylko dla
  kopii Watcom), A2 `std::expected<MidiSong,string> parse_smf(std::span)`
  + `std::byteswap` za ręczne be16/be32 + `enum class`/`std::unreachable`
  (midi_parser), A4 RAII audio (`std::scoped_lock` za ręczny SDL_LockMutex
  ~10 miejsc, RAII na openmpt/sfizz), A7 `consteval` walidacja map
  kSfzMap[128]/kTrackMap. Odrzucone realnie: mdspan na 320×200, moduły,
  jthread/move_only_function. Ryzyka: ABI struktur z plików (save'y,
  SOUND.DAT), wyjątki w starym flow, reformatowanie kopii 1:1 (zabija
  diff-weryfikowalność), stałe tempa (20/50/speed).
- **Backend wideo SDL3 GPU (Vulkan) + shadery CRT — analiza + DECYZJA USERA
  „dodajmy to vulkan api, celem bylyby wydajne shadery a'la crt" +
  implementacja (2026-09-04, agent; make 0 błędów).** Analiza
  (`raporty/backend-wideo-vulkan-2d.md`): okno natywne 320×200, renderer
  SDL3 integer-scale, LUT CPU 64000 px przy present, dławienie 15 ms;
  środowisko: SDL3 3.4.14 z SDL_gpu.h, libvulkan 1.4.357, ICD intel/nouveau/
  nvidia, **brak lavapipe** → fallback obowiązkowy. Werdykt: (a) SDL3 GPU —
  TAK (jedyna droga do shaderów; zysku FPS nie będzie — gra rysuje ~18 fps);
  (b) „prawdziwy klient 2D" (sceny/sprajty) — NIE (gra rysuje wprost do
  banku w ~15 modułach, brak warstwy obiektów; tick 18.2 Hz nietknięty —
  animacje kalibrowane). Implementacja: `port/port_gpu.{h,cpp}` (572 l.:
  LUT CPU → tekstura B8G8R8A8 → quad + shader; minimize → NULL swapchain =
  pomiń klatkę; przebudowa pipeline przy zmianie formatu; vsync FIFO),
  `port/present.vert|frag` (default nearest+integer scale wycentrowany;
  CRT: scanlines co 2 wiersze + barrel + winieta, siły jako uniformy),
  SPIR-V: glslangValidator (fallback glslc) → `build/*.spv` → `spv_data.cpp`
  (symbole `present_*_spv`); `-DPOL_NO_SPV` gdy brak kompilatora (backend
  się nie włącza, fallback); Makefile +40 (reguły SPIR-V ZA regułami
  obiektów — pierwsza wersja przejęła cel domyślny!); `port_sdl.cpp` +82
  (`// PORT: GPU backend`): wybór przez env `POL_VIDEO=gpu` (argv nie
  wchodzi — parsowanie CLI w src/main.cpp, kopia 1:1), fallback z logiem
  „PORT: GPU: brak urzadzenia Vulkan, fallback do renderera"; mysz bez
  renderera liczona przez skalę. **DOMYŚLNIE nadal renderer** (zero zmian
  na starej ścieżce — świadomie: najpierw retest zegara). Test usera:
  `POL_VIDEO=gpu ./pol2 4`, CRT: `POL_CRT=1 POL_VIDEO=gpu ./pol2 4`; log
  `PORT: GPU: skala: … (Vulkan/SDL_GPU)`. Do decyzji po teście: default =
  GPU?, gamescope do zdjęcia?, siła efektu CRT (CRT_WARP/CRT_SCAN/CRT_VIG
  w port_gpu.cpp).
- [ ] **Dokończenie dekompilacji POLANIE.EXE** — agent przerwany na 429
      (limit użycia); wznowić z istniejącego workspace'a
      (`game-linux/dekompilacja/`, cel: RAPORT.md).
- [ ] **Podłożenie zasobów z kopii fanowskich** (wymaga potwierdzenia usera):
      PIC.DAT → `pic.dat` (na pewno); GRAF.DAT + PAL.DAT z polanie_cd → test
      ekranu wyboru kampanii (potem ewentualnie usunięcie workaroundu
      z `src/main.cpp`).
- [ ] **Migracja warstw na `extracted/` (wariant A)** — obrazy z PNG.
- [x] **Muzyka S3M przez libopenmpt + efekty WAV w jednym strumieniu SDL3**
      (2026-09-03, noc): `port/port_audio.{h,cpp}` — własny callback miksera
      (F32 stereo 48 kHz), muzyka renderowana na bieżąco przez
      `openmpt_module_read_interleaved_float_stereo`, efekty WAV (8-bit mono
      22050) dekodowane `SDL_LoadWAV_IO` + cache glosów; `src/cd.cpp`
      `PlayTrack(n)` → `extracted/audio/muzyka/GRAF_0xx.s3m` (mapowanie:
      `port/track_map.h`, hipoteza wg tytułów modułów — do weryfikacji na
      słuch), loop dla menu (2) i plansz (6–14); `src/menegdma.cpp` gra
      efekty jako glosy miksera (nie uciszają muzyki), zegar 18.2 Hz czyści
      `jest_odtwarzany` jak ISR DMA. libopenmpt 0.8.8 obecny na systemie.
- [x] **Efekty/mowa: zostają w WAV** (decyzja usera 2026-09-03, noc — bez
      konwersji na AAC; SDL3 czyta WAV natywnie).
- [ ] **Intro: konwersja SWIAT.DAT → H.264** (ffmpeg, odpala user) + dekoder
      libav w porcie, klatki w locie, pamięć zwalniana po odtworzeniu.
- [ ] **Kampanie 2–6 („nowe plansze", poziomy 26–47)**: `InitBattle` czyta
      `levels/level.N` (binarne, nagłówek `EditStr` po `MaxX*MaxY*4`) — plików
      nie ma nigdzie (GRAF/ ma tylko LEVEL.DAT = kampania 1). Trzeba znaleźć
      w wydaniach fanowskich albo syntetyzować/odszyfrować.
- [ ] Skala: sprawdzić pod gamescope (nearest 8×8 na 2560×1600); ewentualnie
      gamescope `-w 320 -h 200`.

- [x] **Tor MIDI+sfizz (fanowskie .mid + VSCO CE)** (2026-09-03, noc): parser
      SMF `port/midi_parser.{h,cpp}` (format 0/1, delta+running status, mapa
      tempa segmentowo, SMPTE), silnik `port/sfz_engine.{h,cpp}` (sfizz 1.2.3,
      jeden syntezator na kanał MIDI, program GM → .sfz wg `port/sfz_map.h`,
      kanał 10 → GM-StylePerc; backslashe Windows w `.sfz` normalizowane przy
      wczytywaniu do pamięci — bez kopii plików), mapa utworów CD → .mid
      `port/midi_map.h` (2=menu→LUZYCE.MID 55 BPM, 3=teksty→marzenia.mid
      99 BPM, 6–14=plansze→„muzyka z lasu" / „wymarsz na wojnę" naprzemiennie;
      4/5 porażka/zwycięstwo → fallback S3M). Auto-fallback w `POL_MusicPlay`
      (`.mid` nie ma/nie wczyta się → S3M jak dotąd); CLI `--audioType=s3m|sfz`
      (`port/audio_opts.h`, przepuszcza `run_port.sh`). Gorące pętle miksera
      (`port/mix_simd.{h,cpp}`) z dispatchem AVX2 (`__builtin_cpu_supports`,
      log w `POL_MixerInit`), resampling głosów AVX2→1,34× vs skalar (gtest
      benchmark). Naprawa 2026-09-03 późny wieczór: koniec niezapętlonego
      utworu .mid zostawiał ogon bufora miksera ze starymi próbkami
      (`done=frames` bezwarunkowo) — `POL_SfzEngineRender` zwraca teraz liczbę
      wypełnionych klatek, `mix_cb` zeruje resztę.
- [ ] **„Myszka na jednostki nie działa / WAV brak" (retest usera 2026-09-03
      noc, po fixie tap PPM) — diagnoza + poprawki WDROŻONE (2026-09-04, noc,
      agent ścieżki myszy+WAV), czeka retest.** Rozbieżność: build był świeży
      (pol2 23:52 > fix 22:07), więc lepki bit działał, a objaw został.
      Statyczna analiza (2 równoległe śledztwa) nie znalazła śmierci kliku
      WEWNĄTRZ pętli bitwy (maszyna lepki-bit+kwirk+konsumpcja zgodna z DOS);
      znalazła za to trzy realne odchylenia port↔DOS, z których każde umie
      zabić klik/komendę głosową, plus braki w logach:
      (1) **bezpiecznik 200 ms kasował nieskonsumowane liczniki kliknięć**
      (`port_sdl.cpp`) — naruszenie semantyki int 33h (licznik DOS żył do
      odczytu): każda faza >200 ms bez odpytywania (Decision()/AI, ladowanie,
      typewriter tekstów, delay(300)) gubiła klik bez śladu → teraz licznik
      żyje do konsumpcji; kasowanie tylko dla klików >2 s (przejścia ekranów,
      żeby klik „Dalej" z tekstów nie dotarł do bitwy jako fantom)
      `port_sdl.cpp` `STALE_CLICK_MS`;
      (2) **kwirk `GetMsg13h` konsumował licznik PPM przed odczytem bitwy**
      (`src/mouse.cpp` `Ile(1)` → `battle.cpp:422 ile1=0` → warunek rozkazu
      `Button==2 && ile1` nigdy nie przechodził, gdy lepki bit zdążył wygasnąć)
      → nowy `POL_ClickPeek` (podgląd bez konsumpcji), kwirk nie zjada licznika;
      (3) log `ReadMouse13h` przy każdej zmianie X/Y (setki zapisów/s do
      niebuforowanego stderr pod POL_MOUSE_DEBUG=1) mógł rozdmuchiwać iterację
      → dławiony do ~4/s (zmiana przycisków zawsze).
      Tor WAV: statycznie zdrowy (mikser żyje = MIDI gra; głos nie może
      wisieć na active=1; bramka `jest_odtwarzany` otwiera się ≤1 tick);
      cisza = brak komend głosowych = wspólny korzeń z myszką (hipoteza A).
      **Diagnostyka jednego przebiegu (POL_MOUSE_DEBUG=1):** log zdarzeń
      SDL DOWN/UP (`PORT: MSEV:`), lepki bit/wygaśnięcie/kasowanie, konsumpcja
      z wiekiem (`konsumpcja Ile(...)`), wejście DispatchEvent ze stanem
      (`PORT: BATTLE: Dispatch: ile0/ile1/cmd/mode/selIFF...`), rozkazy
      ruch/atak i ZaznaczObiekt, bramka SND, wywołanie `operator()` i start
      głosu (`PORT: SND:`). Nowa kopia `src/battle.cpp` (logika 1:1 z
      `game/battle.cpp`, diff = wyłącznie wstawki `// PORT:`; Makefile buduje
      battle.o z src/). **Retest:** `POL_MOUSE_DEBUG=1 bash run_port.sh` →
      misja → LPM zaznacz, PPM rozkaz; szukać łańcucha MSEV→Dispatch→
      rozkaz→bramka SND→glos start (drzewko decyzyjne w raporcie agenta).

## Kolory renderowania — wariant A palette-exact (2026-09-04)

Retest usera po fixie ABI timera: „teraz klikanie działa i animacje" —
potwierdzone. Nowe zgłoszenie: „bardzo dziwny dithering i artefakty... róż
gdzie zieleń" (tryb domyślny renderera). Audyt + naprawa: pozycja 14 wyżej.
Skrót wdrożenia (wariant A — każdy piksel okna = dokładnie 1 z 256 wpisów
palety, zero kolorów pośrednich):
- `port.h`: `POL_Dac6To8` = `(v*255+31)/63` (== round == DOSBox Staging
  `(c*259+33)>>6`; bit-replikacja `(v<<2)|(v>>4)` różniła się 1 LSB na 10
  wpisach) — jedyne źródło konwersji DAC 6→8 w porcie;
- `port_sdl.cpp` (renderer): LUT przez `POL_Dac6To8`;
- `port_gpu.cpp` + `present.frag`: wariant v2 — ramka = surowe indeksy
  R8_UNORM (64 KB/klatkę, koniec LUT CPU), paleta = LUT 256×1 odświeżana
  tylko przy `POL_SetPalette`, kolor = `texelFetch` (zero interpolacji);
- `tests/test_palette_lut.cpp` (nowy) + brakujący stub `POL_SetTextInputMode`
  w `tests/test_stubs.cpp` (link testów naprawiony); `make`: 0 błędów.
Pełna analiza: `raporty/rendering-kolory-256.md`. Ustalenia dodatkowe:
openpol celowo NIE robi `>>2` na PAL.DAT (pełne 0–255; różnica ≤3/255 tam,
gdzie `bajt & 3 ≠ 0`); polanie-game (SourceForge) rozszerza gołym `<<2`;
`SetExtendedPalette` niszczy rgb in place (`>>2`), `DownPalette` odtwarza
(`<<2`) — w porcie zachowane 1:1 z DOS-em.

Retest (rozstrzyga, czy róż znika i na którym ekranie zostaje): `bash
run_port.sh` (renderer), `POL_VIDEO=gpu ./pol2 4` (GPU v2), `bash run_port.sh
4` (bez gamescope), `make test`. Ekrany do wskazania: wybór kampanii / mapa
świata / bitwa / teksty misji.

Otwarte warianty (do decyzji usera): pełne 8-bit PAL.DAT jak openpol
(świadomie bez `>>2`); podmiana GRAF.DAT/PAL.DAT/pic.dat na rewizję CD z
polanie_cd (prawdziwy ekran kampanii); kolory szkieletu przycisków kampanii
254/255 (pal10); siła efektów CRT.

## Uruchomienie (użytkownik)

```
bash run_port.sh            # kompilacja + gamescope + gra
bash run_port.sh 4          # bez gamescope, okno 4×
bash run_port.sh -s         # strace otwartych plików
bash run_port.sh -c         # tylko kompilacja
bash run_port.sh --audioType=s3m  # wymuszenie toru S3M (libopenmpt)
bash run_port.sh --audioType=sfz  # wymuszenie toru MIDI+sfizz

# backend wideo SDL3 GPU/Vulkan (2026-09-04, nowy tor; domyślnie renderer):
POL_VIDEO=gpu ./pol2 4            # GPU + integer scale w shaderze
POL_CRT=1 POL_VIDEO=gpu ./pol2 4  # + efekt CRT (scanlines/barrel/winieta)
```

## Batch 5 — kontynuacja (2026-09-04): leczenie, zdrowie krów, „Koniec gry?", official

- **`98d7bf7` — fix `-funsigned-char`** (placeN, znaczniki 190–227): naprawa
  budynków/drzew/palisady/mostu działa — user potwierdził. Nowe testy
  `test_char_semantics`.
- **`3a386cb` — mechanika leczenia** (nowe `src/unit_heal.cpp` + hook w
  `src/battle.cpp`, 8 testów UnitHeal): ranne jednostki GRACZA leczą się za
  mleko — tylko castle[0], próg >=75% maxmilk, 1 hp za 1 mleko co ~1 s;
  krowy (type==0) samoleczą się za darmo — obie strony, +1 hp/48 klatek.
  User: „leczenie jednostek działa".
- **Raporty (`50a0469`)** — `raporty/krowy-zdrowie-i-font-end-menu.md`:
  - **Zdrowie krów**: hp = 100 (`Udata[0]`); pasek zdrowia rysowany TYLKO u
    zaznaczonej jednostki (`ShowS`, `game/mover1.cpp:2205-2242`), bez
    warunku `hp<maxhp` — dlatego pobita krowa wygląda na „całą". Biały
    pionowy pasek z prawej = mleczność (udder), nie zdrowie. Krowę ranią
    wrodzi żołnierze/niedźwiedzie i wrogi pastuch (`FindCow`). Retest:
    zaznaczyć pobitą krowę — +1 hp/2,6 s.
  - **Fonty „KNiee" w menu „Koniec gry?"** — BŁĄD ORYGINAŁU: w tle wypalony
    złoty napis „Koniec" (rekord 1 GRAF.DAT, blok 16, przycisk 5), a
    rysowanie `how=1` pomija kolor 0 → dynamiczne „Nie" dokłada się do
    wypalonego. Fix do decyzji: kodowy (zakrycie czystym polem przycisku,
    `how=0` — wymaga portowania `graphics.cpp` do `src/` jak `battle.cpp`)
    albo danych (wywypalić napis z bloku 16 — wymaga zgody usera).
- **Pasek mleka** (biały prostokąt na listwie) — potwierdzony jako oryginał
  1:1, nie bug.
- **Panel akcji — diagnoza w toku (read-only)**: offset/zły start linii ikon
  akcji; brak obrazka zaznaczenia w top-prawo (podejrzenie pustych
  sprite'ów w dyskietkowej rewizji GRAF.DAT); 7 ikon vs 6 w oryginale
  (overflow).
- **`official/` UKOŃCZONE** (osobne repo, `86d1074` + `ed596ff`, BEZ pusha —
  dopóki user nie przetestuje skryptów): `src/{game,game-linux,tools}`,
  `scripts/{install,build,run}.sh`, `ephemeral/` gitignored, scrubbing
  ścieżek/atrybucji, `check_assets`: 0 braków krytycznych. VSCO celowo poza
  official („nie jest gotowe").
- Fałszywy alarm z official: reguła `test: $(TESTY)` w
  `game-linux/Makefile:197` jest POPRAWNA — nic do naprawy.
- **Proces — nowa dyrektywa usera**: agenci NIE kompilują i NIE uruchamiają
  niczego (make/testy/grę odpala wyłącznie user).
- **Kolejka**: wariant „ranna krowa dojona 2x wolniej" (1 linia w
  `Mover1::Milk()`, wymaga przenosin `game/mover1.cpp` →
  `game-linux/src/mover1.cpp`) — czeka na decyzję usera.

## Zgłoszenia po retestach — 2026-09-04 (popołudnie)

- **Muzyka napisów misji ZAPĘTLONA** — fix zacommitowany (`ec289d7`:
  `port/track_map.h` utwór 3 `loop=1` + `tests/test_audio.cpp`). Przyczyna:
  moduł S3M jest krótszy niż napisy, a oryginalnie CD-Audio grało minuty
  (do końca ścieżki). Do retestu przez usera.
- **„KNiee" w menu końcowym** — user doprecyzował: to JEDNO słowo. W tle
  wypalony złoty „Koniec", ale glify się zlewają (K-o z n wygląda jak N,
  c jak e); na wierzchu dokłada się dynamiczne „Nie". Fix w toku — wariant
  kodowy: zakrycie pola przycisku 5 czystym wycięciem PRZED rysowaniem
  „Nie"; `graphics.cpp` portowane do `src/` (agent pracuje).
- **Pomijanie napisów misji Esc/spacją — NIE DZIAŁA** wg usera. Diagnoza:
  typewriter (`OutTextDelay13h`) nie sprawdza klawiatury (Esc działa dopiero
  po całej linijce), a spacja nieobsługiwana w ogóle (`ShowText` zna tylko
  Esc 27/283). Fix w kolejce: spacja = skip + typewriter przyspiesza, gdy
  klawisz czeka; wymaga `src/graphics.cpp` (kolizja z agentem fontowym —
  praca sekwencyjnie).
- **„7 vs 6 ikon" panelu** — kod portu 1:1 z oryginałem: `ShowPanel` ma
  6 gniazd `(274, 18+20i)`, `i<6`; dolny panel tylko ikonę MAPY `(275,139)`
  (`buttons[7]`); 4 wywołania identyczne. Czekamy na screeny usera
  (przepadły przy kompakcie). Hipoteza: dane (rewizja GRAF.DAT) albo stan
  zaznaczenia.
- **„Portret/ikona budynku top-prawo"** — w kodzie oryginału BRAK rysowania
  podglądu zaznaczonego obiektu (tylko pasek mleka = `drewno[2]`
  (299,9)–(314,150), gniazda akcji, MAPA). Czekamy na ponowny screen DOS
  od usera.
- **NOWY BUG (do diagnozy, agent read-only wystawiony)**: kilku robotników
  wysłanych NARAZ do naprawy tego samego budynku — blokują się, pracuje
  tylko jeden; wysłani osobno do tego samego budynku — pracują poprawnie.
- **Pasek mleka panelu** (biały prostokąt) = oryginał 1:1 — nie bug
  (potwierdzone wcześniej).

## Testy użytkownika — 2026-09-04 (wieczór)

- **User potestował grę.** Potwierdził wytłoczone okienko podglądu w prawym
  górnym rogu panelu (screen) — „tu jest miejsce na podgląd".
- **NOWY BUG — kilku robotników wysłanych naraz do naprawy tego samego
  budynku**: blokują się, pracuje tylko jeden; wysłani pojedynczo — pracują
  poprawnie. Diagnoza w toku (agent read-only; obszar: rozkazy
  naprawa/budowa, `game/mover1.cpp`).
- **NOWY BUG — robotnik buduje ukończony budynek**: po ukończeniu budynku
  robotnik dalej go „buduje" — rozkaz budowy nie kończy się po osiągnięciu
  pełnej fazy. W diagnozie (ten sam agent).
- **„KNiee" — doprecyzowanie**: to jedno słowo. W tle wypalony złoty
  „Koniec", glify się zlewają (K+o wygląda jak N, c jak e); na wierzchu
  dynamiczne „Nie". Fix (zakrycie, port `graphics.cpp` → `src/`) — agent
  w toku.
- **Podgląd zaznaczonego obiektu = feature WERSJI CD oryginału** (w źródłach
  dyskietkowych go nie ma): kafelki budynków `picture[137..256]`, budowy
  `picture[127..135]`, ruiny `picture[257..265]` z bloku 5 (`ekran_05`);
  agent implementuje w `src/battle.cpp`.
- **„7 vs 6 ikon"**: kod portu 1:1 z oryginałem (6 gniazd + MAPA, 4
  identyczne wywołania `buttons[7]`); do rozstrzygnięcia potrzebne pełne
  screeny obu wersji — czekamy.
- **Muzyka napisów misji zapętlona** (`ec289d7`) — do retestu usera.
- **Kolejka po fontach — pomijanie napisów misji Esc/spacją**: diagnoza
  gotowa (typewriter nieprzerywalny w linii, spacja nieobsługiwana); fix:
  spacja = skip + przyspieszenie typewritera, gdy klawisz czeka.

## Zgłoszenia późnowieczorne i kolejka fixów — 2026-09-04

- **NOWE ZGŁOSZENIE — overlay zaznaczenia poziomu trudności na mapie
  podboju kampanii** (ekran „Wybierz cel następnej wyprawy", przyciski
  ŁATWY/ŚREDNI/TRUDNY na prawym brzegu): kwadracik zaznaczenia przesunięty
  o parę pikseli w lewo wobec tekstury. Diagnoza wstępna: rozjazd siedzi
  w oryginale — klik ma `MWindow(268/268/272, 148/163/178 … 317)` (trzy
  wiersze, lewa krawędź trzeciego = 272, prawa zawsze 317), a overlay
  rysuje `Rectangle13h(268, y, 313, y+12)` (y = 148/163/178) — 4 px
  rozjazdu na obu krawędziach. Tekstury przycisków w blokach 6/21
  (`ekran_06`); agent analizy (read-only) ustala dokładne obrysy wprost
  z bajtów GRAF.DAT. Fix wymaga przenosin `game/mapa.cpp` →
  `src/mapa.cpp` — w kolejce (kolizja Makefile z agentem fontów).
- **Odkrycie `PressButton` (`game/main.cpp:295-320`)**: przyciski 11–15 =
  gniazda `(274, 38..118)` (co 20 px), 16 = MAPA `(274,138)` — łącznie
  6 pozycji; `ShowPanel` dokłada ramkę na y=18 → 7 pozycji w porcie.
  Hipoteza: DOS-screen użytkownika (6 pozycji) = panel wersji CD bez
  gniazda y18; nasz kod = dyskietka. Agent porównuje bajtowo bloki 3/18
  między rewizjami GRAF.DAT.
- **DECYZJA USERA „poprawiaj"**: naprawa budynku przy 100% hp — do
  batcha (ikona naprawy ukryta przy pełnym hp + klik nie wysyła
  rozkazu).
- **MAPA KAMPANII — KOLORY GRANIC PROWINCJI (2026-09-04, wieczór)**: user
  pokazał referencyjny screen z DOS — granice prowincji na mapie podboju
  mają kolory wg plemienia (czerwona = startowa Polan, niebieskie =
  Mazurowie, żółte = Wieletowie, zielone = Pomorzanie, szare = Wiślanie)
  i PULSUJĄ odcieniami; port rysuje wszystkie granice BIAŁE. Mechanizm
  oryginału (`game/mapa.cpp`): `kolorK[5][5]` (RED=148, GREEN=245,
  YELLOW=233, BLUE=241, GRAY=237, ACTIVE=233, linie 13-26); prowincje
  wycinane z ekranu wirtualnego (`SetScreen(1)` + `ShowPicture(12,0)` +
  `ShowPicture(27,100)`) i wklejane na małą mapę (`ShowPicture(6,0)` +
  `ShowPicture(21,100)`) przez `PutImageChange13h(wsp2[i], kraina, 1,
  ACTIVE, kolorK[prowintion[i]-1][0])` (linie 99-104) — podmiana koloru
  granic 233→kolor właściciela; pętla timera (134-161) pulsuje odcieniami
  `sw1 = licznik/4` z odbijaniem. Port: granice białe — hipotezy: bug
  `PutImageChange13h` (`src/image13h.cpp:327`), martwy ekran wirtualny
  `SetScreen(1)`/`GetImage13h` w porcie, albo indeks granic na teksturze
  ≠ 233. Wystawiony agent diagnozy read-only (raport:
  `raporty/kolory-granic-prowincji.md`). Fix: do scalenia z fixem overlay
  w `src/mapa.cpp` (przenosiny mapa.cpp z GAME_OBJS do PORT_OBJS w
  Makefile) — po fontach.
- **KOLEJKA FIXÓW (sekwencyjnie, ze względu na kolizje plików):**
  (1) pomijanie napisów misji Esc/spacją — `src/graphics.cpp`, po
  agentach fontów; (2) overlay poziomu — `src/mapa.cpp` + Makefile;
  (3) naprawa przy 100% hp — `src/battle.cpp` + `src/graphics.cpp`, po
  agentach podglądu i fontów.
- **Agenci w toku:** fonty „KNiee" (port `graphics.cpp`), podgląd
  zaznaczonego obiektu (`src/battle.cpp`), diagnoza: blokada naprawy
  naraz + budowanie ukończonego budynku, porównanie rewizji panelu,
  analiza overlay poziomu, kolory granic prowincji (diagnoza).

## Wieczór 2026-09-04: diagnozy granic/overlay/CPU, fix naprawy, instalacja official

- **Kolory granic prowincji — diagnoza GOTOWA**
  (`raporty/kolory-granic-prowincji.md`): kod portu 1:1 z oryginałem,
  hipotezy A/B/C/E odrzucone; winne DANE — paleta 6 dyskietkowego
  PAL.DAT: wpisy 232–255 = czysta biel (CD ma 148 = (224,0,0) czerwony
  + odcienie 233–248); kontury dużej mapy mają indeks 233 → podmiana
  233→233 = biel na biel, puls martwy; DOS na dyskietce też miałby
  białe granice. Fix zalecany: punktowe nadpisanie 18 wpisów
  (148 + 232–248) palety 6 wartościami z rewizji CD w
  `LoadExtendedPalette` (`src/image13h.cpp:855`).
- **Overlay poziomu — diagnoza GOTOWA**
  (`raporty/overlay-poziomu-mapakampanii.md`, commit `d9c7599`): błąd
  ORYGINAŁU — przesunięcie dokładnie 4 px w lewo (obrys przycisków
  z bajtów bloku 21: x=272..317; overlay x=268..313; y dokładne);
  `editor/mapa.cpp:62-108` ma już 272/317 (twórcy poprawili w
  edytorze, w grze stary kod). Fix = 12× `Rectangle13h` 268→272,
  313→317 + 2× `MWindow` 268→272 w `src/mapa.cpp` + Makefile; clamp
  kursora X≤300 = oryginalne, nie ruszać.
- **CPU — diagnoza + fix** (`raporty/zuzycie-cpu-petle-prezentacji.md`,
  commit `2123e2b` w `port/port_sdl.cpp`): hipoteza usera „milion fps?"
  prawie trafiona — spin-pętla bitwy busy-wait na tik 18,2 Hz i co
  iterację jawne `POL_Present` (~4–9 tys. prezentacji/s; renderer bez
  vsync; GPU ratowany vsync FIFO). Fix: globalny dławik ~60 fps w
  `POL_Present` + backoff pompy (`SDL_Delay(1)` przy pustej <2 ms) +
  `POL_FPS_LOG=1`. Pomiar: `htop -p $(pgrep pol2)` przed/po, oba
  backendy.
- **AWARIA GIT — kolizje równoległych agentów** (double-commit STATUS;
  cudzy staged plik w cudzym commicie; cykle commit→reset cofnęły
  commity innych, a finał `503b54c` kłamał o zawartości —
  `src/mover1.cpp` nie istniał). Rescue: podgląd battle.cpp w `8560fa3`
  + STATUS w `3531346`; nic trwale nie przepadło. **ZASADY
  WZMOCNIONE: commit wyłącznie per-plik z pathspec, zakaz
  reset/checkout/restore u agentów.**
- **Fix blokady naprawy WDROŻONY** (`d593379`): `src/mover1.cpp` nowy
  (kopia 1:1 + `// PORT:`); Fix A — `Castle::Run` co==3: cele grupy
  rozdzielane po polach 3×3 budynku (licznik `pol_k`, `type==1`;
  k==0 = pole kliknięcia; fallback oryginalny przy moście/palisadzie);
  Fix B — `Move()`: warunek kasowania rozkazu + `command != 8`;
  Fix C — `Repare()`: stop przy `exist==1 && hp>=maxhp` (odstępstwo od
  DOS, oznaczone). Makefile: `mover1` GAME_OBJS→PORT_OBJS + reguła
  z `src/`. Test: `POL_REPAIR_DEBUG=1` — cmd=8 z różnymi xm,ym;
  retest: grupa 4–6 robotników → naprawa naraz.
- **Podgląd zaznaczenia zacommitowany** (`8560fa3`) + retest usera:
  portrety DZIAŁAJĄ, ale (a) nadmiarowy guzik przykrywa portret
  (podejrzani: tarcza Stój `buttons[0]` na (275,19) tryb 1 albo
  7. ikona), (b) prawa część okienka (278,7)–(310,27) pusta — tylko
  lewa połowa rysunku. Diagnoza panelu
  (`raporty/panel-gniazda-rewizje.md`, `73e18be`): CD ma 7. gniazdo
  y=18..33 w tle, dyskietka ma tam OKNO PODGLĄDU + 6 gniazd; kod
  dyskietkowy rysuje Stój na (275,19) NA WIERZCHU okna = 7 pozycji;
  progi mleka sprzężone — nie ukrywać gniazda y=18.
- **Instalacja official** (zgłoszenie „BLAD: Nie znaleziono GRAF.DAT
  w rozpakowanym archiwum"): DYREKTYWA USERA — pobieranie przez
  `git clone` jstasiak/polanie.prv.pl-mirror (nie curl zipa);
  GRAF.DAT szukać `find`em `-iname` po ephemeral (dowolna głębokość);
  DYREKTYWA: „portuj nowe poprawki tam też zawsze" (mirror do official
  przy każdej poprawce); TYLDY (`operwav.cp~`, `operwav.h~`, `wav.h~`
  w `official/src/game/`) = backupy edytora DOS z upstreamu — wyciąć
  z official + `.gitignore *~`; w `game/` zostają.
- **Testy unit 88/88 PASSED** (log `run_tests..log` usera): „dziwne
  komunikaty o złych parametrach" = oczekiwane logi testów negatywnych
  (fopen FAIL, zly_sound.dat) + warning sfizz „Cannot set current
  thread scheduling parameters" (RT prios, nieszkodliwy); drobiazg:
  `run_tests.sh` podwójny przebieg (make test już uruchamia).
- **Kolejka fixów (sekwencyjnie)**: napisy Esc/spacja
  (`src/graphics.cpp`, po fontach) → granice palety 6
  (`src/image13h.cpp`) → overlay (`src/mapa.cpp` + Makefile) → naprawa
  100% hp (`src/battle.cpp` + `src/graphics.cpp`). Agenci: fonty
  KNiee w toku.

## Noc 2026-09-04: retest podgladu OK, fix overlayu i palety granic, official gotowe

- **Retest usera (po 4d2bf03 + 4122292 + roboczy fix podgladu):** ikony
  panelu poprawione — nadmiarowy guzik znika; portrety w okienku
  podgladu dzialaja w pelni. Nadal: klik "naprawa" dziala na zdrowy
  budynek (Fix C w Repare() z d593379 nie wystarcza) — kolejny fix:
  ukrycie ikony naprawy przy pelnym hp (ShowPanel, src/graphics.cpp) +
  blokada wysylki rozkazu 8 (DispatchEvent, src/battle.cpp), po
  commicie fixa podgladu (kolizja battle.cpp).
- **Fix overlayu poziomu trudnosci (4d2bf03):** src/mapa.cpp NOWY
  (kopia 1:1 game/mapa.cpp + naglowek PORT), 12x Rectangle13h
  268->272 i 313->317 + 2x MWindow 268->272 wg raportu
  overlay-poziomu-mapakampanii.md (blad oryginalu; editor/mapa.cpp
  mial poprawne wspolrzedne); klik poprawiony razem z rysowaniem;
  Makefile: mapa GAME_OBJS -> PORT_OBJS.
- **Fix palety granic prowincji (4122292):** LoadExtendedPalette
  (src/image13h.cpp) — gdy pal==6, nadpisanie 18 wpisow
  (148 + 232-248) wartosciami z rewizji CD (zweryfikowane bajty); puls
  granic uruchamia sie sam (odcienie 233-248 wracaja); inne palety
  nietknete; retest: czerwona/zielona/zolta/niebieska granica + puls.
- **Official gotowe (commity 4dbb77f / 42a516e / 9032314 w repo
  official/):** install.sh = pobor przez git clone mirrora jstasiak
  (istniejacy klon: pull --ff-only; POL_MIRROR_URL; unzip tylko przy
  --cd), szukanie GRAF.DAT/PAL.DAT find -iname z logowaniem trafien;
  mirror 7 plikow z game-linux (battle.cpp w stanie 8560fa3 — robocza
  zmiana fixa podgladu wejdzie przy nastepnym mirrorze); tyldy
  (operwav.cp~, operwav.h~, wav.h~) usuniete + .gitignore *~. Bez
  pusha (WSTRZYMANIE PUBLIKACJI). Retest: cd official &&
  ./scripts/install.sh [--cd].
- **Fix podgladu — zmiana podejścia (robocza src/battle.cpp):**
  polOknoTlo wypadlo; tlo okienka = drewno[1] + wyciecie tla gniazda 0
  (GetImage13h 274,18,292,34 -> polGniazdoTlo) do wymazania tarczy
  "Stoj" przy podgladzie; user potwierdzil dzialanie na stanie
  roboczym; commit w toku.

## Noc 2026-09-04: naprawa 100% hp, fix podgladu commit, mirror official, diagnoza instalek ARJ

- **Fix podgladu zaznaczenia ZACOMMITOWANY (d6fca7a, src/battle.cpp,
  +27/−18):** winowajcy potwierdzeni powiekszeniem ekstraktu — tarcza
  „Stoj" (buttons[0], 16×14 z ekranu 3) rysowana dwukrotnie
  (dyskietkowy ShowPanel + celowe odrysowanie w ShowSelectionPreview na
  wierzchu portretu) i ramka gniazda 0 Buttons[3]; prawa polowa okienka
  pusta, bo pasek mleka PutImage13h(299,9,drewno[2]) rysowany PO
  podgladzie zaslanial x299–309; fix: wymazanie tarczy/ramki tlem panelu
  (polGniazdoTlo = GetImage13h(274,18,292,34)), tlo okienka = drewno[1]
  32×21 (jak ShowPanel edytora), ShowSelectionPreview przeniesione za
  blok paska mleka w ShowSelected; klik „Stoj" bez zmian (gniazdo 0 pod
  oknem, jak w CD).
- **STATUS zacommitowany (e70defb)** — sekcja nocna z retestu podgladu i
  fixami overlay/palety.
- **Mirror official (e8e52f6):** battle.cpp (d6fca7a) + image13h.cpp
  (4122292, LoadExtendedPalette — paleta granic; pierwszy raz w
  official); cmp 1:1, bramki czyste, bez pusha.
- **Naprawa 100% hp ZACOMMITOWANA (ef2c2e4, src/graphics.cpp +
  src/battle.cpp, +47):** sprostowanie — ShowPanel co==1 to ROBOTNIK
  (budynek wchodzi jako selectB->type+9/+19, ikony naprawy nigdy nie
  dostaje); ikona naprawy = buttons[8] gniazdo 3 przy co==1&&button==3,
  a button==3 = selectM->type==1&&command==8; KORZEN: rozkaz 8 zostaje
  na robotniku po domykajacej naprawie (Fix C zeruje tylko commandN) →
  ikona swieci przy zdrowym budynku. Fix: ShowPanel rozwiazuje cel z
  place[selectM->xe][selectM->ye] (jak Fix C w Repare()) → przy
  exist==1&&hp>=maxhp button=0 (gniazdo puste); DispatchEvent case 12:
  przy pelnym hp brak wysylki Cmd i mouseCommand=1 (gasi tryb
  celowania, odswieza panel); uszkodzony budynek bez zmian; budowa/
  ruina/polisada nietkniete.
- **INSTALKI — diagnoza formatow (hexdump naglowkow DATA.0XX z
  polanie.zip):** DATA.000/009 = DOS EXE (MZ; 009 = POLANIE.EXE);
  DATA.001/002 = ARJ pojedyncze (GRAF.DAT/PAL.DAT/POST.DAT/SWIAT.DAT/
  LEVEL.*/FONT.DAT/GRAF.0NN/SETUP*); DATA.003–007 = ARJ multi-volume
  (5×1408000 B, seria „DATA.A..." → jeden plik DATA.003 7 209 751 B z
  DATA/W*.DAT); DATA.008 = zaszyfrowany, nie-ARJ (wlasny dekompresor
  INSTALUJ.EXE). p7zip NIE skleja multi-volume → retest usera
  (75f7b58): pojedyncze ARJ OK, multi-volume padl → check_assets:
  5 brakow krytycznych (DATA/, LEVEL.DAT, teksty, WAV, S3M).
- **DYREKTYWY USERA o instalatorze (do reworku install.sh):** (1) ZAKAZ
  szukania po nazwach/rozszerzeniach przed instalacja — kolejnosc:
  klon → unzip instalki → dekompresja ARJ (skrypt replikuje
  INSTALUJ.EXE, ktory dzialal tylko w dosbox) → struktura wynikowa →
  polanie-extract → dopiero wtedy weryfikacja; (2) obsluzyc OBA formaty
  instalek (dyskietkowy ARJ + CD); (3) LEVEL.INI NIE jest opcjonalny
  (check_assets → brak krytyczny); (4) ekstraktor dzialal na plikach PO
  instalacji — nie adaptowac go do danych przed instalacja, skrypt musi
  dac pelna strukture wynikowa (wzorzec dysk/GRY/POLANIE). S3M w
  polanie_cd.zip: brak luzem — moduly siedza w GRAF.DAT (990 000 B
  identyczny w obu rewizjach), wyciaga je polanie-extract.
- **W toku:** rework install.sh (agent): preferowany `arj`
  (multi-volume; Omarchy: AUR), fallback 7z + walidacja rozmiaru
  wynikowego DATA.003 (7 209 751 B), DATA.008 pomijany z logiem; mirror
  battle.cpp+graphics.cpp (ef2c2e4) do official po reworku
  (sekwencyjnie w official — awaria git przy rownoleglych agentach w
  jednym repo).

## Ranek 2026-09-04: instalator assets dziala end-to-end

- **Retest install.sh przeszedl (user):** 0 krytycznych brakow,
  wszystkie pliki OK (GRAF/PAL/FONT/POST/SWIAT.DAT, DATA/W001.dat,
  LEVEL.DAT/INI, teksty, audio 55 dzwiekow + 19 muzyki); jedyny brak
  opcjonalny DATA/I001.dat.
- **Rework instalatora (official 758a679):** `arj x -y -x SWIAT.DAT`
  (bug arj 3.10 stack smashing na plikach przecinajacych granice
  wolumenow), walidacja DATA/W*.DAT zamiast rozmiaru sklejonego
  archiwum, fallback unar, find tylko w trybie --cd.
- **Tryb --cd (official 6467118):** doklada pliki opcjonalne CD
  (DATA/I*.dat, SOUND.DAT, PIC.DAT, SETUP.DAT/PAL, INSTALL.PAL,
  INSTALL1/2.DAT, FONTS1.13H, LEVEL2.INI) miedzy faza 3 a 4;
  check_assets szuka teksty/LEVEL.txt (nazwa ekstraktora), LEVEL.INI
  krytyczny.
- **Mirror official (26fbd96):** ef2c2e4 (battle.cpp + graphics.cpp,
  fix ikony naprawy przy 100% hp).
- **Kolejny krok (user):** `bash scripts/build.sh` (build portu z
  official).

## Ranek 2026-09-04: build official OK + publikacja na GitHub

- **Build official przeszedl po mirroringu unit_heal (7508153):**
  nowe pliki src/unit_heal.cpp i tests/test_unit_heal.cpp (Makefile
  ich wymagal, wczesniej make padl na „No rule to make target"), plus
  przeniesienie nowych mechanik z main-repo na zaadaptowane pliki
  port/ (port.h: deklaracja POL_UnitHealTick; port_sdl.cpp: dlawik
  prezentacji ~60 fps w POL_Present + backoff pustej pompy + liczniki
  FPS; port_fopen.cpp: dane pelnej wersji CD jako ostatni kandydat) —
  zachowane officialowe sciezki cd/polanie_cd zamiast jprok_pliki;
  sound_dat/midi_*/sfz_map bez zmian (roznice tylko konwencja
  sciezek).
- **Instalator domkniety wczesniej:** komity 3f87821 (pliki
  opcjonalne CD domyslnie, nadpisywanie zamiast pomijania) i 8b154c9
  (odblokowanie ekstraktu przed ekstrakcja, pliki read-only z
  archiwow); retest usera: 0 krytycznych brakow, 0 ostrzezen.
- **Publikacja:** repo official wyslane na publiczny GitHub —
  https://github.com/KrzysztofKowalski/polanie (utworzone
  `gh repo create`, remote origin, branch master, HEAD origin/master
  = 7508153, master w pelni zsynchronizowany).
- **Kontrole przed publikacja:** 0 danych gry w 146 sledzonych
  plikach, ephemeral/ i build/ w .gitignore (0 plikow ephemeral w
  gicie), bramka autorstwa (wyszukanie slow kluczowych AI) czysta w
  CALEJ historii commitow, NOTICE zastrzega prawa MDF/Play 1997 i
  wskazuje zrodlo jstasiak/polanie-src; wstrzymanie publikacji
  zdiete jawnym poleceniem usera po pozytywnych retestach instalacji
  i builda.

## Ranek 2026-09-04: skrypt zaleznosci pacman w official (install_deps.sh)

- **Nowy skrypt zaleznosci (official b353b69):** `scripts/install_deps.sh`,
  165 linii, pisany przez podagenta, sprawdzony tylko `bash -n` — nie
  uruchamiany; instalacja przez pacman, wylacznie Arch/Omarchy.
- **Zakres:** rdzen = base-devel sdl3 libopenmpt sfizz unzip p7zip git
  unar pkgconf (`pacman -S --needed`); flaga `--opcjonalne`/`--all`
  doklada gamescope, glslang, gtest.
- **arj tylko z AUR:** pacman go nie zainstaluje — skrypt NIE konczy
  bledem, drukuje UWAGE z komenda `yay`/`paru -S arj` i przypomnieniem,
  ze `7z` z p7zip NIE skleja wolumenow ARJ multi-volume
  (DATA.003..DATA.007).
- **Weryfikacja po instalacji:** `pkg-config --exists` dla
  sdl3/libopenmpt/sfizz, `command -v` dla git/g++/make/unzip/7z/unar
  i arj; raport OK/BRAK, exit 1 przy brakach rdzenia.
- **README i stan pusha:** w README.md pod komenda Arch jedna linia
  („Arch/Omarchy (automatycznie): bash scripts/install_deps.sh");
  wszystko juz na GitHubie — origin/master = 1cedcc5 (komity usera
  „readme." a4c5847/1cedcc5, wczesniej 2d2d917), blob install_deps.sh
  (d8c9186) potwierdzony w drzewie origin/master, local master ==
  origin/master.

## Wieczor 2026-09-05: pelne assety w dysk/GRY/POLANIE + paski hp/magii w panelu bitwy

- **Brakujace „ladne obrazki" = PIC.DAT** (podklady ekranow tekstow misji ShowText -> ShowPicture2, graphics.cpp:1545; port czyta „pic.dat" przez port_fopen.cpp:365-371) — kopia dyskietkowa w dysk/GRY/POLANIE/ nie miala tego pliku (synteza czarnych ekranow w POL_fopen staje sie martwa). Skopiowane pelne assety z rozpakowanej instalacji official (official/ephemeral/dysk/GRY/POLANIE -> dysk/GRY/POLANIE, zgodnie z poleceniem usera): **85 nowych plikow** (PIC.DAT 2 266 880 B = 35 rekordow, SETUP.DAT, top-level GRAF.001-019, W001-055.DAT, LEVEL.DAT/INI, DATA/I001-003 w .dat i .DAT, DATA/SOUND.DAT); **SWIAT.DAT celowo nie nadpisany** — nasz 11 MB ma pelne audio (zrodlo pelnej muzyki w extracted/), officialowy 1,5 MB z dyskietki jest ubozszy; jedyna pozostala roznica md5 (a7532432e7182a71bf64bed8bc503713). Dane poza git (dysk/ untracked), nic nie usuniete (DATA.003 zostaje).
- **GRAF/GRAF.001-018**: instalacja official ma te pliki male (1-12 kB), nasza kopia miala pelne (64-85 kB, pelne moduly S3M). Agent kopii nadpisal je malymi (backup: ephemeral/backup-dysk-20260905/GRAF/), po czym przywrócone wlasne pelne z backupu — GRAF.00x to tylko fallback muzyki (extracted/audio/muzyka ma pelne moduly i tak).
- **Fix „pasek mleka poza limitem"** (game-linux/src/battle.cpp:848-868): oryginal chowal czerwony znacznik maksimum przy maxmilk>1260 (przy duzych maksimach pasek wygladal jak pelny bez limitu), a bialy pasek nie byl clampowany do maxmilk (przyrost jednego ticka mogl przeskoczyc maksimum). Teraz: bialy pasek clampowany tez do maxmilk (maxmilk=0 = bez limitu), czerwony znacznik rysowany zawsze i przyciety do szczytu paska (clamp do 1410 przed /10).
- **Paski przy portrecie w panelu (zyczenie usera, jak w wersji CD)**: w ShowSelectionPreview dwa pionowe paski 3x21 przy okienku podgladu (278,7)-(309,27): lewy (274,7) = zdrowie (LightGreen/Yellow/LightRed, progi maxhp>>1/>>2), prawy (311,7) = magia LightBlue tylko dla jednostek z magic>0 (typy 3/4/11: kaplanka/kaplan/mag — tak ustawia Mover1::Init), maksimum dmagic[exp>>4]; dla budynku oba paski = zdrowie. Kolory i progi identyczne z paskami nad jednostkami na mapie (mover1.cpp:2259-2272); tla pod paskami (polPasekTloL/P) przechwytywane obok polGniazdoTlo i przywracane przy odznaczeniu.
- **Commity**: c2cca63 (main-repo, game-linux/src/battle.cpp, +73/-12), f7e4313 (official, mirror 1:1; md5 obu plikow c3363a23761a351369fb3776731b6661). Bramki czyste, bez pusha.
- **Retest usera**: (1) ekrany tekstow misji (intro/zwyciestwo/porazka) z podkladami z PIC.DAT; (2) misja z duzym maksimum mleka — czerwony znacznik widoczny na szczycie paska, bialy nie przekracza limitu; (3) panel: przy jednostce lewy pasek hp + prawy magii (kaplanka/kaplan/mag), przy budynku oba paski hp.

## Wieczor 2026-09-05, cz. 2: paski v2 wewnatrz okienka, mleko u zrodla, quit okna w bitwie, rework petli 18,2 Hz

- **Paski hp/magii v2** (retes usera: lewy niewidoczny, czarna belka przy jednostkach bez magii): paski przeniesione WEWNATRZ okienka podgladu (278,7)-(309,27) — lewy (278-280) HP, prawy (307-309) magia TYLKO gdy magic>0 (bez czarnego toru), budynek oba HP; polPasekTloL/P usuniete (okienko rysowane co klatke, czyszczenie samoistne). Commity: 9c0ae93 (main) / bcf24b5 (official, mirror md5 9ee0daf1...).
- **Pierwszy push officiala** (feedback usera: „robic synchro zmian zawsze" = commit + mirror + push przy kazdej poprawce): 1cedcc5..bcf24b5 (poszedl tez wiszacy f7e4313); repo glowne bez pusha (origin = upstream jstasiak).
- **Mleko u zrodla** (sugestia usera): mover1.cpp:3061 — przyrost jednego ticka (suma produkcji wszystkich krow) mogl przeskoczyc maxmilk; teraz dokladanie przez pol_dodatek z clampem do limitu (else bez zmian). Commity: 9ec39d2 / e19f640 (official).
- **Zamkniecie okna konczy bitwe**: POL_QuitRequested sprawdzane tylko w menu glownym (main.cpp:263; luka zapisana wyzej, pkt (2)) — dodane sprawdzenie na poczatku ShowSelected() (quitLevel=1 = istniejaca sciezka „konca scenariusza" -> menu -> tam koniec gry) i w menu zapisu (return 4 = anuluj). Commity: 2d8c382 / 2d68705 (official).
- **Rework petli 18,2 Hz (tor WebGL, ryzyko nr 1 z raportu)**: spin bitwy (battle.cpp:604-616, busy-wait na licznik z pol_tick_cb, menegdma.cpp:73) czeka teraz senem 2 ms przez nowe POL_WaitMs (port/port.h +17, port/port_sdl.cpp +8, battle.cpp +14) miedzy odpytaniami o wejscie — tempo logiki dalej taktowane tikami 18,2 Hz (wyjscie z czekania warunkiem na liczniku), CPU w bitwie powinno wyraznie spasc (~500-1000 obiegow/s -> ~300-350). Komentarz przy POL_WaitMs dokumentuje sciezke Emscripten (emscripten_sleep + ASYNCIFY — build webowy to podmiana w jednym miejscu). Petle menu zostawione (pusta pompa zdarzen juszypia SDL_Delay(1) — port_sdl.cpp:529-535). Commity: 2fe6acf + 001b7c9 + 5949657 (main) / 30dce2a + e727eab + 321d20d (official); push bcf24b5..321d20d.
- **GRAF.00x — zagadka rozwiazana**: male moduly (1-12 kB) = WIERNE dyskietkom (wpisane w DATA.002 oryginalnych dyskietek; mirror z publicznego klonu ≡ dyskietki usera — md5 sklejki DATA.003-008 ≡ merged DATA.003 z realnej instalacji, ce4f0b68...); DATA.008 = dokladny ogon tego archiwum (md5 ogona 169751 B ≡ DATA.008, 94630292...), NIE „zaszyfrowany wlasny format" (komentarz w install.sh bledny — poprawka do rozwazenia: skleić DATA.003-008 i pelny arj x -> SWIAT.DAT 11 MB w instalacji, dzis wykluczony jako 1,5 MB). Pelne moduly (64-85 kB) istnieja TYLKO w main (jprok victory tez ma male; SWIAT.DAT bez SCRM).
- **Tor WebGL**: raport ephemeral/raport-webgl.html (rekomendacja: Emscripten/WASM istniejacego portu + WebGL2 R8+paleta-LUT jak w port_gpu.cpp; rdzen danych ~9,6 MB — SWIAT.DAT i DATA.003 zbędne); skrypt build/run w game-linux/scripts/webgl_build.sh (commit w osobnym torze).
- **Retest usera (po rebuild main + git pull official + rebuild official)**: (1) tempo gry i czulosc myszy/klawiatury po reworku petli (animacja MapY moze byc ~1,5x wolniejsza — zwrocic uwage), CPU w htop; (2) zamkniecie okna w trakcie bitwy; (3) paski przy portrecie: jednostka = lewy hp + prawy magia (kaplanka/kaplan/mag), budynek = oba hp; (4) mleko przy pelnym magazynie — nie przestrzeliwa limitu; (5) paski nie rysuja sie wcale = upewnij sie, ze build jest z aktualnych zrodel (poprzedni binary byl sprzed dzisiejszych commitow).

## Wieczor 2026-09-05, cz. 3: paski v3 na zewnatrz portretu + stale kolorow pal3 + research panelu CD vs dyskietka

- **Paski hp/magii v3** (retes usera v2: „za cienkie, w zlym miejscu"; dyrektywa: „oba paseczki powinny byc grube i w dobrym miejscu obrazka - na lewo i prawo kwadrata z portretem"): porownanie z oryginalem dalo dowod layoutu CD — game/graphics.cpp:769-784 zapisuje drewno[0]=(272,7,276,28) 4x21 = tlo POD lewym paskiem; paski CD leza NA ZEWNATRZ okienka portretu (278,7)-(309,27): lewy x272-275, prawy x310-313, grube 4 px, wysoko 21 (y7..27), bez czarnego toru, wymaz tlem z wyciecia (odpowiednik drewno[0]). PolPasekPanel(x, tlo, wart, maks, kolor): wymaz PutImage13h + fill Bar13h(x, 28-h, x+4, 28) od dolu; Bar13h/GetImage13h nieinkluzywne (x2/y2 wykluczajace). Wywolania: budynek oba paski HP; jednostka lewy HP + prawy magia (dmagic[exp>>4]) gdy magic>0, inaczej wymaz tlem. Commity: 3d7afe6 (main) / 7ae0011 (official, push 5500faa..7ae0011, md5 db3860cb...).
- **Retes usera v3: „paski zdrowia dzialaja ale kolory sa branzowe a powinny byc zielone"** — root cause: stale kolorow probkowane w InitPicture (graphics.cpp:439-467) z RealVirtualScreen[0..47] = wiersz 0 ekranu 3 = DREWO panelu (LightGreen=95; pal3[95]=(107,70,20) = braz). Oryginalny 48-kolorowy „pas wzorcowy" palety byl na ekranie intro (DATA/S00*.DAT), ktorego brak w instalacjach dyskietkowej i CD (sa tylko I001-I003 WAV) — probkowanie nie ma zrodla. SetScreen(1) scala bufory (VirtualScreen=RealVirtualScreen), wiec ShowPicture(3,0)+ShowPicture(18,100) nadpisuje pas przed probkowaniem; identyczny przeplyw w DOS/edytorze/porcie.
- **Fix kolorow**: stale ustawiane wprost wg pal3 (paleta bitwy, LoadExtendedPalette(3)) zaraz po init Battle() (battle.cpp:469-492; extern juz w mover.h): zielenie 65/67/78 (LightGreen=78=(14,104,0)), zolcie 164/189/194, czerwienie 128/133/137 (LightRed=137 = tez znacznik maxmilk), niebieskie 213/217/225 (LightBlue=225 = magia), brazy 45/95/111 (DarkBrown=45 = puste kropki jedzenia), szarosci 118/125/233, Color1=38. Indeksy zweryfikowane bajt po bajcie w PAL.DAT (paleta 3, skala 0-255, >>2 do DAC). Commity: a46993e (main) / bc402eb (official, push 7ae0011..bc402eb, md5 mirroru 3776a254...). Bramki czyste.
- **Research: panel bitwy CD vs dyskietka (dekodowanie GRAF.DAT dd+magick)**: dwa warianty bloku 3+18 — A = dysk/victory (victory = wersja NIEMIECKA „OPTIONEN"; GRAF.DAT md5 87734165) z ramka okna portretu w bitmapie i mlekiem krotkim (edytor wycina drewno[2]=(299,41,314,150)); B = polanie_cd/demo (md5 a9b3785e) z czystym drewnem u gory i mlekiem pelnym (299,9)-(314,150) — „wiecej miejsca na mleko" u usera. Dyskietka wcisnela GUZIK (tarcza „Stoj") w wolne miejsce po portrecie: game/ ShowPanel rysuje tarce na (275,19), edytor (CD-era) na (275,39) + portret (278,7)+face[] NAD gniazdem 0 (gniazda od i=1, y38+) — potwierdzenie obserwacji usera. PIC.DAT = tylko tla misji (35 obrazow), panelu bitwy nie zawiera. DYREKTYWA USERA: „uwazaj w fixach aby uzywac cd layoutu".
- **Otwarte (czeka na odpowiedz usera)**: dokladny uklad 4 paskow CD („powinny byc 4 paski" — po dwa na strone okienka?); „nadmiarowy guzik" przy braku zaznaczenia (widok dyskietkowy — fix ma uzyc layoutu CD: portret nad gniazdem 0, tarcza na (275,39), mleko pelne).
- **Retest usera (po rebuild main; official: git pull + rebuild)**: (1) paski hp zielone, zolty < polowy, czerwony < cwierci; (2) magia jasnoniebieska; (3) znacznik maxmilk czerwony, kropki jedzenia bursztyn/braz.

## Poranek 2026-09-06: toolchain clang + C++23 + zero warningow (9a210fb, f454313, f8d677f)

- **Clang zamiast gcc** (dyrektywa usera): `CXX = clang++` TWARDYM przypisaniem (nie `?=` — `?=` nie nadpisuje wbudowanego origin „default" w make) w game-linux/Makefile, tools.mk i webgl_build.sh (CXXFLAGS_WEB). CXXFLAGS: -std=c++23 -O1 -g -Wall -Wextra -funsigned-char (-funsigned-char MUSI zostac — logika Watcom, znaczniki placeN 190-227; usuniete -w i -fpermissive). Target clang-check (-fsyntax-only) zbiera wszystkie bledy. Zainstal? zweryfikuj przez odczyt plikow, nie make.
- **Polskie znaki CP1250 (18 bledow battle.cpp)**: pliki portowane w UTF-8, dane gry/oryginal CP1250 (1 bajt >127) — literaly zamiast tekstu: kody dziesietne z komentarzem /* PORT: 'Ó' CP1250 */ (Ó=211, ş=186, ł=179 itd.), weryfikacja hexdumpem.
- **Dziedzictwo Watcomu, wierne odtworzenie (NIE blad portu)**: `!selectM->type == 8/9` w 6 miejscach (src/battle.cpp:1931/1933/2109/2111/2172/2174) = martwe porownania z 1997 (alternatywa `!=` = zmiana gameplayu — czeka na decyzje usera); `selectM = &castle[0].b[j].m[i]` — Watcom leaky for-scope dawal deterministycznie j=20/i=20 (za tablica), clang dawal smieci stosu; f8d677f odtworzyl wiernie zachowanie Watcomu przez `j = 20; i = 20;` przed indeksowaniem — sensowny fix (b[b].m[p]) czeka na decyzje usera.
- **Full speed (wariant B, f454313)**: 2 kroki symulacji na tick (~36/s) zamiast petli bez limitu (30-100x na nowych CPU); pozycje 1-5 bez zmian; przewijanie max co tick; volatile licznik. speed 0..5, skraj PRAWY = 0 = full, czekanie roznicowe `licznik - licznik2 >= speed` (battle.cpp:632-670).
- **Fix port_sdl.cpp:120**: `static const unsigned char blank[2 * 16];` — const bez inicjalizatora = blad C++23 → `= {0}` (kursor w pelni przezroczysty).
- **Multiplayer design (aea1690)**: game-linux/MULTIPLAYER.md (747 l.) + JEDNOSTKI.md (214 l.); lockstep TCP 18,2 Hz, bufor 3 ticki, brak rand()/floatow w symulacji, Cmd[] 48 B/tick, CLI --host/--join/--port/--name/--map/--seed, fazy F1-F4.
- **Stan repo**: main HEAD f8d677f; official mirror i push znowu zrobione przy kazdej poprawce (3 pliki z f8d677f: battle.cpp/image13h.cpp/menegdma.cpp + port_sdl.cpp + STATUS.md).

## Poranek 2026-09-06: toolchain domkniety + fixy testu (5 pkt) + bug paska mleka po zaladowaniu
- **Toolchain domkniety:** port_sdl.cpp:120 blank[2*16] = {0} (błąd C++23 „default initialization of const"), mirror f8d677f (battle/image13h/menegdma) + STATUS.md jako nowy plik w official, push 364e10e..58779c0 (main 82164a8/4f60cd3; official 58779c0).
- **Test usera 2026-09-06 (po 82164a8) — 5 zgłoszen i naprawy (main 90f97ca + a0cca7e; official 8c107b6, push 58779c0..8c107b6):**
  1. Prawy pasek przy portrecie 310..313 → 311..314 (odstep 1 px w 310, symetria z lewym 272..275; polPasekTloP GetImage13h(311,7,315,9); PolPasekPanel(311) x3 — trzecie czyszczenie przy jednostce bez magii).
  2. Suwak predkosci: realtime (1 krok/tick = 18.2 Hz) z slotu 4 (~90% suwaka) na slot 3 (60% = srodek); tabela pol_tiki[6]={5,3,2,1,1,1} + pol_kroki[6]={1,1,1,1,2,33}; slot 5 = 33 kroki/tik = 600 Hz (user: „limitowac do max 600 hz" — w Watcom full szedl z predkoscia CPU; wczesniej: 2 kroki/tik = 36 Hz).
  3. DRI_PRIME=1 w run_port.sh (export DRI_PRIME="${DRI_PRIME:-1}") — Intel iris zamiast dGPU nouveau (renderD128), per-app Mesa↔Mesa wg ~/Projects/nv-kepler/NOTES.md; TYLKO main (official nie ma run_port.sh — launcher instalatora to scripts/run.sh; maszynowo-specyficzny fix nie publikowany — decyzja, do potwierdzenia przez usera).
  4. Etykiety ekranu Opcji przywrocone (battle.cpp Options — blok byl skomentowany w wydaniu 1997; tlo ekranu 13/28 bez napisow — brak ryzyka podwojnych napisow; ciagi oryginalne Utw%r/Dzwi$ki — kodowanie Transform13h).
  5. Pasek mleka — skala wzgledna do maxmilk: wysokosc = milk*141/maxmilk (pelny magazyn = szczyt), znacznik LightRed zawsze na szczycie (wiersz y=9); bez limitu (maxmilk=0) stara skala milk/10; uzasadnienie: limit = pl.maxmilk=(M-48)*200+50 (battle.cpp:2547) — przy stalej skali 0-1410 wypadal na ~80-90% (user: „czemu nie zbiera do 100%?").
- **BUG „stan po zaladowaniu" — root cause (fix main 381fac9 / official 927bbc2, push 8c107b6..927bbc2, md5 0708140a):** blok mleka (ramka drewno[2]+Bar13h+znacznik) byl rysowany W SRODKU malowania panelu (ShowSelected/if showAll ~825-1094), a po nim kolejne malowania cyklu (ramka drewno[2] pomalowana ponownie w nastepnym przebiegu + ShowSelectionPreview (tlo drewno[1] 278..309×7..27 + tlo prawego paska) kładły wierzch na wypełnieniu → widoczna tylko prawa 4-px kolumna wypełnienia (310..313), lewa czesc 299..309 zaslonieta drewnem. Fix: blok mleka + ShowSelectionPreview przeniesione na SAMO koniec malowania panelu (po ShowBattleMap / tekstach, przed myszka) — nic pozniej nie maluje (299..313, 9..149).
- **Retest usera czeka:** (1) po zaladowaniu: pasek mleka pelna szerokosc 15 px, pelny magazyn = szczyt z kreska na gorze; (2) reszta testu 5 pkt: pasek prawy z odstepem, realtime przy srodku suwaka + 600 Hz, DRI_PRIME=1 w logu run_port.sh, napisy w Opcjach.
- **Otwarte (czeka na decyzje usera):** !selectM->type == 8/9 (martwe porownania Watcomu); selectM b[b].m[p] vs j=20/i=20; dokladny uklad „4 paskow CD"; layout CD tarczy (275,39); publikacja DRI_PRIME do official/scripts/run.sh (do publikacji czy nie).
