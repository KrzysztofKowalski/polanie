#!/usr/bin/env bash
# Instalacja zaleznosci portu Polanie przez pacman (system Arch/Omarchy).
#
# Uzycie:
#   bash scripts/install_deps.sh                # pakiety podstawowe (core)
#   bash scripts/install_deps.sh --opcjonalne   # + gamescope, glslang, gtest
#   bash scripts/install_deps.sh --all          # j.w. (alias)
#   bash scripts/install_deps.sh --help
#
# Co instaluje:
#   CORE (zawsze):
#     base-devel  - g++, make, (narzedzia budowy)
#     pkgconf     - pkg-config (Makefile szuka sdl3/libopenmpt/sfizz)
#     sdl3        - wideo/wejscie/dzwiek portu
#     libopenmpt  - muzyka S3M
#     sfizz       - tor alternatywny MIDI+sfizz
#     unzip, p7zip, git, unar - instalator assets (scripts/install.sh);
#                 unar to fallback dekompresji ARJ
#   OPCJONALNE (--opcjonalne / --all):
#     gamescope   - skalowanie okna gry
#     glslang     - glslangValidator, shadery backendu GPU (POL_VIDEO=gpu)
#     gtest       - testy jednostkowe (make testy)
#
# arj: pacman go NIE zainstaluje (jest w AUR). Po instalacji skrypt sprawdza
# arj i, gdy go brak, drukuje UWAGA (nie konczy bledem): bez arj instalator
# assets uzyje 7z z p7zip, a 7z NIE skleja wolumenow ARJ multi-volume
# (DATA.003..DATA.007), wiec zalecane jest doinstalowanie arj (yay/paru).
#
# Po instalacji drukuje krotki raport: pkg-config sdl3/libopenmpt/sfizz
# oraz command -v dla narzedzi. Nie uruchamia instalatora ani budowy - to
# robia scripts/install.sh i scripts/build.sh.
#
# Inne dystrybucje: komendy instalacji w README.md (sekcja "Wymagania") -
# ten skrypt jest wylacznie dla pacman.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

log() { printf '\n== %s ==\n' "$*"; }
die() { printf 'BLAD: %s\n' "$*" >&2; exit 1; }
uwaga() { printf 'UWAGA: %s\n' "$*"; }

usage() {
  cat <<'EOF'
Uzycie:
  bash scripts/install_deps.sh                # pakiety podstawowe (core)
  bash scripts/install_deps.sh --opcjonalne   # + gamescope, glslang, gtest
  bash scripts/install_deps.sh --all          # j.w. (alias)
EOF
}

# ---- flagi -----------------------------------------------------------------
OPCJONALNE=0
for a in "$@"; do
  case "$a" in
    --opcjonalne|--all) OPCJONALNE=1 ;;
    -h|--help) usage; exit 0 ;;
    *)
      usage >&2
      die "Nieznana opcja: $a"
      ;;
  esac
done

CORE=(base-devel sdl3 libopenmpt sfizz unzip p7zip git unar pkgconf)
OPCJE=(gamescope glslang gtest)

# ---- pacman + uprawnienia ---------------------------------------------------
log "1/3 Sprawdzenie pacman i uprawnien"
if ! command -v pacman >/dev/null 2>&1; then
  die "Nie znaleziono pacman - ten skrypt jest wylacznie dla Arch/Omarchy. Debian/Ubuntu i Fedora: komendy instalacji zaleznosci w README.md (sekcja 'Wymagania')."
fi
echo "  pacman: $(command -v pacman)"
if [ "$(id -u)" -eq 0 ]; then
  PACMAN=(pacman)
  echo "  tryb: root"
elif command -v sudo >/dev/null 2>&1; then
  PACMAN=(sudo pacman)
  echo "  tryb: user + sudo"
else
  die "Nie jestem root i nie ma sudo - uruchom skrypt jako root albo zainstaluj/udostepnij sudo."
fi

# ---- instalacja -------------------------------------------------------------
log "2/3 Instalacja pakietow (pacman -S --needed - nie reinstaluje)"
echo "  polecenie: ${PACMAN[*]} -S --needed ${CORE[*]}"
if ! "${PACMAN[@]}" -S --needed "${CORE[@]}"; then
  die "pacman nie zainstalowal pakietow podstawowych (log powyzej)."
fi

if [ "$OPCJONALNE" -eq 1 ]; then
  echo
  echo "  polecenie: ${PACMAN[*]} -S --needed ${OPCJE[*]}"
  if ! "${PACMAN[@]}" -S --needed "${OPCJE[@]}"; then
    die "pacman nie zainstalowal pakietow opcjonalnych (log powyzej)."
  fi
else
  echo
  echo "  pomijam opcjonalne: ${OPCJE[*]}"
  echo "  (dodaje je flaga --opcjonalne albo --all; opis w naglowku skryptu)"
fi

# ---- weryfikacja ------------------------------------------------------------
log "3/3 Weryfikacja zaleznosci"
BRAK=0

if command -v pkg-config >/dev/null 2>&1; then
  for p in sdl3 libopenmpt sfizz; do
    if pkg-config --exists "$p"; then
      echo "  OK  : pkg-config $p"
    else
      echo "  BRAK: pkg-config $p"
      BRAK=1
    fi
  done
else
  echo "  BRAK: pkg-config (komenda niedostepna - sdl3/libopenmpt/sfizz nie do sprawdzenia)"
  BRAK=1
fi

for n in git g++ make unzip; do
  if c="$(command -v "$n")"; then
    echo "  OK  : $n ($c)"
  else
    echo "  BRAK: $n"
    BRAK=1
  fi
done

# dekompresja ARJ w instalatorze: arj (AUR) -> 7z/7za (p7zip) -> unar
Z7=""
for n in 7z 7za; do
  if c="$(command -v "$n")"; then Z7="$c"; break; fi
done
if [ -n "$Z7" ]; then
  echo "  OK  : 7z/7za ($Z7) - p7zip"
else
  echo "  BRAK: 7z/7za (p7zip)"
  BRAK=1
fi
if c="$(command -v unar)"; then
  echo "  OK  : unar ($c) - fallback dekompresji ARJ"
else
  echo "  BRAK: unar (fallback dekompresji ARJ - niekrytyczny przy arj/7z)"
fi

# arj jest w AUR - pacman go nie zainstaluje; brak = ostrzezenie, nie blad
if c="$(command -v arj)"; then
  echo "  OK  : arj ($c) - dekompresja ARJ multi-volume"
else
  echo "  BRAK: arj (jest w AUR - pacman go nie instaluje)"
  uwaga "brak narzedzia arj. arj jest w AUR, zainstaluj recznie: yay -S arj (albo paru -S arj). Bez arj instalator assets (scripts/install.sh) uzyje 7z z p7zip, a 7z NIE skleja wolumenow ARJ multi-volume (DATA.003..DATA.007), wiec czesc danych (m.in. DATA/W*.DAT - efekty) moze nie dojsc. Zalecany arj."
fi

echo
if [ "$BRAK" -eq 0 ]; then
  log "Zaleznosci kompletne. Kolejne kroki:"
else
  log "Sa braki (patrz wyzej). Po ich uzupelnieniu - kolejne kroki:"
  exit 1
fi
echo "  bash scripts/install.sh   # dane gry -> ephemeral/"
echo "  bash scripts/build.sh     # budowa portu -> ephemeral/pol2"
echo "  bash scripts/run.sh       # uruchomienie gry"