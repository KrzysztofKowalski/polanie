#!/usr/bin/env bash
# Uruchomienie portu Polanie (ephemeral/pol2).
#
# Uzycie:
#   bash scripts/run.sh [skala] [opcje pol2]
#     skala          - 1 px gry = n x n fizycznych px (np. 4); bez niej
#                      okno 320x200
#     --audioType=   - s3m (domyslnie) | sfz | auto
#
# Dane gry (ephemeral/dysk/GRY/POLANIE i ephemeral/extracted) sa przekazywane
# przez POLANIE_DATA / POLANIE_EXTRACTED, gdy istnieja - w przeciwnym razie
# nie ustawiane (gra sama wykrywa ./dysk/GRY/POLANIE).
# Diagnostyka przez zmienne srodowiskowe, np.:
#   POL_MOUSE_DEBUG=1 bash scripts/run.sh   # logi walki/myszy
#   POL_REPAIR_DEBUG=1 bash scripts/run.sh  # logi naprawy/budowy drzew
#   POL_VIDEO=gpu bash scripts/run.sh       # backend wideo SDL3 GPU/Vulkan
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
EPHE="$ROOT/ephemeral"
BIN="$EPHE/pol2"

[ -x "$BIN" ] || {
  printf 'BLAD: brak %s - najpierw: bash scripts/build.sh\n' "$BIN" >&2
  exit 1
}

cd "$EPHE"

# dane: przekaz tylko, gdy istnieja (gra i tak sama je wykrywa)
[ -f "$EPHE/dysk/GRY/POLANIE/GRAF.DAT" ] && export POLANIE_DATA="$EPHE/dysk/GRY/POLANIE"
[ -d "$EPHE/extracted" ] && export POLANIE_EXTRACTED="$EPHE/extracted"

exec "$BIN" "$@"