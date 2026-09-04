#!/usr/bin/env bash
# Instalator danych gry "Polanie" (1997) dla portu Linux.
#
# Odtwarza instalacje, ktora w 1997 r. robil INSTALUJ.EXE pod DOS-em z dyskietek
# (polanie.zip) - bez DOS-a. Kolejnosc faz (idempotentnie - mozna uruchamiac
# wielokrotnie; ponowny przebieg doklada tylko brakujace elementy):
#   1. klonuje publiczny mirror polanie.prv.pl (git clone, nie curl/zip)
#      do ephemeral/mirror/ (istniejacy klon jest odswiezany, nie klonowany),
#   2. rozpakowuje instalke dyskietkowa: polanie.zip (unzip) ->
#      ephemeral/dyskietki/, potem dekompresuje wolumeny DATA.0NN
#      (INSTALUJ.EXE to program DOS - nie uruchamiamy go; skrypt robi to,
#      co on):
#        DATA.001, DATA.002     pojedyncze archiwa ARJ: GRAF.DAT, PAL.DAT,
#                               FONT.DAT, POST.DAT, SWIAT.DAT, LEVEL.DAT/INI,
#                               GRAF.0NN, SETUP1/2.DAT, PLAYER.EXE, POLANIE.EXE,
#        DATA.003..DATA.007     ARJ multi-volume, 5 dyskietek po ~1,4 MB;
#                               p7zip NIE skleja wolumenow ARJ, wiec skrypt
#                               preferuje narzedzie `arj`, a wolumeny podsuwa
#                               pod nazwy serii dowiazaniami w
#                               ephemeral/dyskietki/wolumeny/:
#                               DATA.003 -> DATA.A01 ... DATA.007 -> DATA.A05;
#                               arj rozpakowuje pelny zestaw plikow instalacji
#                               (m.in. FONT.DAT, SETUP.PAL, SETUP1/2.DAT oraz
#                               katalog DATA/ z W001.DAT - efekty WAV)
#                               bezposrednio do ephemeral/dyskietki/rozpakowane/;
#                               SWIAT.DAT jest wykluczony (-x), bo arj 3.10
#                               pada na plikach przecinajacych granice
#                               wolumenow ("stack smashing"), a SWIAT.DAT
#                               i tak juz przychodzi z DATA.001/DATA.002;
#                               przy porazce arj skrypt probuje jeszcze unar,
#        DATA.000, DATA.009     programy DOS (magic MZ): DATA.000 zostaje
#                               DATA.000, DATA.009 lezy w instalacji jako
#                               POLANIE.EXE,
#        DATA.008               zaszyfrowany, wlasny format instalatora -
#                               pomijany z odpowiednim logiem,
#      wynik laduje w ephemeral/dyskietki/rozpakowane/ ukladany wg wzorca
#      instalacji: GRAF.DAT PAL.DAT FONT.DAT PLAYER.EXE POLANIE.EXE DATA.000
#      SETUP.PAL SETUP1/2.DAT + GRAF/ (GRAF.001-018 + LEVEL.DAT + LEVEL.INI)
#      + DATA/ (W001.DAT ... - efekty WAV, z wolumenow multi-volume),
#   3. kopiuje te strukture do ephemeral/dysk/GRY/POLANIE (doklada braki)
#      i sprawdza pelnosc wzorca - brak = koniec z lista i prosba o feedback,
#   4. buduje ekstraktor (src/tools/polanie_extract.cpp),
#   5. ekstrahuje zasoby (PNG / WAV / S3M / teksty) do ephemeral/extracted,
#   6. buduje i uruchamia check_assets (raport kompletnosci danych).
#   Ekstraktor dziala na plikach PO instalacji (pelna struktura wynikowa,
#   jak po INSTALUJ.EXE). Jesli dekompresja czegos nie dostarczyla, skrypt
#   konczy z lista brakow i prosba o feedback - ekstraktor nie jest zmieniany.
#
# Opcjonalnie (bash scripts/install.sh --cd): miedzy fazami 3 i 4 rozpakowuje
# polanie_cd.zip (pelna wersja CD, wziety z tego samego klonu; GRAF.DAT jest
# w nim bezposrednio) do ephemeral/cd/ - NIE skaluje sie z ephemeral/dysk/;
# gra sama korzysta z cd/, gdy tor efektow czegos szuka. Przy okazji doklada
# do ephemeral/dysk/GRY/POLANIE brakujace pliki opcjonalne, ktorych dyskietki
# nie dostarczaja (pelny zestaw efektow DATA/I*.dat, DATA/SOUND.DAT - bank
# instrumentow, banki obrazow PIC.DAT/SETUP.DAT/INSTALL*), dzieki czemu
# ekstrakcja (faza 5) powstaje od razu pelna.
#
# Wszystko laduje w ephemeral/ (mozna skasowac w calosci i uruchomic od nowa).
# Repozytorium NIE zawiera danych gry - pobiera je ten skrypt.
#
# Uzycie:
#   bash scripts/install.sh          # dane pelnej wersji (z mirrora polanie.prv.pl)
#   bash scripts/install.sh --cd     # + wersja CD (polanie_cd.zip)
#
# Zaleznosci instalatora: git, g++, unzip oraz arj (zalecane - skleja ARJ
# multi-volume; Arch/Omarchy: yay -S arj) albo 7z/7za (p7zip; NIE skleja
# wolumenow ARJ) + opcjonalnie unar (fallback, gdyby arj padl na plikach
# przecinajacych granice wolumenow).
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
command -v unzip >/dev/null 2>&1 || die "Potrzebny unzip (Debian/Ubuntu: apt install unzip; Arch/Omarchy: pacman -S unzip; Fedora: dnf install unzip)."
# dekompresja ARJ: preferowany pakiet arj (skleja multi-volume), fallback 7z
ARJ_BIN="$(command -v arj || true)"
Z7_BIN="$(command -v 7z || command -v 7za || true)"
if [ -z "$ARJ_BIN" ] && [ -z "$Z7_BIN" ]; then
  die "Potrzebne narzedzie arj (zalecane) albo 7z/7za (p7zip-full). arj - Debian/Ubuntu: apt install arj; Arch/Omarchy: yay -S arj; Fedora: dnf install arj. p7zip - Debian/Ubuntu: apt install p7zip-full; Arch/Omarchy: pacman -S p7zip. UWAGA: p7zip NIE skleja wolumenow ARJ multi-volume (DATA.003..DATA.007), wiec zalecany jest pakiet arj."
fi
if [ -z "$ARJ_BIN" ]; then
  echo "UWAGA: brak narzedzia arj - uzywam 7z (fallback). p7zip NIE skleja wolumenow ARJ multi-volume; zalecany pakiet arj (Debian/Ubuntu: apt install arj; Arch/Omarchy: yay -S arj), ewentualnie unar."
fi

# ---- szukanie pliku w katalogu (case-insensitive) --------------------------
# plik_ci <katalog> <nazwa>  -> drukuje pelna sciezke trafienia, kod 1 = brak.
# Sprawdza kilka wariantow wielkosci liter (nazwy z DOS-owych archiwow ARJ
# sa przewaznie wielkie, ale 7z potrafi wypakowac malymi).
plik_ci() { # plik_ci <katalog> <nazwa>
  local dir="$1" nazwa="$2" w
  [ -n "$dir" ] || return 1
  for w in "$nazwa" "${nazwa,,}" "${nazwa^}"; do
    if [ -f "$dir/$w" ]; then
      printf '%s' "$dir/$w"
      return 0
    fi
  done
  return 1
}

# ---- dekompresja ARJ (arj preferowane, 7z jako fallback) -------------------
rozpakuj_archiwum() { # rozpakuj_archiwum <archiwum> <katalog_celu>
  if [ -n "$ARJ_BIN" ]; then
    "$ARJ_BIN" x -y "$1" "$2"
  else
    "$Z7_BIN" x -y -o"$2" "$1"
  fi
}

# ---- szukanie GRAF.DAT (uzywane wylacznie przez tryb --cd) -----------------
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
  echo "  wybrano: $ZNALEZIONY_GRAF (katalog z PAL.DAT)"
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

# ---- porzadkowanie wyniku dekompresji do wzorca instalacji -----------------
#   GRAF.0NN -> GRAF/         (instalator trzymal muzyke S3M w podkatalogu
#                             GRAF; ekstraktor czyta GRAF/GRAF.0xx),
#   LEVEL.DAT/LEVEL.INI -> GRAF/ (kopia; ekstraktor czyta GRAF/, a
#                             check_assets szuka ich w GRAF/ albo LEVELS/).
normalizuj_rozpakowane() { # normalizuj_rozpakowane <katalog>
  local rozp="$1" src p
  # GRAF.0NN lezace luzem -> GRAF/ (zawsze, takze pojedyncze duplikaty)
  local -a arkusze=()
  shopt -s nullglob
  arkusze=("$rozp"/GRAF.[0-9][0-9][0-9] "$rozp"/graf.[0-9][0-9][0-9])
  shopt -u nullglob
  if [ "${#arkusze[@]}" -gt 0 ]; then
    mkdir -p "$rozp/GRAF"
    mv -f "${arkusze[@]}" "$rozp/GRAF/"
    echo "  ulozone: GRAF.0NN -> GRAF/"
  fi
  for p in LEVEL.DAT LEVEL.INI; do
    if ! plik_ci "$rozp/GRAF" "$p" >/dev/null; then
      src="$(plik_ci "$rozp" "$p" || true)"
      if [ -n "$src" ]; then
        mkdir -p "$rozp/GRAF"
        cp -a "$src" "$rozp/GRAF/$p"
        echo "  ulozone: $p -> GRAF/ (kopia; oryginal zostaje luzem)"
      fi
    fi
  done
  return 0
}

# ---- ARJ multi-volume (DATA.003..DATA.007 -> pelny zestaw plikow + DATA/) --
# 5 dyskietek po ~1,4 MB to jedna seria ARJ o nazwie serii DATA.A: narzedzie
# arj szuka kolejnych wolumenow o nazwach DATA.A01..DATA.A05, wiec w
# ephemeral/dyskietki/wolumeny/ skrypt robi dowiazania do DATA.003..DATA.007
# i wypakowuje DATA.A01 (arj laczy wolumeny sam; p7zip tego nie potrafi).
# SWIAT.DAT jest wykluczony (-x): plik przecina granice wolumenow, a arj 3.10
# pada na nim ("stack smashing"); SWIAT.DAT i tak przychodzi z DATA.001/002.
# Wynik: pelny zestaw plikow instalacji (m.in. SETUP.PAL, SETUP1/2.DAT) oraz
# katalog DATA/ z W*.DAT (efekty) - wypakowywany bezposrednio do rozpakowane/.
# Fallback przy porazce arj: unar (obsuguje ARJ). Walidacja: liczba i
# sumaryczny rozmiar DATA/W*.DAT.
dekompresuj_multi_volume() { # dekompresuj_multi_volume <dyskietki> <rozpakowane> <wolumeny>
  local cel="$1" rozp="$2" wol="$3" n src i f komenda="(brak proby)"
  for n in 003 004 005 006 007; do
    plik_ci "$cel" "DATA.$n" >/dev/null \
      || die "Brak wolumenu DATA.$n - seria multi-volume DATA.003..DATA.007 jest niekompletna (katalog: $(wzgledem "$cel"))."
  done
  if plik_ci "$rozp/DATA" W001.DAT >/dev/null; then
    echo "  DATA/ (efekty) juz rozpakowana z wolumenow multi-volume - pomijam"
    return 0
  fi
  mkdir -p "$wol"
  i=1
  for n in 003 004 005 006 007; do
    src="$(plik_ci "$cel" "DATA.$n")"
    ln -sfn "../DATA.$n" "$wol/DATA.A0$i"
    i=$((i + 1))
  done
  echo "  dowiazania serii ARJ: $(wzgledem "$wol")/DATA.A01..A05 -> DATA.003..DATA.007"
  if [ -n "$ARJ_BIN" ]; then
    komenda="arj x -y -x SWIAT.DAT DATA.A01"
    echo "  dekompresuje wolumeny ARJ ($komenda) -> $(wzgledem "$rozp")"
    "$ARJ_BIN" x -y -x SWIAT.DAT "$wol/DATA.A01" "$rozp" || true
  fi
  if ! plik_ci "$rozp/DATA" W001.DAT >/dev/null; then
    if command -v unar >/dev/null 2>&1; then
      komenda="unar -force-overwrite -o <rozpakowane> DATA.A01"
      echo "  arj nie dostarczyl DATA/ - probuje unar (fallback)"
      ( cd "$wol" && unar -force-overwrite -o "$rozp" DATA.A01 ) || true
    fi
  fi
  # wariant awaryjny: W*.DAT wypakowane luzem zamiast w katalogu DATA/
  if ! plik_ci "$rozp/DATA" W001.DAT >/dev/null; then
    src="$(plik_ci "$rozp" W001.DAT || true)"
    if [ -n "$src" ]; then
      local -a wavs=()
      shopt -s nullglob
      wavs=("$rozp"/W[0-9][0-9][0-9].DAT "$rozp"/W[0-9][0-9][0-9].dat
            "$rozp"/I[0-9][0-9][0-9].DAT "$rozp"/I[0-9][0-9][0-9].dat)
      shopt -u nullglob
      mkdir -p "$rozp/DATA"
      if [ "${#wavs[@]}" -gt 0 ]; then
        mv -f "${wavs[@]}" "$rozp/DATA/"
      fi
      for w in SOUND.DAT sound.dat; do
        if [ -f "$rozp/$w" ]; then mv -f "$rozp/$w" "$rozp/DATA/"; fi
      done
      echo "  ulozone: W*.DAT/I*.DAT -> DATA/"
    fi
  fi
  local -a wdaty=()
  shopt -s nullglob
  wdaty=("$rozp"/DATA/W[0-9][0-9][0-9].DAT "$rozp"/DATA/W[0-9][0-9][0-9].dat)
  shopt -u nullglob
  if [ "${#wdaty[@]}" -eq 0 ]; then
    echo "  zawartosc $(wzgledem "$rozp"):" >&2
    ls -la "$rozp" >&2 2>/dev/null || true
    die "Wolumeny ARJ multi-volume (DATA.003..DATA.007) nie dostarczyl DATA/W*.DAT (efekty). Odpalona komenda: $komenda. Zalecane arj z AUR (Arch/Omarchy: yay -S arj - sprawdz wersje arj), ewentualnie unar (Debian/Ubuntu: apt install unar; Arch/Omarchy: pacman -S unar). Dolacz pelny log do zgloszenia."
  fi
  local licznik=0 suma=0
  for f in "${wdaty[@]}"; do
    licznik=$((licznik + 1))
    suma=$((suma + $(wc -c <"$f")))
  done
  echo "  DATA/: $licznik plikow W*.DAT, lacznie $suma B"
  return 0
}

# ---- rozpakowanie instalki dyskietkowej (polanie.zip + ARJ) ----------------
# Kolejnosc: unzip polanie.zip -> pojedyncze ARJ (DATA.001/002) -> multi-volume
# (DATA.003..007, pelny zestaw + DATA/) -> porzadkowanie -> programy DOS
# (DATA.000, DATA.009->POLANIE.EXE) -> pomijanie DATA.008.
# Idempotentnie: pelna struktura wynikowa (GRAF.DAT + DATA/W001.DAT) = koniec.
rozpakuj_instalke() {
  local CEL="$EPHE/dyskietki"
  local ROZP="$CEL/rozpakowane"
  local WOL="$CEL/wolumeny"
  local zip arch src n
  mkdir -p "$ROZP"

  if plik_ci "$ROZP" GRAF.DAT >/dev/null && plik_ci "$ROZP/DATA" W001.DAT >/dev/null; then
    echo "  juz rozpakowane i ulozone: $(wzgledem "$ROZP") - pomijam"
    return 0
  fi

  # unzip polanie.zip (wolumeny DATA.0NN)
  if [ -f "$CEL/DATA.001" ] || [ -f "$CEL/data.001" ]; then
    echo "  wolumeny DATA.0NN juz wypakowane z polanie.zip - pomijam unzip"
  else
    zip="$(find "$CLONE_DIR" -not -path '*/.git/*' -iname 'polanie.zip' 2>/dev/null | head -n1 || true)"
    if [ -z "$zip" ]; then
      die "W klonie mirrora nie ma instalki polanie.zip (szukano w: $(wzgledem "$CLONE_DIR"))."
    fi
    echo "  instalka: $zip"
    echo "  rozpakowuje polanie.zip (unzip) -> $(wzgledem "$CEL")"
    mkdir -p "$CEL"
    unzip -o -q "$zip" -d "$CEL" \
      || die "unzip nie powiodl sie na polanie.zip ($zip)."
  fi

  # pojedyncze archiwa ARJ: DATA.001, DATA.002 (dekompresowane przy kazdym
  # niepelnym przebiegu fazy 2 - tanie, nadpisuje te same pliki)
  for n in 001 002; do
    arch="$(plik_ci "$CEL" "DATA.$n" || true)"
    if [ -z "$arch" ]; then
      echo "  brak DATA.$n - pomijam"
      continue
    fi
    echo "  dekompresuje DATA.$n (ARJ) -> $(wzgledem "$ROZP")"
    if ! rozpakuj_archiwum "$arch" "$ROZP"; then
      echo "  UWAGA: nie udalo sie rozpakowac DATA.$n (jesli to tez wolumen serii ARJ - dolacz log do zgloszenia)"
    fi
  done

  # multi-volume: DATA.003..DATA.007 -> pelny zestaw plikow + katalog DATA/
  dekompresuj_multi_volume "$CEL" "$ROZP" "$WOL"

  normalizuj_rozpakowane "$ROZP"

  # programy DOS: DATA.000 zostaje DATA.000, DATA.009 -> POLANIE.EXE
  # (po multi-volume - wolumeny tez moga je wypakowac, a wzorzec instalacji
  # przewiduje wersje z DATA.009)
  src="$(plik_ci "$CEL" DATA.000 || true)"
  if [ -n "$src" ]; then
    cp -a "$src" "$ROZP/DATA.000"
    echo "  DATA.000 (program DOS) -> rozpakowane/DATA.000"
  fi
  src="$(plik_ci "$CEL" DATA.009 || true)"
  if [ -n "$src" ]; then
    cp -a "$src" "$ROZP/POLANIE.EXE"
    echo "  DATA.009 (POLANIE.EXE) -> rozpakowane/POLANIE.EXE"
  fi

  # DATA.008 - zaszyfrowany, wlasny format instalatora (nie ARJ)
  if plik_ci "$CEL" DATA.008 >/dev/null; then
    echo "  pomijam DATA.008 (wlasny format instalatora - zaszyfrowany, nie ARJ)"
  fi
  return 0
}

# ---- kontrola pelnego wzorca instalacji ------------------------------------
# Weryfikuje to, czego wymaga ekstraktor i check_assets. Brak = die z lista
# (ekstraktor pracuje na plikach PO instalacji - niedoboru nie "naprawiamy"
# w ekstraktorze, tylko raportujemy jako feedback).
kontroluj_strukture() { # kontroluj_strukture <katalog>
  local dir="$1" brak=""
  plik_ci "$dir" GRAF.DAT >/dev/null || brak+="  - GRAF.DAT (grafika)
"
  plik_ci "$dir" PAL.DAT >/dev/null || brak+="  - PAL.DAT (palety)
"
  plik_ci "$dir" FONT.DAT >/dev/null || brak+="  - FONT.DAT (font)
"
  plik_ci "$dir/DATA" W001.DAT >/dev/null || brak+="  - DATA/W001.DAT (katalog efektow DATA/ - pochodzi z DATA.003)
"
  plik_ci "$dir/GRAF" GRAF.001 >/dev/null || brak+="  - GRAF/GRAF.001 (muzyka S3M)
"
  plik_ci "$dir/GRAF" LEVEL.DAT >/dev/null || brak+="  - GRAF/LEVEL.DAT (teksty)
"
  if [ -n "$brak" ]; then
    printf 'BLAD: struktura wynikowa niekompletna w %s.\nDekompresja nie dostarczyla:\n%s' \
      "$(wzgledem "$dir")" "$brak" >&2
    die "Dolacz pelny log skryptu (unzip / arj / 7z) do zgloszenia jako feedback - ekstraktor nie jest zmieniany."
  fi
}

# ---- program ---------------------------------------------------------------

mkdir -p "$EPHE/install"

log "1/6 Pobieranie danych gry (git clone mirrora polanie.prv.pl)"
klonuj_mirror

log "2/6 Instalka dyskietkowa: polanie.zip + wolumeny ARJ DATA.0NN"
rozpakuj_instalke

log "3/6 Kopiowanie danych do ephemeral/dysk/GRY/POLANIE (wg wzorca instalacji)"
ROZP="$EPHE/dyskietki/rozpakowane"
DYSK="$EPHE/dysk/GRY/POLANIE"
if plik_ci "$DYSK" GRAF.DAT >/dev/null && plik_ci "$DYSK/DATA" W001.DAT >/dev/null \
  && plik_ci "$DYSK/GRAF" GRAF.001 >/dev/null && plik_ci "$DYSK/GRAF" LEVEL.DAT >/dev/null; then
  echo "  juz kompletny - pomijam (kasuj ephemeral/dysk, aby odswiezyc)"
else
  mkdir -p "$DYSK"
  shopt -s nullglob
  for el in "$ROZP"/*; do
    nazwa="$(basename "$el")"
    # aplikacja i konfiguracja instalatora DOS - gra i ekstraktor ich nie czytaja
    if [ "$nazwa" = "SETUP.EXE" ] || [ "$nazwa" = "setup.exe" ] \
      || [ "$nazwa" = "SETUP.INI" ] || [ "$nazwa" = "setup.ini" ]; then
      continue
    fi
    if [ -d "$el" ]; then
      # katalogi kopiowane caloscia (uzupelnia polstany, np. GRAF/ albo DATA/)
      mkdir -p "$DYSK/$nazwa"
      cp -a "$el/." "$DYSK/$nazwa/"
      echo "  katalog: $nazwa (uzupelniony)"
    elif [ ! -e "$DYSK/$nazwa" ]; then
      cp -a "$el" "$DYSK/$nazwa"
      echo "  kopiuje: $nazwa"
    fi
  done
  shopt -u nullglob
fi
kontroluj_strukture "$DYSK"

# ---- opcjonalna wersja CD: rozpakowanie + dokladanie plikow opcjonalnych ---
# polanie_cd.zip (pelna wersja CD) dostarcza tego, czego dyskietki nie maja:
# pelnego zestawu efektow DATA/I*.dat, banku instrumentow DATA/SOUND.DAT oraz
# dodatkowych bankow obrazow (PIC.DAT, SETUP.DAT, INSTALL*). Dozucane do
# ephemeral/dysk/GRY/POLANIE idempotentnie (tylko brakujace) PRZED ekstrakcja,
# dzieki czemu ekstrakt powstaje od razu pelny. Zawartosc CD laduje tez w
# ephemeral/cd/polanie_cd/ (nie scalane; gra sama korzysta z cd/, gdy tor
# efektow czegos szuka).
if [ "${1:-}" = "--cd" ]; then
  log "CD 1/2 Wersja CD (polanie_cd.zip z klona mirrora, opcjonalna)"
  CD_ZIP="$(find "$CLONE_DIR" -not -path '*/.git/*' -iname 'polanie_cd.zip' 2>/dev/null | head -n1)"
  [ -n "$CD_ZIP" ] || die "W klonie mirrora nie ma polanie_cd.zip (przeszukano: $(wzgledem "$CLONE_DIR"))."
  echo "  archiwum CD: $CD_ZIP"
  log "CD 2/2 Rozpakowanie -> ephemeral/cd/ + dokladanie plikow opcjonalnych"
  rm -rf "$EPHE/cd/unzipped"
  mkdir -p "$EPHE/cd/unzipped"
  unzip -q "$CD_ZIP" -d "$EPHE/cd/unzipped"
  szukaj_graf "wersja CD" "$EPHE/cd/unzipped" \
    || die "Nie znaleziono GRAF.DAT w polanie_cd.zip (przeszukano: $(wzgledem "$EPHE/cd/unzipped"))."
  CD_SRC="$ZNALEZIONY_KATALOG"
  mkdir -p "$EPHE/cd/polanie_cd"
  cp -a "$CD_SRC/." "$EPHE/cd/polanie_cd/"
  echo "  wersja CD: ephemeral/cd/polanie_cd/ (do toru efektow/muzyki CD)"

  # pliki opcjonalne z CD (DATA/ moze byc podkatalogiem albo pliki leza luzem)
  CD_DATA="$CD_SRC/DATA"
  [ -d "$CD_DATA" ] || CD_DATA="$CD_SRC"
  mkdir -p "$DYSK/DATA"
  shopt -s nullglob
  for plik in "$CD_DATA"/I[0-9][0-9][0-9].DAT "$CD_DATA"/I[0-9][0-9][0-9].dat \
              "$CD_DATA"/SOUND.DAT "$CD_DATA"/sound.dat; do
    nazwa="$(basename "$plik")"
    if ! plik_ci "$DYSK/DATA" "$nazwa" >/dev/null; then
      cp -a "$plik" "$DYSK/DATA/$nazwa"
      echo "  z CD dokladam: DATA/$nazwa"
    fi
  done
  shopt -u nullglob
  for p in POST.DAT SWIAT.DAT PIC.DAT SETUP.DAT SETUP.PAL INSTALL.PAL \
           INSTALL1.DAT INSTALL2.DAT FONTS1.13H LEVEL2.INI; do
    src="$(plik_ci "$CD_SRC" "$p" || true)"
    if [ -n "$src" ] && ! plik_ci "$DYSK" "$p" >/dev/null; then
      cp -a "$src" "$DYSK/$p"
      echo "  z CD dokladam: $p"
    fi
  done
fi

log "4/6 Budowa ekstraktora (src/tools/polanie_extract.cpp)"
g++ -std=c++20 -O2 -o "$EPHE/install/polanie-extract" "$SRC/tools/polanie_extract.cpp"
echo "  gotowe: ephemeral/install/polanie-extract"

log "5/6 Ekstrakcja zasobow -> ephemeral/extracted"
"$EPHE/install/polanie-extract" "$DYSK" "$EPHE/extracted"

log "6/6 Weryfikacja danych (src/tools/check_assets.cpp)"
g++ -std=c++20 -O2 -o "$EPHE/check_assets" "$SRC/tools/check_assets.cpp"
POLANIE_DATA="$DYSK" POLANIE_EXTRACTED="$EPHE/extracted" \
  "$EPHE/check_assets"

log "Instalacja zakonczona. Kolejny krok: bash scripts/build.sh"