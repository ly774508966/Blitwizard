
/* blitwizard game engine - source code file

  Copyright (C) 2013 Jonas Thiem

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

///
// @author Jonas Thiem  (jonas.thiem@gmail.com)
// @copyright 2011-2013
// @license zlib
// @module blitwizard

#include "os.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <float.h>
#include <math.h>

#include "luaheader.h"

#include "luastate.h"
#include "luaerror.h"
#include "luafuncs_rundelayed.h"
#include "timefuncs.h"

int nextdelayedfuncid = 0;
struct timeoutfunc {
    int deleted;
    int id;
    uint64_t triggerTime;
    struct timeoutfunc* next;
};
static struct timeoutfunc* timeoutfuncs = NULL;
static uint64_t runDelayedTS = 0;
static uint64_t currentRelativeTS = 0;
static int insideRunDelayedCallback = 0;

__attribute__ ((constructor)) void luacfuncs_runDelayed_Init() {
    runDelayedTS = time_GetMilliseconds();
}

void luacfuncs_runDelayed_CleanDelayedRuns(lua_State* l) {
    if (insideRunDelayedCallback) {
        return;
    }
    struct timeoutfunc* prev = NULL;
    struct timeoutfunc* f = timeoutfuncs;
    while (f) {
        struct timeoutfunc* fnext = f->next;
        int deleted = 0;

        if (f->deleted) {
            // Remove timeout function from registry:
            char funcname[64];
            snprintf(funcname, sizeof(funcname),
            "timeoutfunc%d", f->id);
            lua_pushstring(l, funcname);
            lua_pushnil(l);
            lua_settable(l, LUA_REGISTRYINDEX);
            free(f);
        }

        if (!deleted) {
            prev = f;
        } else {
            if (prev) {
                prev->next = fnext;
            } else {
                timeoutfuncs = fnext;
            }
        }
        f = fnext;
    } 
}

void luacfuncs_runDelayed_Do() {
    lua_State* l = luastate_GetStatePtr();
    if (runDelayedTS == 0) {
        runDelayedTS = time_GetMilliseconds();
        return;
    }
    uint64_t oldTime = runDelayedTS;
    runDelayedTS = time_GetMilliseconds();
    if (oldTime < runDelayedTS) {
        // time has passed, check timeouts
        struct timeoutfunc* tf = timeoutfuncs;
        struct timeoutfunc* tfprev = NULL;
        while (tf) {
            struct timeoutfunc* tfnext = tf->next;
            if (tf->triggerTime <= runDelayedTS && !tf->deleted) {
                currentRelativeTS = tf->triggerTime;
                // obtain callback:
                char funcname[64];
                snprintf(funcname, sizeof(funcname),
                "timeoutfunc%d", tf->id);
                lua_pushstring(l, funcname);
                lua_gettable(l, LUA_REGISTRYINDEX);

                // run callback:
                insideRunDelayedCallback = 1;

                insideRunDelayedCallback = 0; 

                // remove callback function:
                lua_pushstring(l, funcname);
                lua_pushnil(l);
                lua_settable(l, LUA_REGISTRYINDEX);

                // remove trigger:
                if (tfprev) {
                    tfprev->next = tf->next;
                } else {
                    timeoutfuncs->next = tf->next;
                }
                free(tf);

                // unset current relative ts:
                currentRelativeTS = 0;
            } else {
                tfprev = tf;
            }
            tf = tfnext;
        }
    }
}

/// Cancel a function which was scheduled to be run with
// @{blitwizard.runDelayed} using the handle you received
// for it.
//
// If the function has already been executed, this function
// will return false, otherwise it will return true and
// the function won't be executed.
// @function cancelDelayedRun
// @tparam userdata handle the handle returned by @{blitwizard.runDelayed}
int luafuncs_cancelDelayedRun(lua_State* l) {
    if (lua_type(l, 1) != LUA_TUSERDATA) {
        return haveluaerror(l, badargument1, 1, "blitiwzard.cancelDelayedRun",
        "runDelayed handle", lua_strtype(l, 1));
    }
    if (lua_rawlen(l, 1) != sizeof(struct luaidref)) {
        return haveluaerror(l, badargument2, 1, "blitwizard.cancelDelayedRun",
        "not a valid runDelayed handle");
    }
    struct luaidref* idref = lua_touserdata(l, 1);
    if (!idref || idref->magic != IDREF_MAGIC
    || idref->type != IDREF_TIMEOUTHANDLE) {
        return haveluaerror(l, badargument2, 1, "blitwizard.cancelDelayedRun",
        "not a valid runDelayed handle");
    }
    int id = idref->ref.id;
    struct timeoutfunc* tf = timeoutfuncs;
    while (tf) {
        if (tf->id == id) {
            tf->deleted = 1;
            lua_pushboolean(l, 1);
            return 1;
        }
        tf = tf->next;
    }
    lua_pushboolean(l, 0);
    return 1;
}

/// This function allows scheduling a function to be executed
// after a specified time delay.
//
// blitwizard.runDelayed will return instantly, and lateron
// when the specified amount of time has passed, the function will
// be run once.
//
// If you know JavaScript, this function works similar to
// JavaScript's setTimeout.
//
// The return value of @{blitwizard.runDelayed|runDelayed}
// is a handle which you can use to cancel the
// scheduled function run with @{blitwizard.cancelDelayedRun},
// as long as it hasn't been run yet.
// @function runDelayed
// @tparam function function the function which shall be executed later
// @tparam number delay the delay in milliseconds before executing the function
// @treturn userdata handle which can be used with @{blitwizard.cancelDelayedRun|cancelDelayedRun} 
int luafuncs_runDelayed(lua_State* l) {
    // remove previous completed runDelay calls:
    luacfuncs_runDelayed_CleanDelayedRuns(l);

    // check function parameter:
    if (lua_type(l, 1) != LUA_TFUNCTION) {
        return haveluaerror(l, badargument1, 1, "luafuncs.setTimeout",
        "function", lua_strtype(l, 1));
    }

    // check delay parameter:
    if (lua_type(l, 2) != LUA_TNUMBER) {
        return haveluaerror(l, badargument1, 2, "luafuncs.setTimeout",
        "number", lua_strtype(l, 2));
    }
    int d = lua_tointeger(l, 2);
    if (d < 0) {
        return haveluaerror(l, badargument2, 2, "luafuncs.setTimeout",
        "timeout needs to be positive");
    }

    // see which id we would want to use:
    int useid = nextdelayedfuncid;
    if (nextdelayedfuncid >= INT_MAX) {
        nextdelayedfuncid = 0;
    } else {
        nextdelayedfuncid++;
    }

    // set function provided to registry:
    char funcname[256];
    snprintf(funcname, sizeof(funcname),
    "timeoutfunc%d", useid);
    if (lua_gettop(l) > 2) {  // remove all unrequired stuff
        lua_pop(l, lua_gettop(l)-2);
    }
    lua_insert(l, -2);  // move delay back to front
    lua_pushstring(l, funcname);
    lua_insert(l, -2);  // move func name in front of value
    lua_settable(l, LUA_REGISTRYINDEX);

    // allocate timeout info struct:
    struct timeoutfunc* tf = malloc(sizeof(*tf));
    if (!tf) {
        // remove function in lua registry:
        lua_pushstring(l, funcname);
        lua_pushnil(l);
        lua_insert(l, LUA_REGISTRYINDEX);
        return haveluaerror(l, "setTimeout struct allocation failed");
    }
    memset(tf, 0, sizeof(*tf));

    // set trigger info:
    tf->id = nextdelayedfuncid;
    tf->triggerTime = currentRelativeTS;
    if (tf->triggerTime == 0) {
        tf->triggerTime = runDelayedTS;
    }
    tf->triggerTime += d;

    tf->next = timeoutfuncs;
    timeoutfuncs = tf;
    return 0;
}
