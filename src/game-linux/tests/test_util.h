// PORT: wspolne narzedzia testow jednostkowych portu Polanie (game-linux).
// Dolaczane przez pliki test_*; UWAGA: testy sa kompilowane BEZ -include
// polshim.h, wiec fopen/fclose to zwykle funkcje glibc.
#ifndef POL_TEST_UTIL_H
#define POL_TEST_UTIL_H

#include <cstdio>
#include <string>
#include <vector>

// Katalog tymczasowy z danymi testowymi (tworzony w test_main.cpp,
// usuwany po testach). Jednocześnie ustawiany jako POLANIE_DATA i
// POLANIE_EXTRACTED przed startem testow - POL_fopen zapamietuje baze
// przy pierwszym wywolaniu, wiec srodowisko musi byc gotowe wczesniej.
extern std::string POL_TEST_TMP;

inline std::string testpath(const std::string &name) {
  return POL_TEST_TMP + "/" + name;
}

// Zapis pliku testowego w katalogu tymczasowym.
inline bool test_write(const std::string &name,
                       const std::vector<unsigned char> &data) {
  FILE *f = fopen(testpath(name).c_str(), "wb");
  if (!f)
    return false;
  size_t n = data.empty() ? 0 : fwrite(data.data(), 1, data.size(), f);
  fclose(f);
  return n == data.size();
}

#endif // POL_TEST_UTIL_H