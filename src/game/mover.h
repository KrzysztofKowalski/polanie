#if !defined(__MOVER_H)
#define __MOVER_H

// PORT: dawne MaxX/MaxY = 66 (stala rama mapy). Teraz MaxX/MaxY to fizyczny
// rozmiar tablic (MaxMapSize), a rzeczywisty rozmiar biezacego poziomu trzyma
// mapX/mapY (stary format ASCII nie ma rozmiaru - fallback 66 w InitBattle).
// PORT: cala skala mapy (wspolrzedne, nr, liczniki) na int64 - zwiekszenie
// MaxMapSize/MaxBuildings nie wymaga juz zmiany typow (gorny sufit utrzymuje
// pamiec tablic place/placeG/placeN/Place/attack, nie 31 bitow).
#define MaxUnitsInCastle 40
#define MaxBuildings 65536 // limit budynkow (miejsce na duzej mapie)
// PORT: 4096^2 = 16.7M komorek. Koszt pamieci statycznej: place (8B) +
// Place (8B) + placeG (4B) + attack (4B) + placeN (1B) = ~419 MB, oba zamki
// ~170 MB, kolejka BFS ~134 MB (heap). Przy 2048^2 ~3/4 tej wartosci.
#define MaxMapSize 4096 // maks. bok mapy (fizyczny rozmiar tablic)
#define MaxPictures 284
#define MaxX MaxMapSize
#define MaxY MaxMapSize
extern long long mapX, mapY; // rzeczywisty rozmiar mapy biezacego poziomu

// PORT: kodowanie nr - pelne int32 zamiast 8-bitowej maski (dawne
// IFF*256 + Nr*10 + slot): bity 0..3 = slot (pola budynku 0-3, osoba
// w budynku 4-9), bity 4..26 = indeks budynku / numer m[] zamku,
// bit 27 = jednostka zamku, bity 28-29 = IFF-1. Drzewa i znaczniki mapy
// nie sa kodowane (stale 768+indeks, zawsze << 2^27).
#define NR_CASTLE (1 << 27)
// PORT: NR_ENC - znacznik "to jest zakodowany nr" (bit 30). KONIECZNY:
// bity 28-29 (IFF-1) po stronie gracza (IFF=1) sa zerowe i budynki nie
// maja NR_CASTLE, wiec bez tego znacznika nasze obiekty nie odroznialy
// sie od drzew/tokenow mapy (sel co dziala poprawnie).
#define NR_ENC (1 << 30)
// PORT: IFF-1 siedzi w bitach 28-29; bit 30 (NR_ENC) trzeba zamaskowac
#define NR_IFF(nr) ((((nr) >> 28) & 3) + 1)
#define NR_BUILD(nr) (((nr) >> 4) & 0x7fffff)
#define NR_SLOT(nr) ((nr) & 0xf)
// PORT: czy wartosc place[] to zakodowany nr (bit 30 NR_ENC)
#define POL_NRENC(v) ((v) & NR_ENC)
// PORT: wartosc place[] to drzewo (stale 768 + indeks; max 200767 < 2^30)
#define POL_TREE(v) ((v) > 768 && !POL_NRENC(v))

#define KUSZNIK_LEV 32
#define PASTUCH_LEV 26
#define MAG_LEV 40
//////// definicje kolorow /////////////////
extern int ExperienceColor[10];
extern int Color1;

extern int LightRed;
extern int Red;
extern int DarkRed;

extern int LightYellow;
extern int Yellow;
extern int DarkYellow;

extern int LightGreen;
extern int Green;
extern int DarkGreen;

extern int LightBlue;
extern int Blue;
extern int DarkBlue;

extern int LightGray;
extern int Gray;
extern int DarkGray;

extern int LightBrown;
extern int Brown;
extern int DarkBrown;

#define Black 0
#define White 255
#define FontColor 181
#define MsgFontColor 255
///////////////////////////////////////////////////////////
struct MMessage {
  int licznik, count;
  int X, x;
  int Y, y;
  int dzwiek;
  int ddzwiek;
  char msg[40];
};
#endif

//================================================
struct memory {
  int dx, dy;
  struct memory *next;
};
//-----------------------------------------------
class Missile {
public:
  long long x, y, xt, yt, target; // PORT: int64 - mapa do 2^63 komorek
  int dx, dy;                     // kierunki i przyrosty ekranowe (mala)
  char type;
  char *view;
  char visible;
  char exist;
  char damage;
  void Init(int, int, int, int, int, int);
  void Move();
  void Show(int, int);
};
//----------------------------------------------
class Mover1 {
public:
  int xr, yr;
  char wybrany;
  long long nr; // PORT: int64 - dawne "bity 0-7 nr, 8-9 IFF, 10-15 typ" wyszlo
                // z gamutu: 8-miejsce maski ograniczalo do 25 budynkow
  int hp, maxhp;
  int mainTarget; // flaga: 0/1 (nie przechowuje nr)
  int visible;
  int command, commandN; // 0-nic 1-go 2-fight
  int type;              // 0-krowa 1-miecz 2-luk 3-mag
  class Missile missile;

  Mover1(void);
  ~Mover1(void);
  void Move(void);
  void Show(void);
  void Hide(void);
  void SetEnd(int, int);
  void SetStart(int, int);
  void Prepare(int, int, int);
  void ShowS(void);
  void Disable();
  void Repare(void);
  void SetNr(int);
  void SetIFF(int);
  void Init(int, int, int, int, int);
  void SetCommand(int);
  void SetTarget(int);
  void Run(void);
  int Milk(void);
  int OK(void) { return exist; };

  void FindGrass();
  void Labeling(void);
  void Run1(void);
  void Run2(void);
  int LookAround(void);
  int LokateTarget(void);
  void Graze(void); // pasienie sie
  void Attack(void);
  int Distance(void);

  int exist;

  int IFF; // zawsze 1 lub 2
  int exp;
  long long x, y; // PORT: int64 - wspolrzedne mapy
  long long xe, ye;
  int dx, dy; // kierunki ruchu (-1..1)
  int phase;  // 0-faza ruch  1-faza ruch 2-atak
  int faza;
  int magic; // 100
  long long xp, yp, xm, ym; // PORT: int64 - wspolrzedne mapy
  int armour, marmour;
  int inmove;
  int inattack;
  int drange;
  int damage;
  int udder;
  int base;
  int s_range;
  int a_range;
  long long target; // PORT: int64 - wartosc nr z place[] (stare 8-bitowe
                    // odwolanianie nie przebylo jeszcze 8-bitowych zmian)
  int ShowHit;
  int delay, maxdelay;
  int ispath;
  int path[6][2]; // kierunki kroku sciezki (-1..1)
};
/////////////////////////////////////////////////
struct Command {
  int co;
  int command;
  int nrb;
  int nrm;
  int x;
  int y;
};
//////////////////////////////////////////////////////
//  class Building
///////////////////////////////////////////////////////

class Building {
public:
  int exist;
  long long x, y; // PORT: int64 - wspolrzedne mapy
  int faza;
  int type;
  int hp;
  int armour;
  int maxhp;
  int food, maxfood;
  int IFF;
  long long nr; // PORT: int64 - jak Mover1::nr
  class Mover1 m[6];
  void Prepare(int, int, int);
  void ShowS(int, int, int);
  void Run();
  int Wynik(int);
  int NewMan(int);
  int Milk();
  void Rebuild();
  void Init(int, int, int, int, int);
};
//////////////////////////////////////////////////////
//  class Castle
///////////////////////////////////////////////////////
class Castle {
public:
  int IFF;
  int faza;
  // PORT: milk/maxmilk int64 (skala zapasow; UI rzutuje do int, save 8B)
  long long milk, maxmilk;
  class Mover1 m[MaxUnitsInCastle];
  class Building b[MaxBuildings];
  struct Command command;
  void Init(int, int);
  void GetCmd(struct Command *Cmd);
  void SetCmd(struct Command *Cmd);
  void Prepare(int, int, int);
  void ShowS(int, int, int);
  void Run(void);
  void DisableUnits(void);
  void FreeUnits(void);
};
