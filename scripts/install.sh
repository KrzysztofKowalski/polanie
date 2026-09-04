#!/usr/bin/env bash
# Instalator danych gry "Polanie" (1997) dla portu Linux.
#
# Co robi (idempotentnie - mozna uruchamiac wielokrotnie):
#   1. klonuje publiczny mirror polanie.prv.pl (git clone, nie curl/zip)
#      do ephemeral/mirror/ (istniejacy klon jest odswiezany, nie klonowany od nowa),
#   2. szuka GRAF.DAT (find, dowolna glebokosc) w klonie oraz w ephemeral/;
#      w klonie GRAF.DAT nie lezy luzem - lezy w instalce dyskietkowej
#      polanie.zip, ktorej wolumeny DATA.0NN to archiwa ARJ. Jak szukanie
#      luzem zawiedzie, skrypt rozpakowuje: polanie.zip (unzip) do
#      ephemeral/dyskietki/, potem DATA.0NN (7z, p7zip) do
#      ephemeral/dyskietki/rozpakowane/ i szuka GRAF.DAT ponownie
#      (INSTALUJ.EXE to program DOS - nie uruchamiamy go),
#   3. kopiuje dane do ephemeral/dysk/GRY/POLANIE,
#   4. buduje ekstraktor (src/tools/polanie_extract.cpp),
#   5. ekstrahuje zasoby (PNG / WAV / S3M / teksty) do ephemeral/extracted,
#   6. buduje i uruchamia check_assets (raport kompletnosci danych).
#
# Opcjonalnie (bash scripts/install.sh --cd): dodatkowo rozpakowuje polanie_cd.zip
# (pelna wersja CD, wziety z tego samego klonu; GRAF.DAT jest w nim bezposrednio)
# do ephemeral/cd/ - NIE skaluje sie z ephemeral/dysk/; gra sama korzysta z cd/,
# gdy tor efektow czegos szuka.
#
# Wszystko laduje w ephemeral/ (mozna skasowac w calosci i uruchomic od nowa).
# Repozytorium NIE zawiera danych gry - pobiera je ten skrypt.
#
# Uzycie:
#   bash scripts/install.sh          # dane pelnej wersji (z mirrora polanie.prv.pl;
#                                    # instalka polanie.zip, ARJ przez 7z)
#   bash scripts/install.sh --cd     # + wersja CD (polanie_cd.zip)
#
# Zaleznosci instalatora: git, g++, unzip, 7z albo 7za (pakiet p7zip).
#
# Zmienne srodowiskowe:
#   POL_MIRROR_URL - nadpisanie URL-i klonu (domyslnie patrz nizej)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
EPHE="$ROOT/ephemeral"
SRC="$ROOT/src"
MIRROR_URL="${POL_MIRROR_URL:-https://github.com/jstasiak/polanie.prv.pl-mirror.git}"
CLONE_DIR="$EPHE/mirror/polanie.prv.pl-mirror"

log() { printf '\n== %s ==\n' "$*"; }
die() { printf 'BLAD: %s\n' "$*" >&2; exit 1; }
wzgledem() { printf '%s' "${1#"$ROOT/"}"; }

# ---- zaleznosci -----------------------------------------------------------
command -v git >/dev/null 2>&1 || die "Potrzebny git (Debian/Ubuntu: apt install git; Fedora: dnf install git)."
command -v g++ >/dev/null 2>&1 || die "Potrzebny g++ (Debian/Ubuntu: apt install g++; Fedora: dnf install gcc-c++)."
# unzip i 7z (p7zip) sa potrzebne zawsze: instalka polanie.zip to wolumeny ARJ
command -v unzip >/dev/null 2>&1 || die "Potrzebny unzip (Debian/Ubuntu: apt install unzip; Arch/Omarchy: pacman -S unzip; Fedora: dnf install unzip)."
if ! command -v 7z >/dev/null 2>&1 && ! command -v 7za >/dev/null 2>&1; then
  die "Potrzebny 7z albo 7za (pakiet p7zip) do rozpakowania wolumenow ARJ z polanie.zip (Debian/Ubuntu: apt install p7zip-full; Arch/Omarchy: pacman -S p7zip; Fedora: dnf install p7zip p7zip-plugins)."
fi

# ---- szukanie danych gry (GRAF.DAT) ---------------------------------------
# Szuka GRAF.DAT (find, dowolna glebokosc) w podanych katalogach; loguje
# wszystkie trafienia i wybiera jeden katalog: preferowane te, ktore maja
# tez PAL.DAT, przy kilku - katalog z najwiekszym GRAF.DAT.
# Wynik: ZNALEZIONY_GRAF (sciezka do pliku) i ZNALEZIONY_KATALOG (jego katalog).
szukaj_graf() { # szukaj_graf <etykieta> <katalog>...
  local etykieta="$1"; shift
  local graf dir rozmiar
  local najwiekszy="" roz_najw=-1 pal_najlepszy="" roz_pal=-1
  local -a trafienia=()
  for dir in "$@"; do
    [ -d "$dir" ] || continue
    while IFS= read -r graf; do
      trafienia+=("$graf")
      rozmiar="$(wc -c <"$graf")"
      if [ "$rozmiar" -gt "$roz_najw" ]; then najwiekszy="$graf"; roz_najw="$rozmiar"; fi
      if [ -f "${graf%/*}/PAL.DAT" ] || [ -f "${graf%/*}/pal.dat" ]; then
        if [ "$rozmiar" -gt "$roz_pal" ]; then pal_najlepszy="$graf"; roz_pal="$rozmiar"; fi
      fi
    done < <(find "$dir" -not -path '*/.git/*' -iname 'GRAF.DAT' 2>/dev/null)
  done
  if [ -z "$najwiekszy" ]; then
    return 1
  fi
  echo "  trafienia GRAF.DAT ($etykieta):"
  printf '    %s (%s B)\n' "${trafienia[@]}"
  ZNALEZIONY_GRAF="${pal_najlepszy:-$najwiekszy}"
  ZNALEZIONY_KATALOG="${ZNALEZIONY_GRAF%/*}"
  echo "  wybrano: $ZNALEZIONY_GRAF ($roz_pal B, katalog z PAL.DAT)"
  return 0
}

# ---- klon mirrora (zamiast pobierania zipa) --------------------------------
klonuj_mirror() {
  if [ -d "$CLONE_DIR/.git" ]; then
    echo "  klon juz istnieje: $(wzgledem "$CLONE_DIR") - odswiezam (git pull)"
    git -C "$CLONE_DIR" pull --ff-only \
      || die "Odswiezenie klona nie powiodlo sie. Kasuj katalog $(wzgledem "$CLONE_DIR") i uruchom skrypt ponownie."
  else
    echo "  klonuje: $MIRROR_URL"
    git clone --depth 1 "$MIRROR_URL" "$CLONE_DIR" \
      || die "git clone nie powiodlo sie (sprawdz polaczenie i URL: $MIRROR_URL)."
  fi
}

# ---- rozpakowanie instalki dyskietkowej (polanie.zip + ARJ) ----------------
# W klonie GRAF.DAT lezy w instalce dyskietkowej polanie.zip: po unzip sa tam
# wolumeny DATA.000..DATA.009 - archiwa ARJ (003-007 po ~1,4 MB to dyskietki).
# INSTALUJ.EXE to program DOS - nie uruchamiamy go; wolumeny rozpakowuje 7z.
# Idempotentnie: jak w ephemeral/dyskietki/rozpakowane/ jest juz GRAF.DAT,
# cale rozpakowanie jest pomijane.
rozpakuj_instalke() {
  local zip z7 arj n
  local CEL="$EPHE/dyskietki"
  local ROZP="$CEL/rozpakowane"

  if [ -f "$ROZP/GRAF.DAT" ] || [ -f "$ROZP/graf.dat" ]; then
    echo "  juz rozpakowane: $(wzgledem "$ROZP") - pomijam unzip/7z"
    return 0
  fi

  zip="$(find "$CLONE_DIR" -not -path '*/.git/*' -iname 'polanie.zip' 2>/dev/null | head -n1 || true)"
  [ -n "$zip" ] || die "Nie znaleziono GRAF.DAT ani polanie.zip. Przeszukane katalogi: ${SZUKANE[*]}. polanie.zip szukano w: $(wzgledem "$CLONE_DIR")."
  echo "  instalka: $zip"

  echo "  rozpakowuje polanie.zip (unzip) -> $(wzgledem "$CEL")"
  mkdir -p "$CEL"
  unzip -o -q "$zip" -d "$CEL" \
    || die "unzip nie powiodl sie na polanie.zip ($zip)."

  z7="$(command -v 7z || command -v 7za || true)"
  echo "  rozpakowuje wolumeny ARJ DATA.0NN (7z) -> $(wzgledem "$ROZP")"
  mkdir -p "$ROZP"
  # najpierw duze wolumeny (003-007, dyskietki ~1,4 MB), potem reszta;
  # porazka pojedynczego wolumenu nie zabija skryptu (set -e): DATA.000 moze
  # nie byc ARJ, a wolumeny ciag dalszy moga prosic o poprzedni/nastepny
  for n in 003 004 005 006 007 000 001 002 008 009; do
    arj="$CEL/DATA.$n"
    [ -f "$arj" ] || { echo "  brak DATA.$n - pomijam"; continue; }
    "$z7" x -y -o"$ROZP" "$arj" \
      || echo "  pomijam DATA.$n (brak ARJ/inny format)"
  done
}

mkdir -p "$EPHE/install"

log "1/6 Pobieranie danych gry (git clone mirrora polanie.prv.pl)"
klonuj_mirror

log "2/6 Szukanie GRAF.DAT (klon mirrora + ephemeral, ew. instalka polanie.zip)"
SZUKANE=( "$CLONE_DIR" "$EPHE" )
if [ -d "$HOME/.local/share/polanie/ephemeral" ]; then
  SZUKANE+=( "$HOME/.local/share/polanie/ephemeral" )
fi
SRC_GAME=""
if szukaj_graf "pelna wersja" "${SZUKANE[@]}"; then
  SRC_GAME="$ZNALEZIONY_KATALOG"
else
  echo "  GRAF.DAT nie lezy luzem w klonie - rozpakowuje instalke polanie.zip"
  echo "  (wolumeny DATA.0NN to archiwa ARJ, rozpakowuje je 7z)"
  rozpakuj_instalke
  szukaj_graf "pelna wersja (z instalki)" "$EPHE/dyskietki/rozpakowane" \
    || die "Nie znaleziono GRAF.DAT. Przeszukane katalogi: ${SZUKANE[*]} oraz ephemeral/dyskietki/rozpakowane. Rozpakowano polanie.zip (unzip) i wolumeny DATA.0NN (7z) do ephemeral/dyskietki/. Prosze dolaczyc do zgloszenia pelny log wyjscia 7z (i unzip)."
  SRC_GAME="$ZNALEZIONY_KATALOG"
fi
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
  log "CD 1/2 Wersja CD (polanie_cd.zip z klona mirrora, opcjonalna)"
  CD_ZIP="$(find "$CLONE_DIR" -not -path '*/.git/*' -iname 'polanie_cd.zip' 2>/dev/null | head -n1)"
  [ -n "$CD_ZIP" ] || die "W klonie mirrora nie ma polanie_cd.zip (przeszukano: $(wzgledem "$CLONE_DIR"))."
  echo "  archiwum CD: $CD_ZIP"
  log "CD 2/2 Rozpakowanie -> ephemeral/cd/ (nie scalane z ephemeral/dysk/)"
  rm -rf "$EPHE/cd/unzipped"
  mkdir -p "$EPHE/cd/unzipped"
  unzip -q "$CD_ZIP" -d "$EPHE/cd/unzipped"
  szukaj_graf "wersja CD" "$EPHE/cd/unzipped" \
    || die "Nie znaleziono GRAF.DAT w polanie_cd.zip (przeszukano: $(wzgledem "$EPHE/cd/unzipped"))."
  mkdir -p "$EPHE/cd/polanie_cd"
  cp -a "$ZNALEZIONY_KATALOG/." "$EPHE/cd/polanie_cd/"
  echo "  wersja CD: ephemeral/cd/polanie_cd/ (do toru efektow/muzyki CD)"
fi

log "Instalacja zakonczona. Kolejny krok: bash scripts/build.sh"