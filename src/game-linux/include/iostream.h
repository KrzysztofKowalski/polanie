// PORT: shim <iostream.h> (stary C++ Watcom) -> <iostream>
#ifndef POL_IOSTREAM_H
#define POL_IOSTREAM_H

#include <iostream>
using std::cerr;
using std::cout;
using std::endl;

// PORT: <iostream> ciagnie <cstdio>, a to robi "#undef fopen" i zabija makro
// fopen->POL_fopen z polshim.h - przywracamy je na koncu tego naglowka.
#ifndef fopen
#define fopen POL_fopen
#endif

#endif // POL_IOSTREAM_H