#!/usr/bin/env bash
# Instalator danych gry "Polanie" (1997) dla portu Linux.
#
# Co robi (idempotentnie - mozna uruchamiac wielokrotnie):
#   1. pobiera polanie.zip (pelna wersja) z publicznego mirrora polanie.prv.pl
#      do ephemeral/install/,
#   2. rozpakowuje i szuka katalogu z GRAF.DAT,
#   3. kopiuje dane do ephemeral/dysk/GRY/POLANIE,
#   4. buduje ekstraktor (src/tools/polanie_extract.cpp),
#   5. ekstrahuje zasoby (PNG / WAV / S3M / teksty) do ephemeral/extracted,
#   6. buduje i uruchamia check_assets (raport kompletnosci danych).
#
# Opcjonalnie (bash scripts/install.sh --cd): dodatkowo pobiera i rozpakowuje
# polanie_cd.zip (pelna wersja CD) do ephemeral/cd/ - NIE skaluje sie z
# ephemeral/dysk/; gra sama korzysta z cd/, gdy tor efektow czegos szuka.
#
# Wszystko ląduje w ephemeral/ (mozna skasowac w calosci i uruchomic od nowa).
# Repozytorium NIE zawiera danych gry - pobiera je ten skrypt.
#
# Uzycie:
#   bash scripts/install.sh          # dane pelnej wersji (polanie.zip)
#   bash scripts/install.sh --cd     # + wersja CD (polanie_cd.zip)
#
# Zmienne srodowiskowe:
#   POL_MIRROR_BASE - nadpisanie bazy mirrora (domyslnie patrz nizej)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
EPHE="$ROOT/ephemeral"
SRC="$ROOT/src"
MIRROR_BASE="${POL_MIRROR_BASE:-https://raw.githubusercontent.com/jstasiak/polanie.prv.pl-mirror/master/polanie.prv.pl/files/pliki/}"

log() { printf '\n== %s ==\n' "$*"; }
die() { printf 'BLAD: %s\n' "$*" >&2; exit 1; }
wzgledem() { printf '%s' "${1#"$ROOT/"}"; }

# ---- zaleznosci -----------------------------------------------------------
FETCH=""
if command -v curl >/dev/null 2>&1; then
  FETCH=curl
elif command -v wget >/dev/null 2>&1; then
  FETCH=wget
else
  die "Potrzebny curl albo wget (Debian/Ubuntu: apt install curl unzip; Fedora: dnf install curl unzip)."
fi
command -v unzip >/dev/null 2>&1 || die "Potrzebny unzip (Debian/Ubuntu: apt install unzip; Fedora: dnf install unzip)."
command -v g++ >/dev/null 2>&1 || die "Potrzebny g++ (Debian/Ubuntu: apt install g++; Fedora: dnf install gcc-c++)."

fetch() { # fetch <url> <plik_docelowy>
  local url="$1" out="$2"
  if [ -s "$out" ]; then
    echo "  juz pobrane: $(wzgledem "$out")"
    return 0
  fi
  echo "  pobieram: $url"
  if [ "$FETCH" = curl ]; then
    curl -fL --retry 2 -o "$out" "$url"
  else
    wget -q -O "$out" "$url"
  fi
}

# katalog z GRAF.DAT w rozpakowanym archiwum (prosto albo w podkatalogu);
# preferowane trafienia, ktore maja tez PAL.DAT
znajdz_dane() { # znajdz_dane <korzen_rozpakowania>
  local korzen="$1" graf
  while IFS= read -r graf; do
    if [ -f "${graf%/*}/PAL.DAT" ] || [ -f "${graf%/*}/pal.dat" ]; then
      printf '%s\n' "${graf%/*}"
      return 0
    fi
  done < <(find "$korzen" -iname GRAF.DAT 2>/dev/null)
  find "$korzen" -iname GRAF.DAT 2>/dev/null | head -n1 | xargs -r -I{} dirname {}
}

mkdir -p "$EPHE/install"

log "1/6 Pobieranie archiwum gry (polanie.zip, pelna wersja)"
fetch "$MIRROR_BASE/polanie.zip" "$EPHE/install/polanie.zip"

log "2/6 Rozpakowanie i szukanie GRAF.DAT"
rm -rf "$EPHE/install/unzipped"
mkdir -p "$EPHE/install/unzipped"
unzip -q "$EPHE/install/polanie.zip" -d "$EPHE/install/unzipped"
SRC_GAME="$(znajdz_dane "$EPHE/install/unzipped")"
[ -n "$SRC_GAME" ] || die "Nie znaleziono GRAF.DAT w rozpakowanym archiwum (uszkodzony pobór?)."
echo "  dane gry: $(wzgledem "$SRC_GAME")"

log "3/6 Kopiowanie danych do ephemeral/dysk/GRY/POLANIE"
if [ -f "$EPHE/dysk/GRY/POLANIE/GRAF.DAT" ]; then
  echo "  juz istnieje - pomijam (kasuj ephemeral/dysk, aby odswiezyc)"
else
  mkdir -p "$EPHE/dysk/GRY/POLANIE"
  cp -a "$SRC_GAME/." "$EPHE/dysk/GRY/POLANIE/"
  echo "  skopiowano: $(wzgledem "$SRC_GAME") -> ephemeral/dysk/GRY/POLANIE"
fi

log "4/6 Budowa ekstraktora (src/tools/polanie_extract.cpp)"
g++ -std=c++20 -O2 -o "$EPHE/install/polanie-extract" "$SRC/tools/polanie_extract.cpp"
echo "  gotowe: ephemeral/install/polanie-extract"

log "5/6 Ekstrakcja zasobow -> ephemeral/extracted"
"$EPHE/install/polanie-extract" "$EPHE/dysk/GRY/POLANIE" "$EPHE/extracted"

log "6/6 Weryfikacja danych (src/tools/check_assets.cpp)"
g++ -std=c++20 -O2 -o "$EPHE/check_assets" "$SRC/tools/check_assets.cpp"
POLANIE_DATA="$EPHE/dysk/GRY/POLANIE" POLANIE_EXTRACTED="$EPHE/extracted" \
  "$EPHE/check_assets"

# ---- opcjonalna wersja CD -------------------------------------------------
if [ "${1:-}" = "--cd" ]; then
  log "CD 1/2 Pobieranie polanie_cd.zip (pelna wersja CD, opcjonalna)"
  fetch "$MIRROR_BASE/polanie_cd.zip" "$EPHE/install/polanie_cd.zip"
  log "CD 2/2 Rozpakowanie -> ephemeral/cd/ (nie scalane z ephemeral/dysk/)"
  rm -rf "$EPHE/cd/unzipped"
  mkdir -p "$EPHE/cd/unzipped"
  unzip -q "$EPHE/install/polanie_cd.zip" -d "$EPHE/cd/unzipped"
  SRC_CD="$(znajdz_dane "$EPHE/cd/unzipped")"
  [ -n "$SRC_CD" ] || die "Nie znaleziono GRAF.DAT w polanie_cd.zip (uszkodzony pobor?)."
  mkdir -p "$EPHE/cd/polanie_cd"
  cp -a "$SRC_CD/." "$EPHE/cd/polanie_cd/"
  echo "  wersja CD: ephemeral/cd/polanie_cd/ (do toru efektow/muzyki CD)"
fi

log "Instalacja zakonczona. Kolejny krok: bash scripts/build.sh"