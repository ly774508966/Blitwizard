
/* blitwizard game engine - source code file

  Copyright (C) 2011-2014 Jonas Thiem et al

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

#include "config.h"
#include "os.h"

#include "resources.h"
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <inttypes.h>
#ifdef WINDOWS
#include <windows.h>
#define _WINDOWS_
#endif
#ifdef ANDROID
#include <android/log.h>
#endif

#define MAXSCRIPTARGS 1024

// set physics callbacks:
void luacfuncs_object_initialisePhysicsCallbacks(void);

// report sprite visibility:
void graphics2dsprites_reportVisibility(void);

// lua funcs doStep processing function:
int luacfuncs_object_doAllSteps(int count);

// update all object graphics:
void luacfuncs_object_updateGraphics(void);

// informing lua graphics code of new frame:
void luacfuncs_objectgraphics_newFrame(void);

// update object physics references:
struct physicsobject;
void luacfuncs_objectphysics_replaceObjectRef(
    struct physicsobject* old, struct physicsobject* new);

// media cleanup callback:
void checkAllMediaObjectsForCleanup(void);

// counting current amount of scheduled functions (runDelayed):
size_t luacfuncs_runDelayed_getScheduledCount(void);

// trigger scheduled runDelayed functions:
void luacfuncs_runDelayed_Do(void);

// per-object mouse event handling:
void luacfuncs_objectgraphics_processMouseClick(int x, int y,
int button);
void luacfuncs_objectgraphics_processMouseMove(int x, int y);

int wantquit = 0; // set to 1 if there was a quit event
int suppressfurthererrors = 0; // a critical error was shown, don't show more
int windowisfocussed = 0;
int appinbackground = 0; // app is in background (mobile/Android)
char* templatepath = NULL; // global template directory path as determined at runtime
char* gameluapath = NULL; // game.lua path as determined at runtime
char* binpath = NULL;  // path to blitwizard binary
extern int failsafeaudio;  // whether audio is set to failsafe or not

#include "threading.h"
#include "luastate.h"
#include "file.h"
#include "timefuncs.h"
#include "audio.h"
#include "main.h"
#include "audiomixer.h"
#include "logging.h"
#include "audiosourceffmpeg.h"
#include "signalhandling.h"
#include "physics.h"
#include "connections.h"
#include "listeners.h"
#ifdef USE_SDL_GRAPHICS
#include <SDL2/SDL.h>
#endif
#include "graphicstexture.h"
#include "graphicstexturemanager.h"
#include "graphics.h"
#include "graphicsrender.h"

int TIMESTEP = 16;
int MAXLOGICITERATIONS = 50;  // 50 * 16 = 800ms
int MAXBATCHEDLOGIC = 50/3;
int MAXPHYSICSITERATIONS = 50;

void main_SetTimestep(int timestep) {
    if (timestep < 16) {
        timestep = 16;
    }
    TIMESTEP = timestep;
    MAXLOGICITERATIONS = 800 / timestep;  // never do logic ..
            // ... for longer than 800ms
    if (MAXLOGICITERATIONS < 2) {
        MAXLOGICITERATIONS = 2;
    }
    MAXBATCHEDLOGIC = MAXLOGICITERATIONS/3;
    if (MAXBATCHEDLOGIC < 2) {
        MAXBATCHEDLOGIC = 2;
    }
}

struct physicsworld* physics2ddefaultworld = NULL;
void* main_DefaultPhysics2dPtr() {
    return physics2ddefaultworld;
}

void main_Quit(int returncode) {
    listeners_CloseAll();
    // close graphics first since that is fast:
#ifdef USE_GRAPHICS
    //graphics_Quit();
#endif
    // then close audio (might hang for 1-3 seconds):
#ifdef USE_SDL_AUDIO
    //audio_Quit();
    // FIXME: make sure this is ok with
    // http://bugzilla.libsdl.org/show_bug.cgi?id=1396
#endif
    exit(returncode);
}

void fatalscripterror(void) {
    wantquit = 1;
    suppressfurthererrors = 1;
}

int simulateaudio = 0;
int audioinitialised = 0;
void main_initAudio(void) {
#ifdef USE_AUDIO
    if (audioinitialised) {
        return;
    }
    audioinitialised = 1;

    // get audio backend
    char* p = luastate_GetPreferredAudioBackend();

    // load FFmpeg if we happen to want it
    if (luastate_GetWantFFmpeg()) {
        audiosourceffmpeg_loadFFmpeg();
    } else {
        audiosourceffmpeg_disableFFmpeg();
    }

#if defined(USE_SDL_AUDIO) || defined(WINDOWS)
    char* error;

    // initialise audio - try 32bit first
    s16mixmode = 0;
#ifndef FORCES16AUDIO
    if (!audio_Init(&audiomixer_GetBuffer, 0, p, 0, &error)) {
        if (error) {
            free(error);
        }
#endif
        // try 16bit now
        s16mixmode = 1;
        if (!audio_Init(&audiomixer_GetBuffer, 0, p, 1, &error)) {
            printwarning("Warning: Failed to initialise audio: %s", error);
            if (error) {
                free(error);
            }
            // non-fatal: we will simulate audio manually:
            simulateaudio = 1;
            s16mixmode = 0;
        }
#ifndef FORCES16AUDIO
    }
#endif
    if (p) {
        free(p);
    }
#else  // USE_SDL_AUDIO || WINDOWS
    // simulate audio:
    simulateaudio = 1;
    s16mixmode = 0;
#endif  // USE_SDL_AUDIO || WINDOWS
#else // ifdef USE_AUDIO
    // we don't support any audio
    return;
#endif  // ifdef USE_AUDIO
}

void luacfuncs_onError(const char* funcname, const char* error);
static void quitevent(void) {
    char* error;
    if (!luastate_CallFunctionInMainstate("blitwizard.onClose",
    0, 1, 1, &error, NULL, NULL)) {
        luacfuncs_onError("blitwizard.onClose", error);
        if (error) {
            free(error);
        }
    }
    exit(0);
}

static void mousebuttonevent(int button, int release, int x, int y) {
#ifdef USE_GRAPHICS 
    char* error;
    char onmouseup[] = "blitwizard.onMouseUp";
    const char* funcname = "blitwizard.onMouseDown";
    if (release) {
        funcname = onmouseup;
    }
    double realx = ((double)x) / UNIT_TO_PIXELS;
    double realy = ((double)y) / UNIT_TO_PIXELS;
    if (!luastate_PushFunctionArgumentToMainstate_Double(realx)) {
        printfatalerror("Error when pushing func args to %s", funcname);
        fatalscripterror();
        main_Quit(1);
        return;
    }
    if (!luastate_PushFunctionArgumentToMainstate_Double(realy)) {
        printfatalerror("Error when pushing func args to %s", funcname);
        fatalscripterror();
        main_Quit(1);
        return;
    }
    if (!luastate_PushFunctionArgumentToMainstate_Double(button)) {
        printfatalerror("Error when pushing func args to %s", funcname);
        fatalscripterror();
        main_Quit(1);
        return;
    }
    if (!luastate_CallFunctionInMainstate(funcname, 3, 1, 1, &error, NULL,
    NULL)) {
        luacfuncs_onError(funcname, error);
        if (error) {
            free(error);
        }
    }
    if (!release) {
        luacfuncs_objectgraphics_processMouseClick(x, y, button);
    }
#endif
}
static void mousemoveevent(int x, int y) {
#ifdef USE_GRAPHICS
    char* error;
    if (!luastate_PushFunctionArgumentToMainstate_Double(x)) {
        printfatalerror("Error when pushing func args to "
            "blitwizard.onMouseMove");
        fatalscripterror();
        main_Quit(1);
        return;
    }
    if (!luastate_PushFunctionArgumentToMainstate_Double(y)) {
        printfatalerror("Error when pushing func args to "
            "blitwizard.onMouseMove");
        fatalscripterror();
        main_Quit(1);
        return;
    }
    if (!luastate_CallFunctionInMainstate("blitwizard.onMouseMove",
    2, 1, 1, &error, NULL, NULL)) {
        luacfuncs_onError("blitwizard.onMouseMove", error);
        if (error) {
            free(error);
        }
    }
    luacfuncs_objectgraphics_processMouseMove(x, y);
#endif
}

static void keyboardevent(const char* key, int release) {
    char* error;

    // We handle key up and key down,
    // and for both we ask a hidden (undocumented)
    // event function for the templates whether they
    // want to have the key event.
    //
    // (This will allow for a neat transparent
    // addition of an ingame developer console)
    const char onkeyup[] = "blitwizard.onKeyUp";
    const  char onkeyup_templates[] = "blitwizard._onKeyUp_Templates";
    const char* funcname = "blitwizard.onKeyDown";
    const char* funcname_templates = "blitwizard._onKeyDown_Templates";
    if (release) {
        funcname = onkeyup;
        funcname_templates = onkeyup_templates;
    }

    // Call function template function first:
    int returnboolean = 0;
    if (!luastate_PushFunctionArgumentToMainstate_String(key)) {
        printfatalerror("Error when pushing func args to %s",
            funcname_templates);
        fatalscripterror();
        main_Quit(1);
    }
    if (!luastate_CallFunctionInMainstate(funcname_templates, 1, 1, 1, &error,
    NULL, &returnboolean)) {
        luacfuncs_onError(funcname_templates, error);
        if (error) {
            free(error);
        }
        return;
    }

    // if the templates event function has returned true, it handles
    // the event and we don't propagate it further:
    if (returnboolean) {
        return;
    }

    // otherwise, call the regular event function now:
    if (!luastate_PushFunctionArgumentToMainstate_String(key)) {
        printfatalerror("Error when pushing func args to %s", funcname);
        fatalscripterror();
        main_Quit(1);
        return;
    }
    if (!luastate_CallFunctionInMainstate(funcname, 1, 1, 1, &error, NULL,
    NULL)) {
        luacfuncs_onError(funcname, error);
        if (error) {
            free(error);
        }
        return;
    }
}
static void textevent(const char* text) {
    char* error;

    // first, call the undocumented template pre-event function:
    int returnboolean = 0;
    if (!luastate_PushFunctionArgumentToMainstate_String(text)) {
        printfatalerror("Error when pushing func args to "
        "blitwizard._onText_Templates");
        fatalscripterror();
        main_Quit(1);
        return;
    }
    if (!luastate_CallFunctionInMainstate("blitwizard._onText_Templates",
    1, 1, 1, &error, NULL, &returnboolean)) {
        luacfuncs_onError("blitwizard._onText_Templates", error);
        if (error) {
            free(error);
        }
        return;
    }

    // now if the template event function returned true,
    // it handles the event and we don't propagate it further:
    if (returnboolean) {
        return;
    }

    // since the template event function didn't object, continue with
    // regular event function:
    if (!luastate_PushFunctionArgumentToMainstate_String(text)) {
        printfatalerror("Error when pushing func args to blitwizard.onText");
        fatalscripterror();
        main_Quit(1);
        return;
    }
    if (!luastate_CallFunctionInMainstate("blitwizard.onText",
    1, 1, 1, &error, NULL, NULL)) {
        luacfuncs_onError("blitwizard.onText", error);
        if (error) {
            free(error);
        }
        return;
    }
}

static void putinbackground(int background) {
    if (background) {
        // remember we are in the background
        appinbackground = 1;
    } else {
        // restore textures and wipe old ones
#ifdef ANDROID
        graphics_ReopenForAndroid();
#endif
        // we are back in the foreground! \o/
        appinbackground = 0;
    }
}

int attemptTemplateLoad(const char* path) {
#ifdef WINDOWS
    // check for invalid absolute unix paths:
    if (path[0] == '/' || path[0] == '\\') {
        return 0;
    }
#endif

    // path to init.lua:
    char* p = malloc(strlen(path) + 1 + strlen("init.lua") + 1);
    if (!p) {
        printfatalerror("Error: failed to allocate string when "
        "attempting to run templates init.lua");
        main_Quit(1);
        return 0;
    }
    memcpy(p, path, strlen(path));
    p[strlen(path)] = '/';
    memcpy(p+strlen(path)+1, "init.lua", strlen("init.lua"));
    p[strlen(path)+1+strlen("init.lua")] = 0;
    file_makeSlashesNative(p);

    int loadFromZip = 0;
    struct resourcelocation loc;
    if (!resource_isFolderInZip(path)) {
        // if file doesn't exist, report failure:
        if (!file_doesFileExist(p)) {
            free(p);
            return 0;
        }
    } else {
        // check for file in our .zip archives:
        loadFromZip = 1;
        if (!resources_locateResource(p, &loc)) {
            free(p);
            return 0;
        }
    }

    // update global template path:
    if (templatepath) {
        free(templatepath);
    }
    templatepath = file_getAbsolutePathFromRelativePath(path);

    // run file:
    char outofmem[] = "Out of memory";
    char* error;
    if (!luastate_DoInitialFile(p, 0, &error)) {
        if (error == NULL) {
            error = outofmem;
        }
        printfatalerror("Error: An error occured when running "
        "templates init.lua: %s", error);
        if (error != outofmem) {
            free(error);
        }
        fatalscripterror();
        free(p);
        main_Quit(1);
        return 0;
    }
    free(p);
    return 1;
}

#ifdef UNIX
static void determineBinaryPath(const char* argv0) {
    if (strlen(argv0) <= 0) {
        return;
    }

    // see if this is an absolute path or
    // definitely a relative path:
    if (argv0[0] == '/' || (strlen(argv0) > 1
    && argv0[0] == '.' && argv0[0] == '/')
    || strstr(argv0, "/")) {
        // it is. use it:
        binpath = file_getAbsolutePathFromRelativePath(argv0);
        return;
    }

    // abort if argument is possibly dangerous:
    const char* name = argv0;
    size_t i = 0;
    while (i < strlen(name)) {
        if ((name[i] < 'a' || name[i] > 'z') &&
                (name[i] < 'A' || name[i] > 'Z') &&
                (name[i] < '0' || name[i] > '9') &&
                name[i] != '_' && name[i] != '-') {
            // some weird char in there.
            return;
        }
        i++;
    }

    // ok it looks like we were globally run
    // (system-wide install) with no proper path.
    // this means we will need to search ourselves:
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "type %s", name);
    FILE* f = popen(cmd, "r");
    if (!f) {
        return;
    }
    char resultbuf[256];
    char* s = fgets(resultbuf, sizeof(resultbuf), f);
    pclose(f);
    // see if this starts with: argv0 is /some/path..
    if (!s || strlen(s) < strlen(argv0) + strlen(" is ")) {
        return;
    }
    if (memcmp(s, argv0, strlen(argv0)) != 0) {
        return;
    }
    if (memcmp(s + strlen(argv0), " is ", strlen(" is ")) != 0) {
        return;
    }
    if (s[strlen(argv0) + strlen(" is ")] == '/') {
        // yes! this looks useful.
        binpath = strdup(s + strlen(argv0) + strlen(" is "));
        if (binpath[strlen(binpath)-1] == '\n') {
            binpath[strlen(binpath)-1] = 0;
        }
    }
}
#endif

// this part remembers the directory we were told to run in
// (= where we ran from originally or -changedir'd to)
static char mainrundir[512] = "";
const char* main_getRunDir() {
    if (strlen(mainrundir) > 0) {
        return mainrundir;
    }
    return file_getCwd();
}

// this will be polled by lua (implementation in luastate.c)
// to check on whether a script is hanging for too long
// (=suspected hang)
uint64_t scriptTerminateTime = 0;
int scriptMaxRuntime = 5000;
int terminateCurrentScript(void* userdata) {
    return (time_getMilliseconds() < scriptTerminateTime);
}


int luafuncs_ProcessNetEvents(void);

// command line args:
static const char* script = "game.lua";
static int scriptargfound = 0;
static int option_changedir = 0;
static char* option_templatepath = NULL;
static int nextoptionistemplatepath = 0;
static int nextoptionisscriptarg = 0;
static int gcframecount = 0;
static char** scriptargs = NULL;
static int scriptargcount = 0;

int main_startup_do(int argc, char** argv) {
    thread_markAsMainThread();

    scriptTerminateTime = time_getMilliseconds() + scriptMaxRuntime;

#if defined(ANDROID) || defined(__ANDROID__)
    printinfo("Blitwizard %s starting", VERSION);
#endif

    // set signal handlers:
    signalhandling_Init();

    // set path to blitwizard binary:
#if (defined(UNIX) && !defined(ANDROID))
    if (argc > 0) {
        determineBinaryPath(argv[0]);
    }
#endif

    // evaluate command line arguments:

    // we want to store the script arguments so we can pass them to lua:
    scriptargs = malloc(sizeof(char*) * MAXSCRIPTARGS);
    if (!scriptargs) {
        printfatalerror("Error: failed to allocate script args space");
        return 1;
    }
    scriptargcount = 0;

    // parse command line arguments:
    int i = 1;
    while (i < argc) {
        if (!scriptargfound) { // pre-scriptname arguments
            // process template path option parameter:
            if (nextoptionistemplatepath) {
                nextoptionistemplatepath = 0;
                if (option_templatepath) {
                    free(option_templatepath);
                }
                option_templatepath = strdup(argv[i]);
                if (!option_templatepath) {
                    printfatalerror("Error: failed to strdup() template "
                    "path argument");
                    main_Quit(1);
                    return 1;
                }
                file_makeSlashesNative(option_templatepath);
                i++;
                continue;
            }

            // various options:
            if ((argv[i][0] == '-' || strcasecmp(argv[i],"/?") == 0)
            && !nextoptionisscriptarg) {
                if (strcasecmp(argv[i], "--") == 0) {
                    // this enforces the next arg to be the script name:
                    nextoptionisscriptarg = 1;
                    i++;
                    continue;
                }

                if (strcasecmp(argv[i],"--help") == 0 || strcasecmp(argv[i], "-help") == 0
                || strcasecmp(argv[i], "-?") == 0 || strcasecmp(argv[i],"/?") == 0
                || strcasecmp(argv[i],"-h") == 0) {
                    printf("blitwizard %s (C) 2011-2013 Jonas Thiem et al\n",
                    VERSION);
                    printf("Usage:\n   blitwizard [blitwizard options] "
                           "[script name] [script options]\n\n");
                    printf("The script name should be a .lua file containing\n"
                    "Lua source code for use with blitwizard.\n\n");
                    printf("The script options (optional) are passed through\n"
                    "to the script.\n\n");
                    printf("Supported blitwizard options:\n");
                    printf("   -changedir             Change working directory to "
                           "the\n"
                           "                          folder of the script\n");
                    printf("   -failsafe-audio        Use 16bit signed int audio and avoid\n"
                           "                          audio backends known as troublesome\n");
                    printf("   -help                  Show this help text and quit\n");
                    printf("   -long-execution        Default to lengthier script execution\n"
                           "                          time\n");
                    printf("   -templatepath [path]   Check another place for "
                           "templates\n"
                           "                          (not the default "
                           "\"templates/\"\n"
                           "                          or system-wide installed)\n");
                    printf("   -version               Show extended version info and quit\n");
                    return 0;
                }
                if (strcasecmp(argv[i], "-failsafe-audio") == 0) {
                    failsafeaudio = 1;
                    i++;
                    continue;
                }
                if (strcasecmp(argv[i], "-long-execution") == 0) {
                    scriptMaxRuntime = 20000;
                    scriptTerminateTime = time_getMilliseconds() +
                        scriptMaxRuntime;
                    i++;
                    continue;
                }
                if (strcasecmp(argv[i], "-changedir") == 0) {
                    option_changedir = 1;
                    i++;
                    continue;
                }
                if (strcasecmp(argv[i], "-templatepath") == 0) {
                    nextoptionistemplatepath = 1;
                    i++;
                    continue;
                }
                if (strcmp(argv[i], "-v") == 0 || strcasecmp(argv[i], "-version") == 0
                || strcasecmp(argv[i], "--version") == 0) {
                    printf("blitwizard %s (C) 2011-2013 Jonas Thiem et al\n",VERSION);
                    printf("\nSupported features of this build:\n");

                    #ifdef USE_SDL_AUDIO
                    printf("  Audio device: SDL 2\n");
                    #else
                    #ifdef USE_AUDIO
                    #ifdef WINDOWS
                    printf("  Audio device: waveOut\n");
                    #else
                    printf("  Audio device: only virtual (not audible)\n");
                    #endif
                    #else
                    printf("  Audio device: no\n");
                    printf("     Playback support: none, audio disabled\n");
                    printf("     Resampling support: none, audio disabled\n");
                    #endif
                    #endif

                    #if (defined(USE_SDL_AUDIO) || defined(USE_AUDIO))
                    printf("     Playback support: Ogg (libogg)%s%s\n",
                    #if defined(USE_FLAC_AUDIO)
                    ", FLAC (libFLAC)"
                    #else
                    ""
                    #endif
                    ,
                    #if defined(USE_FFMPEG_AUDIO)
                    #ifndef USE_FLAC_AUDIO
                    ", FLAC (FFmpeg),\n      mp3 (FFmpeg), WAVE (FFmpeg), mp4 (FFmpeg),\n      many more.. (FFmpeg)\n     (Please note FFmpeg can fail to load at runtime,\n     resulting in FFmpeg playback support not working)"
                    #else
                    ",\n      mp3 (FFmpeg), WAVE (FFmpeg), mp4 (FFmpeg),\n      many more.. (FFmpeg)\n     (Please note FFmpeg can fail to load at runtime,\n      resulting in FFmpeg playback support not working)"
                    #endif
                    #else
                    ""
                    #endif
                    );
                    #if defined(USE_SPEEX_RESAMPLING)
                    printf("     Resampling: libspeex\n");
                    #else
                    printf("     Resampling: none (non-48kHz audio will sound wrong!)\n");
                    #endif
                    #endif

                    #ifdef USE_GRAPHICS
                    #ifdef USE_SDL_GRAPHICS
                    #ifdef USE_OGRE_GRAPHICS
                    printf("  Graphics device: SDL 2, Ogre\n");
                    printf("     2d graphics support: SDL 2, Ogre\n");
                    printf("     3d graphics support: Ogre\n");
                    #else
                    printf("  Graphics device: SDL 2\n");
                    printf("     2d graphics support: SDL 2\n");
                    printf("     3d graphics support: none\n");
                    #endif
                    #else
                    printf("  Graphics device: only virtual (not visible)\n");
                    printf("     2d graphics support: virtual\n");
                    printf("     3d graphics support: none\n");
                    #endif
                    #else
                    printf("  Graphics device: none\n");
                    printf("     2d graphics support: none, graphics disabled\n");
                    printf("     3d graphics support: none, graphics disabled\n");
                    #endif
                    #if defined(USE_PHYSICS2D) || defined(USE_PHYSICS3D)
                    printf("  Physics: yes\n");
                    #else
                    printf("  Physics: no\n");
                    #endif
                    #if defined(USE_PHYSICS2D)
                    printf("     2d physics: Box2D\n");
                    #else
                    printf("     2d physics: none\n");
                    #endif
                    #if defined(USE_PHYSICS3D)
                    printf("     3d physics: bullet\n");
                    #else
                    printf("     3d physics: none\n");
                    #endif
                    #if defined(USE_PHYSFS)
                    printf("  .zip archive resource loading: yes\n");
                    #else
                    printf("  .zip archive resource loading: no\n");
                    #endif     

                    printf("\nVarious build options:\n");
                    printf("  SYSTEM_TEMPLATE_PATH:\n   %s\n",
                    SYSTEM_TEMPLATE_PATH);
                    #if defined(USE_LIB_FLAGS)
                    printf("  FINAL_USE_LIB_FLAGS:\n   %s\n",
                    USE_LIB_FLAGS);
                    #endif

                    printf("\nCheck out http://www.blitwizard.de/"
                    " for info about blitwizard.\n");

                    fflush(stdout);
                    exit(0);
                }
                printwarning("Warning: Unknown Blitwizard option: %s", argv[i]);
            } else {
                scriptargfound = 1;
                script = argv[i];
            }
        } else {
            // post-scriptname arguments -> store them for Lua
            if (scriptargcount < MAXSCRIPTARGS) {
                scriptargs[scriptargcount] = strdup(argv[i]);
                scriptargcount++;
            }
        }
        i++;
    }

#ifdef USE_AUDIO
    // This needs to be done at some point before we actually 
    // initialise audio so that the mixer is ready for use then
    audiomixer_Init();
#endif

    // if no template path was provided, default to "templates/"
    if (!option_templatepath) {
        option_templatepath = strdup("templates/");
        if (!option_templatepath) {
            printfatalerror("Error: failed to allocate initial template path");
            main_Quit(1);
            return 1;
        }
        file_makeSlashesNative(option_templatepath);
    }
    return 0;
}

int main_startup_openScript(int argc, char** argv) {
#if defined(ANDROID) || defined(__ANDROID__)
    printinfo("Blitwizard startup: locating lua start script...");
#endif

    // load internal resources appended to this binary,
    // so we can load the game.lua from it if there is any inside:
#ifdef WINDOWS
    // windows
    // try encrypted first:
    if (!resources_loadZipFromOwnExecutable(NULL, 1)) {
        // ... ok, then attempt unencrypted:
        resources_loadZipFromOwnExecutable(NULL, 0);
    }
#else
#ifndef ANDROID
    // unix systems
    // encrypted first:
    char* argv0 = NULL;
    if (argc > 0) {
        argv0 = argv[0];
    }
    if (!resources_loadZipFromOwnExecutable(argv0, 1)) {
        // ... ok, then attempt unencrypted:
        resources_loadZipFromOwnExecutable(argv0, 0);
    }
#endif
#endif

    // check the provided path:
    char outofmem[] = "Out of memory";
    char* error;
    char* filenamebuf = NULL;

    // check if provided script path is a folder:
    if (file_IsDirectory(script)) {
        // make sure it isn't inside a resource file as a proper file:
        if (!resources_locateResource(script, NULL)) {
            // it isn't, so we can safely assume it is a folder.
            // -> append "game.lua" to the path
            if (filenamebuf) {
                free(filenamebuf);
            }
            filenamebuf = file_AddComponentToPath(script, "game.lua");
            if (!filenamebuf) {
                printfatalerror("Error: failed to add component to "
                    "script path");
                main_Quit(1);
                return 1;
            }
            script = filenamebuf;
        }
    }

    // check if script file is internal resource or disk file
    int scriptdiskfile = 0;
    struct resourcelocation s;
    if (!resources_locateResource(script, &s)) {
        printfatalerror("Error: cannot locate script file \"%s\"", script);
        main_Quit(1);
        return 1;
    } else {
        if (s.type == LOCATION_TYPE_ZIP) {
            scriptdiskfile = 0;
        } else{
            scriptdiskfile = 1;
        }
    }
 
    // compose game.lua path variable (for os.gameluapath())
    if (scriptdiskfile) {
        gameluapath = file_getAbsolutePathFromRelativePath(script);
    } else {
        gameluapath = strdup(script);
    }
    if (!gameluapath) { // string allocation failed
        printfatalerror("Error: failed to allocate script path (gameluapath)");
        main_Quit(1);
        return 1;
    } else {
        if (gameluapath) {
            file_makeSlashesCrossplatform(gameluapath);
        }
    }

    // check if we want to change directory to the provided script path:
    if (option_changedir) {
        char* p = file_getAbsoluteDirectoryPathFromFilePath(script);
        if (!p) {
            printfatalerror("Error: NULL returned for absolute directory");
            main_Quit(1);
            return 1;
        }
        char* newfilenamebuf = file_GetFileNameFromFilePath(script);
        if (!newfilenamebuf) {
            free(p);
            printfatalerror("Error: NULL returned for file name");
            main_Quit(1);
            return 1;
        }
        if (filenamebuf) {
            free(filenamebuf);
        }
        filenamebuf = newfilenamebuf;
        if (!file_Cwd(p)) {
            free(filenamebuf);
            printfatalerror("Error: Cannot cd to \"%s\"", p);
            free(p);
            main_Quit(1);
            return 1;
        }
        free(p);
        script = filenamebuf;
    }

    // now that changedir was evaluated, set the mainrundir:
    char *cwd = file_getCwd();
    if (cwd) {
        strncpy(mainrundir, cwd, sizeof(mainrundir)-1);
        mainrundir[sizeof(mainrundir)-1] = 0;
        free(cwd);
    }
    return 0;
}

static uint64_t lastFPSmeasurement = 0;
static int FPSframeCounter = 0;
double measuredFPS = 0;
static void measureFPS(void) {
    if (lastFPSmeasurement == 0) {
        lastFPSmeasurement = time_getMilliseconds();
        return;
    }
    uint64_t now = time_getMilliseconds();
    // don't continue if time frame passed is too short:
    if (now - lastFPSmeasurement < 500) {
        return;
    }
    // measure the FPS:
    double timeDiff = ((uint64_t)now - lastFPSmeasurement);
    measuredFPS = ((double)FPSframeCounter) / (timeDiff / 1000.0);
    // reset conter and timer:
    FPSframeCounter = 0;
    lastFPSmeasurement = now;
}

static void measureFPS_frameDrawn(void) {
    FPSframeCounter++;
}

// NO MAIN IF UNIT TEST:
#ifndef UNITTEST
// ---

#if (defined(__ANDROID__) || defined(ANDROID))
int SDL_main(int argc, char** argv) {
#else
#ifdef WINDOWS
int CALLBACK WinMain(HINSTANCE hInstance,
        HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
#else
int main(int argc, char** argv) {
#endif
#endif

#ifdef WINDOWS
#if (defined(USE_SDL_AUDIO) || defined(USE_SDL_GRAPHICS))
    // tell SDL we're ready to go:
    SDL_SetMainReady();
#endif
#endif

#ifdef WINDOWS
    // obtain command line arguments a special way on windows:
    int argc = __argc;
    char** argv = __argv;
#endif

    if (main_startup_do(argc, argv) != 0) {
        return 1;
    }

    if (main_startup_openScript(argc, argv) != 0) {
        return 1;
    }

#if defined(ANDROID) || defined(__ANDROID__)
    printinfo("Blitwizard startup: Initialising physics...");
#endif

#ifdef USE_PHYSICS2D
    // initialise physics
    physics2ddefaultworld = physics_createWorld(0);
    if (!physics2ddefaultworld) {
        printfatalerror("Error: Failed to initialise Box2D physics");
        fatalscripterror();
        main_Quit(1);
        return 1;
    }
    luacfuncs_object_initialisePhysicsCallbacks();
#endif

#if defined(ANDROID) || defined(__ANDROID__)
    printinfo("Blitwizard startup: reading templates if present...");
#endif

    // Search & run templates. Separate code for desktop/android due to
    // android having the templates in embedded resources (where cwd'ing to
    // isn't supported), while for the desktop it is a regular folder.
#if !defined(ANDROID)
    int checksystemwidetemplate = 1;
    // see if the template path points to a virtual zip folder:
    if (resource_isFolderInZip(option_templatepath)) {
        // it does. run templates from here.
        checksystemwidetemplate = 0;
        if (!attemptTemplateLoad(option_templatepath)) {
            checksystemwidetemplate = 1;
        }
    } else {
        // see if there is a template directory & file:
        if (file_doesFileExist(option_templatepath)
        && file_IsDirectory(option_templatepath)) {
            checksystemwidetemplate = 0;

            // now run template file:
            if (!attemptTemplateLoad(option_templatepath)) {
                checksystemwidetemplate = 1;
            }
        }
    }
#if defined(SYSTEM_TEMPLATE_PATH)
    if (checksystemwidetemplate) {
        attemptTemplateLoad(SYSTEM_TEMPLATE_PATH);
    }
#endif
#else // if !defined(ANDROID)
    // on Android, we only allow templates/init.lua.
    // see if we can read the file:
    int exists = 0;
    SDL_RWops* rwops = SDL_RWFromFile("templates/init.lua", "rb");
    if (rwops) {
        exists = 1;
        rwops->close(rwops);
    }
    if (exists) {
        // run the template file:
        attemptTemplateLoad("templates/");
    }
#endif

    // free template dir now that we've loaded things:
    free(option_templatepath);

#if defined(ANDROID) || defined(__ANDROID__)
    printinfo("[main] android: blitwizard startup: "
        "executing lua start script...");
#endif

    // push command line arguments into script state:
    int i = 0;
    int pushfailure = 0;
    while (i < scriptargcount) {
        if (!luastate_PushFunctionArgumentToMainstate_String(scriptargs[i])) {
            pushfailure = 1;
            break;
        }
        i++;
    }
    if (pushfailure) {
        printfatalerror("Error: couldn't push all script arguments "
            "into script state");
        main_Quit(1);
        return 1;
    }

    // free arguments:
    i = 0;
    while (i < scriptargcount) {
        free(scriptargs[i]);
        i++;
    }
    free(scriptargs);

    // open and run provided script file and pass the command line arguments:
    char* error;
    char outofmem[] = "Out of memory";
    if (!luastate_DoInitialFile(script, scriptargcount, &error)) {
        if (error == NULL) {
            error = outofmem;
        }
        printfatalerror("Error: an error occured when running \"%s\": %s",
            script, error);
        if (error != outofmem) {
            free(error);
        }
        fatalscripterror();
        main_Quit(1);
        return 1;
    }

    // enable blitwizard.onLog
    doConsoleLog();

    printinfo("[main] blitwizard startup: calling blitwizard.onInit...");
    doConsoleLog();

    // call init
    if (!luastate_CallFunctionInMainstate("blitwizard.onInit", 0, 1, 1,
            &error, NULL, NULL)) {
        printfatalerror("Error: an error occured when calling "
        "blitwizard.onInit: %s",error);
        if (error != outofmem) {
            free(error);
        }
        fatalscripterror();
        main_Quit(1);
        return 1;
    }
    doConsoleLog();

    // when graphics or audio is open, run the main loop
    printinfo("[main] blitwizard startup: Entering main loop...");
    doConsoleLog();

    // Initialise audio when it isn't
    main_initAudio();
    doConsoleLog();

    // If we failed to initialise audio, we want to simulate it
#ifdef USE_AUDIO
    uint64_t simulateaudiotime = 0;
    if (simulateaudio) {
        simulateaudiotime = time_getMilliseconds();
    }
#endif

    uint64_t logictimestamp = time_getMilliseconds();
    uint64_t lastdrawingtime = 0;
    uint64_t physicstimestamp = time_getMilliseconds();
    while (!wantquit) {
        // tell scripts running this frame how long they can run:
        scriptTerminateTime = time_getMilliseconds() + scriptMaxRuntime;

        // do console logging:
        doConsoleLog(); 
        uint64_t timeNow = time_getMilliseconds();

#ifdef USE_AUDIO
        // simulate audio
        if (simulateaudio) {
            while (simulateaudiotime < time_getMilliseconds()) {
                char buf[48 * 4 * 2];
                audiomixer_GetBuffer(buf, 48 * 4 * 2);
                simulateaudiotime += 1; // 48 * 1000 times * 4 bytes * 2
                    // channels per second = simulated 48kHz 32bit stereo audio
            }
        }
#endif // ifdef USE_AUDIO

        // check for unused, no longer playing media objects:
        checkAllMediaObjectsForCleanup();

        // slow sleep: check if we can safe some cpu by waiting longer
        unsigned int deltaspan = TIMESTEP;
#ifndef USE_GRAPHICS
        int nodraw = 1;
#else
        int nodraw = 1;
        if (graphics_areGraphicsRunning()) {
            nodraw = 0;
        }
#endif
        // see how much time as already passed since the last frame:
        uint64_t delta = time_getMilliseconds() - lastdrawingtime;
        if (delta > 600) {
            printwarning("[main] warning: huge hang (%d ms) detected, "
                "skipping logic",
            (int)delta);
            // forget about keeping up with time, this was a huge hang:
            delta = 0;
            lastdrawingtime = time_getMilliseconds();
            physicstimestamp = time_getMilliseconds();
            logictimestamp = time_getMilliseconds();
        }

        // sleep/limit FPS as much as we can
        if (delta < (deltaspan-10)) {
            // the time passed is smaller than the optimal waiting time
            // -> sleep
            if (connections_NoConnectionsOpen() &&
                    !listeners_HaveActiveListeners()) {
                // no connections, use regular sleep
                time_sleep((deltaspan-10)-delta);
                connections_SleepWait(0);
            } else {
                // use connection select wait to get connection events
                connections_SleepWait(deltaspan-delta);
            }
        } else {
            // the time passed exceeds the optimal waiting time already
            // -> don't slow down at all
            connections_SleepWait(0);  // check on connections
        }

        // Remember drawing time and process net events
        lastdrawingtime = time_getMilliseconds();
        if (!luafuncs_ProcessNetEvents()) {
            // there was an error processing the events
            main_Quit(1);
        }

#ifdef USE_GRAPHICS
        // check and trigger all sort of input events
        graphics_checkEvents(&quitevent, &mousebuttonevent, &mousemoveevent,
            &keyboardevent, &textevent, &putinbackground);
#endif
        doConsoleLog();

        // call the step function and advance physics
        int physicsiterations = 0;
        int logiciterations = 0;
        time_t iterationStart = time(NULL);
#if defined(USE_PHYSICS2D)
        int psteps2d_max = ((float)TIMESTEP/
        (float)physics_getStepSize(physics2ddefaultworld));
        psteps2d_max++;
#endif
        while (
                // allow maximum of iterations in an attempt to keep up:
                (logictimestamp < timeNow || physicstimestamp < timeNow) &&
                (logiciterations < MAXLOGICITERATIONS
#if defined(USE_PHYSICS2D) || defined(USE_PHYSICS3D)
                ||  physicsiterations < MAXPHYSICSITERATIONS
#endif
                )
                // .. unless we're already doing this for >2 seconds:
                && iterationStart + 2 >= time(NULL)
            ) {
#ifdef USE_PHYSICS2D
            if (physicsiterations < MAXPHYSICSITERATIONS &&
                    physicstimestamp < timeNow &&
                    (physicstimestamp <= logictimestamp
                    || logiciterations >= MAXLOGICITERATIONS)) {
                int psteps = psteps2d_max;
                while (psteps > 0) {
                    physics_step(physics2ddefaultworld,
                        &luacfuncs_objectphysics_replaceObjectRef);
                    physicstimestamp += physics_getStepSize(
                        physics2ddefaultworld);
                    psteps--;
                }
                physicsiterations++;
            }
#else
            physicstimestamp = timeNow + 2000;
#endif
            if (logiciterations < MAXLOGICITERATIONS &&
                    logictimestamp < timeNow &&
                    (logictimestamp <= physicstimestamp
                    || physicsiterations >= MAXPHYSICSITERATIONS)) {
                // check how much logic we might want to do in a batch:
                int k = (timeNow - logictimestamp)/TIMESTEP;
                if (k > MAXBATCHEDLOGIC) {
                    k = MAXBATCHEDLOGIC;
                }
                if (k < 1) {
                    k = 1;
                }

                // call logic functions of all objects:
                int i = luacfuncs_object_doAllSteps(k);
                doConsoleLog();

                // advance time step:
                logictimestamp += i * TIMESTEP;
                logiciterations += i;
            }
        }

        // check if we ran out of iterations:
        if (logiciterations >= MAXLOGICITERATIONS ||
        physicsiterations >= MAXPHYSICSITERATIONS
        || iterationStart + 2 < time(NULL)) {
            if (
#if defined(USE_PHYSICS2D) || defined(USE_PHYSICS3D)
                    physicstimestamp < timeNow ||
#endif
                 logictimestamp < timeNow) {
                // we got a problem: we aren't finished,
                // but we hit the iteration limit
                physicstimestamp = time_getMilliseconds();
                logictimestamp = time_getMilliseconds();
                printwarning("[main] warning: logic is too slow, "
                "maximum logic iterations have been reached (%d)",
                (int)MAXLOGICITERATIONS);
            } else {
                // we don't need to iterate anymore -> everything is fine
            }
        }

        // handle runDelayed calls:
        luacfuncs_runDelayed_Do();

#ifdef USE_GRAPHICS
        // report visibility of sprites to texture manager:
        graphics2dsprites_reportVisibility();
#endif

#ifdef USE_GRAPHICS
        // texture manager tick:
        texturemanager_tick();
#endif

        // update object graphics:
        luacfuncs_object_updateGraphics();
        doConsoleLog();

#ifdef USE_GRAPHICS
        if (graphics_areGraphicsRunning()) {
#ifdef ANDROID
            if (!appinbackground) {
#endif
                // draw a frame
                graphicsrender_draw();
#ifdef ANDROID
            }
#endif
        }
#endif

        // measure FPS:
        measureFPS_frameDrawn();
        measureFPS();

        // we might want to quit if there is nothing else to do
#ifdef USE_AUDIO
        if (
#ifdef USE_GRAPHICS
        !graphics_areGraphicsRunning() &&
#endif
        connections_NoConnectionsOpen() &&
        !listeners_HaveActiveListeners() && audiomixer_NoSoundsPlaying()
        && luacfuncs_runDelayed_getScheduledCount() == 0) {
#else
        if (
#ifdef USE_GRAPHICS
        !graphics_AreGraphicsRunning() &&
#endif
        connections_NoConnectionsOpen() &&
        !listeners_HaveActiveListeners()
        && luacfuncs_runDelayed_getScheduledCount() == 0) {
#endif
            printinfo("[main] nothing interesting remains running; quit!");
            main_Quit(1);
        }

#ifdef USE_GRAPHICS
        // be very sleepy if in background
        if (appinbackground) {
#ifdef ANDROID
            time_Sleep(40);
#endif
        }
#endif

        // do some garbage collection:
        /*gcframecount++;
        if (gcframecount > 100) {
            // do a gc step once in a while
            luastate_GCCollect();
        }*/

        // new frame:
#ifdef USE_GRAPHICS
        luacfuncs_objectgraphics_newFrame();
#endif
    }
    main_Quit(0);
    return 0;
}

// NO MAIN IF UNIT TEST:
#endif  // UNITTEST
// ---

