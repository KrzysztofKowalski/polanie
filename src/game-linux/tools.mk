# game-linux/tools.mk — narzędzia offline (tor S3M->VSCO).
# OSOBNY plik make: nie jest dołączany do Makefile gry (ten pozostaje nietknięty).
#
# Użycie (z korzenia repo):
#   make -f game-linux/tools.mk tools   # buduje game-linux/build/tools/s3m_vsco_render
#   make -f game-linux/tools.mk clean
#
# Narzędzie: tools/s3m_vsco_render.cpp — parser wariantu S3M Polan + symulator
# oryginalnego odtwarzacza (game/plays3m.cpp) + sfizz (VSCO CE) -> WAV.
# Tor OPL (openmpt123) nie wymaga kompilacji — to CLI systemowe (libopenmpt).

# PORT: spójnie z game-linux/Makefile — clang + C++23 + warningi (GCC nadal:
# make -f game-linux/tools.mk CXX=g++ CXXFLAGS='-std=c++20 -O2 -Wall')
CXX      ?= clang++
CXXFLAGS ?= -std=c++23 -O2 -Wall -Wextra
SFIZZ_LIBS ?= -lsfizz

TOOLS_BIN := game-linux/build/tools
TOOLS_SRC := tools/s3m_vsco_render.cpp
TOOLS_HDR := game-linux/port/sfz_map.h

.PHONY: tools clean

tools: $(TOOLS_BIN)/s3m_vsco_render

$(TOOLS_BIN)/s3m_vsco_render: $(TOOLS_SRC) $(TOOLS_HDR)
	@mkdir -p $(TOOLS_BIN)
	$(CXX) $(CXXFLAGS) -I game-linux $(TOOLS_SRC) -o $@ $(SFIZZ_LIBS)

clean:
	rm -f $(TOOLS_BIN)/s3m_vsco_render