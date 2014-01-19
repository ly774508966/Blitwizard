
/* blitwizard game engine - source code file

  Copyright (C) 2014 Jonas Thiem

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

#ifndef BLITWIZARD_GRAPHICS2DSPRITESLIST_H_
#define BLITWIZARD_GRAPHICS2DSPRITESLIST_H_

struct graphics2dsprite;

void graphics2dspriteslist_addToList(struct graphics2dsprite*
    sprite);

void graphics2dspriteslist_doForAllSpritesBottomToTop(
    int (*callback)(struct graphics2dsprite* sprite, void* userdata),
    void* userdata);

void graphics2dspriteslist_doForAllSpritesTopToBottom(
    int (*callback)(struct graphics2dsprite* sprite, void* userdata),
    void* userdata);

void graphics2dspriteslist_removeFromList(struct graphics2dsprite*
    sprite);

#endif  // BLITWIZARD_GRAPHICS2DSPRITESLIST_H_
