/////////////////////////////////////////////////////////////////////
//   image13h.cpp
//    kompilowac w modelu Large
////////////////////////////////////////////////////////////////////
#include "image13h.h"
#include <conio.h>
#include <dos.h>
#include <malloc.h>
#include <mem.h>
#include <stdio.h>
#include <string.h>
#include "port.h" // PORT: warstwa SDL2 zamiast pamieci A0000/VGA
//////////////////////////////////////////////////////////////////
// Zmienne srodowiskowe
//////////////////////////////////////////////////////////////////
FILE *palettefile, *graphicfile;
char *VirtualScreen;
char *RealVirtualScreen;

char *rgb = NULL, *Buffer330 = NULL, *Rgb = NULL;
int length[91] = {5, 3, 5, 7, 7, 7, 7, 7, 5, 5, 7, 7,  3, 5, 3, 7, 7, 6,  7,
                  7, 7, 7, 7, 7, 7, 7, 3, 3, 9, 7, 9,  7, 8, 9, 8, 8, 8,  7,
                  7, 9, 8, 3, 6, 8, 7, 9, 8, 9, 7, 9,  8, 8, 7, 8, 9, 12, 8,
                  9, 8, 8, 8, 8, 7, 8, 5, 7, 7, 7, 7,  7, 5, 7, 7, 3, 3,  6,
                  3, 9, 7, 7, 7, 7, 5, 7, 4, 7, 7, 11, 7, 7, 7};
char *index[91];
int lineLength = 320;
extern char drive[4];
int ClipX1 = 0, ClipX2 = 319, ClipY1 = 0, ClipY2 = 199;
///////////////////////////////////////////////////////////////////////
// inicjowanie ekranu virtualnego
///////////////////////////////////////////////////////////////////////
int InitVirtualScreen(void) {
  RealVirtualScreen = (char *)malloc(64000);
  if (RealVirtualScreen == NULL)
    return 0;
  memset(RealVirtualScreen, 0, 64000);
  return 1;
}
///////////////////////////////////////////////////////////////////////
//
///////////////////////////////////////////////////////////////////////
void ClearScreen13h() { memset(VirtualScreen, 0, 64000); }
//////////////////////////////////////////////////////////////////////
// kopiowanie virtualnego obrazu na ekran
//////////////////////////////////////////////////////////////////////
void ShowVirtualScreen(void) {
  if (RealVirtualScreen == NULL)
    return;
  // PORT: blit do okna SDL zamiast memcpy do pamieci VGA A0000
  memcpy(POL_FrameBuffer(), RealVirtualScreen, 64000);
  POL_Present();
}
///////////////////////////////////////////////////////////////////////////
// Zwalnianie pamieci zajmowanej przez ekran
//////////////////////////////////////////////////////////////////////
void FreeVirtualScreen(void) {
  if (RealVirtualScreen == NULL)
    return;
  free(RealVirtualScreen);
  RealVirtualScreen = NULL;
}
////////////////////////////////////////////////////////////////////////////
// ustawienie aktualnego ekranu dla funkcji graficznych
//   0-rzeczywisty       1-wirtualny
//////////////////////////////////////////////////////////////////////////
void SetScreen(int Screen) {
  if ((Screen) && (RealVirtualScreen != NULL))
    VirtualScreen = RealVirtualScreen;
  else
    VirtualScreen = (char *)POL_FrameBuffer(); // PORT: zamiast A0000
}
///////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////
void Init13h(void) {
  // PORT: tryb VGA 13h -> okno SDL (skala ustawiona przez CLI/auto)
  POL_VideoInit();
  SetScreen(0);
  memset(POL_FrameBuffer(), 0, 64000);
}
///////////////////////////////////////////////////////////
int InitBuffers13h() {
  if ((rgb = (char *)malloc(768)) == NULL)
    return 1;
  if ((Rgb = (char *)malloc(768)) == NULL)
    return 1;
  if ((Buffer330 = (char *)malloc(330)) == NULL)
    return 1;
  return 0;
}
//////////////
void FreeBuffers13h() {
  if (rgb != NULL)
    free(rgb);
  if (Rgb != NULL)
    free(Rgb);
  if (Buffer330 != NULL)
    free(Buffer330);
}

/////////////////////////////////////////////////////////////////
// END13H
///////////////////////////////////////////////////////////////
void Close13h(void) {
  // PORT: powrot do trybu tekstowego -> zamkniecie okna SDL
  POL_VideoQuit();
}
///////////////////////////////////////////////////////////
//
//   SetClippingArea
//
///////////////////////////////////////////////////////////
void SetClippingArea13h(int x1, int y1, int x2, int y2) {
  ClipX1 = x1;
  ClipX2 = x2;
  ClipY1 = y1;
  ClipY2 = y2;
}

///////////////////////////////////////////////////////////////////////
//  rysowanie punktu
///////////////////////////////////////////////////////////////////////
void PutPixel13h(int x, int y, int color) {
  VirtualScreen[x + y * 320] = (unsigned char)color;
}

//////////////////////////////////////////////////////////////////
// Load image from file "name" to picture
//////////////////////////////////////////////////////////////////

char *LoadImage13h(char *name) { // return adres do obrazka lub NULL w przypadku
  // wystapienia bledu
  FILE *file;
  char *picture;
  short int sizex, sizey;

  file = fopen(name, "rb");
  if (file == NULL)
    return NULL;
  // PORT: pliki z dysku (FONT.DAT itp.) maja naglowek [u16 size][u16 w][u16 h]
  // (pole size jest zawyzone), a obrazy tworzone przez gre ([w][h][2B][1][0])
  // maja szerokosc od razu w polu 0. Rozpoznajemy format po wartosci pola 0:
  // > 1000 to size (FONT.DAT 319x100 da size=32326), wtedy w/h bierzemy z pol
  // 2 i 3 i budujemy naglowek [w][h], ktorego oczekuje PutImage13h.
  {
    short int f0, f1, f2;
    if (fread(&f0, 2, 1, file) != 1) {
      fclose(file);
      return NULL;
    }
    if (fread(&f1, 2, 1, file) != 1) {
      fclose(file);
      return NULL;
    }
    if (f0 > 1000 && f1 > 0 && f1 <= 1000) {
      if (fread(&f2, 2, 1, file) != 1) {
        fclose(file);
        return NULL;
      }
      sizex = f1;
      sizey = f2;
    } else {
      sizex = f0;
      sizey = f1;
    }
  }
  picture = (char *)malloc((size_t)sizex * (size_t)sizey + 6);
  if (picture == NULL) {
    fclose(file);
    return NULL;
  }
  // naglowek w formacie [w][h][1][0] + dane od offsetu 6 (wiersze po w bajtow)
  picture[0] = (char)(sizex & 0xFF);
  picture[1] = (char)((sizex >> 8) & 0xFF);
  picture[2] = (char)(sizey & 0xFF);
  picture[3] = (char)((sizey >> 8) & 0xFF);
  picture[4] = 1;
  picture[5] = 0;
  fread(picture + 6, 1, (size_t)sizex * (size_t)sizey, file);
  fclose(file);
  return picture;
}

////////////////////////////////////////////////////////////////////
int LoadToScreen13h(int offset, int line) {
  int i = 0, j = 1;
  int Offset = offset * 33000;
  short size;

  if (graphicfile == NULL)
    return 1;

  fseek(graphicfile, Offset, 0);
  fread(&size, 2, 1, graphicfile);
  fread(&size, 2, 1, graphicfile);
  fread(&size, 2, 1, graphicfile);
  if (line)
    j = 0;
  for (i = 0; i < 99 + j; i++) {
    size = fread((void *)(VirtualScreen + (line * 320) + (i * 320)), 1, 319,
                 graphicfile);
    if (size != 319)
      j = 2;
  }

  return j;
}

////////////////////////////////////////////////////////////////////////////
// Save picture image to file "name"
//////////////////////////////////////////////////////////////////////////
int SaveImage256(char *name, char *picture) { // return 0 if OK
  FILE *file;

  short int sizex, sizey, *buf = (short int *)picture;
  file = fopen(name, "wb");
  if (file == NULL)
    return 1;
  sizex = buf[0];
  sizey = buf[1];
  fwrite(picture, 1, GetImageSize13h(0, 0, sizex - 1, sizey - 1), file);
  fclose(file);
  return 0;
}

//////////////////////////////////////////////////////////////////
//  laduje bitmape pod adres map
//////////////////////////////////////////////////////////////////
void GetImage13h(int x1, int y1, int x2, int y2, char *picture) {
  int i, j;
  short int *Buf = (short *)picture;

  Buf[0] = (short)(x2 - x1);
  Buf[1] = (short)(y2 - y1);
  picture[4] = 1;
  picture[5] = 0;
  for (j = 0; j < y2 - y1; j++) {
    for (i = 0; i < x2 - x1; i++)
      picture[6 + i + j * (x2 - x1)] = VirtualScreen[i + x1 + (j + y1) * 320];
  }
}
////////////////////////////////////////////////////////////////////////
//
//       GetImageSize256
//
////////////////////////////////////////////////////////////////////////
int GetImageSize13h(int x1, int y1, int x2, int y2) {
  return (x2 - x1 + 1) * (y2 - y1 + 1) + 6;
}
//////////////////////////////////////////////////////////////////
//  wyswietla bitmape
//////////////////////////////////////////////////////////////////
void PutImage13h(int x, int y, char *picture, int how) {
  int length, height, j;
  short int *Buf = (short int *)picture;
  char color;
  char *screen;
  char *picturePos;
  if (x < 0 || y < 0)
    return;

  if (!how) {
    length = Buf[0];
    height = Buf[1];
    if (x + length < ClipX1 || y + height < ClipY1 || x > ClipX2 || y > ClipY2)
      return;
    if (y + height > 199)
      height = 199 - y;

    screen = &VirtualScreen[(y * 320) + x];
    picturePos = picture + 6;
    for (j = 0; j < height; j++) {
      memcpy((void *)screen, (void *)picturePos, length);
      picturePos += length;
      screen += 320;
    }
  } else {
    length = Buf[0];
    height = Buf[1];
    if (x + length < ClipX1 || y + height < ClipY1 || x > ClipX2 || y > ClipY2)
      return;

    picturePos = picture + 6;
    if (y + height > ClipY2)
      height = ClipY2 - y;
    if (y < ClipY1) {
      picturePos += length * (ClipY1 - y);
      height -= (ClipY1 - y);
      y = ClipY1;
    }
    int a = 0;
    int b = 0;
    int c = 0;
    if (x < ClipX1) {
      a = ClipX1 - x;
      picturePos += a;
      x = ClipX1;
    }
    if (x + length > ClipX2) {
      b = x + length - ClipX2;
    }

    // line poczatek linii na ekranie
    screen = &VirtualScreen[(y * 320) + x];
    c = a + b;
    length -= c;
    for (j = 0; j < height; j++) {
      for (int i = 0; i < length; i++) {
        color = *picturePos;
        picturePos++;
        if (color) {
          *screen = color;
        }
        screen++;
      }
      picturePos += c;
      screen += (320 - length);
    }
  }
}

//////////////////////////////////////////////////////////////////
//  wyswietla bitmape i zamienia kolor c1 na c2
//////////////////////////////////////////////////////////////////
void PutImageChange13h(int x, int y, char *picture, int how, char c1, char c2) {
  int length, height, j;
  short int *Buf = (short int *)picture;
  char color;
  char *screen, *picturePos;
  if (x > ClipX2)
    return;

  length = Buf[0];
  height = Buf[1];
  if (x + length < ClipX1 || y + height < ClipY1 || x > ClipX2 || y > ClipY2)
    return;

  picturePos = picture + 6;
  if (y + height > ClipY2)
    height = ClipY2 - y;

  if (y < ClipY1) {
    picturePos += length * (ClipY1 - y);
    height -= (ClipY1 - y);
    y = ClipY1;
  }
  int a = 0;
  int b = 0;
  int c = 0;
  if (x < ClipX1) {
    a = ClipX1 - x;
    picturePos += a;
    x = ClipX1;
  }
  if (x + length > ClipX2) {
    b = x + length - ClipX2;
  }
  c = a + b;
  length -= c;
  screen = &VirtualScreen[(y * 320) + x]; // line poczatek linii na ekranie
  for (j = 0; j < height; j++) {
    for (int i = 0; i < length; i++) {
      color = *picturePos;

      if (color || !how) {
        if (color == c1) {
          *screen = c2;
        } else {
          *screen = color;
        }
      }
      picturePos++;
      screen++;
    }
    screen += 320 - length;
    picturePos += c;
  }
}

//////////////////////////////////////////////////////////////////
//  wyswietla bitmape inwersyjnie i zamienia kolor c1 na c2
//////////////////////////////////////////////////////////////////
void PutImageChangeInverse13h(int x, int y, char *picture, int how, char c1,
                              char c2) {
  int length, height, j;
  short int *Buf = (short int *)picture;
  char color;
  char *screen, *picturePos;

  length = Buf[0];
  height = Buf[1];
  if (x + length < ClipX1 || y + height < ClipY1 || x > ClipX2 || y > ClipY2)
    return;

  if (y + height > ClipY2)
    height = ClipY2 - y;
  picturePos = picture + 6;
  if (y < ClipY1) {
    picturePos += length * (ClipY1 - y);
    height -= (ClipY1 - y);
    y = ClipY1;
  }
  int a = 0;
  int b = 0;
  int c = 0;
  if (x < ClipX1) {
    a = ClipX1 - x;
    // picturePos+=a;
    x = ClipX1;
  }
  if (x + length > ClipX2) {
    b = x + length - ClipX2;
  }
  c = a + b;
  length -= c;
  screen = &VirtualScreen[(y * 320) + x]; // line poczatek linii na ekranie
  picturePos += length + b - 1;
  for (j = 0; j < height; j++) {
    for (int i = 0; i < length; i++) {
      color = *picturePos;

      if (color || !how) {
        if (color == c1) {
          *screen = c2;
        } else {
          *screen = color;
        }
      }
      picturePos--;
      screen++;
    }
    screen += 320 - length;
    picturePos += (length + length + c);
  }
}

//////////////////////////////////////////////////////////////////
//  wyswietla kwadrat
//////////////////////////////////////////////////////////////////
void Rectangle13h(int x1, int y1, int x2, int y2, int color) {
  int a;
  if (x1 > x2) {
    a = x1;
    x1 = x2;
    x2 = a;
  }
  if (y1 > y2) {
    a = y1;
    y1 = y2;
    y2 = a;
  }
  int Y1 = y1 * lineLength;
  int Y2 = y2 * lineLength;
  int b = 1, c = 1, d = 1;
  a = 1;

  if (x1 >= ClipX2 || x2 <= ClipX1 || y1 >= ClipY2 || y2 <= ClipY1)
    return;

  if (x1 <= ClipX1) {
    x1 = ClipX1 + 1;
    a = 0;
  }
  if (x2 >= ClipX2) {
    x2 = ClipX2 - 1;
    b = 0;
  }
  if (y1 <= ClipY1) {
    y1 = ClipY1 + 1;
    c = 0;
  }
  if (y2 >= ClipY2) {
    y2 = ClipY2 - 1;
    d = 0;
  }

  // SetAreas(x1,y1,x2,y2);

  if (c)
    for (int i = x1; i <= x2; i++) {
      VirtualScreen[i + Y1] = (short)color;
    }

  if (d)
    for (int i = x1; i <= x2; i++) {
      VirtualScreen[i + Y2] = (short)color;
    }

  Y1 = y1 * lineLength + x1;
  Y2 = y1 * lineLength + x2;

  if (a)
    for (int i = y2 - y1; i > 0; i--) {
      VirtualScreen[Y1 += lineLength] = (short)color;
    }

  if (b)
    for (int i = y2 - y1; i > 0; i--) {
      VirtualScreen[Y2 += lineLength] = (short)color;
    }
}

//////////////////////////////////////////////////////////////////
//  wyswietla bar'a
//////////////////////////////////////////////////////////////////
void Bar13h(int x1, int y1, int x2, int y2, int color) {
  int a;
  int length;
  if ((x1 == x2) || (y1 == y2))
    return;
  if (x1 > x2) {
    a = x1;
    x1 = x2;
    x2 = a;
  }
  if (y1 > y2) {
    a = y1;
    y1 = y2;
    y2 = a;
  }
  if (x1 > ClipX2 || x2 < ClipX1 || y1 > ClipY2 || y2 < ClipY1)
    return;
  if (x1 < ClipX1)
    x1 = ClipX1;
  if (x2 > ClipX2)
    x2 = ClipX2;
  if (y1 < ClipY1)
    y1 = ClipY1;
  if (y2 > ClipY2)
    y2 = ClipY2;
  if (x1 > x2 || y1 > y2)
    return;
  if (x1 > x2 || y1 > y2)
    return;

  length = x2 - x1;
  for (int j = y1; j < y2; j++) {
    memset((void *)&VirtualScreen[x1 + j * lineLength], color, length);
  }
}

//--------------------------------------------------------
//            InitText13h
//--------------------------------------------------------
int InitText13h() {
  unsigned int size;
  int i, x1, x2;
  char *literki;
  char ss[50];
  sprintf(ss, "%sfont.dat", drive);
  literki = LoadImage13h(ss); //??????????
  if (literki == NULL)
    return 1;
  PutImage13h(0, 0, literki, 0);
  free(literki);

  x1 = 7;
  x2 = 11;
  for (i = 0; i < 33; i++) {
    x2 = x1 + length[i] - 1;
    size = GetImageSize13h(x1, 8, x2, 20);
    index[i] = (char *)malloc(size);
    if (index[i] != NULL)
      GetImage13h(x1, 8, x2, 20, index[i]);
    else
      return 1;
    x1 = x2;
  }

  x1 = 8;
  x2 = 16;
  for (i = 33; i < 64; i++) {
    x2 = x1 + length[i] - 1;
    size = GetImageSize13h(x1, 32, x2, 44);
    index[i] = (char *)malloc(size);
    if (index[i] != NULL)
      GetImage13h(x1, 32, x2, 44, index[i]);
    else
      return 1;
    x1 = x2;
  }

  x1 = 7;
  x2 = 11;
  for (i = 64; i < 91; i++) {
    x2 = x1 + length[i] - 1;
    size = GetImageSize13h(x1, 56, x2, 78);
    index[i] = (char *)malloc(size);
    if (index[i] != NULL)
      GetImage13h(x1, 56, x2, 78, index[i]);
    else
      return 1;
    x1 = x2;
  }

  return 0;
}

//------------------------------------------------------
//               Transform
//------------------------------------------------------
char Transform13h(unsigned char znak) {
  switch (znak) {
  case 134:
    return '#'; // a
  case 145:
    return '$'; // e
  case 162:
    return '%'; // o'
  case 166:
    return '&'; // z'
  case 167:
    return 39; // z
  case 158:
    return '*'; // s
  case 141:
    return '+'; // c
  case 164:
    return 47; // n
  case 143:
    return '<'; // A
  case 144:
    return '='; // E
  case 163:
    return '>'; // O
  case 160:
    return '@'; // Z'
  case 161:
    return '['; // Z
  case 149:
    return '\\'; // C
  case 152:
    return ']'; // S
  case 156:
    return '^'; // L
  case 165:
    return '_'; // N
  case 146:
    return '`'; // l
  }
  return znak;
}

//--------------------------------------------------------
//             ClearText
//-------------------------------------------------------
void ClearText13h() {
  int i;

  for (i = 0; i < 92; i++)
    if (index[i] != NULL)
      free(index[i]);
}
//--------------------------------------------------------
//             OutTextDelay13h                wypisywanie liter
//-------------------------------------------------------
// PORT: const char* - funkcja tylko czyta tekst (literaly sa const w C++23)
void OutTextDelay13h(int x, int y, const char *text, int colour1, int colour2,
                     int del) {
  char *letter;
  unsigned char znak;

  while (*text != 0) { // PORT: bylo NULL (char != wskaznik-null; -Wnull)
    znak = Transform13h(*text);
    if (znak < 32 || znak > 32 + 91)
      return;
    letter = (char *)index[znak - 32];
    if (letter != NULL)
      PutImageChange13h(x - 1, y, letter, 1, 255, colour2);
    PutImageChange13h(x, y + 1, letter, 1, 255, colour2);
    PutImageChange13h(x, y, letter, 1, 255, colour1);
    x = x + length[znak - 32] - 1;
    if (x > 320)
      return;
    delay(del);
    text++;
  }
}
//--------------------------------------------------------
//             CenterText13h                wypisywanie liter
//-------------------------------------------------------
// PORT: const char* - wywolania z literalami (mapa.cpp, battle.cpp); obcinanie
// (*text = NULL w oryginale) robi na kopii lokalnej, bo literala nie wolno
// modyfikowac. Cialo funkcji bez zmian (text wskazuje kopie).
void CenterText13h(int xl, int yg, int xp, int yd, const char *text_in,
                   int colour) {
  char bufor[256];
  char *text = bufor;
  int x, y, dl = 0, wsk;
  int i, ile = 0;

  {
    size_t n = strlen(text_in);
    if (n > sizeof(bufor) - 1)
      n = sizeof(bufor) - 1;
    memcpy(bufor, text_in, n);
    bufor[n] = 0;
  }
  wsk = strlen(text);
  while (*text != 0) { // PORT: bylo NULL (char != wskaznik-null; -Wnull)
    ile = ile + length[*text - 32] - 1;
    if (ile > (xp - xl - 12))
      *text = 0; // PORT: bylo NULL (przypisanie do char - koniec napisu)
    text++;
  }
  for (i = 0; i < wsk; i++)
    text--;

  wsk = strlen(text);
  while (*text != 0) { // PORT: bylo NULL (char != wskaznik-null; -Wnull)
    dl = dl + length[*text - 32] - 1;
    text++;
  }
  if (xp - xl < dl)
    return;
  if (yd - yg < 13)
    return;
  y = (yd - yg + 1) >> 1;
  y = yg + y - 7;
  x = (xp - xl - dl) >> 1;
  x = xl + x;
  for (i = 0; i < wsk; i++)
    text--;
  OutText13h(x, y, text, colour);
}

//--------------------------------------------------------
//             OutText13h                wypisywanie liter
//-------------------------------------------------------
// PORT: const char* - funkcja tylko czyta tekst (wywolania z literalami)
void OutText13h(int x, int y, const char *text, int colour) {
  char *letter;
  char znak;

  while (*text != 0) { // PORT: bylo NULL (char != wskaznik-null; -Wnull)
    znak = Transform13h(*text);
    letter = index[znak - 32];
    PutImageChange13h(x, y, letter, 1, 255, colour);
    x = x + length[znak - 32] - 1;
    text++;
  }
}
//--------------------------------------------------------
//             Write13h                wypisywanie liter
//-------------------------------------------------------

// PORT: int Write13h - brak typu zwracanego (implicit int zniesiony w C++23;
// deklaracja w image13h.h ma int)
int Write13h(int x, int y, int maxx, int maxdl, char *txt, int tcolour,
             int bcolour) {
  // PORT: ekran edycji tekstu (jedyne pole wpisywania w grze) - WASD musi
  // dochodzic jako litery ASCII, nie jako kody strzalek (mapowanie WASD ->
  // strzalki w port_sdl.cpp; decyzja usera 2026-09-04). Tryb gaszony przy
  // kazdym wyjsciu z Write13h.
  POL_SetTextInputMode(1);
  int cx = 0, a, ll, xp, wsk = 0, ile = 0;
  char k, l;
  char str[2] = {0, 0};
  wsk = strlen(txt);
  while (*txt != 0) { // PORT: bylo NULL (char != wskaznik-null; -Wnull)
    ile = ile + length[*txt - 32] - 1;
    if (ile > maxx - 12)
      *txt = 0; // PORT: bylo NULL (przypisanie do char - koniec napisu)
    txt++;
  }
  for (int i = 0; i < wsk; i++)
    txt--;
  cx = strlen(txt);
  do {
    if (maxx == 0)
      Bar13h(x, y, x + (maxdl + 1) * 11 - 1, y + 14, bcolour);
    else
      Bar13h(x, y, x + maxx, y + 14, bcolour);
    xp = x;
    for (a = 0; a < (int)strlen(txt); a++) { // PORT: cast - int vs size_t
      str[0] = txt[a];
      if (a == cx) {
        Bar13h(xp, y + 11, xp + length[txt[a] - 32], y + 12, tcolour);
        OutText13h(xp, y, str, tcolour);
      } else {
        OutText13h(xp, y, str, tcolour);
      }
      xp += length[txt[a] - 32] - 1;
    }

    if (cx == (int)strlen(txt)) { // PORT: cast - int vs size_t
      Bar13h(xp, y + 11, xp + 8, y + 12, tcolour);
    }
    ll = strlen(txt);
    k = getch();
    if (!k)
      l = getch();
    if (!k)
      switch (l) {
      case 75:
        if (cx > 0)
          cx--;
        break;
      case 77:
        if (cx < ll)
          cx++;
        break;
      case 71:
        cx = 0;
        break;
      case 79:
        cx = ll;
        break;
      case 83:
        if (cx < ll)
          for (a = cx; a < ll; a++)
            txt[a] = txt[a + 1];
        break;
      }
    else
      switch (k) {
      case 8:
        if (cx > 0) {
          cx--;
          for (a = cx; a < ll; a++)
            txt[a] = txt[a + 1];
        }
        break;
      default:
        if ((k > 31) && (ll < maxdl) && (xp < maxx + x - 18)) {
          // PORT: nawiasy grupujace && (clang -Wlogical-op-parentheses);
          // && wiaze mocniej, semantyka bez zmian
          if ((k == ' ') || ((k >= '0') && (k <= 'z') &&
                             ((k < ';') || (k > '@')) &&
                             ((k < '[') || (k >= 'a')))) {
            for (a = ll + 1; a > cx; a--)
              txt[a] = txt[a - 1];
            txt[cx] = k;
            cx++;
          }
          break;
        }
      }
  } while ((k != 13) && (k != 27));
  POL_SetTextInputMode(0); // PORT: wyjscie z edycji tekstu -> WASD znowu strzalki
  return (k);
}
/////////////////////////////////////////////////////////////////
//
///////////////////////////////////////////////////////////////
void OpenGraphicFile() {
  char ss[50];
  sprintf(ss, "graf.dat");
  graphicfile = fopen(ss, "rb");
}
/////////////////////////////////////////////////////////////////
//
///////////////////////////////////////////////////////////////
void CloseGraphicFile() { fclose(graphicfile); }

/////////////////////////////////////////////////////////////////
//
///////////////////////////////////////////////////////////////
void OpenPaletteFile() {
  char ss[50];
  sprintf(ss, "pal.dat");
  palettefile = fopen(ss, "rb");
}
/////////////////////////////////////////////////////////////////
//
///////////////////////////////////////////////////////////////
void ClosePaletteFile() { fclose(palettefile); }

/////////////////////////////////////////////////////////////////
//
///////////////////////////////////////////////////////////////
void LoadExtendedPalette(int pal) {
  long whence = pal * 768;
  if (rgb == NULL)
    return;
  if (palettefile == NULL)
    return;
  fseek(palettefile, whence, 0);
  fread(rgb, 768, 1, palettefile);
  // PORT: granice prowincji - paleta 6 dyskietki ma biel w 232-255, wstawiamy
  // odcienie z rewizji CD (raporty/kolory-granic-prowincji.md).
  if (pal == 6) {
    // Rewizja dyskietkowa PAL.DAT ("Powrot Mirka", dysk/GRY/POLANIE) ma wpisy
    // 148 i 232-255 wypalone do czystej bieli, wiec granice plemion na mapie
    // podboju (PutImageChange13h: ACTIVE=233 -> kolorK 233-248, RED=148)
    // wychodza biale i bez pulsowania odcieniem. Ponizej wartosci z rewizji CD
    // (jprok_pliki/rozpakowane/polanie_cd/PAL.DAT, paleta 6) - te same indeksy
    // maja tam kolory plemion i odcienie pulsu. Format: surowe 0-255 jak w
    // buforze rgb (>>2 do DAC robi SetExtendedPalette, 6->8 bitow POL_Dac6To8
    // - lancuch identyczny jak w DOS). Paleta 6 uzywana wylacznie przez
    // NextConquest (mapa.cpp); pozostale palete (0-5, 7-12) nietkniete.
    static const unsigned char pal6CD[18][3] = {
        {0xE0, 0x00, 0x00}, /* 148 - czerwony, Polanie (RED) */
        {0x00, 0x8C, 0xFF}, /* 232 */
        {0xB3, 0x92, 0x00}, /* 233 - zolty, kontur (ACTIVE) */
        {0xCC, 0xA6, 0x00}, /* 234 */
        {0xE6, 0xBF, 0x00}, /* 235 */
        {0xFF, 0xFF, 0x00}, /* 236 - zolci (YELLOW) */
        {0x80, 0x80, 0x80}, /* 237 */
        {0x99, 0x99, 0x99}, /* 238 */
        {0xB3, 0xB3, 0xB3}, /* 239 */
        {0xCC, 0xCC, 0xCC}, /* 240 - szarzy (GRAY) */
        {0x00, 0x00, 0xE6}, /* 241 */
        {0x52, 0x52, 0xFF}, /* 242 */
        {0x00, 0xA1, 0xE6}, /* 243 */
        {0x00, 0xC4, 0xFF}, /* 244 - niebiescy (BLUE) */
        {0x00, 0x99, 0x00}, /* 245 */
        {0x00, 0xB3, 0x00}, /* 246 */
        {0x00, 0xCC, 0x00}, /* 247 */
        {0x00, 0xFF, 0x00}, /* 248 - zieloni (GREEN) */
    };
    static const unsigned char pal6Idx[18] = {148, 232, 233, 234, 235, 236, 237,
                                              238, 239, 240, 241, 242, 243, 244,
                                              245, 246, 247, 248};
    for (int i = 0; i < 18; i++)
      for (int c = 0; c < 3; c++)
        rgb[pal6Idx[i] * 3 + c] = (char)pal6CD[i][c];
  }
}

void LoadPalette13h(char *name) {
  FILE *f = fopen(name, "rb");
  if (f == NULL)
    return;
  if (rgb == NULL)
    return;
  if (palettefile == NULL)
    return;
  fread(rgb, 768, 1, f);
  fclose(f);
}

//////////////////////////////////////////////////////////
//
//////////////////////////////////////////////////////////
void SetExtendedPalette() {
  if (rgb == NULL)
    return;
  char *Palette = (char *)rgb;
  /* This function sets the palette (will work on any VGA or SVGA card) by
     programming the card directly.  There IS a BIOS function to do it, but
     doing it directly is both quicker and - for protected
          mode programs - more straightforward   */

  short int i;
  // PORT: usuniete nieuzywane "char *p; // far" (clang -Wunused-variable)

  // PORT: char w g++ jest ze znakiem - wartosci 128..255 (0xFF>>2=-1) dawaly
  // 0xFF zamiast 0x3F w DAC; rzutujemy na unsigned char, jak w oryginale
  // (Watcom: plain char unsigned). Bez tego palety >127 przygaszaly zle.
  for (i = 0; i < 768; i++)
    Palette[i] = (char)(((unsigned char)Palette[i]) >> 2);

  // PORT: paleta -> SDL (wartosci DAC VGA 0-63)
  POL_SetPalette((const unsigned char *)Palette);

  return;
}
/////////////////////////////////////////////////////////////////
void DownPalette(int speed) {
  if (rgb == NULL) {
    return;
  }
  if (Rgb == NULL) {
    SetExtendedPalette();
    return;
  }
  // przepisanie rgb->Rgb i wyzerowanie rgb
  for (int x = 0; x < 768; x++)
    Rgb[x] = rgb[x] << 2;
  //  ---- zciemnienie palety
  for (int i = 1; i < 128; i++) {
    for (int x = 0; x < 768; x++) {
      if (Rgb[x] > i)
        rgb[x] = Rgb[x] - i;
      else
        rgb[x] = 0;
    }
    SetExtendedPalette();
    delay(speed);
  }
  BlackPalette();
}
//////////////////////////////////////////////////////////////////////
void BlackPalette(void) {
  if (rgb == NULL)
    return;
  memset(rgb, 0, 768);
  SetExtendedPalette();
}
/////////////////////////////////////////////////////////////////
void RisePalette(int speed) {
  if (rgb == NULL) {
    return;
  }
  if (Rgb == NULL) {
    SetExtendedPalette();
    return;
  }
  // przepisanie rgb->Rgb i wyzerowanie rgb
  for (int x = 0; x < 768; x++)
    Rgb[x] = rgb[x];
  //  ---- rozjasnienie palety
  for (int i = 128; i > 0; i--) {
    for (int x = 0; x < 768; x++) {
      if (Rgb[x] > i)
        rgb[x] = Rgb[x] - i;
      else
        rgb[x] = 0;
    }
    SetExtendedPalette();
    delay(speed);
  }
  for (int x = 0; x < 768; x++) // PORT: w C++ zmienna z petli wyzej nie zyje
    rgb[x] = Rgb[x];
  SetExtendedPalette();
}
//////////////////////////////////////////////////////////////////////
