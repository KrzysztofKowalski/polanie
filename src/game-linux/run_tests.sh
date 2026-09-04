#!/usr/bin/env bash
# Kompilacja i uruchomienie TESTOW JEDNOSTKOWYCH portu Polanie (src/game-linux/).
#
# Uzycie:
#   bash run_tests.sh           - skompiluj i uruchom testy (gtest);
#                                 kod wyjscia skryptu = kod wyjscia testow
#   bash run_tests.sh -c        - tylko kompilacja, bez uruchamiania
#                                 (gdyby testy kiedyś chciały okna/audio)
#   bash run_tests.sh -h        - krotki opis
#   bash run_tests.sh -- --gtest_filter='NormalizacjaSciezki.*'
#                               - argumenty po "--" ida prosto do binarki testow
#
# PORT (2026-09-03): przy FAILU detale asercji wypisywane sa NA KONCU wyjscia
# (user wkleja ogon loga - detale w srodku bezpowrotnie ginialy i tracilismy
# iteracje debugowania). Pelny przebieg idzie przez tee do pliku tymczasowego
# (mktemp, sprzatane w trapie na EXIT); kod wyjscia skryptu = kod wyjscia
# PIERWSZEGO (pelnego) przebiegu; przy porazce wyciagane sa nazwy zepsutych
# testow z loga i uruchamiany drugi przebieg tylko z nimi (jego kod wyjscia
# jest ignorowany).
#
# Testy nie potrzebuja danych gry ani okna: same buduja katalog tymczasowy
# z syntetycznymi plikami (GRAF.DAT/PAL.DAT/...) w /tmp i sprzataja po sobie;
# warstwa wideo/audio portu jest w testach podmieniona na stuby (tests/).
# Ekwiwalent reczny:  make -C src/game-linux testy
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
SRC="$DIR"
EPHE="$(cd "$DIR/../.." && pwd)/ephemeral"
TESTY="$EPHE/build/testy"

MODE_RUN=1
ARGS=()
for arg in "$@"; do
  case "$arg" in
    -c|--compile) MODE_RUN=0 ;;
    -h|--help)
      sed -n '2,26p' "$0" | sed 's/^# \{0,1\}//'
      exit 0 ;;
    --) shift; ARGS+=("$@"); break ;;
    *) echo "Nieznany argument: $arg (pomoc: bash run_tests.sh -h)"; exit 1 ;;
  esac
done

mkdir -p "$EPHE/build"

echo "== 1/2 Kompilacja testow (g++ + Google Test) =="
if ! pkg-config gtest >/dev/null 2>&1; then
  echo "  UWAGA: pkg-config nie zna gtest - jesli kompilacja padnie na <gtest/gtest.h>,"
  echo "         zainstaluj gtest (Debian/Ubuntu: libgtest-dev; Fedora: gtest-devel)."
fi
make -C "$SRC" testy BUILD="$EPHE/build" BIN="$EPHE/pol2"

if [ "$MODE_RUN" = 0 ]; then
  echo; echo "Gotowe (tylko kompilacja). Binarka: $TESTY"
  exit 0
fi

echo "== 2/2 Uruchomienie testow =="
LOG="$(mktemp /tmp/polanie_tests.XXXXXX.log)"
trap 'rm -f "$LOG"' EXIT

# PORT: set +e wokol pelnego przebiegu - pipefail + tee zjadalby RC binarki;
# kod wyjscia skryptu bierny z PIPESTATUS[0] (pierwszy, pelny przebieg).
set +e
"${TESTY}" "${ARGS[@]+"${ARGS[@]}"}" 2>&1 | tee "$LOG"
RC=${PIPESTATUS[0]}
set -e

if [ "$RC" -ne 0 ]; then
  # PORT: nazwy zepsutych testow = linie "[  FAILED  ] Suite.Test" z loga
  FILTER="$(grep '^\[  FAILED  \] [^0-9]' "$LOG" | sed 's/^\[  FAILED  \] //' | paste -sd: -)"
  echo
  echo "== 2b/2 Detale zepsutych testow =="
  if [ -n "$FILTER" ]; then
    # drugi przebieg tylko zepsutych: detale na stdout, RC ignorowane
    "${TESTY}" --gtest_filter="$FILTER" || true
  else
    echo "  brak nazw zepsutych testow w logu (mozliwy crash binarki) - pelny log: $LOG"
  fi
fi

exit $RC