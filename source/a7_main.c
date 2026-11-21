/* a7_main.c -- Asphalt 7 on android32: from the player's APK to the game's
 * frame loop.
 *
 * port_load: the runtime has found the APK (by its package); the setup puts
 *   libAndroid.so into the game folder (a7_setup.c, once) and checks the
 *   OBBs. Then the library is mapped, relocated, bound to the runtime's
 *   bionic shims and sealed. (No SWP in this build: nothing to rewrite.)
 *
 * port_run: what the Java side does on a phone (classes.dex of the APK,
 * GLGame extends com.gameloft.glf.GL2JNIActivity):
 *     GLGame.onCreate          System.loadLibrary("Android")  -> JNI_OnLoad
 *                              Device.init / SUtils.init       -> nativeInit()     static
 *     GLGame.a()               Misc.initTemplateJNI            -> Game.nativeInit() static
 *     C2DMAndroidUtils.<clinit>                                -> nativeInit()     static
 *     GL2JNIActivity.a()       GL2JNILib.init()                                    static
 *                                (GL2JNILib.createView(): a7_jni.c)
 *     GL2JNIView.ConfigChooser GL2JNILib.getViewSettings(), initGL()
 *                                (getViewSettings makes glf's App -- NewApp --
 *                                 then calls back GL2JNILib.setupPaths(), which
 *                                 calls GL2JNILib.setPaths(obb, files, cache))
 *   Game.nativeInit takes its JNIEnv from NVThreadGetCurrentJNIEnv(), which
 *   only GL2JNILib.init would make work: NVThreadInit(vm) runs right after
 *   JNI_OnLoad here.
 *     the context factory      getNumExtraContext() -> setNumExtraContext(made)
 *     Renderer (GL thread):
 *       onSurfaceChanged       GL2JNILib.resize(w, h)
 *       onDrawFrame            GL2JNILib.step()
 *       pause / resume         GL2JNILib.stateChanged(bool)
 *   Here everything runs on the main thread, which is the GL thread too, and
 *   the GLES 2 context exists before the first native (a7_gl.c): Glitch may
 *   touch GL from GL2JNILib.init already.
 *
 * Runs on hardware (0.2.0): menus, races, sound. Every step is logged
 * ([a7] ... done); a missing Java answer shows as "[jni] unhandled ..." in
 * debug.log. MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "dcr_boost.h"
#include "dcr_path.h"
#include "dcr_setup.h"
#include "error.h"
#include "jni.h"
#include "port_config.h"
#include "rt_applet.h"
#include "rt_boot.h"
#include "rt_cfg.h"
#include "rt_window.h"
#include "so_util.h"
#include "util.h"

#include "a7.h"

so_module g_a7_mod;

/* opensles.c (RT_OPENSLES 1) */
void dcr_audio_pause(int paused);
void dcr_audio_close(void);
int g_a7_swapped;

typedef int (*jni_onload_fn)(void *vm, void *reserved);
typedef void (*jni_void_fn)(void *env, void *cls);
typedef int (*jni_int_fn)(void *env, void *cls);
typedef void (*jni_i_fn)(void *env, void *cls, int a);
typedef void (*jni_ii_fn)(void *env, void *cls, int a, int b);
typedef void (*jni_z_fn)(void *env, void *cls, uint8_t z);
typedef void (*jni_sss_fn)(void *env, void *cls, void *a, void *b, void *c);

a7_touch_fn g_a7_touch;
a7_accel_fn g_a7_accel;
a7_key_fn g_a7_key;
void *g_a7_glf_cls, *g_a7_glgame_cls;

static jni_void_fn f_step, f_pause_game, f_resume_game;
static jni_z_fn f_state;
static int g_paths_set; /* a7_jni.c's setupPaths answer ran */

/* ---------------------------------------------------------------- setup */
/* The library is found by a7_setup_lib (not the plan's own unpacking: see
 * a7_setup.c), the OBBs by a7_setup_obbs; both run at every start and
 * return at once when nothing changed. */
static const RtSetupStep k_steps[] = {
    {"engine library", RT_STEP_WITH_ZIP, 0, 950, a7_setup_lib},
    {"expansion files", RT_STEP_AFTER_ZIP, 950, 1000, a7_setup_obbs},
};

const RtSetupPlan port_setup_plan = {
    .libs = NULL,
    .nlibs = 0,
    .apk_requirement = "Asphalt 7: Heat (" PORT_PACKAGE ")",
    .classes_p0 = 950,
    .classes_p1 = 950,
    .steps = k_steps,
    .nsteps = sizeof k_steps / sizeof k_steps[0],
};

const char *port_apk_help(void) {
  return "Copy the APK of your own Asphalt 7: Heat to /switch/" PORT_NAME "/ (any name),\n"
         "and its two OBB files to /switch/" PORT_NAME "/" A7_OBB_DIR "/.\n"
         "If the APK carries no " A7_LIB ", copy that file to /switch/" PORT_NAME "/ too.";
}

/* The kernel's user helpers (__kuser_cmpxchg 0xffff0fc0, __kuser_memory_barrier
 * 0xffff0fa0) as FUNCTION POINTER VARIABLES in the data segment: the
 * atomics of protobuf / the STL keep them in globals and call through them
 * (google_utils::protobuf::internal::OnShutdown+0x2c: ldr pc, [r3] with
 * r3 -> a word holding 0xffff0fa0). The runtime's so_fix_kuser_helpers
 * rewrites the literals in the code only; these 6 words (this build:
 * +0x13b52b0/4, +0x13bcfcc/d0/d8/dc) crashed in the constructors with an
 * instruction abort at 0xffff0fa0 (hardware 2026-11-19). Same rewrite here,
 * over the writable PT_LOADs, between so_relocate and so_finalize. */
void dcr_kuser_cmpxchg(void);        /* kuser.S */
void dcr_kuser_memory_barrier(void); /* kuser.S */

static void fix_kuser_data(so_module *m) {
  uint8_t *img = m->state == SO_STAGED     ? (uint8_t *)m->load_base
               : m->state == SO_WRITABLE ? (uint8_t *)m->load_virtbase : NULL;
  if (!img)
    return;
  int cmpxchg = 0, barrier = 0;
  for (int i = 0; i < m->phnum; i++) {
    const Elf32_Phdr *ph = &m->phdr[i];
    if (ph->p_type != PT_LOAD || (ph->p_flags & PF_X))
      continue;
    uint32_t *w = (uint32_t *)((uintptr_t)(img + ph->p_vaddr + 3) & ~3u);
    size_t nw = ph->p_filesz / 4;
    for (size_t k = 0; k < nw; k++) {
      if (w[k] == 0xffff0fc0u) {
        w[k] = (uint32_t)(uintptr_t)dcr_kuser_cmpxchg;
        if (cmpxchg++ < 8)
          debugPrintf("[a7] data +0x%x: kuser cmpxchg pointer -> kuser.S\n",
                      (unsigned)((uintptr_t)&w[k] - (uintptr_t)img));
      } else if (w[k] == 0xffff0fa0u) {
        w[k] = (uint32_t)(uintptr_t)dcr_kuser_memory_barrier;
        if (barrier++ < 8)
          debugPrintf("[a7] data +0x%x: kuser barrier pointer -> kuser.S\n",
                      (unsigned)((uintptr_t)&w[k] - (uintptr_t)img));
      }
    }
  }
  debugPrintf("[a7] kernel helper pointers in the data: %d cmpxchg, %d barrier\n", cmpxchg, barrier);
}

int port_load(const char *apk) {
  dcr_setup_from_apk(apk);

  char path[DCR_PATH_MAX];
  rt_root_path(path, sizeof path, A7_LIB);
  long size = rt_file_size(path);
  if (size <= 0)
    fatal_error("%s is missing.\n\n%s", path, port_apk_help());
  if (A7_LIB_CRC32) {
    unsigned long crc = rt_crc_of_file(path);
    if (crc != A7_LIB_CRC32 || (unsigned long)size != A7_LIB_SIZE)
      fatal_error("This %s is not the build this port was made for\n"
                  "(CRC %08lx, %ld bytes; expected %08lx, %lu bytes).\n\n%s",
                  A7_LIB, crc, size, A7_LIB_CRC32, A7_LIB_SIZE, port_apk_help());
  }
  debugPrintf("[a7] %s: %ld bytes%s\n", A7_LIB, size,
              (unsigned long)size == A7_LIB_SIZE ? " (the size this port was made with)"
                                                 : " (NOT the size this port was made with: expect trouble)");

  if (so_load(&g_a7_mod, path, NULL, PORT_SO_REGION_BYTES) != 0)
    fatal_error("Could not load %s.", path);
  so_relocate(&g_a7_mod);
  so_resolve(&g_a7_mod, NULL, 0, 1);
  so_fix_kuser_helpers(&g_a7_mod);
  fix_kuser_data(&g_a7_mod);
  so_finalize(&g_a7_mod);
  debugPrintf("[a7] %s mapped at %p\n", A7_LIB, g_a7_mod.load_virtbase);
  return 0;
}

/* ---------------------------------------------------------------- focus */
void port_focus_lost(void) {
  if (f_pause_game)
    f_pause_game(g_jni_env, g_a7_glgame_cls);
  if (f_state)
    f_state(g_jni_env, g_a7_glf_cls, 0);
  dcr_audio_pause(1);
}

void port_focus_gained(void) {
  dcr_audio_pause(0);
  if (f_state)
    f_state(g_jni_env, g_a7_glf_cls, 1);
  if (f_resume_game)
    f_resume_game(g_jni_env, g_a7_glgame_cls);
}

/* ---------------------------------------------------------------- run */
static void *need(const char *sym) {
  void *p = (void *)so_try_find_addr_rx(&g_a7_mod, sym);
  if (!p)
    fatal_error("%s has no %s: not the expected build?", A7_LIB, sym);
  return p;
}

static void *maybe(const char *sym) {
  void *p = (void *)so_try_find_addr_rx(&g_a7_mod, sym);
  if (!p)
    debugPrintf("[a7] (no %s in this build)\n", sym);
  return p;
}


/* The installer's lock pointers (lockPointer1..4, .bss): GDRMPolicy's
 * nativeAllow points them at ints holding 1 once the license is good (the
 * same scheme the PS Vita port found in MC3). */
static int g_drm_ok[4] = {1, 1, 1, 1};

static void set_drm_flags(void) {
  static const char *const names[] = {"lockPointer1", "lockPointer2", "lockPointer3", "lockPointer4"};
  for (int i = 0; i < 4; i++) {
    int **p = (int **)maybe(names[i]);
    if (p)
      *p = &g_drm_ok[i];
    else
      debugPrintf("[mc4] no %s symbol\n", names[i]);
  }
}

static void *cls(const char *name) { return jni_class(name)->obj; }

#define J   "Java_com_gameloft_android_ANMP_GloftA7HM_"
#define GLF "Java_com_gameloft_glf_GL2JNILib_"

/* a7_jni.c: GL2JNILib.setupPaths() (the engine's call) ends here. */
void a7_set_paths(void) {
  jni_sss_fn set_paths = (jni_sss_fn)maybe(GLF "setPaths");
  if (!set_paths)
    return;
  JObj *obb = jni_str(A7_ANDROID_OBB), *files = jni_str(A7_ANDROID_FILES), *cache = jni_str(A7_ANDROID_CACHE);
  obb->immortal = files->immortal = cache->immortal = 1; /* the engine may keep the pointers */
  set_paths(g_jni_env, g_a7_glf_cls, obb, files, cache);
  g_paths_set = 1;
  debugPrintf("[a7] GL2JNILib.setPaths(\"%s\", \"%s\", \"%s\") done\n", A7_ANDROID_OBB, A7_ANDROID_FILES,
              A7_ANDROID_CACHE);
}

void port_run(void) {
  jni_init();
  so_execute_init_array(&g_a7_mod);

  g_a7_glf_cls = cls(A7_CLS_GLF);
  g_a7_glgame_cls = cls(A7_CLS_GLGAME);

  jni_onload_fn onload = (jni_onload_fn)need("JNI_OnLoad");
  debugPrintf("[a7] JNI_OnLoad -> 0x%x\n", onload(g_jni_vm, NULL));
  set_drm_flags();

  /* NVThreadInit(vm): Game.nativeInit (and every native that wants a JNIEnv)
   * gets it from NVThreadGetCurrentJNIEnv(), which returns NULL until the VM
   * is known -- Game.nativeInit then dereferences that NULL at once. JNI_OnLoad
   * does not set it; the library's only caller is GL2JNILib.init (which runs
   * later here and sets it again, harmlessly). */
  typedef void (*nv_init_fn)(void *vm);
  nv_init_fn nv_init = (nv_init_fn)maybe("NVThreadInit");
  if (nv_init) {
    nv_init(g_jni_vm);
    debugPrintf("[a7] NVThreadInit(vm) done\n");
  } else {
    debugPrintf("[a7] no NVThreadInit: Game.nativeInit will find no JNIEnv\n");
  }

  /* GLGame.onCreate: Device.init() and SUtils.init() (static natives that
   * keep the class for their Java calls). */
  jni_void_fn fn;
  if ((fn = (jni_void_fn)maybe(J "GLUtils_Device_nativeInit"))) {
    fn(g_jni_env, cls(A7_CLS_DEVICE));
    debugPrintf("[a7] Device.nativeInit done\n");
  }
  if ((fn = (jni_void_fn)maybe(J "GLUtils_SUtils_nativeInit"))) {
    fn(g_jni_env, cls(A7_CLS_SUTILS));
    debugPrintf("[a7] SUtils.nativeInit done\n");
  }

  /* Misc.initTemplateJNI: Game.GetPhoneInfo (Java only: its answers are the
   * Game.Get* handlers in a7_jni.c), then Game.nativeInit(). */
  ((jni_void_fn)need(J "Game_nativeInit"))(g_jni_env, cls(A7_CLS_GAME));
  debugPrintf("[a7] Game.nativeInit done\n");

  /* C2DMAndroidUtils.<clinit> on a phone: APushNotification::init (the
   * method IDs of the push class, answered in a7_jni.c) and ABundle_Init.
   * Without it every push call goes through a NULL method ID ("bad method
   * ID" twice a frame) and SendPushToMyself's NULL string crashes the exit
   * (createDelayedLocalNotification -> strcmp(NULL, ...)). */
  if ((fn = (jni_void_fn)maybe(J "PushNotification_C2DMAndroidUtils_nativeInit"))) {
    fn(g_jni_env, cls(A7_CLS_PUSH));
    debugPrintf("[a7] C2DMAndroidUtils.nativeInit done\n");
  }

  /* The context, before any native that may touch GL. */
  if (a7_gl_init() != 0)
    fatal_error("The graphics could not start (EGL). See debug.log.");

  /* GL2JNIActivity.a(): GL2JNILib.init() -- the engine starts, and calls back
   * setupPaths (a7_jni.c -> a7_set_paths) and createView. */
  ((jni_void_fn)need(GLF "init"))(g_jni_env, g_a7_glf_cls);
  debugPrintf("[a7] GL2JNILib.init done\n");
  jni_z_fn gyros = (jni_z_fn)maybe(GLF "setGyrosEnabled");
  if (gyros)
    gyros(g_jni_env, g_a7_glf_cls, 0);

  /* GL2JNIView: the config chooser asks for the view settings and lets the
   * engine set up GL; the context factory makes the extra (loader) contexts
   * the engine asks for -- none here: the engine then loads on its own
   * thread's context, which is this one. */
  /* getViewSettings (glf::AndroidGetViewSettings) is where glf builds its
   * globals and the App (NewApp -> glf::gApp), then calls back setupPaths
   * (-> a7_set_paths) and App::MyInit. setPaths writes into gApp's Fs, so it
   * must not run before this: with gApp NULL it strcpy's to 0x100d0+. */
  if ((fn = (jni_void_fn)maybe(GLF "getViewSettings"))) {
    fn(g_jni_env, g_a7_glf_cls);
    debugPrintf("[a7] GL2JNILib.getViewSettings done\n");
  }
  if (!g_paths_set) { /* the engine did not ask: give them now (gApp exists) */
    debugPrintf("[a7] setupPaths was not called by the engine: setting the paths now\n");
    a7_set_paths();
  }
  ((jni_void_fn)need(GLF "initGL"))(g_jni_env, g_a7_glf_cls);
  debugPrintf("[a7] GL2JNILib.initGL done\n");
  jni_int_fn num_extra = (jni_int_fn)maybe(GLF "getNumExtraContext");
  jni_i_fn set_extra = (jni_i_fn)maybe(GLF "setNumExtraContext");
  if (num_extra) {
    int want = num_extra(g_jni_env, g_a7_glf_cls);
    debugPrintf("[a7] the engine asks for %d extra GL context(s): made 0\n", want);
    if (set_extra)
      set_extra(g_jni_env, g_a7_glf_cls, 0);
  }

  int w = 1280, h = 720;
  dcr_window_size(&w, &h);
  ((jni_ii_fn)need(GLF "resize"))(g_jni_env, g_a7_glf_cls, w, h);
  debugPrintf("[a7] GL2JNILib.resize(%d, %d) done\n", w, h);

  f_step = (jni_void_fn)need(GLF "step");
  f_state = (jni_z_fn)maybe(GLF "stateChanged");
  f_pause_game = (jni_void_fn)maybe(J "GLGame_pauseGame");
  f_resume_game = (jni_void_fn)maybe(J "GLGame_resumeGame");
  g_a7_touch = (a7_touch_fn)maybe(GLF "touchEvent");
  g_a7_accel = (a7_accel_fn)maybe(GLF "accelerometerEvent");
  g_a7_key = (a7_key_fn)maybe(J "GLGame_nativeHandleKey");
  if (f_state)
    f_state(g_jni_env, g_a7_glf_cls, 1);
  a7_input_init();

  debugPrintf("[a7] frame loop\n");
  uint64_t frame = 0;
  /* Frame pacing over each 300 frames: how many missed a vsync (60 fps:
   * > 17.5 ms, 30 fps: > 34 ms), how many missed two, and the worst.
   * [frame] lines only show frames over 120 ms; race stutter is far below. */
  uint64_t fp_prev = 0, fp_worst = 0;
  unsigned fp_miss1 = 0, fp_miss2 = 0;
  const uint64_t fp_budget = g_a7_cfg.fps60 ? 17500 : 34000; /* us */
  while (!rt_exit_requested() && appletMainLoop()) {
    rt_applet_poll();
    if (!rt_focused()) {
      fp_prev = 0; /* the time away is not a late frame */
      svcSleepThread(50000000LL);
      continue;
    }
    dcr_boost_frame_begin();
    a7_input_poll();
    if (g_a7_dump_request) {
      /* No frame for 12 s: the watchdog (10 s) snapshots every thread and
       * logs where each one is. */
      debugPrintf("[a7] hang dump requested (L+R+Minus): pausing 12 s for the watchdog\n");
      log_flush_ring();
      svcSleepThread(12000000000ll);
      g_a7_dump_request = 0;
      debugPrintf("[a7] hang dump done, resuming\n");
      log_flush_ring();
    }
    g_a7_swapped = 0;
    f_step(g_jni_env, g_a7_glf_cls);
    /* GLSurfaceView swaps after every onDrawFrame -- unless the engine
     * swapped itself (GL2JNILib.SwapBuffer, Game.SetIfNativeSwapBuffer). */
    if (!g_a7_swapped)
      a7_gl_swap();
    if (frame < 3)
      debugPrintf("[a7] frame %llu rendered%s\n", (unsigned long long)frame,
                  g_a7_swapped ? " (the engine swapped itself)" : "");
    dcr_boost_frame_end(++frame);
    {
      uint64_t now = armTicksToNs(armGetSystemTick()) / 1000ull;
      if (fp_prev) {
        uint64_t dt = now - fp_prev;
        if (dt > fp_budget)
          fp_miss1++;
        if (dt > fp_budget * 2)
          fp_miss2++;
        if (dt > fp_worst)
          fp_worst = dt;
      }
      fp_prev = now;
    }
    if (frame % 300 == 0) {
      a7_heap_report("every 300 frames");
      debugPrintf("[pace] last 300 frames at %d fps: %u late by a vsync or more, %u by two or more, worst %llu ms\n",
                  g_a7_cfg.fps60 ? 60 : 30, fp_miss1, fp_miss2, (unsigned long long)(fp_worst / 1000));
      fp_miss1 = fp_miss2 = 0;
      fp_worst = 0;
    }
  }
  rt_applet_stop();
  dcr_audio_close();
  debugPrintf("[a7] exit after %llu frames\n", (unsigned long long)frame);
  log_flush_ring();
}
