#!/usr/bin/env bash
# ============================================================================
# POLANIE (game-linux) — build i uruchomienie wersji WebGL/WASM w Chromium.
#
# UWAGA — SKRYPT ODPALA UŻYTKOWNIK (nie agent): zgodnie z zasadami projektu
# agenci tylko piszą kod i skrypty; kompilację, testy i grę odpala człowiek.
# Pierwszy przebieg może wymagać iteracji — prześlij pełny log do agenta
# (szczególnie Faza 2: build libopenmpt pod Emscripten), a on poprawi skrypt.
#
# UŻYCIE:
#   game-linux/scripts/webgl_build.sh               # tryb pełny (muzyka S3M)
#   game-linux/scripts/webgl_build.sh --bez-muzyki  # stub audio (pierwszy przebieg)
#
# Wynik: ephemeral/web-build/index.html (+ index.js / index.wasm / index.data)
# uruchamiane pod http://127.0.0.1:8080/ (serwer startuje Faza 6).
#
# Fazy:
#   0. Zależności: emcc (emsdk lokalnie do ephemeral/emsdk, bez sudo),
#      python3, chromium, git, make, curl.
#   1. Lista źródeł z game-linux/Makefile (GAME_OBJS/PORT_OBJS/LAYER_OBJS)
#      + generacja stubu webowego (brak port_gpu / sfz_engine).
#   2. Muzyka: tryb pełny — libopenmpt pod Emscripten (klon + build + .a);
#      tryb --bez-muzyki — bez libopenmpt (wbudowany stub w port_audio.cpp).
#   3. Kompilacja obiektów em++ (src/ + game/ + port/ + stub).
#   4. Dane gry -> ephemeral/web-data/ (rdzeń ~9,6 MB; SWIAT.DAT i DATA.003
#      pomiń — port ich nie czyta).
#   5. Link finalny: index.html + .js + .wasm + .data (--preload-file).
#   6. Uruchomienie: python3 -m http.server :8080 (w tle) + Chromium.
#
# Flagi Emscripten i dlaczego:
#   -sUSE_SDL=3             — SDL3 z portów Emscripten (pierwsze uruchomienie
#                             pobiera port do cache emsdk): wideo spina się do
#                             canvas, audio do WebAudio.
#   -sASYNCIFY=1            — SDL_Delay w POL_WaitMs/POL_Present przechodzi w
#                             emscripten_sleep ("portalny sen", port/port.h:
#                             76-91): pętle gry czekające na tik zegara 18,2 Hz
#                             (src/battle.cpp:616-617) oddają kontrolę
#                             przeglądarce zamiast zamrażać stronę.
#   -sALLOW_MEMORY_GROWTH=1 — bufory gry i cache dźwięków rosną dynamicznie,
#                             do tego preload ~9,6 MB danych; heap rośnie bez
#                             sztywnego limitu.
#   -O2                     — rozsądny środek: ASYNCIFY pod -O0 bywa
#                             niepoprawny i ogromny, -Oz kompresuje, ale wolniej
#                             chodzi; -O2 jest punktem startowym do iteracji.
#
# Wyłączone w buildzie webowym (stub generowany w Faza 1):
#   port_gpu.cpp                — SDL_GPU nie ma backendu webowego
#                                 (Vulkan/D3D12/Metal; patrz raport, sekcja 10);
#   sfz_engine.cpp/midi_parser.cpp — sfizz (VSCO) pod WASM nie budujemy;
#                                 mikser woła tylko POL_SfzEngine* — no-op stub
#                                 wystarcza (tor S3M jest domyślny).
#
# libopenmpt (Faza 2, tryb pełny) — DWA kroki w tym samym Makefile:
#   a) make CONFIG=emscripten EMSCRIPTEN_TARGET=wasm — oficjalny target
#      (walidacja toolchainu); jego produkty (libopenmpt.js/.wasm) NIE służą
#      do linku: config-emscripten.mk ustawia SHARED_LIB=1 / STATIC_LIB=0.
#   b) make CONFIG=emscripten STATIC_LIB=1 SHARED_LIB=0 — GNU make: zmienne
#      z linii poleceń nadpisują konfigurację => powstaje bin/<flavour>/
#      libopenmpt.a (reguła libopenmpt.a w Makefile), którą linkujemy z grą.
#      libopenmpt używa wyjątków — finalny link wymaga
#      -sDISABLE_EXCEPTION_CATCHING=0 (konfig uruchomieniowy emscripten to ma
#      w LDFLAGS; nasz finalny link dostawia jawnie).
#      Alternatywa (przepis maintainera, forum.openmpt.org topic 6998):
#      em++ -c common/*.cpp sounddsp/*.cpp soundlib/*.cpp soundlib/plugins/
#      *.cpp soundlib/plugins/dmo/*.cpp libopenmpt/*.cpp -std=c++20 -Oz
#      -DLIBOPENMPT_BUILD -I. -Icommon -Isrc + emar rcs libopenmpt.a *.o.
# ============================================================================
set -euo pipefail

# ---------------------------------------------------------------- konfiguracja
ROOT="$(cd "$(dirname "$(readlink -f "$0")")/../.." && pwd)"
GL="$ROOT/game-linux"
BUILD="$ROOT/ephemeral/web-build"
OBJDIR="$BUILD/obj"
DATA="$ROOT/ephemeral/web-data"
SRCDANE="$ROOT/dysk/GRY/POLANIE"
EMSDK="$ROOT/ephemeral/emsdk"
OMPT="$ROOT/ephemeral/openmpt"
OMPT_A="$ROOT/ephemeral/openmpt-lib/libopenmpt.a"
HTTP_PORT=8080
HTTP_URL="http://127.0.0.1:$HTTP_PORT/"

TRYB_MUZYKI="pelny"
for arg in "$@"; do
  case "$arg" in
    --bez-muzyki) TRYB_MUZYKI="stub" ;;
    -h|--help)
      grep -E '^#( |$)' "$0" | head -70 | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *)
      echo "BLAD: nieznany argument '$arg'" >&2
      echo "Użycie: webgl_build.sh [--bez-muzyki]" >&2
      exit 1
      ;;
  esac
done

log()  { printf '\n===[ FAZA %s: %s ]===\n' "$1" "$2"; }
info() { printf '  • %s\n' "$*"; }
warn() { printf '  ! OSTRZEŻENIE: %s\n' "$*" >&2; }
die() { # die <faza> <komunikat> <podpowiedź>
  printf '\nBLAD (Faza %s): %s\n' "$1" "$2" >&2
  if [ -n "${3:-}" ]; then printf 'Podpowiedź: %s\n' "$3" >&2; fi
  exit 1
}

# ------------------------------------------------------------------ Faza 0 ---
log 0 "zależności"

for tool in git make curl; do
  command -v "$tool" >/dev/null 2>&1 \
    || die 0 "brak narzędzia: $tool" "sudo pacman -S $tool"
done
command -v python3 >/dev/null 2>&1 \
  || die 0 "brak python3 (serwer plików w Faza 6)" "sudo pacman -S python"

CHROME="$(command -v chromium || command -v chromium-browser || command -v google-chrome-stable || true)"
if [ -z "$CHROME" ]; then
  die 0 "brak chromium / chromium-browser / google-chrome-stable" \
    "sudo pacman -S chromium  (albo AUR: google-chrome-stable)"
fi
info "python3: $(command -v python3)"
info "chromium: $CHROME"

# emcc — emsdk lokalnie w ephemeral/emsdk (bez sudo)
if ! command -v emcc >/dev/null 2>&1; then
  if [ ! -d "$EMSDK" ]; then
    info "emsdk brak w systemie — klonuję do $EMSDK (bez sudo; chwilę zajmie)"
    git clone --depth 1 https://github.com/emscripten-core/emsdk "$EMSDK"
  fi
  info "emsdk install/activate latest (pobiera toolchain — może potrwać kilka minut)"
  ( cd "$EMSDK" && ./emsdk install latest && ./emsdk activate latest ) \
    || die 0 "instalacja emsdk nie powiodła się" \
       "Uruchom ponownie skrypt; jeśli padnie sieć — ephemeral/emsdk jest już sklonowany, wystarczy './emsdk install latest' w tym katalogu."
  # source w bieżącej powłoce (emsdk_env.sh ustawia PATH emcc/em++/node)
  # shellcheck disable=SC1091
  . "$EMSDK/emsdk_env.sh"
  command -v emcc >/dev/null 2>&1 \
    || die 0 "emcc nadal niedostępny po instalacji emsdk" \
       "Sprawdź: . ephemeral/emsdk/emsdk_env.sh && command -v emcc"
fi
info "emcc: $(command -v emcc)"

# ------------------------------------------------------------------ Faza 1 ---
log 1 "lista źródeł z Makefile + generacja stubu webowego"

# Wylicza wartość zmiennej z Makefile (łączy linie kontynuowane "\").
make_var() { # make_var <plik> <nazwa-zmiennej>
  awk -v name="$2" '
    $0 ~ "^" name "[ \t]*:?[ \t]*=[ \t]*" {
      invar = 1
      line = $0
      sub("^" name "[ \t]*:?[ \t]*=[ \t]*", "", line)
      cont = (line ~ /\\[ \t]*$/)
      sub(/[ \t]*\\[ \t]*$/, "", line)
      buf = line
      if (!cont) { print buf; exit }
      next
    }
    invar {
      line = $0
      cont = (line ~ /\\[ \t]*$/)
      sub(/[ \t]*\\[ \t]*$/, "", line)
      buf = buf " " line
      if (!cont) { print buf; exit }
    }
  ' "$1"
}

GAME_OBJS="$(make_var "$GL/Makefile" GAME_OBJS)"
PORT_OBJS="$(make_var "$GL/Makefile" PORT_OBJS)"
LAYER_OBJS="$(make_var "$GL/Makefile" LAYER_OBJS)"
if [ -z "$GAME_OBJS" ] || [ -z "$PORT_OBJS" ] || [ -z "$LAYER_OBJS" ]; then
  die 1 "nie wyliczyłem źródeł z game-linux/Makefile" \
    "Zmienne GAME_OBJS/PORT_OBJS/LAYER_OBJS zmieniły nazwę/kształt — dostosuj make_var()/wykluczenia w tym skrypcie."
fi
info "GAME_OBJS:  $GAME_OBJS"
info "PORT_OBJS:  $PORT_OBJS"
info "LAYER_OBJS: $LAYER_OBJS"

# Wykluczenia webowe (patrz nagłówek): port_gpu (SDL_GPU bez backendu webowego),
# sfz_engine + midi_parser (sfizz nie budujemy pod WASM), spv_data (SPIR-V dla
# port_gpu). Reszta warstwy przenosi się bez zmian.
LAYER_WEB=""
for o in $LAYER_OBJS; do
  case "$o" in
    port_gpu|sfz_engine|midi_parser|spv_data) ;; # — pominięte na web
    *) LAYER_WEB="$LAYER_WEB $o" ;;
  esac
done
info "warstwa webowa: $LAYER_WEB"

mkdir -p "$BUILD"
cat > "$BUILD/sfz_gpu_stub.cpp" <<'EOF'
// GENEROWANE przez game-linux/scripts/webgl_build.sh — nie edytować (skrypt nadpisuje).
// Stub webowy dla buildu Emscripten:
//  - POL_SfzEngine*: brak sfizz pod WASM (tor MIDI+VSCO wyłączony na webie;
//    domyślny tor muzyki to S3M). Prepare() = -1 => play_midi_track() porzuca
//    tor MIDI i (przy --audioType=auto) wraca do S3M.
//  - POL_GPU_*: SDL_GPU nie ma backendu webowego; POL_GPU_Want() = 0 utrzymuje
//    port_sdl na programowym rendererze (identycznie jak natywnie bez
//    POL_VIDEO=gpu).
#include <cstddef>

struct SDL_Window;

extern "C" {
int POL_SfzEngineInit(float) { return 1; }
void POL_SfzEngineShutdown(void) {}
int POL_SfzEnginePrepare(const unsigned char *, size_t, const char *,
                         const char *) { return -1; }
void POL_SfzEngineCommit(int) {}
void POL_SfzEngineAbort(void) {}
void POL_SfzEngineStop(void) {}
int POL_SfzEnginePlaying(void) { return 0; }
const char *POL_SfzEngineCurrentName(void) { return ""; }
size_t POL_SfzEngineRender(float *, size_t) { return 0; }
int POL_SfzLoadNormalizedFile(void *, const char *) { return -1; }
void POL_SfzEngineSetGain(float) {}

int POL_GPU_Want(void) { return 0; }
int POL_GPU_Crt(void) { return 0; }
int POL_GPU_Init(struct SDL_Window *) { return -1; }
void POL_GPU_Quit(void) {}
void POL_GPU_SetPalette(const unsigned char *) {}
void POL_GPU_Present(const unsigned char *) {}
int POL_GPU_GetScale(void) { return 0; }
}
EOF
info "stub: $BUILD/sfz_gpu_stub.cpp"

# ------------------------------------------------------------------ Faza 2 ---
log 2 "muzyka (tryb: $TRYB_MUZYKI)"

OPENMPT_LIB=""
OPENMPT_INC=""
if [ "$TRYB_MUZYKI" = "pelny" ]; then
  if [ ! -d "$OMPT" ]; then
    info "klonuję openmpt (shallow) -> $OMPT"
    git clone --depth 1 https://github.com/OpenMPT/openmpt "$OMPT"
  fi
  # (a) oficjalny target — walidacja toolchainu; produkty .js/.wasm NIE służą
  #     do linku (patrz nagłówek skryptu). Porażka nie jest śmiertelna: do
  #     linku budujemy (b).
  info "make CONFIG=emscripten EMSCRIPTEN_TARGET=wasm (oficjalny target; walidacja)"
  if ( cd "$OMPT" && make -j"$(nproc)" CONFIG=emscripten EMSCRIPTEN_TARGET=wasm ); then
    info "oficjalny target OK (produkty libopenmpt.js/.wasm — nieużywane do linku)"
  else
    warn "oficjalny build nieudany (log wyżej) — przechodzę do statycznego .a (krok b)"
  fi
  # (b) statyczne archiwum do wspólnego WASM z grą (nadpisanie konfiguracji
  #     z linii poleceń GNU make).
  info "make CONFIG=emscripten STATIC_LIB=1 SHARED_LIB=0 (libopenmpt.a do linku)"
  if ! ( cd "$OMPT" && make -j"$(nproc)" CONFIG=emscripten STATIC_LIB=1 SHARED_LIB=0 ); then
    die 2 "budowa libopenmpt.a (STATIC_LIB=1) nie powiodła się" \
      "Prześlij pełny log do agenta (pierwszy przebieg bywa iteracyjny). Na teraz uruchom: ./game-linux/scripts/webgl_build.sh --bez-muzyki (efekty WAV działają, muzyka S3M off)."
  fi
  AFILE="$(find "$OMPT/bin" -name 'libopenmpt.a' 2>/dev/null | head -1 || true)"
  [ -n "$AFILE" ] || die 2 "nie znalazłem libopenmpt.a w $OMPT/bin" \
    "Prześlij log do agenta; awaryjnie --bez-muzyki."
  mkdir -p "$(dirname "$OMPT_A")"
  cp -f "$AFILE" "$OMPT_A"
  info "libopenmpt.a: $OMPT_A"
  # Nagłówek C API <libopenmpt/libopenmpt.h> leży w <klon>/libopenmpt/ —
  # więc -I na KORZEŃ klonu wystarcza (potrzebne już przy kompilacji
  # port_audio.cpp: __has_include włącza tor muzyki).
  OPENMPT_INC="-I$OMPT"
  OPENMPT_LIB="$OMPT_A"
else
  warn "tryb --bez-muzyki: bez libopenmpt — muzyka S3M cicha (log „libopenmpt brak”),"
  warn "efekty WAV i SOUND.DAT działają; na drugi przebieg odpal bez flagi."
fi

# ------------------------------------------------------------------ Faza 3 ---
log 3 "kompilacja obiektów em++"

CXXFLAGS_WEB="-std=c++17 -fpermissive -funsigned-char -w -O2"
# jak w game-linux/Makefile: -include polshim.h przed każdym plikiem
SHIM="-include $GL/include/polshim.h"
INC="-I$GL/include -I$GL/port -I$ROOT/game"
EMFLAGS="-sUSE_SDL=3 -sASYNCIFY=1 -sALLOW_MEMORY_GROWTH=1"

rm -rf "$OBJDIR"
mkdir -p "$OBJDIR"

compile_obj() { # compile_obj <plik .cpp> <nazwa obiektu>
  info "em++ $(basename "$1")"
  # shellcheck disable=SC2086
  em++ $EMFLAGS $CXXFLAGS_WEB $INC $SHIM $OPENMPT_INC -c "$1" -o "$OBJDIR/$2.o"
}

for o in $GAME_OBJS; do compile_obj "$ROOT/game/$o.cpp" "$o"; done
for o in $PORT_OBJS; do compile_obj "$GL/src/$o.cpp" "$o"; done
for o in $LAYER_WEB; do compile_obj "$GL/port/$o.cpp" "$o"; done
compile_obj "$BUILD/sfz_gpu_stub.cpp" "sfz_gpu_stub"

# ------------------------------------------------------------------ Faza 4 ---
log 4 "dane gry -> ephemeral/web-data"

[ -f "$SRCDANE/GRAF.DAT" ] || die 4 "brak danych gry w $SRCDANE (GRAF.DAT)" \
  "Sprawdź instalację danych (dysk/GRY/POLANIE); struktura musi zgadzać się z port_fopen.cpp (sonda GRAF.DAT)."

mkdir -p "$DATA/GRAF" "$DATA/DATA"

# root katalogu bazowego (rdzeń; POST.DAT/SETUP.DAT port nie czyta —
# w pakiecie wg listy rdzenia, można pominąć)
for f in GRAF.DAT PAL.DAT FONT.DAT PIC.DAT POST.DAT SETUP.DAT; do
  if [ -f "$SRCDANE/$f" ]; then
    cp -L -f "$SRCDANE/$f" "$DATA/$f"
    info "root/$f"
  else
    warn "brak $SRCDANE/$f (pomijam)"
  fi
done

# kampania: port pyta o "levels/level.dat" -> fallback <baza>/GRAF/
# (port_fopen.cpp:316-318); w tej kopii pliki leżą w root — kopiuję i tam, i do
# GRAF/, żeby ścieżka była pokryta niezależnie od toru
for f in LEVEL.DAT LEVEL.INI; do
  if [ -f "$SRCDANE/$f" ]; then
    cp -L -f "$SRCDANE/$f" "$DATA/$f"
    cp -L -f "$SRCDANE/$f" "$DATA/GRAF/$f"
    info "LEVEL/$f (root + GRAF/)"
  else
    warn "brak $SRCDANE/$f (pomijam)"
  fi
done

# muzyka S3M: music_path_for() -> <baza>/GRAF/GRAF.0xx (port_audio.cpp:252-267)
for i in $(seq 1 19); do
  n="$(printf '%03d' "$i")"
  if [ -f "$SRCDANE/GRAF/GRAF.$n" ]; then
    cp -L -f "$SRCDANE/GRAF/GRAF.$n" "$DATA/GRAF/"
  else
    warn "brak GRAF/GRAF.$n (utwór $n będzie cichy)"
  fi
done

# efekty + mowa + SOUND.DAT (POL_ResolveDataFile: "data\\W0nn.dat" -> DATA/)
for f in "$SRCDANE"/DATA/W0*.DAT "$SRCDANE"/DATA/I0*.DAT; do
  [ -f "$f" ] || continue
  cp -L -f "$f" "$DATA/DATA/"
done
if [ -f "$SRCDANE/DATA/SOUND.DAT" ]; then
  cp -L -f "$SRCDANE/DATA/SOUND.DAT" "$DATA/DATA/"
  info "DATA/SOUND.DAT"
else
  warn "brak DATA/SOUND.DAT (głosy jednostek będą ciche)"
fi

# SWIAT.DAT i DATA.003 celowo pominięte: port ich nie otwiera (−18,3 MB).
info "razem: $(du -sh "$DATA" | cut -f1) w $DATA"

# ------------------------------------------------------------------ Faza 5 ---
log 5 "link finalny -> ephemeral/web-build/index.html"

# Ścieżka /dysk/GRY/POLANIE trafia w kandydata "./dysk/GRY/POLANIE" z
# get_base_dir() (port_fopen.cpp:165-172) — zero glue JS: katalog bazowy
# wykrywa się sam sonda GRAF.DAT, save.00x lądują w MEMFS "/" (CWD).
PRELOAD="--preload-file $DATA@/dysk/GRY/POLANIE"

EXC=""
if [ "$TRYB_MUZYKI" = "pelny" ]; then
  # libopenmpt używa wyjątków — finalny link musi mieć włączone łapanie
  # (wymóg konfiguracji emscripten openmpt: LDFLAGS w config-emscripten.mk).
  EXC="-sDISABLE_EXCEPTION_CATCHING=0"
fi

# shellcheck disable=SC2086
em++ $EMFLAGS $EXC "$OBJDIR"/*.o $OPENMPT_LIB $PRELOAD -O2 -o "$BUILD/index.html"
info "wyjście: $BUILD/index.html (+ index.js / index.wasm / index.data)"
info "rozmiary: $(ls -la "$BUILD" | awk 'NR>1 && $NF ~ /index\./ {printf "%s=%s ", $NF, $5}')"

# ------------------------------------------------------------------ Faza 6 ---
log 6 "uruchomienie: serwer plików + Chromium"

# Serwer działa W TLE (nohup) — żyje po zakończeniu skryptu; powtórny przebieg
# go wykryje i nie zacznie drugiego na tym samym porcie.
SRV_PID=""
if curl -sf -o /dev/null "$HTTP_URL" 2>/dev/null; then
  warn "port $HTTP_PORT już odpowiada — zakładam, że serwer z poprzedniego przebiegu działa"
else
  nohup python3 -m http.server "$HTTP_PORT" --directory "$BUILD" \
    > "$BUILD/.http.log" 2>&1 &
  SRV_PID=$!
  echo "$SRV_PID" > "$BUILD/.http.pid"
  # czekaj aż port odpowie
  curl -sf --retry 30 --retry-delay 1 --retry-connrefused -o /dev/null "$HTTP_URL" \
    || die 6 "serwer $HTTP_URL nie odpowiedział" \
       "Sprawdź $BUILD/.http.log; zatrzymaj zablokowany proces: kill \$(cat $BUILD/.http.pid)"
  info "serwer: python3 -m http.server $HTTP_PORT --directory $BUILD (PID $SRV_PID, log: $BUILD/.http.log)"
fi

info "ZATRZYMANIE SERWERA po skończonej grze: kill \$(cat $BUILD/.http.pid)"
info "  (albo: pkill -f 'http.server $HTTP_PORT')"

# Chromium (w tle; nie blokuje kończenia skryptu)
nohup "$CHROME" "$HTTP_URL" >/dev/null 2>&1 &

info "otwarto $HTTP_URL w $CHROME"
info "PAMIĘTAJ: PIERWSZY KLIK/kliwisz w przeglądarce odblokowuje audio"
info " (polityka autoplay WebAudio) — ekran startowy „kliknij, aby zacząć”."
echo
echo "Gotowe — gra pod: $HTTP_URL"