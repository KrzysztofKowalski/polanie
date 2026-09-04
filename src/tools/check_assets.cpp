// check_assets.cpp — weryfikacja danych gry "Polanie" (1997) i ekstraktu
// dla portu Linux (game-linux). Uruchamiane przez scripts/install.sh po
// ekstrakcji; mozna tez odpalic recznie.
//
// Co sprawdza (raport OK/brak per pozycja, kod wyjscia 0/1):
//   katalog bazowy gry (GRAF.DAT + PAL.DAT):
//     - env POLANIE_DATA, albo pierwszy argument,
//     - ./dysk/GRY/POLANIE, ../dysk/GRY/POLANIE, ../../dysk/GRY/POLANIE
//   <baza>/DATA/      : W001.dat (krytyczny), I001.dat (opcjonalny - pelny
//                       zestaw efektow jest w wersji CD), licznik W*/I*
//   <baza>/GRAF/      : LEVEL.DAT (krytyczny) i LEVEL.INI (krytyczny),
//                       case-insensitive; dopuszczalny wariant
//                       GRAF/levels/level.dat(+level.ini) albo LEVELS/
//   <baza>/           : FONT.DAT (krytyczny), POST.DAT (opcjonalny)
//   ekstrakt (env POLANIE_EXTRACTED, albo ../extracted wzgledem bazy):
//   teksty/LEVEL.txt (konwersja LEVEL.DAT z ekstraktora; dopuszczalny tez
//   surowy LEVEL.DAT), audio/dzwieki i audio/muzyka niepuste
//
// Budowa: g++ -std=c++20 -O2 -o check_assets check_assets.cpp

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ------------------------------------------------------------ pomocnicze --

static int eqni(const char *a, const char *b) {
  while (*a && *b) {
    if (std::tolower((unsigned char)*a) != std::tolower((unsigned char)*b))
      return 0;
    ++a;
    ++b;
  }
  return *a == 0 && *b == 0;
}

// nazwa w katalogu (case-insensitive) -> pelna sciezka; false gdy brak
static bool find_ci(const fs::path &dir, const char *name, fs::path *hit) {
  std::error_code ec;
  if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec))
    return false;
  for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
       it != end; it.increment(ec)) {
    if (ec)
      return false;
    if (eqni(it->path().filename().string().c_str(), name)) {
      if (hit)
        *hit = it->path();
      return true;
    }
  }
  return false;
}

// liczba regularnych plikow w katalogu (0, gdy brak)
static size_t count_files(const fs::path &dir) {
  std::error_code ec;
  if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec))
    return 0;
  size_t n = 0;
  for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
       it != end; it.increment(ec)) {
    if (ec)
      break;
    if (it->is_regular_file(ec))
      ++n;
  }
  return n;
}

// ---------------------------------------------------------------- raport --

struct Wynik {
  std::string opis;    // co sprawdzano
  bool ok;             // znaleziony
  bool krytyczny;      // brak = exit 1
  std::string sciezka; // trafienie (moze byc puste)
};

static std::vector<Wynik> wyniki;
static int braki = 0; // braki krytyczne
static int ostrz = 0; // braki opcjonalne

static void report(const char *opis, bool ok, bool krytyczny, const fs::path &sciezka) {
  // przy braku nie zapamiętujemy sciezki - wspolny bufor trafien moze
  // przechowywac wynik poprzedniego sprawdzenia (lektym)
  wyniki.push_back({opis, ok, krytyczny, ok ? sciezka.string() : std::string()});
  if (!ok)
    (krytyczny ? braki : ostrz) += 1;
}

// ------------------------------------------------------------- szukanie ---

// katalog bazowy gry: env POLANIE_DATA, argument 1, albo kandydaci wzgledni
static fs::path znajdzBaze(const char *arg1) {
  std::vector<fs::path> kandydaci;
  if (const char *env = std::getenv("POLANIE_DATA"))
    if (env[0])
      kandydaci.push_back(env);
  if (arg1 && arg1[0])
    kandydaci.push_back(arg1);
  kandydaci.push_back("./dysk/GRY/POLANIE");
  kandydaci.push_back("../dysk/GRY/POLANIE");
  kandydaci.push_back("../../dysk/GRY/POLANIE");
  for (const auto &k : kandydaci) {
    std::error_code ec;
    if (!fs::exists(k, ec))
      continue;
    if (find_ci(k, "GRAF.DAT", nullptr) && find_ci(k, "PAL.DAT", nullptr))
      return fs::absolute(k);
  }
  return {};
}

// katalog ekstraktu: env POLANIE_EXTRACTED, argument 2, albo wzgledem bazy/CWD
static fs::path znajdzEkstrakt(const char *arg2, const fs::path &baza) {
  std::vector<fs::path> kandydaci;
  if (const char *env = std::getenv("POLANIE_EXTRACTED"))
    if (env[0])
      kandydaci.push_back(env);
  if (arg2 && arg2[0])
    kandydaci.push_back(arg2);
  if (!baza.empty())
    kandydaci.push_back(baza / ".." / "extracted");
  kandydaci.push_back("./extracted");
  kandydaci.push_back("../extracted");
  for (const auto &k : kandydaci) {
    std::error_code ec;
    if (fs::exists(k, ec) && fs::is_directory(k, ec))
      return fs::absolute(k);
  }
  return {};
}

// liczba plikow o prefiksie [lit] + cyfra (np. W001.DAT -> lit='w')
static size_t policzPrefiks(const fs::path &dir, char lit) {
  std::error_code ec;
  size_t n = 0;
  if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec))
    return 0;
  for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
       it != end; it.increment(ec)) {
    if (ec)
      break;
    std::string nazwa = it->path().filename().string();
    if (nazwa.size() >= 4 &&
        std::tolower((unsigned char)nazwa[0]) == std::tolower((unsigned char)lit) &&
        std::isdigit((unsigned char)nazwa[1]))
      ++n;
  }
  return n;
}

// -------------------------------------------------------------- sprawdza --

// LEVEL.DAT/INI: GRAF/ (case-insensitive), GRAF/levels/ albo LEVELS/
static bool znajdzLevel(const fs::path &baza, const char *plik, fs::path *hit) {
  const char *podkatalogi[] = {"GRAF", "GRAF/levels", "LEVELS"};
  for (const char *pk : podkatalogi) {
    fs::path d = baza / pk;
    if (find_ci(d, plik, hit))
      return true;
  }
  return false;
}

int main(int argc, char **argv) {
  if (argc > 3) {
    std::fprintf(stderr, "Uzycie: check_assets [katalog_gry] [katalog_ekstraktu]\n");
    return 2;
  }
  const char *arg1 = argc > 1 ? argv[1] : nullptr;
  const char *arg2 = argc > 2 ? argv[2] : nullptr;

  fs::path baza = znajdzBaze(arg1);
  fs::path ekstrakt = znajdzEkstrakt(arg2, baza);

  printf("check_assets — weryfikacja danych gry Polanie (1997)\n");
  printf("  baza gry : %s\n", baza.empty() ? "(NIE ZNALEZIONO)" : baza.string().c_str());
  printf("  ekstrakt : %s\n", ekstrakt.empty() ? "(NIE ZNALEZIONO)" : ekstrakt.string().c_str());
  printf("\n  [dane gry]\n");
  if (baza.empty()) {
    printf("  BLAD: katalog gry z GRAF.DAT i PAL.DAT nie znaleziony.\n");
    printf("  Ustaw POLANIE_DATA albo podaj katalog jako pierwszy argument.\n");
    return 1;
  }

  fs::path hit;
  report("GRAF.DAT", find_ci(baza, "GRAF.DAT", &hit), true, hit);
  report("PAL.DAT", find_ci(baza, "PAL.DAT", &hit), true, hit);
  report("FONT.DAT", find_ci(baza, "FONT.DAT", &hit), true, hit);
  report("POST.DAT", find_ci(baza, "POST.DAT", &hit), false, hit);
  report("SWIAT.DAT", find_ci(baza, "SWIAT.DAT", &hit), false, hit);

  // DATA/ (efekty)
  fs::path data = baza / "DATA";
  std::error_code ec;
  if (fs::exists(data, ec) && fs::is_directory(data, ec)) {
    report("DATA/W001.dat", find_ci(data, "W001.dat", &hit), true, hit);
    report("DATA/I001.dat", find_ci(data, "I001.dat", &hit), false, hit);
    size_t nw = policzPrefiks(data, 'W'), ni = policzPrefiks(data, 'I');
    printf("  (DATA: %zu plikow W* (efekty), %zu plikow I* (wersja CD))\n", nw, ni);
  } else {
    report("DATA/ (katalog efektow)", false, true, data);
  }

  // teksty LEVEL.DAT/INI (GRAF/ albo warianty)
  report("LEVEL.DAT", znajdzLevel(baza, "LEVEL.DAT", &hit), true, hit);
  report("LEVEL.INI", znajdzLevel(baza, "LEVEL.INI", &hit), true, hit);

  // ekstrakt
  printf("\n  [ekstrakt]\n");
  if (ekstrakt.empty()) {
    report("ekstrakt (extracted/)", false, true, fs::path());
  } else {
    // ekstraktor zapisuje konwersje jako teksty/LEVEL.txt (nie LEVEL.DAT)
    report("teksty/LEVEL.txt", find_ci(ekstrakt / "teksty", "LEVEL.txt", &hit)
                                 || find_ci(ekstrakt / "teksty", "LEVEL.DAT", &hit),
           true, hit);
    fs::path dzw = ekstrakt / "audio" / "dzwieki";
    fs::path muz = ekstrakt / "audio" / "muzyka";
    size_t ndz = count_files(dzw);
    size_t nmz = count_files(muz);
    printf("  (audio/dzwieki: %zu plikow, audio/muzyka: %zu plikow)\n", ndz, nmz);
    report("audio/dzwieki (W*.wav)", ndz > 0, true, dzw);
    report("audio/muzyka (GRAF_*.s3m)", nmz > 0, true, muz);
  }

  // podsumowanie
  printf("\n  [raport]\n");
  for (const auto &w : wyniki) {
    char linia[512];
    std::snprintf(linia, sizeof(linia), "%-26s %s", w.opis.c_str(),
                  w.ok ? "OK" : (w.krytyczny ? "BRAK" : "brak (opcjonalny)"));
    if (!w.ok && !w.sciezka.empty()) {
      size_t dl = std::strlen(linia);
      std::snprintf(linia + dl, sizeof(linia) - dl, "  (%s)", w.sciezka.c_str());
    }
    printf("  %s\n", linia);
  }
  printf("\nPodsumowanie: %d krytycznych brakow, %d ostrzezen.\n", braki, ostrz);
  if (braki == 0)
    printf("Wszystko gotowe - uruchamiaj: bash scripts/run.sh\n");
  else
    printf("Braki krytyczne: uruchom ponownie bash scripts/install.sh albo "
           "uzupelnij dane.\n");
  if (ostrz > 0 && braki == 0)
    printf("Braki opcjonalne: wersja CD dolacza pelny zestaw efektow "
           "(DATA/I*.dat) i banki obrazow - bash scripts/install.sh --cd\n");
  return braki == 0 ? 0 : 1;
}