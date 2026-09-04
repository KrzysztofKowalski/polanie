#!/usr/bin/env bash
# Kompilacja i uruchomienie NATYWNEGO portu Polanie (src/game-linux/, SDL3).
#
# Cienki wrapper nad scripts/build.sh + scripts/run.sh w korzeniu official/ -
# tam zyja sciezki ephemeral/ (dane, artefakty builda) i detale uruchomienia.
#
# Uzycie:
#   bash run_port.sh            - skompiluj i uruchom
#   bash run_port.sh -c         - tylko kompilacja, bez uruchamiania
#   bash run_port.sh [skala] [--audioType=s3m|sfz|auto] [inne opcje pol2]
#                                 - argumenty po -c/-s ida do run.sh/pol2
#                                 (skala: 1 px gry = n x n fizycznych px)
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"

if [ "${1:-}" = "-c" ]; then
  bash "$DIR/../../scripts/build.sh"
  exit 0
fi

bash "$DIR/../../scripts/build.sh"
exec bash "$DIR/../../scripts/run.sh" "$@"