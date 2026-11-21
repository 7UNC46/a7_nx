/* a7.h -- what the port's files share. MIT. */
#ifndef A7_H
#define A7_H
#include <stdint.h>

#include "so_util.h"

extern so_module g_a7_mod;

/* The Java classes the engine is handed (as jclass) or looks up itself. */
#define A7_JPKG        "com/gameloft/android/ANMP/GloftA7HM/"
#define A7_CLS_GLGAME  A7_JPKG "GLGame"          /* the activity (extends GL2JNIActivity) */
#define A7_CLS_GAME    A7_JPKG "Game"            /* phone info, OBB names, keyboard, ads */
#define A7_CLS_MISC    A7_JPKG "Misc"
#define A7_CLS_DEVICE  A7_JPKG "GLUtils/Device"
#define A7_CLS_SUTILS  A7_JPKG "GLUtils/SUtils"
#define A7_CLS_SENDINFO A7_JPKG "SendInfo"
#define A7_CLS_PUSH    A7_JPKG "PushNotification/C2DMAndroidUtils"
#define A7_CLS_SPLASH  A7_JPKG "SplashScreenActivity"
#define A7_CLS_IAB     A7_JPKG "iab/InAppBilling"
#define A7_CLS_GLF_ACT "com/gameloft/glf/GL2JNIActivity"
#define A7_CLS_GLF     "com/gameloft/glf/GL2JNILib"  /* the frame loop's natives */
#define A7_CLS_GLOT    "com/gameloft/glot/PortingJNI"
#define A7_CLS_SOCIAL  "com/gameloft/GLSocialLib/PlatformAndroid"
#define A7_CLS_FB      "com/gameloft/GLSocialLib/facebook/FacebookAndroidGLSocialLib"

/* The Android-side folders the Java answers name (the runtime's path rules
 * turn them into folders of <game folder>). */
#define A7_ANDROID_OBB   "/sdcard/Android/obb/" PORT_PACKAGE   /* Context.getObbDir() */
#define A7_ANDROID_FILES "/data/data/" PORT_PACKAGE "/files"   /* Context.getFilesDir() */
#define A7_ANDROID_CACHE "/data/data/" PORT_PACKAGE "/cache"   /* Context.getCacheDir() */

/* a7_setup.c: libAndroid.so out of the APK (or the folder), the OBBs checked. */
#include "dcr_setup.h"
void a7_setup_lib(RtSetupCtx *ctx);
void a7_setup_obbs(RtSetupCtx *ctx);

/* a7_gl.c: the EGL window context (GLES 2), made before the first GL call. */
int a7_gl_init(void);
void a7_gl_swap(void);
void a7_gl_set_interval(int interval);
/* 1 when the engine swapped through GL2JNILib.SwapBuffer this frame. */
extern int g_a7_swapped;

/* a7_main.c: the engine's natives the input needs. */
typedef void (*a7_touch_fn)(void *env, void *cls, int action, int x, int y, int id);
typedef void (*a7_accel_fn)(void *env, void *cls, float x, float y, float z);
typedef void (*a7_key_fn)(void *env, void *cls, int key, int action); /* GLGame.nativeHandleKey: action 0 = down, 1 = up */
extern a7_touch_fn g_a7_touch;
extern a7_accel_fn g_a7_accel;
extern a7_key_fn g_a7_key;
extern void *g_a7_glf_cls, *g_a7_glgame_cls;

/* GL2JNILib.touchEvent(action, x, y, pointer id): the actions glf's
 * GL2JNIActivity.onTouchEvent sends (classes.dex: releaseAllTouchPointers
 * lets every finger go with 0). TO CHECK on hardware: debug.log [touch]. */
#define A7_TOUCH_UP   0
#define A7_TOUCH_DOWN 1
#define A7_TOUCH_MOVE 2

/* a7_input.c: controllers + touch -> the engine. */
void a7_input_init(void);
void a7_input_poll(void);
/* L+R+Minus held: a7_main.c stops presenting so the watchdog dumps threads. */
extern volatile int g_a7_dump_request;

/* a7_config.c: config.ini. */
typedef struct {
  int fps60;         /* 1: 60 fps, 0: 30 */
  float steer_gain;  /* left stick -> accelerometer tilt */
  int touch;         /* touch screen passed to the game */
  int steer_mode;    /* 0 left stick, 1 motion, 2 both */
  float motion_gain; /* motion: how many "phone tilts" one console tilt is */
  int motion_invert; /* motion: steer the other way */
  int tex_format;    /* Game.GetTextureFormat: 1 PVRTC, 2 ATC, 3 DXT, 4 ETC1 */
} A7Config;
extern A7Config g_a7_cfg;
extern int g_a7_dump_combo;
/* The engine's language number (Game.GetPhoneLanguage: 0 English, 1 French,
 * 2 German, 3 Spanish, 4 Italian, 5 Portuguese, 6 Japanese, 7 Korean,
 * 8 Chinese, 9 Russian): config or console. */
int a7_language(void);
/* ...and the two-letter code GLGame / glot hand out ("en", "de"...). */
const char *a7_language_code(void);

/* a7_cursor.c: the pointer drawn before each swap (crosshair: unused here). */
void a7_cursor_set(float x, float y, int shown, int pressed, int crosshair);

/* a7_heap.c: one [mem] line (the game vs the whole heap). */
void a7_heap_report(const char *why);

#endif /* A7_H */
