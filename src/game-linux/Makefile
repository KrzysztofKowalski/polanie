# PORT: build portu Polanie (game-linux) - Linux x86-64 + SDL2
# Uzycie:
#   make            - buduje pol2
#   make run        - buduje i uruchamia z danymi gry
# Skala: ./pol2 [n]  (1 px gry = n x n fizycznych px; bez n = auto/fullscreen)

# PORT: toolchain clang + C++23 + warningi jako bledy (-Werror; wymaganie usera 2026-09-06 - zero warningow).
# PORT: CXX = clang++ (twardo, nie ?=) - GNU make ma wbudowana zmienna CXX=g++
# (origin "default"), ktora wygrywa z ?= (to nadpisuje tylko niezdefiniowane).
# Build pod GCC nadal mozliwy, np.:
#   make CXX=g++ CXXFLAGS='-std=c++17 -O1 -g -w -fpermissive -funsigned-char'
CXX      = clang++
# PORT: Watcom mial plain char bez znaku - znaczniki placeN 190-227 (naprawa
# 220-227, drzewa 200, palisada/most >225) musza byc > 127, inaczej logika
# naprawy/budowy jest martwa (220 czytane jako -36) => -funsigned-char MUSI zostac
CXXFLAGS ?= -std=c++23 -O1 -g -Wall -Wextra -Werror -funsigned-char
SDL3FLAGS := $(shell pkg-config sdl3 --cflags 2>/dev/null || echo -I/usr/include/SDL3)
SDLLIBS  := $(shell pkg-config sdl3 --libs 2>/dev/null || echo -lSDL3)
# PORT: muzyka S3M (port_audio.cpp) - libopenmpt; gdy brak w systemie,
# warstwa muzyki zostaje za interfejsem (stub z logiem w port_audio.cpp)
OPENMPTFLAGS := $(shell pkg-config libopenmpt --cflags 2>/dev/null)
OPENMPTLIBS := $(shell pkg-config libopenmpt --libs 2>/dev/null || echo -lopenmpt)
# PORT: tor MIDI+sfizz (sfz_engine.cpp) - sfizz-lib (zainstalowany w systemie).
# UWAGA: plik .pc sfizza wypuszcza "-llibsfizz" (zly format -l); linkujemy
# poprawnie jako -lsfizz (libsfizz.so).
SFIZZLIBS := -lsfizz

GAME := ../game
PORTDIR := port
SRCDIR := src
INCDIR := include
BUILD := build
BIN := pol2

# PORT: -include polshim.h -> wspolny shim przed kazdym plikiem
# PORT: -MMD -MP -> pliki .d, zeby zmiana polshim.h przebudowywala wszystko
# (bez tego obiekty kompilowane przed dodaniem shimow woluly glibc fopen!)
CPPFLAGS := -I$(INCDIR) -I$(PORTDIR) -I$(GAME) $(SDL3FLAGS) $(OPENMPTFLAGS) \
            -include $(INCDIR)/polshim.h -MMD -MP

# pliki gry kompilowane bez zmian (z katalogu game/)
# PORT: battle, graphics, mover1 i mapa przeniesione do src/ (kopie 1:1 +
# oznaczone "// PORT:")
GAME_OBJS := ladowani opiswava world
# pliki portowane (kopie z katalogu src/, oznaczone "// PORT:")
# PORT: battle dołączony do portowanych - kopia w src/battle.cpp (logika bez
# zmian, tylko logi diagnostyczne "// PORT:" pod POL_MOUSE_DEBUG=1)
# PORT: graphics - kopia w src/graphics.cpp (logika bez zmian, jedno zakrycie
# wypalonego napisu "Koniec" z tla menu koncowego, patrz
# raporty/krowy-zdrowie-i-font-end-menu.md)
# PORT: mover1 - kopia w src/mover1.cpp (logika 1:1 + Fix A/B/C blokady
# naprawy budynku przez kilku robotnikow naraz, patrz
# raporty/naprawa-wielu-robotnikow.md; game/mover1.cpp zostaje lustrem)
# PORT: mapa - kopia w src/mapa.cpp (kopia 1:1 + fix offsetu overlayu poziomu
# trudnosci na mapie podboju 268/313 -> 272/317, patrz
# raporty/overlay-poziomu-mapakampanii.md)
PORT_OBJS := main image13h mouse menegdma cd dpmi playfli battle unit_heal \
             graphics mover1 mapa
# nowa warstwa SDL2
LAYER_OBJS := port_sdl port_shims port_fopen port_audio sound_dat \
              midi_parser midi_map_files sfz_engine mix_simd port_gpu
# PORT: backend GPU (SDL3 GPU/Vulkan, port_gpu.cpp) wymaga SPIR-V - shadery
# GLSL port/present.{vert,frag} kompilowane w czasie budowy (glslangValidator
# albo glslc), wbudowane jako tablice bajtow (xxd -> build/spv_data.cpp).
# Maszyna bez kompilatora shaderow: backend wylaczony przez -DPOL_NO_SPV
# (port_gpu.cpp loguje i zwraca fallback; reszta builda bez zmian). Reguly
# kompilacji .spv i generacji spv_data.cpp sa nizej (po regulach obiektow) -
# cel domyslny "all" musi pozostac pierwsza regula w pliku.
SPIRV_TOOL := $(or $(shell command -v glslangValidator 2>/dev/null),$(shell command -v glslc 2>/dev/null))
ifneq ($(SPIRV_TOOL),)
LAYER_OBJS += spv_data
else
CPPFLAGS += -DPOL_NO_SPV
endif

OBJS := $(addprefix $(BUILD)/,$(GAME_OBJS:%=%.o) $(PORT_OBJS:%=%.o) $(LAYER_OBJS:%=%.o))

all: $(BUILD) $(BIN)

$(BUILD):
	mkdir -p $(BUILD)

# --- zrodla gry (bez zmian) ---
# PORT: battle.o z kopii src/battle.cpp (logika bez zmian + logi diagnostyczne)
$(BUILD)/battle.o: $(SRCDIR)/battle.cpp $(wildcard $(GAME)/*.h)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@
# PORT: graphics.o z kopii src/graphics.cpp (kopia 1:1 + zakrycie wypalonego
# napisu "Koniec" z tla menu koncowego)
$(BUILD)/graphics.o: $(SRCDIR)/graphics.cpp $(wildcard $(GAME)/*.h)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@
$(BUILD)/ladowani.o: $(GAME)/ladowani.cpp $(wildcard $(GAME)/*.h)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@
$(BUILD)/opiswava.o: $(GAME)/opiswava.cpp $(wildcard $(GAME)/*.h)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@
$(BUILD)/world.o: $(GAME)/world.cpp $(wildcard $(GAME)/*.h)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

# --- pliki portowane (kopie w src/, edycje oznaczone "// PORT:") ---
$(BUILD)/main.o: $(SRCDIR)/main.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@
$(BUILD)/image13h.o: $(SRCDIR)/image13h.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@
$(BUILD)/mouse.o: $(SRCDIR)/mouse.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@
$(BUILD)/menegdma.o: $(SRCDIR)/menegdma.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@
$(BUILD)/cd.o: $(SRCDIR)/cd.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@
$(BUILD)/dpmi.o: $(SRCDIR)/dpmi.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@
$(BUILD)/playfli.o: $(SRCDIR)/playfli.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@
# PORT: nowa mechanika - leczenie rannych jednostek mlekiem zamku + krowy
# (naturalne samoleczenie); czyste funkcje balansu testuje test_unit_heal
$(BUILD)/unit_heal.o: $(SRCDIR)/unit_heal.cpp $(PORTDIR)/port.h \
                      $(wildcard $(GAME)/*.h)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@
# PORT: mover1.o z kopii src/mover1.cpp (kopia 1:1 game/mover1.cpp + Fix A/B/C
# blokady naprawy wielu robotnikow naraz, raporty/naprawa-wielu-robotnikow.md)
$(BUILD)/mover1.o: $(SRCDIR)/mover1.cpp $(wildcard $(GAME)/*.h)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@
# PORT: mapa.o z kopii src/mapa.cpp (kopia 1:1 + wspolrzedne overlayu
# poziomu trudnosci 268/313 -> 272/317, patrz raporty/overlay-poziomu-mapakampanii.md)
$(BUILD)/mapa.o: $(SRCDIR)/mapa.cpp $(wildcard $(GAME)/*.h)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

# --- warstwa SDL2 ---
$(BUILD)/port_sdl.o: $(PORTDIR)/port_sdl.cpp $(PORTDIR)/port.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@
$(BUILD)/port_shims.o: $(PORTDIR)/port_shims.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@
$(BUILD)/port_fopen.o: $(PORTDIR)/port_fopen.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@
# PORT: audio (mikser muzyka S3M + WAV) - libopenmpt opcjonalne (__has_include)
$(BUILD)/port_audio.o: $(PORTDIR)/port_audio.cpp $(PORTDIR)/port_audio.h \
                       $(PORTDIR)/port.h $(PORTDIR)/track_map.h \
                       $(PORTDIR)/sound_dat.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

# PORT: parser SOUND.DAT - bez zaleznosci SDL (testy linkuja go osobno)
$(BUILD)/sound_dat.o: $(PORTDIR)/sound_dat.cpp $(PORTDIR)/sound_dat.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

# PORT: tor MIDI+sfizz (parser SMF, mapa .mid, silnik sfizz) i gorace petle
# miksera (AVX2/scalar - dispatch runtime, bez -mavx2 dla calego projektu)
$(BUILD)/midi_parser.o: $(PORTDIR)/midi_parser.cpp $(PORTDIR)/midi_parser.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@
$(BUILD)/midi_map_files.o: $(PORTDIR)/midi_map_files.cpp $(PORTDIR)/midi_map_files.h \
                           $(PORTDIR)/midi_map.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@
$(BUILD)/sfz_engine.o: $(PORTDIR)/sfz_engine.cpp $(PORTDIR)/sfz_engine.h \
                       $(PORTDIR)/midi_parser.h $(PORTDIR)/sfz_map.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@
$(BUILD)/mix_simd.o: $(PORTDIR)/mix_simd.cpp $(PORTDIR)/mix_simd.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

# --- PORT: backend GPU (SDL3 GPU/Vulkan) ---
$(BUILD)/port_gpu.o: $(PORTDIR)/port_gpu.cpp $(PORTDIR)/port_gpu.h \
                     $(PORTDIR)/port.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@
ifneq ($(SPIRV_TOOL),)
# wbudowane SPIR-V: GLSL port/present.{vert,frag} -> .spv -> spv_data.cpp (xxd)
ifneq ($(findstring glslangValidator,$(SPIRV_TOOL)),)
SPV_FLAGS := -V --target-env vulkan1.0
else
SPV_FLAGS := --target-env=vulkan1.0
endif
$(BUILD)/present_vert.spv: $(PORTDIR)/present.vert | $(BUILD)
	$(SPIRV_TOOL) $(SPV_FLAGS) -o $@ $<
$(BUILD)/present_frag.spv: $(PORTDIR)/present.frag | $(BUILD)
	$(SPIRV_TOOL) $(SPV_FLAGS) -o $@ $<
$(BUILD)/spv_data.cpp: $(BUILD)/present_vert.spv $(BUILD)/present_frag.spv
	@echo '// PORT: generowane - shadery SPIR-V jako tablice C (xxd). Nie edytowac.' > $@
	@echo 'extern "C" {' >> $@
	cd $(BUILD) && xxd -i present_vert.spv >> spv_data.cpp
	cd $(BUILD) && xxd -i present_frag.spv >> spv_data.cpp
	@echo '}' >> $@
$(BUILD)/spv_data.o: $(BUILD)/spv_data.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@
endif

$(BIN): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(SDLLIBS) $(OPENMPTLIBS) $(SFIZZLIBS)

run: $(BIN)
	./$(BIN)

# --- testy jednostkowe (Google Test; bez okna/SDL - warstwa POL_* ze stubow) ---
TESTDIR := tests
TESTY := $(BUILD)/testy
TEST_SRCS := test_main test_fopen test_image13h test_cd test_audio test_paire \
             test_stubs test_sfx test_midi test_sfz test_palette_lut \
             test_char_semantics test_unit_heal
TEST_OBJS := $(addprefix $(BUILD)/,$(TEST_SRCS:%=%.o))
GTEST_CFLAGS := $(shell pkg-config --cflags gtest 2>/dev/null)
GTEST_LIBS := $(shell pkg-config --libs gtest 2>/dev/null || echo -lgtest)

# testy bez -include polshim.h (zeby fopen w testach byl zwyklym fopenem glibc)
# i bez -MMD; standard C++23 (gtest + filesystem), spojnie z CXXFLAGS
# PORT: -funsigned-char jak w CXXFLAGS - testy musza miec ta sama semantyke
# char co kod gry (znaczniki placeN 190-227)
$(BUILD)/test_%.o: $(TESTDIR)/test_%.cpp $(wildcard $(TESTDIR)/*.h)
	$(CXX) -std=c++23 -funsigned-char -O1 -g -w $(GTEST_CFLAGS) \
	       -I$(PORTDIR) -I$(TESTDIR) -I$(GAME) -I../tools -c $< -o $@

# PORT: sound_dat.o w linku testow - regresje toru efektow (POL_ListFiles,
# POL_ResolveDataFile, parsowanie SOUND.DAT) bez urzadzenia audio ani okna
# PORT: midi_parser/midi_map_files/mix_simd/sfz_engine - nowe tory (SMF,
# mapa .mid, AVX2 vs scalar, sfizz z syntetycznym .sfz); sfizz w SFIZZLIBS
# PORT: unit_heal.o - czyste funkcje balansu leczenia (test_unit_heal); symbol
# castle dostarcza sam test (definicja + puste ctory Mover1 w test_unit_heal)
$(TESTY): $(TEST_OBJS) $(BUILD)/port_fopen.o $(BUILD)/port_shims.o \
          $(BUILD)/image13h.o $(BUILD)/cd.o $(BUILD)/sound_dat.o \
          $(BUILD)/midi_parser.o $(BUILD)/midi_map_files.o \
          $(BUILD)/mix_simd.o $(BUILD)/sfz_engine.o $(BUILD)/unit_heal.o
	$(CXX) -std=c++23 -o $@ $^ $(GTEST_LIBS) -pthread $(SFIZZLIBS)

test: $(TESTY)
	./$(TESTY)

testy: test

# --- PORT: szybka walidacja skladni calego buildu (clang, -fsyntax-only) ---
# Nie tworzy obiektow ani binarki; nie przerywa na pierwszym bledzie - kompiluje
# skladniowo wszystkie zrodla i na koncu wypisuje sumaryczny licznik; kod
# wyjscia != 0, gdy byly bledy. Pelny log: $(BUILD)/clang-check.log
# spv_data.cpp jest generowany z shaderow (xxd) - pominiety.
# Celowo hardcode clang++ (nie $(CXX)): sens celu to wlasnie clang; przelaczenie
# CXX=... nie ma psuc jego przeznaczenia. Pelny build pod GCC: patrz naglowek.
CLANGXX        := clang++
CPPFLAGS_CHECK := $(filter-out -MMD -MP,$(CPPFLAGS))

clang-check:
	@command -v $(CLANGXX) >/dev/null 2>&1 || { \
	  echo 'clang-check: brak clang++ w PATH (pacman -S clang)'; exit 127; }
	@mkdir -p $(BUILD)
	@rm -f $(BUILD)/clang-check.log
	@fail=0; files=0; \
	for o in $(GAME_OBJS); do files=$$((files+1)); \
	  $(CLANGXX) $(CXXFLAGS) $(CPPFLAGS_CHECK) -fsyntax-only $(GAME)/$$o.cpp \
	    >> $(BUILD)/clang-check.log 2>&1 || fail=$$((fail+1)); \
	done; \
	for o in $(PORT_OBJS); do files=$$((files+1)); \
	  $(CLANGXX) $(CXXFLAGS) $(CPPFLAGS_CHECK) -fsyntax-only $(SRCDIR)/$$o.cpp \
	    >> $(BUILD)/clang-check.log 2>&1 || fail=$$((fail+1)); \
	done; \
	for o in $(LAYER_OBJS); do \
	  case "$$o" in spv_data) continue;; esac; \
	  files=$$((files+1)); \
	  $(CLANGXX) $(CXXFLAGS) $(CPPFLAGS_CHECK) -fsyntax-only $(PORTDIR)/$$o.cpp \
	    >> $(BUILD)/clang-check.log 2>&1 || fail=$$((fail+1)); \
	done; \
	errs=$$(grep -c ' error: ' $(BUILD)/clang-check.log || true); \
	warns=$$(grep -c ' warning: ' $(BUILD)/clang-check.log || true); \
	echo "clang-check: $$files plikow, $$fail z bledami, $$errs bledow, $$warns warningow"; \
	echo "  pelny log: $(BUILD)/clang-check.log"; \
	[ "$$fail" -eq 0 ]

clean:
	rm -rf $(BUILD) $(BIN)

.PHONY: all run clean test testy clang-check

# PORT: zaleznosci naglowkowe (.d z gcc) dolaczane NA KOCU - jak na poczatku,
# pierwszy cel z pliku .d stalby sie celem domyslnym make zamiast "all"
-include $(wildcard $(BUILD)/*.d)