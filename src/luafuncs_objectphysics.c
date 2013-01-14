
/* blitwizard 2d engine - source code file

  Copyright (C) 2011-2012 Jonas Thiem

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

/// Blitwizard namespace
// @author Jonas Thiem  (jonas.thiem@gmail.com)
// @copyright 2011-2013
// @license zlib
// @module blitwizard

#if (defined(USE_PHYSICS2D) || defined(USE_PHYSICS3D))

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include "luaheader.h"

#include "logging.h"
#include "luaerror.h"
#include "luastate.h"
#include "blitwizardobject.h"
#include "physics.h"
#include "objectphysicsdata.h"
#include "luafuncs_object.h"
#include "luafuncs_objectphysics.h"
#include "main.h"
/// Blitwizard object which represents an 'entity' in the game world
/// with visual representation, behaviour code and collision shape.
// @type object

// Put the collision callback of the given object on stack
static void luafuncs_pushcollisioncallback(lua_State* l,
struct blitwizardobject* obj) {
    char funcname[200];
    snprintf(funcname, sizeof(funcname), "collisioncallback%p", obj);
    funcname[sizeof(funcname)-1] = 0;
    lua_pushstring(l, funcname);
    lua_gettable(l, LUA_REGISTRYINDEX);
}

// Attempt to trigger a user-defined collision callback for a given object.
// When no callback is set by the user or if the callback succeeds,
// 1 will be returned. 
// In case of a lua error in the callback, 0 will be returned and a
// traceback printed to stderr.
static int luafuncs_trycollisioncallback(struct blitwizardobject* obj, struct blitwizardobject* otherobj, double x, double y, double z, double normalx, double normaly, double normalz, double force, int* enabled, int use3d) {
    // get global lua state we use for blitwizard (no support for multiple
    // states as of now):
    lua_State* l = luastate_GetStatePtr();

    // obtain the collision callback:
    luafuncs_pushcollisioncallback(l, obj);

    // check if the collision callback is not nil (-> defined):
    if (lua_type(l, -1) != LUA_TNIL) {
        // we got a collision callback for this object -> call it
        lua_pushcfunction(l, (lua_CFunction)internaltracebackfunc());
        lua_insert(l, -2);

        // push all args:
        luafuncs_pushbobjidref(l, otherobj);
        lua_pushnumber(l, x);
        lua_pushnumber(l, y);
        if (use3d) {
            lua_pushnumber(l, z);
        }
        lua_pushnumber(l, normalx);
        lua_pushnumber(l, normaly);
        if (use3d) {
            lua_pushnumber(l, normalz);
        }
        lua_pushnumber(l, force);

        // Call the function:
        int ret = lua_pcall(l, 6+2*use3d, 1, -(8+2*use3d));
        if (ret != 0) {
            callbackerror(l, "<blitwizardobject>:onCollision", lua_tostring(l, -1));
            lua_pop(l, 2); // pop error string, error handling function
            return 0;
        } else {
            // evaluate return result...
            if (!lua_toboolean(l, -1)) {
                *enabled = 0;
            }

            // pop error handling function and return value:
            lua_pop(l, 2);
        }
    } else {
        // callback was nil and not defined by user
        lua_pop(l, 1); // pop the nil value
    }
    return 1;
}

static int luafuncs_trycollisioncallback3d(struct blitwizardobject* obj, struct blitwizardobject* otherobj, double x, double y, double z, double normalx, double normaly, double normalz, double force, int* enabled) {
    return luafuncs_trycollisioncallback(obj, otherobj, x, y, z, normalx, normaly, normalz, force, enabled, 1);
}

static int luafuncs_trycollisioncallback2d(struct blitwizardobject* obj, struct blitwizardobject* otherobj, double x, double y, double normalx, double normaly, double force, int* enabled) {
    return luafuncs_trycollisioncallback(obj, otherobj, x, y, 0, normalx, normaly, 0, force, enabled, 0);
}

// This function can throw lua out of memory errors (but no others) and should
// therefore be pcall'ed. Since we don't handle out of memory sanely anyway,
// it isn't pcalled for now: FIXME
// This function gets the information about two objects colliding, and will
// subsequently attempt to call both object's collision callbacks.
// 
// The user callbacks can decide that the collision shouldn't be handled,
// in which case this function will return 0. Otherwise, it will return 1.
// If a lua error happens in the user callbacks (apart from out of memory),
// it will instant-quit blitwizard with backtrace (it will never return).
int luafuncs_globalcollision2dcallback_unprotected(void* userdata, struct physicsobject* a, struct physicsobject* b, double x, double y, double normalx, double normaly, double force) {
    // we want to track if any of the callbacks wants to ignore the collision:
    int enabled = 1;

    // get the associated blitwizard objects to the collision objects:
    struct blitwizardobject* aobj = (struct blitwizardobject*)physics_GetObjectUserdata(a);
    struct blitwizardobject* bobj = (struct blitwizardobject*)physics_GetObjectUserdata(b);
    
    // call first object's callback:
    if (!luafuncs_trycollisioncallback2d(aobj, bobj, x, y, normalx, normaly, force, &enabled)) {
        // a lua error happened and backtrace was spilled out -> ignore and continue
    }

    // call second object's callback:
    if (!luafuncs_trycollisioncallback2d(bobj, aobj, x, y, -normalx, -normaly, force, &enabled)) {
        // a lua error happened in the callback was spilled out -> ignore and continue
    }

    // if any of the callbacks wants to ignore the collision, return 0:
    if (!enabled) {
        return 0;
    }
    return 1;
}

int luafuncs_globalcollision3dcallback_unprotected(void* userdata, struct physicsobject* a, struct physicsobject* b, double x, double y, double z, double normalx, double normaly, double normalz, double force) {
    // we want to track if any of the callbacks wants to ignore the collision:
    int enabled = 1;

    // get the associated blitwizard objects to the collision objects:
    struct blitwizardobject* aobj = (struct blitwizardobject*)physics_GetObjectUserdata(a);
    struct blitwizardobject* bobj = (struct blitwizardobject*)physics_GetObjectUserdata(b);

    // call first object's callback:
    if (!luafuncs_trycollisioncallback3d(aobj, bobj, x, y, z, normalx, normaly, normalz, force, &enabled)) {
        // a lua error happened and backtrace was spilled out -> ignore and continue
    }

    // call second object's callback:
    if (!luafuncs_trycollisioncallback3d(bobj, aobj, x, y, z, -normalx, -normaly, -normalz, force, &enabled)) {
        // a lua error happened in the callback was spilled out -> ignore and continue
    }

    // if any of the callbacks wants to ignore the collision, return 0:
    if (!enabled) {
        return 0;
    }
    return 1;
}

int luafuncs_enableCollision(lua_State* l, int movable) {
    struct blitwizardobject* obj = toblitwizardobject(l, 1, 1,
    "blitwizard.object:enableCollision");

    if (obj->deleted) {
        lua_pushstring(l, "Object was deleted");
        return lua_error(l);
    }

    // validate: parameters need to be a list of shape info tables
    int argcount = lua_gettop(l)-1;
    if (argcount <= 0) {
        return haveluaerror(l, badargument1, 2,
        "blitwizard.object:enableCollision", "table", "nil");
    } else {
        // check for args to be a table
        int i = 0;
        while (i < argcount) {
            if (lua_type(l, 2+i) != LUA_TTABLE &&
            lua_type(l, 2+i) != LUA_TNIL) {
                if (i == 0) {
                    return haveluaerror(l, badargument1, 2+i,
                    "blitwizard.object:enableCollision", "table",
                    lua_strtype(l, 2+i));
                } else {
                    return haveluaerror(l, badargument1, 2+i,
                    "blitwizard.object:enableCollision", "table or nil",
                    lua_strtype(l, 2+i));
                }
            }
            i++;
        }
    }

    // construct a shape list from the given shape tables:
    struct physicsobjectshape* shapes =
    physics_CreateEmptyShapes(argcount);
    int i = 0;
    while (i < argcount) {
        lua_pushstring(l, "type");
        lua_gettable(l, 2+i);
        if (lua_type(l, -1) != LUA_TSTRING) {
            physics_DestroyShapes(shapes, argcount);
            return haveluaerror(l, badargument2, 2+i,
            "blitwizard.object:enableCollision",
            "shape has invalid type: expected string");
        } else {
            // check the shape type being valid:
            const char* shapetype = lua_tostring(l, -1);
            if (obj->is3d) {
                // see if this is a usable 3d shape:
                int isok = 0;
                if (strcmp(shapetype, "decal") == 0) {
                    isok = 1;
                    // flat 3d decal with width and height
                    int width,height;
                    lua_pushstring(l, "width");
                    lua_gettable(l, 2+i);
                    if (lua_type(l, -1) != LUA_TNUMBER) {
                        physics_DestroyShapes(shapes, argcount);
                        return haveluaerror(l, badargument2, 2+i,
                        "blitwizard.object:enableCollision",
                        "shape \"decal\" needs \"width\" specified"
                        " as a number");
                    }
                    width = lua_tonumber(l, -1);
                    lua_pop(l, 1);
                    lua_pushstring(l, "height");
                    lua_gettable(l, 2+i);
                    if (lua_type(l, -1) != LUA_TNUMBER) {
                        physics_DestroyShapes(shapes, argcount);
                        return haveluaerror(l, badargument2, 2+i,
                        "blitwizard.object:enableCollision",
                        "shape \"decal\" needs \"height\" specified"
                        " as a number");
                    }
                    height = lua_tonumber(l, -1);
                    lua_pop(l, 1);
                    physics_Set3dShapeDecal(GET_SHAPE(shapes, i),
                    width, height);
                }
                if (strcmp(shapetype, "ball") == 0) {
                    isok = 1;
                    // 3d ball with diameter
                    int diameter;
                    lua_pushstring(l, "diameter");
                    lua_gettable(l, 2+i);
                    if (lua_type(l, -1) != LUA_TNUMBER) {
                        physics_DestroyShapes(shapes, argcount);
                        return haveluaerror(l, badargument2, 2+i,
                        "blitwizard.object:enableCollision",
                        "shape \"ball\" needs \"diameter\" specified"
                        " as a number");
                    }
                    diameter = lua_tonumber(l, -1);
                    lua_pop(l, 1);
                    physics_Set3dShapeBall(GET_SHAPE(shapes, i),
                    diameter);
                }
                if (strcmp(shapetype, "box") == 0 ||
                strcmp(shapetype, "elliptic ball") == 0) {
                    isok = 1;
                    // box or elliptic ball with x/y/z_size
                    int x_size,y_size,z_size;
                    lua_pushstring(l, "x_size");
                    lua_gettable(l, 2+i);
                    if (lua_type(l, -1) != LUA_TNUMBER) {
                        physics_DestroyShapes(shapes, argcount);
                        return haveluaerror(l, badargument2, 2+i,
                        "blitwizard.object:enableCollision",
                        "shape \"box\" or \"elliptic ball\" needs"
                        " \"x_size\" specified as a number");
                    }
                    x_size = lua_tonumber(l, -1);
                    lua_pop(l, 1);

                    lua_pushstring(l, "y_size");
                    lua_gettable(l, 2+i);
                    if (lua_type(l, -1) != LUA_TNUMBER) {
                        physics_DestroyShapes(shapes, argcount);
                        return haveluaerror(l, badargument2, 2+i,
                        "blitwizard.object:enableCollision",
                        "shape \"box\" or \"elliptic ball\" needs"
                        " \"y_size\" specified as a number");
                    }
                    y_size = lua_tonumber(l, -1);
                    lua_pop(l, 1);

                    lua_pushstring(l, "y_size");
                    lua_gettable(l, 2+i);
                    if (lua_type(l, -1) != LUA_TNUMBER) {
                        physics_DestroyShapes(shapes, argcount);
                        return haveluaerror(l, badargument2, 2+i,
                        "blitwizard.object:enableCollision",
                        "shape \"box\" or \"elliptic ball\" needs"
                        " \"y_size\" specified as a number");
                    }
                    z_size = lua_tonumber(l, -1);
                    lua_pop(l, 1);

                    if (strcmp(shapetype, "box") == 0) {
                        physics_Set3dShapeBox(GET_SHAPE(shapes, i),
                        x_size, y_size, z_size);
                    } else {
                        physics_Set3dShapeBox(GET_SHAPE(shapes, i),
                        x_size, y_size, z_size);
                    }
                }
                if (!isok) {
                    // not a valid shape for a 3d object
                    char invalidshape[50];
                    snprintf(invalidshape, sizeof(invalidshape),
                    "not a valid shape for a 3d object: \"%s\"", shapetype);
                    invalidshape[sizeof(invalidshape)-1] = 0;
                    physics_DestroyShapes(shapes, argcount);
                    return haveluaerror(l, badargument2, 2+i,
                    "blitwizard.object:enableCollision",
                    invalidshape);
                }
            } else {
                // see if this is a usable 2d shape:
                int isok = 0;
                if (strcmp(shapetype, "rectangle") == 0 ||
                strcmp(shapetype, "oval") == 0) {
                    isok = 1;
                    // rectangle or oval with width and height
                    int width,height;
                    lua_pushstring(l, "width");
                    lua_gettable(l, 2+i);
                    if (lua_type(l, -1) != LUA_TNUMBER) {
                        physics_DestroyShapes(shapes, argcount);
                        return haveluaerror(l, badargument2, 2+i,
                        "blitwizard.object:enableCollision",
                        "shape \"rectangle\" or \"oval\" needs"
                        " \"width\" specified as a number");
                    }
                    width = lua_tonumber(l, -1);
                    lua_pop(l, 1);

                    lua_pushstring(l, "height");
                    lua_gettable(l, 2+i);
                    if (lua_type(l, -1) != LUA_TNUMBER) {
                        physics_DestroyShapes(shapes, argcount);
                        return haveluaerror(l, badargument2, 2+i,
                        "blitwizard.object:enableCollision",
                        "shape \"rectangle\" or \"oval\" needs"
                        " \"height\" specified as a number");
                    }
                    height = lua_tonumber(l, -1);
                    lua_pop(l, 1);

                    if (strcmp(shapetype, "oval") == 0) {
                        physics_Set2dShapeOval(GET_SHAPE(shapes, i),
                        width, height);
                    } else {
                        physics_Set2dShapeRectangle(GET_SHAPE(shapes, i),
                        width, height);
                    }
                }
                if (strcmp(shapetype, "circle") == 0) {
                    isok = 1;
                    // rectangle or oval with width and height
                    int diameter;
                    lua_pushstring(l, "diameter");
                    lua_gettable(l, 2+i);
                    if (lua_type(l, -1) != LUA_TNUMBER) {
                        physics_DestroyShapes(shapes, argcount);
                        return haveluaerror(l, badargument2, 2+i,
                        "blitwizard.object:enableCollision",
                        "shape \"circle\" needs \"diameter\" specified"
                        " as a number");
                    }
                    diameter = lua_tonumber(l, -1);
                    lua_pop(l, 1);

                    physics_Set2dShapeCircle(GET_SHAPE(shapes, i),
                    diameter);
                }
                if (!isok) {
                    // not a valid shape for a 2d object
                    char invalidshape[50];
                    snprintf(invalidshape, sizeof(invalidshape),
                    "not a valid shape for a 2d object: \"%s\"", shapetype);
                    invalidshape[sizeof(invalidshape)-1] = 0;
                    physics_DestroyShapes(shapes, argcount);
                    return haveluaerror(l, badargument2, 2+i,
                    "blitwizard.object:enableCollision",
                    invalidshape);
                }
            }
            lua_pop(l, 1);  // pop shapetype string
        }            
        i++;
    } 

    // prepare physics data:
    if (!obj->physics) {
        obj->physics = malloc(sizeof(struct objectphysicsdata));
        memset(obj->physics, 0, sizeof(obj->physics));
    }

    // delete old physics representation if any:
    if (obj->physics->object) {
        physics_DestroyObject(obj->physics->object);
    }

    // create a physics object from the shapes:
    obj->physics->object = physics_CreateObject(main_DefaultPhysics3dPtr(),
    obj, movable, shapes);
    physics_DestroyShapes(shapes, argcount);

    obj->physics->movable = 1;
    return 1;
}

/// This is how you should submit shape info to object:enableStaticCollision and object:enableMovableCollision (THIS TABLE DOESN'T EXIST, it is just a guide on how to construct it yourself)
// @tfield string type The shape type, for 2d shapes: "rectangle", "circle", "oval", "polygon" (needs to be convex!), "edge list" (simply a list of lines that don't need to be necessarily connected as it is for the polygon), for 3d shapes: "decal" (= 3d rectangle), "box", "ball", "elliptic ball" (deformed ball with possibly non-uniform radius, e.g. rather a capsule), "triangle mesh" (a list of 3d triangles)
// @tfield number width required for "rectangle", "oval" and "decal"
// @tfield number height required for "rectangle", "oval" and "decal"
// @tfield number diameter required for "circle" and "ball"
// @tfield number x_size required for "box" and "elliptic ball"
// @tfield number y_size required for "box" and "elliptic ball"
// @tfield number z_size required for "box" and "elliptic ball"
// @tfield table points required for "polygon": a list of two pair coordinates which specify the corner points of the polygon, e.g. [ [ 0, 0 ], [ 1, 0 ], [ 0, 1 ] ]  (keep in mind the polygon needs to be convex!)
// @tfield table edges required for "edge list": a list of edges, whereas an edge is itself a 2-item list of two 2d points, each 2d point being a list of two coordinates. Example: [ [ [ 0, 0 ], [ 1, 0 ] ], [ [ 0, 1 ], [ 1, 1 ] ] ] 
// @tfield table triangles required for "triangle mesh": a list of triangles, whereas a triangle is itself a 3-item list of three 3d points, each 3d point being a list of three coordinates.
// @tfield number x_offset (optional) x coordinate offset for any 2d or 3d shape, defaults to 0
// @tfield number y_offset (optional) y coordinate offset for any 2d or 3d shape, defaults to 0
// @tfield number z_offset (optional) z coordinate offset for any 3d shape, defaults to 0
// @tfield number rotation (optional) rotation of any 2d shape 0..360 degree (defaults to 0)
// @tfield number rotation_pan (optional) rotation of any 3d shape 0..360 degree left and right (horizontally)
// @tfield number rotation_tilt (optional) rotation of any 3d shape 0..360 degree up and down, applied after horizontal rotation
// @tfield number rotation_roll (optional) rotation of any 3d shape 0..360 degree around itself while remaining faced forward (basically overturning/leaning on the side), applied after the horizontal and vertical rotations
// @table shape_info

/// Enable the physics simulation on the given object and allow other
// objects to collide with it. The object itself will remain static
// - this is useful for immobile level geometry. You will be required to
// provide shape information that specifies the desired collision shape
// of the object (not necessarily similar to its visual appearance).
// @function enableStaticCollision
// @tparam table shape_info a @{object:shape_info|shape_info} table with info for a given physics shape. Note: you can add more shape info tables as additional parameters following this one - the final collision shape will consist of all overlapping shapes
int luafuncs_enableStaticCollision(lua_State* l) {
    return luafuncs_enableCollision(l, 0);
}

/// Enable the physics simulation on the given object and make it
// movable and collide with other movable and static objects.
// You will be required to
// provide shape information that specifies the desired collision shape
// of the object (not necessarily similar to its visual appearance).
//
// Note: some complex shape types are unavailable for
// movable objects, and some shapes (very thin/long, very complex or
// very tiny or huge) can be unstable. Create all your movable objects
// roughly of sizes between 0.1 and 10 to avoid instability.
// @function enableMovableCollision
// @tparam table shape_info a @{object:shape_info|shape_info} table with info for a given physics shape. Note: you can add more shape info tables as additional parameters following this one - the final collision shape will consist of all overlapping shapes
int luafuncs_enableMovableCollision(lua_State* l) {
    return luafuncs_enableCollision(l, 1);
}

/// Disable the physics simulation on an object. It will no longer collide
// with anything.
// @function disableCollision
int luafuncs_disableCollision(lua_State* l) {
    struct blitwizardobject* obj = toblitwizardobject(l, 1, 1,
    "blitwizard.object:disableCollision");
    assert(obj->refcount > 0);

    if (obj->deleted) {
        lua_pushstring(l, "Object was deleted");
        return lua_error(l);
    }
    if (!obj->physics) {
        // no physics info was set, ignore
        return 0;
    }

    if (obj->physics->object) {
        physics_DestroyObject(obj->physics->object);
        obj->physics->object = NULL;
    }
    return 0;
}

int luafuncs_freeObjectPhysicsData(struct objectphysicsdata* d) {
    // free the given physics data
    if (d->object) {
        // void collision callback
        /*char funcname[200];
        snprintf(funcname, sizeof(funcname), "collisioncallback%p",
        d->object);
        funcname[sizeof(funcname)-1] = 0;
        lua_pushstring(l, funcname);
        lua_pushnil(l);
        lua_settable(l, LUA_REGISTRYINDEX);*/

        // delete physics body
        physics_DestroyObject(d->object);
        d->object = NULL;
    }
    free(d);
    return 0;
}

static void applyobjectsettings(struct blitwizardobject* obj) {
    if (!obj->physics->object) {
        return;
    }
    if (obj->is3d) {
        if (obj->physics->rotationrestriction3dfull) {
            physics_Set3dRotationRestrictionAllAxis(obj->physics->object);
        } else {
            if (obj->physics->rotationrestriction3daxis) {
                physics_Set3dRotationRestrictionAroundAxis(
                obj->physics->object,
                obj->physics->rotationrestriction3daxisx,
                obj->physics->rotationrestriction3daxisy,
                obj->physics->rotationrestriction3daxisz);
            } else {
                physics_Set3dNoRotationRestriction();
            }
        }
    } else {
        physics_Set2dRotationRestriction(obj->physics->object,
        obj->physics->rotationrestriction2d);
    }
    physics_SetRestitution(obj->physics->object, obj->physics->restitution);
    physics_SetFriction(obj->physics->object, obj->physics->friction);
    physics_SetAngularDamping(obj->physics->object,
    obj->physics->angulardamping);
    physics_SetLinearDamping(obj->physics->object,
    obj->physics->lineardamping);
}

int luafuncs_impulse(lua_State* l) {
    struct blitwizardobject* obj = toblitwizardobject(l, 1, 1,
    "blitwizard.object:impulse");
    if (obj->deleted) {
        lua_pushstring(l, "Object was deleted");
        return lua_error(l);
    }
    if (!obj->physics->object) {
        lua_pushstring(l, "Object has no physics shape");
        return lua_error(l);
    }
    if (!obj->physics->movable) {
        lua_pushstring(l, "Impulse can be only applied to movable objects");
        return lua_error(l);
    }
    if (lua_type(l, 2) != LUA_TNUMBER) {
        lua_pushstring(l, "Second parameter is not a valid source x number");
        return lua_error(l);
    }
    if (lua_type(l, 3) != LUA_TNUMBER) {
        lua_pushstring(l, "Third parameter is not a valid source y number");
        return lua_error(l);
    }
    if (lua_type(l, 4) != LUA_TNUMBER) {
        lua_pushstring(l, "Fourth parameter is not a valid force x number");
        return lua_error(l);
    }
    if (lua_type(l, 5) != LUA_TNUMBER) {
        lua_pushstring(l, "Fifth parameter is not a valid force y number");
        return lua_error(l);
    }
    double sourcex = lua_tonumber(l, 2);
    double sourcey = lua_tonumber(l, 3);
    double forcex = lua_tonumber(l, 4);
    double forcey = lua_tonumber(l, 5);
    physics2d_ApplyImpulse(obj->object, forcex, forcey, sourcex, sourcey);
    return 0;
}

int luafuncs_ray(lua_State* l, int use3d) {
    if (lua_type(l, 1) != LUA_TNUMBER) {
        lua_pushstring(l, "First parameter is not a valid start x position");
        return lua_error(l);
    }
    if (lua_type(l, 2) != LUA_TNUMBER) {
        lua_pushstring(l, "Second parameter is not a valid start y position");
        return lua_error(l);
    }
    if (use3d) {
        if (lua_type(l, 3) != LUA_TNUMBER) {
            lua_pushstring(l, "Fourth parameter is not a valid start z position");
            return lua_error(l);
        }
    }
    if (lua_type(l, 3 + use3d) != LUA_TNUMBER) {
        lua_pushstring(l, "Third parameter is not a valid target x position");
        return lua_error(l);
    }
    if (lua_type(l, 4 + use3d) != LUA_TNUMBER) {
        lua_pushstring(l, "Fourth parameter is not a valid target y position");
        return lua_error(l);
    }
    if (use3d) {
        if (lua_type(l, 6) != LUA_TNUMBER) {
            lua_pushstring(l, "Fourth parameter is not a valid target z position");
            return lua_error(l);
        }
    }

    double startx = lua_tonumber(l, 1);
    double starty = lua_tonumber(l, 2);
    double startz;
    if (use3d) {
        startz = lua_tonumber(l, 3);
    }
    double targetx = lua_tonumber(l, 3+use3d);
    double targety = lua_tonumber(l, 4+use3d);
    double targetz;
    if (use3d) {
        targetz = lua_tonumber(l, 6);
    }

    struct physicsobject2d* obj;
    double hitpointx,hitpointy,hitpointz;
    double normalx,normaly,normalz;

    int returnvalue;
    if (use3d) {
        returnvalue = physics3d_Ray(main_DefaultPhysics2dPtr(), startx, starty, targetx, targety, &hitpointx, &hitpointy, &obj, &normalx, &normaly);
    } else {
        returnvalue = physics3d_Ray(main_DefaultPhysics2dPtr(), startx, starty, targetx, targety, &hitpointx, &hitpointy, &obj, &normalx, &normaly);
    }
    
    if (returnvalue) {
        // create a new reference to the (existing) object the ray has hit:
        luafuncs_pushbobjidref(l, (struct blitwizardobject*)physics2d_GetObjectUserdata(obj));

        // push the other information we also want to return:
        lua_pushnumber(l, hitpointx);
        lua_pushnumber(l, hitpointy);
        if (use3d) {
            lua_pushnumber(l, hitpointz);
        }
        lua_pushnumber(l, normalx);
        lua_pushnumber(l, normaly);
        if (use3d) {
            lua_pushnumber(l, normalz);
        }
        return 5+2*use3d;  // return it all
    }
    lua_pushnil(l);
    return 1;
}

int luafuncs_ray2d(lua_State* l) {
    return luafuncs_ray(l, 0);
}

int luafuncs_ray3d(lua_State* l) {
    return luafuncs_ray(l, 1);
}

int luafuncs_restrictRotation(lua_State* l) {
    struct blitwizardobject* obj = toblitwizardobject(l, 1, 1,
    "blitwizard.object:restrictRotation");
    if (obj->deleted) {
        lua_pushstring(l, "Object was deleted");
        return lua_error(l);
    }
    if (lua_type(l, 2) != LUA_TBOOLEAN) {
        lua_pushstring(l, "Second parameter is not a valid rotation restriction boolean");
        return lua_error(l);
    }
    if (!obj->physics->movable) {
        lua_pushstring(l, "Mass can be only set on movable objects");
        return lua_error(l);
    }
    obj->rotationrestriction = lua_toboolean(l, 2);
    applyobjectsettings(obj);
    return 0;
}

int luafuncs_setGravity(lua_State* l) {
    struct blitwizardobject* obj = toblitwizardobject(l, 1, 1,
    "blitwizard.object:restrictRotation");
    if (obj->deleted) {
        lua_pushstring(l, "Object was deleted");
        return lua_error(l);
    }
    if (!obj->object) {
        lua_pushstring(l, "Object has no shape");
        return lua_error(l);
    }

    int set = 0;
    double gx,gy;
    if (lua_gettop(l) >= 3 && lua_type(l, 3) != LUA_TNIL) {
        if (lua_type(l, 2) != LUA_TNUMBER) {
            lua_pushstring(l, "Second parameter is not a valid gravity x number");
            return lua_error(l);
        }
        if (lua_type(l, 3) != LUA_TNUMBER) {
            lua_pushstring(l, "Third parameter is not a valid gravity y number");
            return lua_error(l);
        }
        gx = lua_tonumber(l, 2);
        gy = lua_tonumber(l, 3);
        set = 1;
    }
    if (set) {
        physics2d_SetGravity(obj->object, gx, gy);
    } else {
        physics2d_UnsetGravity(obj->object);
    }
    return 0;
}

int luafuncs_setMass(lua_State* l) {
    struct blitwizardobject* obj = toblitwizardobject(l, 1, 1,
    "blitwizard.object:setMass");
    if (obj->deleted) {
        lua_pushstring(l, "Object was deleted");
        return lua_error(l);
    }
    if (!obj->physics->movable) {
        lua_pushstring(l, "Mass can be only set on movable objects");
        return lua_error(l);
    }
    if (lua_gettop(l) < 2 || lua_type(l, 2) != LUA_TNUMBER || lua_tonumber(l, 2) <= 0) {
        lua_pushstring(l, "Second parameter is not a valid mass number");
        return lua_error(l);
    }
    double centerx = 0;
    double centery = 0;
    double mass = lua_tonumber(l, 2);
    if (lua_gettop(l) >= 3 && lua_type(l, 3) != LUA_TNIL) {
        if (lua_type(l, 3) != LUA_TNUMBER) {
            lua_pushstring(l, "Third parameter is not a valid x center offset number");
            return lua_error(l);
        }
        centerx = lua_tonumber(l, 3);
    }
    if (lua_gettop(l) >= 4 && lua_type(l, 4) != LUA_TNIL) {
        if (lua_type(l, 4) != LUA_TNUMBER) {
            lua_pushstring(l, "Fourth parameter is not a valid y center offset number");
            return lua_error(l);
        }
        centery = lua_tonumber(l, 4);
    }
    if (!obj->object) {
        lua_pushstring(l, "Object has no shape");
        return lua_error(l);
    }
    physics2d_SetMass(obj->object, mass);
    physics2d_SetMassCenterOffset(obj->object, centerx, centery);
    return 0;
}

void transferbodysettings(struct physicsobject2d* oldbody, struct physicsobject2d* newbody) {
    double mass = physics2d_GetMass(oldbody);
    double massx,massy;
    physics2d_GetMassCenterOffset(oldbody, &massx, &massy);
    physics2d_SetMass(newbody, mass);
    physics2d_SetMassCenterOffset(newbody, massx, massy);
}

int luafuncs_warp(lua_State* l) {
    struct blitwizardobject* obj = toblitwizardobject(l, 1, 1,
    "blitwizard.object:warp");
    if (obj->deleted) {
        lua_pushstring(l, "Object was deleted");
        return lua_error(l);
    }
    if (!obj->object) {
        lua_pushstring(l, "Object doesn't have a shape");
        return lua_error(l);
    }
    double warpx,warpy,warpangle;
    physics2d_GetPosition(obj->object, &warpx, &warpy);
    physics2d_GetRotation(obj->object, &warpangle);
    if (lua_gettop(l) >= 2 && lua_type(l, 2) != LUA_TNIL) {
        if (lua_type(l, 2) != LUA_TNUMBER) {
            lua_pushstring(l, "Second parameter not a valid warp x position number");
            return lua_error(l);
        }
        warpx = lua_tonumber(l, 2);
    }
    if (lua_gettop(l) >= 3 && lua_type(l, 3) != LUA_TNIL) {
        if (lua_type(l, 3) != LUA_TNUMBER) {
            lua_pushstring(l, "Third parameter not a valid warp y position number");
            return lua_error(l);
        }
        warpy = lua_tonumber(l, 3);
    }
    if (lua_gettop(l) >= 4 && lua_type(l, 4) != LUA_TNIL) {
        if (lua_type(l, 4) != LUA_TNUMBER) {
            lua_pushstring(l, "Fourth parameter not a valid warp angle number");
            return lua_error(l);
        }
        warpangle = lua_tonumber(l, 4);
    }
    physics2d_Warp(obj->object, warpx, warpy, warpangle);
    return 0;
}

int luafuncs_getPosition(lua_State* l) {
    struct blitwizardobject* obj = toblitwizardobject(l, 1, 1,
    "blitwizard.object:getPosition");
    if (obj->deleted) {
        lua_pushstring(l, "Object was deleted");
        return lua_error(l);
    }
    if (!obj->object) {
        lua_pushstring(l, "Object doesn't have a shape");
        return lua_error(l);
    }
    double x,y;
    physics2d_GetPosition(obj->object, &x, &y);
    lua_pushnumber(l, x);
    lua_pushnumber(l, y);
    return 2;
}

int luafuncs_setRestitution(lua_State* l) {
    struct blitwizardobject* obj = toblitwizardobject(l, 1, 1,
    "blitwizard.object:setRestitution");
    if (obj->deleted) {
        lua_pushstring(l, "Object was deleted");
        return lua_error(l);
    }
    if (lua_type(l, 2) != LUA_TNUMBER) {
        lua_pushstring(l, "Second parameter not a valid restitution number");
        return lua_error(l);
    }
    obj->restitution = lua_tonumber(l, 2);
    applyobjectsettings(obj);
    return 0;
}

int luafuncs_setFriction(lua_State* l) {
    struct blitwizardobject* obj = toblitwizardobject(l, 1, 1,
    "blitwizard.object:setFriction");
    if (obj->deleted) {
        lua_pushstring(l, "Object was deleted");
        return lua_error(l);
    }
    if (lua_type(l, 2) != LUA_TNUMBER) {
        lua_pushstring(l, "Second parameter not a valid friction number");
        return lua_error(l);
    }
    obj->friction = lua_tonumber(l, 2);
    applyobjectsettings(obj);
    return 0;
}

int luafuncs_setLinearDamping(lua_State* l) {
    struct blitwizardobject* obj = toblitwizardobject(l, 1, 1,
    "blitwizard.object:setLinearDamping");
    if (obj->deleted) {
        lua_pushstring(l, "Object was deleted");
        return lua_error(l);
    }
    if (lua_type(l, 2) != LUA_TNUMBER) {
        lua_pushstring(l, "Second parameter not a valid angular damping number");
        return lua_error(l);
    }
    obj->lineardamping = lua_tonumber(l, 2);
    applyobjectsettings(obj);
    return 0;
}

int luafuncs_setAngularDamping(lua_State* l) {
    struct blitwizardobject* obj = toblitwizardobject(l, 1, 1,
    "blitwizard.object:setAngularDamping");
    if (obj->deleted) {
        lua_pushstring(l, "Object was deleted");
        return lua_error(l);
    }
    if (lua_type(l, 2) != LUA_TNUMBER) {
        lua_pushstring(l, "Second parameter not a valid angular damping number");
        return lua_error(l);
    }
    obj->angulardamping = lua_tonumber(l, 2);
    applyobjectsettings(obj);
    return 0;
}

int luafuncs_getRotation(lua_State* l) {
    struct blitwizardobject* obj = toblitwizardobject(l, 1, 1,
    "blitwizard.object:getRotation");
    if (obj->deleted) {
        lua_pushstring(l, "Object was deleted");
        return lua_error(l);
    }
    if (!obj->object) {
        lua_pushstring(l, "Object doesn't have a shape");
        return lua_error(l);
    }
    double angle;
    physics2d_GetRotation(obj->object, &angle);
    lua_pushnumber(l, angle);
    return 1;
}

int luafuncs_setShapeEdges(lua_State* l) {
    struct blitwizardobject* obj = toblitwizardobject(l, 1, 1,
    "blitwizard.object:setShapeEdges");
    if (obj->deleted) {
        lua_pushstring(l, "Object was deleted");
        return lua_error(l);
    }
    if (obj->object) {
        lua_pushstring(l, "Object already has a shape");
        return lua_error(l);
    }
    if (lua_gettop(l) < 2 || lua_type(l, 2) != LUA_TTABLE) {
        lua_pushstring(l, "Second parameter is not a valid edge list table");
        return lua_error(l);
    }
    if (obj->physics->movable) {
        lua_pushstring(l, "This shape is not allowed for movable objects");
        return lua_error(l);
    }

    struct physicsobject2dedgecontext* context = physics2d_CreateObjectEdges_Begin(main_DefaultPhysics2dPtr(), obj, 0, obj->friction);

    int haveedge = 0;
    double d = 1;
    while (1) {
        lua_pushnumber(l, d);
        lua_gettable(l, 2);
        if (lua_type(l, -1) != LUA_TTABLE) {
            if (lua_type(l, -1) == LUA_TNIL && haveedge) {
                break;
            }
            lua_pushstring(l, "Edge list contains non-table value or is empty");
            physics2d_DestroyObject(physics2d_CreateObjectEdges_End(context));
            return lua_error(l);
        }
        haveedge = 1;

        double x1,y1,x2,y2;
        lua_pushnumber(l, 1);
        lua_gettable(l, -2);
        x1 = lua_tonumber(l, -1);
        lua_pop(l, 1);

        lua_pushnumber(l, 2);
        lua_gettable(l, -2);
        y1 = lua_tonumber(l, -1);
        lua_pop(l, 1);

        lua_pushnumber(l, 3);
        lua_gettable(l, -2);
        x2 = lua_tonumber(l, -1);
        lua_pop(l, 1);

        lua_pushnumber(l, 4);
        lua_gettable(l, -2);
        y2 = lua_tonumber(l, -1);
        lua_pop(l, 1);

        physics2d_CreateObjectEdges_Do(context, x1, y1, x2, y2);
        lua_pop(l, 1);
        d++;
    }

    struct physicsobject2d* oldobject = obj->object;

    obj->object = physics2d_CreateObjectEdges_End(context);
    if (!obj->object) {
        lua_pushstring(l, "Creation of the edges shape failed");
        return lua_error(l);
    }

    if (oldobject) {
        transferbodysettings(oldobject, obj->object);
        physics2d_DestroyObject(oldobject);
    }
    applyobjectsettings(obj);
    return 0;
}


/*int luafuncs_setCollisionCallback(lua_State* l) {
    struct blitwizardobject* obj = toblitwizardobject(l, 1);
    if (obj->deleted) {
        lua_pushstring(l, "Object was deleted");
        return lua_error(l);
    }
    if (!obj->object) {
        lua_pushstring(l, "Object doesn't have a shape");
        return lua_error(l);
    }
    if (lua_gettop(l) < 2 || lua_type(l, 2) != LUA_TFUNCTION) {
        return haveluaerror(l, badargument1, 2, "blitwiz.physics.setCollisionCallback", "function", lua_strtype(l, 2));
    }

    if (lua_gettop(l) > 2) {
        lua_pop(l, lua_gettop(l)-2);
    }

    char funcname[200];
    snprintf(funcname, sizeof(funcname), "collisioncallback%p", obj->physics);
    funcname[sizeof(funcname)-1] = 0;
    lua_pushstring(l, funcname);
    lua_insert(l, -2);
    lua_settable(l, LUA_REGISTRYINDEX);

    return 0;
}*/


