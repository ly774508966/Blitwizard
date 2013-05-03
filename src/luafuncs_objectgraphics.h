
/* blitwizard game engine - source code file

  Copyright (C) 2012-2013 Jonas Thiem

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

*/

#ifndef BLITWIZARD_LUAFUNCS_OBJECTGRAPHICS_H_
#define BLITWIZARD_LUAFUNCS_OBJECTGRAPHICS_H_

#include "blitwizardobject.h"

// load object graphics. might fail, hence call this each frame.
// (you can safely call this when graphics are already loaded)
void luafuncs_objectgraphics_load(struct blitwizardobject* o,
const char* resource);

// unload object graphics. use on object destruction.
void luafuncs_objectgraphics_unload(struct blitwizardobject* o);

// check if a geometry callback needs to be fired.
// returns 1 if that is the case, 0 if not.
int luafuncs_objectgraphics_NeedGeometryCallback(
struct blitwizardobject* o);

// check if a visibility callback needs to be fired.
// returns 1 if that is the case, 0 if not.
int luafuncs_objectgraphics_NeedVisibleCallback(
struct blitwizardobject* o);

// Move graphical representation of object to correct position:
void luacfuncs_objectgraphics_updatePosition(struct blitwizardobject* o);

// get dimensions of object:
int luacfuncs_objectgraphics_getDimensions(
struct blitwizardobject* o, double *x, double *y, double *z);
// returns 1 on success and 0 on failure.
// for 2d objects, the z component won't be altered.

// inform lua graphics code of new frame:
void luacfuncs_objectgraphics_newFrame(void);

// set the object alpha (0 invisible, 1 solid)
void luacfuncs_objectgraphics_setAlpha(struct blitwizardobject* o,
double alpha);

// get the object alpha (0 invisible, 1 solid)
double luacfuncs_objectgraphics_getAlpha(struct blitwizardobject* o);

// set and unset texture clipping window:
void luacfuncs_objectgraphics_unsetTextureClipping(struct blitwizardobject* o);
void luacfuncs_objectgraphics_setTextureClipping(struct blitwizardobject* o,
size_t x, size_t y, size_t width, size_t height);

#endif  // BLITWIZARD_LUAFUNCS_OBJECTGRAPHICS_H_

