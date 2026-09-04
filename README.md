# Polanie (1997) — natywny port na Linux (SDL3)

Natywny port klasycznej polskiej strategii czasu rzeczywistego **„Polanie"**
(MDF/Play, 1997) na współczesny Linux. Kod źródłowy oryginału (DOS, Watcom C++)
kompilowany jest tu bez zmian logiki za pomocą GCC, z cienką warstwą
kompatybilności (shimami) mapującą DOS-owe API na SDL3: wideo, wejście, dźwięk,
timery i system plików. Rozdzielczość gry pozostaje oryginalna (320×200, 256
kolorów), skalowanie robi okno/gamescope.

## Cechy portu

- oryginalna logika gry (plansze, walka, naprawa, budowa) bez zmian;
- wideo SDL3 (renderer 2D albo opcjonalny backend SDL3 GPU/Vulkan z shaderem
  prezentacji, w tym tryb CRT) — przełączane zmienną `POL_VIDEO=gpu`;
- muzyka: oryginalne moduły S3M przez libopenmpt (tor domyślny);
- tor alternatywny MIDI + sfizz (próbki VSCO-2-CE) — opcjonalny, do
  wymuszenia `--audioType=sfz`;
- efekty: WAV z danych gry; parser SOUND.DAT z wersji CD jako uzupełnienie;
- mysz na jednostki (pierwotnie sterowanie przez port 0x60) — decyzje
  zgodne z oryginałem: naprawa bez końca, panel warunkowy, brak portretów
  postaci — tak jak w DOS (celowo 1:1).

## Wymagania

- Linux x86-64, `g++` (C++17/C++20), `make`;
- `pkg-config`, SDL3 (`libsdl3-dev` / `sdl3`);
- `libopenmpt` (muzyka S3M), `sfizz` (tylko tor MIDI+sfizz);
- gamescope 
- opcjonalnie: `glslangValidator` albo `glslc` (backend GPU), Google Test
  (testy jednostkowe);
- do instalatora: `git`, `unzip` oraz `7z`/`7za` (pakiet `p7zip`) — wolumeny
  ARJ instalacji dyskietkowej rozpakowuje `7z`.

Debian/Ubuntu: `sudo apt install g++ make pkg-config libsdl3-dev libopenmpt-dev libsfizz-dev unzip p7zip-full`
Arch/Omarchy: `sudo pacman -S --needed base-devel sdl3 libopenmpt sfizz unzip p7zip`

## Szybki start

```bash
bash scripts/install.sh   # 1. pobiera dane gry z publicznego mirrora
                          #    (instalka polanie.zip: wolumeny ARJ przez 7z)
                          #    i ekstrahuje je do ephemeral/
bash scripts/build.sh     # 2. buduje port do ephemeral/ (binarka ephemeral/pol2)
bash scripts/run.sh       # 3. uruchamia grę
```

Opcjonalnie: `bash scripts/install.sh --cd` pobiera dodatkowo pełną wersję CD
(efekty i dane, których nie ma w wersji dyskietkowej) do `ephemeral/cd/`.

Skala okna: `bash scripts/run.sh 4` (1 px gry = 4×4 px). Tor muzyki:
`bash scripts/run.sh --audioType=s3m|sfz|auto` (domyślnie s3m).

## Struktura katalogów

```
official/
  src/            źródła — wszystko w jednym folderze
    game/         oryginalny kod gry (DOS/Watcom), bez zmian
    game-linux/   port: Makefile, src/ (pliki portowane), port/ (warstwa SDL3),
                  include/ (shimami nagłówków DOS), tests/ (gtest),
                  run_port.sh / run_tests.sh
    tools/        narzędzia C++: polanie_extract.cpp (ekstrakcja danych),
                  check_assets.cpp (weryfikacja), s3m_vsco_render.cpp
  scripts/        install.sh, build.sh, run.sh
  ephemeral/      WSZYSTKO, co powstaje w czasie instalacji i budowy:
                  pobrane archiwa, rozpakowane dane, ekstrakt, obiekty,
                  binarka pol2, check_assets. Gitignored — safe do skasowania.
  README.md, LICENSE, NOTICE, .gitignore
```

Repozytorium **nie zawiera danych gry** — `scripts/install.sh` pobiera je
z publicznego mirrora strony polanie.prv.pl i składa w `ephemeral/`.
GRAF.DAT nie leży tam luzem: jest w instalacji dyskietkowej `polanie.zip`,
której wolumeny `DATA.0NN` to archiwa ARJ (rozpakowuje je `7z`).

## Testy

```bash
make -C src/game-linux testy          # albo:
bash src/game-linux/run_tests.sh      # kompiluje i uruchamia (gtest),
                                      # przy porażce wypisuje detale na końcu
```

Testy nie potrzebują danych gry ani okna (stuby warstw wideo/audio,
syntetyczne dane w /tmp).

## Diagnostyka

- `POL_MOUSE_DEBUG=1` — logi walki/myszy (pod `// PORT:` w battle.cpp),
- `POL_REPAIR_DEBUG=1` — logi naprawy/drzew/palisad/mostów,
- `POL_VIDEO=gpu` — backend wideo SDL3 GPU/Vulkan (wymaga SPIR-V w czasie
  budowy; bez kompilatora shaderów backend wyłączone, fallback na renderer),
- `POLANIE_DATA` / `POLANIE_EXTRACTED` — nadpisanie katalogów danych,
- `POLANIE_MIDI` — katalog z dodatkowymi utworami MIDI (tor sfz),
- `POLANIE_VSCO` — katalog biblioteki VSCO-2-CE (tor sfz).

## Zgodność z oryginałem

Celowo odwzorowane zachowania DOS-owe (1:1, to nie są błędy portu):

- naprawa bez końca (gra nigdy nie kończy animacji naprawy),
- panel warunkowy (elementy interfejsu pojawiają się warunkowo),
- brak portretów postaci (grafika portretów nie występowała w DOS).

## Muzyka na torze MIDI+sfizz

Tor wymaga biblioteki sampli **VSCO-2-CE** (licencja CC-BY, pobierana
i instalowana osobno — nie jest częścią repo ani instalatora). Bez niej
tor sfz nie wystartuje; domyślny tor S3M działa bez niej. Ścieżkę do
biblioteki podaje się przez `POLANIE_VSCO`.

## Licencje

Kod portu: MIT (patrz LICENSE). Kod gry „Polanie" © 1997 MDF/Play —
dystrybuowany tu jako źródła na podstawie publicznego repo
`jstasiak/polanie-src`; patrz NOTICE.
