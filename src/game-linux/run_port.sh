#!/usr/bin/env bash
# Kompilacja i uruchomienie NATYWNEGO portu Polanie (game-linux/, SDL2).
#
# Użycie:
#   bash run_port.sh            - skompiluj i uruchom (skala dobierana auto,
#                                 na retina zwykle 8x8 fizycznych px)
#   bash run_port.sh 8          - wymuszona skala: 1 px gry = 8x8 px
#   bash run_port.sh -s         - tryb diagnostyczny: strace otwartych plików
#                                 (do szukania, czemu gra nie znajduje danych)
#   bash run_port.sh -s 8       - obie rzeczy naraz
#   bash run_port.sh -c         - tylko kompilacja, bez uruchamiania
#   bash run_port.sh --audioType=sfz   - wymusza tor muzyki MIDI+sfizz
#   bash run_port.sh --audioType=mt32  - tor MIDI+FluidSynth: soundfont
#                                 MT-32 Hedsound — SF2 brane z
#                                 assets/soundfont/*GM.sf2 automatycznie albo
#                                 POLANIE_MT32SF2=/sciezka/do/*GM.sf2;
#                                 brak libfluidsynth w buildzie -> pol2
#                                 zgłosi "mt32 niedostępne"; tor ciężki
#                                 (sfload ~0,6-1 GB RAM przy 447 MiB SF2)
#                                 (--audioType=s3m|sfz|mt32|auto; inne
#                                 argumenty poza -c/-s i skala tez przepuszczane)
#   Domyślnie: --audioType=s3m (decyzja usera 2026-09-04: VSCO niestabilne;
#   mt32 także tylko jawnie).
#
# Dane gry: wykrywane automatycznie (dysk/GRY/POLANIE względem repo) lub
# ustaw ręcznie:  POLANIE_DATA=/sciezka/do/gry bash run_port.sh
#
# DECYZJA PROJEKTOWA (2026-09-03): skalowanie robi gamescope (nearest,
# 320x200 -> 2560x1600) — port renderuje zawsze natywne 320x200. Argument
# [n] wymusza skalę w oknie i pomija gamescope.
set -euo pipefail
cd "$(dirname "$0")"

REPO="$(pwd)"
DATA="$REPO/dysk/GRY/POLANIE"
BIN="$REPO/game-linux/pol2"

MODE_RUN=1; MODE_TRACE=0; SCALE=""
EXTRA_ARGS=()
for arg in "$@"; do
  case "$arg" in
    -c) MODE_RUN=0 ;;
    -s) MODE_TRACE=1 ;;
    [0-9]*) SCALE="$arg" ;;
    # PORT: przepuszczenie argumentow do pol2 (tor muzyki i przyszle opcje)
    --*) EXTRA_ARGS+=("$arg") ;;
    *) echo "Nieznany argument: $arg"; exit 1 ;;
  esac
done

# PORT: decyzja usera (2026-09-04) — VSCO niestabilne; domyślnie oryginalny
# tor muzyki S3M. Tor MIDI+sfizz tylko na jawne --audioType=sfz|auto.
AT_DEFAULT=1
for arg in "${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"}"; do
  case "$arg" in --audioType=*) AT_DEFAULT=0 ;; esac
done
[ "$AT_DEFAULT" = 1 ] && EXTRA_ARGS+=("--audioType=s3m")

echo "== 1/2 Kompilacja (clang + SDL2) =="
if ! pkg-config sdl2 >/dev/null 2>&1; then
  echo "  UWAGA: pkg-config nie zna sdl2 — jeśli kompilacja padnie na #include <SDL2/SDL.h>,"
  echo "         zainstaluj:  sudo pacman -S sdl2"
fi
make -C game-linux

if [ "$MODE_RUN" = 0 ]; then
  echo; echo "Gotowe (tylko kompilacja). Binarka: $BIN"
  exit 0
fi

echo; echo "== 2/2 Uruchomienie =="
# podpowiedz portowi, gdzie są dane (port i tak sam wykrywa, to tylko skrót)
if [ -f "$DATA/GRAF.DAT" ]; then
  export POLANIE_DATA="$DATA"
  echo "  dane gry: $POLANIE_DATA"
else
  echo "  UWAGA: nie widzę $DATA/GRAF.DAT — jeśli gra nie ruszy, ustaw POLANIE_DATA."
fi

# PORT: tor MT32 — POLANIE_MT32SF2 (jeśli ustawiona przez usera) przechodzi
# bez zmian do pol2; w przeciwnym razie port wykryje assets/soundfont/*GM.sf2
# (to echo informacyjne, nic nie nadpisujemy).
if [ -n "${POLANIE_MT32SF2:-}" ]; then
  echo "  soundfont MT32: $POLANIE_MT32SF2 (z POLANIE_MT32SF2)"
else
  echo "  soundfont MT32: automat (assets/soundfont/*GM.sf2) — albo ustaw POLANIE_MT32SF2=/sciezka/do/*GM.sf2"
fi

# DECYZJA PROJEKTOWA: skalowanie obrazu robi gamescope (nearest 320x200 ->
# 2560x1600), port renderuje natywne 320x200. Bez gamescope -> bezposrednio
# (wtedy opcjonalna skala n: 1 px gry = n x n px okna).
GS=""
for g in "$REPO/../om-rim/gamescope/build/src/gamescope" "$(command -v gamescope 2>/dev/null || true)"; do
  if [ -n "$g" ] && [ -x "$g" ]; then GS="$g"; break; fi
done

CMD=()
if [ -n "$GS" ] && [ -z "$SCALE" ]; then
  echo "  skalowanie: gamescope nearest 320x200 -> 2560x1600 ($GS)"
  CMD+=("$GS" -W 2560 -H 1600 --filter nearest -f --)
else
  [ -n "$GS" ] && echo "  (skala ręczna: pomijam gamescope)"
  [ -z "$GS" ] && echo "  (brak gamescope — okno bez skalowania; użyj: bash run_port.sh 4)"
fi
CMD+=("$BIN")
[ -n "$SCALE" ] && CMD+=("$SCALE")
[ ${#EXTRA_ARGS[@]} -gt 0 ] && CMD+=("${EXTRA_ARGS[@]}")

# PORT: wymuszenie GL na Intel Iris Pro zamiast wolnego nouveau (dGPU Kepler);
# DRI_PRIME=1 per-app (Mesa↔Mesa, patrz ~/Projects/nv-kepler/NOTES.md).
export DRI_PRIME="${DRI_PRIME:-1}"
if [ "$DRI_PRIME" = 1 ]; then
  echo "  GPU wymuszony: DRI_PRIME=1 (Intel iris) — dGPU nouveau pomijany"
else
  echo "  GPU: DRI_PRIME ustawione ręcznie ($DRI_PRIME) — bez wymuszenia"
fi

if [ "$MODE_TRACE" = 1 ]; then
  if command -v strace >/dev/null; then
    strace -f -e trace=openat,access -o /tmp/pol2.strace "${CMD[@]}"
    echo; echo "  ślad plików zapisany w /tmp/pol2.strace (grep SETUP.INI / GRAF.DAT)"
  else
    echo "  (brak strace w systemie — uruchamiam bez śledzenia)"
    "${CMD[@]}"
  fi
else
  "${CMD[@]}"
fi