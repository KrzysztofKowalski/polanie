////////////////////////////////////////////////////////////////////////
// mouse.cpp
//
// PORT: wersja Linux/SDL2 - zamiast przerwania int 33h pozycje i przyciski
// myszy pochodza ze zdarzen SDL (warstwa game-linux/port). Interfejs klasy
// bez zmian (game/mouse.h); kod gry nie ruszany.
//////////////////////////////////////////////////////////////////////////////////
#include "mouse.h"
#include "port.h"
#include <conio.h>
#include <stdio.h>
#include <stdlib.h>

Mouse mouse;
// PORT: stan do logu diagnostycznego (POL_MOUSE_DEBUG)
static int dbg_but = -1, dbg_x = -1, dbg_y = -1;
//--------------------------------------------------------------
int Mouse::Ile(int button) {
  // PORT: licznik klikniec od ostatniego odczytu (0=lewy, 1=prawy)
  int c = POL_ClickCount(button);
  // PORT: log diagnostyczny - tylko przy faktycznym kliknieciu (tanie)
  static int dbg = -1;
  if (dbg < 0)
    dbg = getenv("POL_MOUSE_DEBUG") ? 1 : 0;
  if (dbg && c)
    fprintf(stderr, "MOUSE: Ile(%d)=%d (Button=%d X=%d Y=%d)\n", button, c,
            Button, X, Y);
  return c;
}
//-------------------------------------------------------------
void Mouse::TButtonUp() {
  while (Button > 0)
    TReadMouse();
}
//-----------------------------------------------------------------
void Mouse::GButtonUp() {
  while (Button > 0)
    GReadMouse();
}
//--------------------------------------------------
int Mouse::GGetMsg() {
  if (kbhit()) {
    Key = getch();
    if (!Key)
      Key = getch();
    return 1;
  }
  Key = 0;
  GReadMouse();
  return Button;
}
//-------------------------------------------
int Mouse::GetMsg13h() {
  if (kbhit()) {
    Key = getch();
    if (!Key)
      Key = getch();
    return 1;
  }
  Key = 0;
  ReadMouse13h();
  // PORT: kwirk z oryginalu ustawial Button = wartosc licznika klikniec PPM
  // (zwykle 1 - udawal LEWY przycisk). Gdy przycisk zostal juz puszczony
  // (tap PPM miedzy odpytaniami), klik musi byc raportowany jako prawy (2).
  // PORT-POPRAWKA 2026-09-04: oryginalne `Ile(1)` KONSUMOWALO licznik PPM,
  // a bitwa czyta go dopiero nizej (`ile1 = mouse.Ile(1)`, game/battle.cpp:422)
  // -> gdy lepki bit portowy juz wygasl (>200 ms, faza bez odpytywania),
  // kwirk zjadal licznik, `ile1` czytal 0 i warunek rozkazu PPM
  // (`Button==2 && ile1`, battle.cpp:1685) nigdy nie przechodzil.
  // POL_ClickPeek podglada licznik BEZ konsumpcji - klik dozywa odczytu w
  // `Ile(1)`, jak pod DOS-em (int 33h AX=0005h zyl do odczytu).
  if (!Button && POL_ClickPeek(1)) {
    Button = 2;
    static int dbg = -1;
    if (dbg < 0)
      dbg = getenv("POL_MOUSE_DEBUG") ? 1 : 0;
    if (dbg)
      fprintf(stderr, "MOUSE: kwirk PPM -> Button=2 (X=%d Y=%d, ile1=%d)\n", X,
              Y, POL_ClickPeek(1));
  }
  return Button;
}
//-------------------------------------------

int Mouse::TGetMsg() {
  if (kbhit()) {
    Key = getch();
    if (!Key)
      Key = getch();
    return 1;
  }
  Key = 0;
  TReadMouse();
  return Button;
}

//---------------------------
int Mouse::MWindow(int x1, int y1, int x2, int y2) {
  if ((Button) && (X >= x1) && (X <= x2) && (Y >= y1) && (Y <= y2))
    return 1;
  return 0;
}

int Mouse::MouseInit() {
  // PORT: mysz zawsze dostepna (zdarzenia SDL); oryginal sprawdzal int 33h
  ReadMouse13h();
  return 1;
}

void Mouse::TextCursor() {}

void Mouse::ShowCursor() {}

void Mouse::HideCursor() {}

void Mouse::TReadMouse() {
  int cx, cy, but;
  POL_MousePos(&cx, &cy, &but);
  Button = but;
  X = cx >> 3;
  Y = cy >> 3;
}

// 1234567890
void Mouse::GReadMouse() {
  int cx, cy, but;
  POL_MousePos(&cx, &cy, &but);
  Button = but;
  X = cx >> 1; // PORT: koordynaty 640x200 -> ekran 320x200
  Y = cy;
}
void Mouse::ReadMouse13h() {
  int cx, cy, but;
  POL_MousePos(&cx, &cy, &but);
  Button = but;
  X = cx >> 1;
  Y = cy;
  // PORT: tani log diagnostyczny (POL_MOUSE_DEBUG=1) - zmiana STANU PRZYCISKOW
  // zawsze; sama zmiana pozycji dlawiona do ~4/s (bez tego przy ruchu myszy
  // log zalewa stderr - setki zapisow na sekunde do niebuforowanego strumienia
  // potrafily rozdmuchac iteracje petli bitwy i zjesc kliki na bezpieczniku).
  static int dbg = -1;
  static unsigned long last_xy_log_ms = 0;
  if (dbg < 0)
    dbg = getenv("POL_MOUSE_DEBUG") ? 1 : 0;
  if (dbg && (Button != dbg_but || X != dbg_x || Y != dbg_y)) {
    unsigned long now_ms = POL_GetTicks();
    int but_changed = (Button != dbg_but);
    if (but_changed || now_ms - last_xy_log_ms >= 250) {
      dbg_but = Button;
      dbg_x = X;
      dbg_y = Y;
      last_xy_log_ms = now_ms;
      fprintf(stderr, "MOUSE: Button=%d X=%d Y=%d\n", Button, X, Y);
    }
  }
}

void Mouse::TMoveCursor(int x, int y) {
  // PORT: bez sensu w SDL (tryb tekstowy)
  (void)x;
  (void)y;
}

void Mouse::GMoveCursor(int x, int y) {
  // PORT: warp kursora do pozycji w koordynatach int 33h (640x200)
  POL_WarpMouse640(x, y);
}

void Mouse::TMWindow(int x1, int y1, int x2, int y2) {
  (void)x1;
  (void)y1;
  (void)x2;
  (void)y2;
}

void Mouse::GMWindow(int x1, int y1, int x2, int y2) {
  (void)x1;
  (void)y1;
  (void)x2;
  (void)y2;
}

// Przesuniecie poziome i pionowe
void Mouse::TMCounter(int &dx, int &dy) {
  dx = 0;
  dy = 0;
}
//-------------------------------------
void Mouse::GMCounter(int &dx, int &dy) {
  dx = 0;
  dy = 0;
}
//-----------------------------------
void Mouse::TMouseTrap(int, int, int, int) {}

void Mouse::GMouseTrap(int, int, int, int) {}

void Mouse::SetMouseSpeed(int) {}

void Mouse::SetMouseSensitiv(int, int) {}

void Mouse::SetCursorMask(int *, int, int) {}