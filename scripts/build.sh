#!/usr/bin/env bash
# Budowa portu Polanie (src/game-linux/, SDL3) - artefakty w ephemeral/.
#
# Uzycie:
#   bash scripts/build.sh
#
# Artefakty: ephemeral/build/ (obiekty), ephemeral/pol2 (binarka gry).
# Zrodla: src/game/ (oryginal, bez zmian) + src/game-linux/ (port).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SRC="$ROOT/src/game-linux"
EPHE="$ROOT/ephemeral"
BIN="$EPHE/pol2"

log() { printf '\n== %s ==\n' "$*"; }
die() { printf 'BLAD: %s\n' "$*" >&2; exit 1; }
uwaga() { printf 'UWAGA: %s\n' "$*"; }

log "0/2 Zaleznosci"
if ! command -v pkg-config >/dev/null 2>&1; then
  uwaga "brak pkg-config - make nie znajdzie sdl3 (Debian/Ubuntu: apt install pkg-config)."
fi
if ! pkg-config sdl3 >/dev/null 2>&1; then
  uwaga "pkg-config nie zna sdl3 (Debian/Ubuntu: apt install libsdl3-dev)."
fi
if ! pkg-config libopenmpt >/dev/null 2>&1; then
  uwaga "pkg-config nie zna libopenmpt - muzyka S3M zostanie za interfejsem (stub). (Debian/Ubuntu: apt install libopenmpt-dev)."
fi
if ! pkg-config sfizz >/dev/null 2>&1 && ! ls /usr/include/sfizz.h /usr/local/include/sfizz.h >/dev/null 2>&1; then
  uwaga "brak sfizz (tor MIDI+sfizz) - tor zostanie za interfejsem (Debian/Ubuntu: apt install libsfizz-dev)."
fi
if ! command -v glslangValidator >/dev/null 2>&1 && ! command -v glslc >/dev/null 2>&1; then
  uwaga "brak glslangValidator/glslc - backend wideo GPU (POL_VIDEO=gpu) wylaczony (bez shadrow SPIR-V). Opcjonalne: apt install glslang-tools."
fi
if ! pkg-config gtest >/dev/null 2>&1; then
  uwaga "pkg-config nie zna gtest - testy (make testy) sie nie zbuduja. Opcjonalne: apt install libgtest-dev."
fi

log "1/2 Kompilacja (make -C src/game-linux)"
make -C "$SRC" -j4 BUILD="$EPHE/build" BIN="$EPHE/pol2"

log "2/2 Gotowe."
echo "  binarka gry : ephemeral/pol2"
echo "  uruchomienie: bash scripts/run.sh"
echo "  testy       : bash src/game-linux/run_tests.sh"