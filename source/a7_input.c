/* a7_input.c -- controllers and the touch screen -> Asphalt 7.
 *
 * On a phone Asphalt 7 steers by tilting (GL2JNILib.accelerometerEvent, from
 * the sensor), and everything else is the touch screen (GL2JNILib.touchEvent:
 * action, x, y, finger). Hardware keys reach GLGame.nativeHandleKey(code,
 * down) as Android key codes -- GLGame.onKeyDown passes every key but camera,
 * focus, menu, search, move-end and the volume keys -- and the engine has an
 * Xperia Play layer (D-pad, cross / circle / square / triangle, L1 / R1,
 * start / select), which these codes are.
 *
 * Here:
 *   left stick X   -> tilt, scaled by steer_gain (or motion: config.ini)
 *   buttons        -> Android key codes (k_keys below), sent with the
 *                     KeyEvent action: 0 = down, 1 = up
 *   L (click)      -> the pointer on / off: right stick moves it, ZR is a
 *                     finger at its tip (for every menu the keys miss)
 *   touch screen   -> passed through (handheld)
 *
 * Checked on hardware (0.2.0): steering sign, key down/up, ZR (brake /
 * drift), the look-back key. debug.log shows touches ([touch]) and keys
 * ([input]). MIT.
 */
#include <math.h>
#include <switch.h>

#include "jni.h"
#include "rt_pad.h"
#include "rt_window.h"
#include "util.h"

#include "a7.h"

/* Android key codes */
#define AK_BACK        4
#define AK_DPAD_UP     19
#define AK_DPAD_DOWN   20
#define AK_DPAD_LEFT   21
#define AK_DPAD_RIGHT  22
#define AK_DPAD_CENTER 23
#define AK_MENU        82
#define AK_BUTTON_X    99  /* Xperia Play square */
#define AK_BUTTON_Y    100 /* triangle */
#define AK_BUTTON_L1   102
#define AK_BUTTON_R1   103
#define AK_BUTTON_L2   104
#define AK_BUTTON_R2   105
#define AK_START       108
#define AK_SELECT      109

static PadState g_pad;
static int g_w = 1280, g_h = 720;
volatile int g_a7_dump_request;

/* Virtual fingers: ids above any real touch (0..9). */
#define VF_CURSOR 12

static const struct { u64 btn; int key; } k_keys[] = {
    {HidNpadButton_Up, AK_DPAD_UP},       {HidNpadButton_Down, AK_DPAD_DOWN},
    {HidNpadButton_Left, AK_DPAD_LEFT},   {HidNpadButton_Right, AK_DPAD_RIGHT},
    {HidNpadButton_A, AK_DPAD_CENTER},    {HidNpadButton_B, AK_BACK},
    {HidNpadButton_X, AK_BUTTON_X},       {HidNpadButton_Y, AK_BUTTON_Y},
    {HidNpadButton_R, AK_BUTTON_R1},      {HidNpadButton_ZL, AK_BUTTON_L2},
    {HidNpadButton_Plus, AK_START},       {HidNpadButton_Minus, AK_SELECT},
};
/* ZR: R2 while the pointer is off; with it on, ZR is the pointer's finger.
 * L: the pointer switch (L1 is not sent). */

/* ------------------------------------------------------------ motion */
static HidSixAxisSensorHandle g_sx_handheld, g_sx_full, g_sx_dual[2];
static int g_sx_ok;

static void motion_init(void) {
  if (g_a7_cfg.steer_mode == 0)
    return;
  int ok = 0;
  if (R_SUCCEEDED(hidGetSixAxisSensorHandles(&g_sx_handheld, 1, HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld)) &&
      R_SUCCEEDED(hidStartSixAxisSensor(g_sx_handheld)))
    ok |= 1;
  if (R_SUCCEEDED(hidGetSixAxisSensorHandles(&g_sx_full, 1, HidNpadIdType_No1, HidNpadStyleTag_NpadFullKey)) &&
      R_SUCCEEDED(hidStartSixAxisSensor(g_sx_full)))
    ok |= 2;
  if (R_SUCCEEDED(hidGetSixAxisSensorHandles(g_sx_dual, 2, HidNpadIdType_No1, HidNpadStyleTag_NpadJoyDual)) &&
      R_SUCCEEDED(hidStartSixAxisSensor(g_sx_dual[0])))
    ok |= 4;
  g_sx_ok = ok;
  debugPrintf("[input] motion sensors: handheld %s, Pro Controller %s, Joy-Con %s\n", ok & 1 ? "ok" : "no",
              ok & 2 ? "ok" : "no", ok & 4 ? "ok" : "no");
}

/* -1 .. 1 (0: none / level), from the controller in use. */
static float motion_steer(void) {
  u32 style = padGetStyleSet(&g_pad);
  HidSixAxisSensorHandle h;
  if ((style & HidNpadStyleTag_NpadFullKey) && (g_sx_ok & 2))
    h = g_sx_full;
  else if ((style & HidNpadStyleTag_NpadJoyDual) && (g_sx_ok & 4))
    h = g_sx_dual[0];
  else if (g_sx_ok & 1)
    h = g_sx_handheld;
  else
    return 0.0f;
  HidSixAxisSensorState st;
  if (!hidGetSixAxisSensorStates(h, &st, 1))
    return 0.0f;
  float x = st.acceleration.x * g_a7_cfg.motion_gain; /* in g */
  if (g_a7_cfg.motion_invert)
    x = -x;
  return x > 1.0f ? 1.0f : x < -1.0f ? -1.0f : x;
}

/* ------------------------------------------------------------ to the engine */
static void touch(int action, int x, int y, int id) {
  if (g_a7_touch)
    g_a7_touch(g_jni_env, g_a7_glf_cls, action, x, y, id);
}

static void key(int k, int down) {
  static int told;
  if (told < 40) {
    told++;
    debugPrintf("[input] key %d %s\n", k, down ? "down" : "up");
  }
  /* The second argument is the KeyEvent action, not a "pressed" flag:
   * nativeHandleKey calls Application::KeyReleased for 1 (ACTION_UP) and
   * KeyPressed for anything else (ACTION_DOWN = 0). Passing 1 for a press
   * had every key stuck on after one tap (brake/drift, look back). */
  if (g_a7_key)
    g_a7_key(g_jni_env, g_a7_glgame_cls, k, down ? 0 : 1);
}

void a7_input_init(void) {
  dcr_window_size(&g_w, &g_h);
  rt_pad_setup(1, 1);
  padInitializeDefault(&g_pad);
  hidInitializeTouchScreen();
  motion_init();
  debugPrintf("[input] keys %s, touch %s, accel %s\n", g_a7_key ? "ok" : "MISSING", g_a7_touch ? "ok" : "MISSING",
              g_a7_accel ? "ok" : "MISSING");
}

static void poll_touch(void) {
  static HidTouchScreenState old;
  HidTouchScreenState st;
  if (!hidGetTouchScreenStates(&st, 1))
    return;
  if (!g_a7_cfg.touch)
    st.count = 0;
  /* The panel is 1280x720 whatever the rendering size. */
  float sx = g_w / 1280.0f, sy = g_h / 720.0f;
  for (int i = 0; i < st.count; i++) {
    int id = st.touches[i].finger_id, x = (int)(st.touches[i].x * sx), y = (int)(st.touches[i].y * sy), seen = 0;
    for (int j = 0; j < old.count; j++)
      seen |= old.touches[j].finger_id == (u32)id;
    static int told;
    if (!seen && told < 40) {
      told++;
      debugPrintf("[touch] finger %d down at %d,%d (%.2f, %.2f of the screen)\n", id, x, y, (double)x / g_w,
                  (double)y / g_h);
    }
    touch(seen ? A7_TOUCH_MOVE : A7_TOUCH_DOWN, x, y, id);
  }
  for (int j = 0; j < old.count; j++) {
    int gone = 1;
    for (int i = 0; i < st.count; i++)
      gone &= st.touches[i].finger_id != old.touches[j].finger_id;
    if (gone)
      touch(A7_TOUCH_UP, (int)(old.touches[j].x * sx), (int)(old.touches[j].y * sy), (int)old.touches[j].finger_id);
  }
  old = st;
}

/* ------------------------------------------------------------ the pointer */
static int g_ptr_on, g_ptr_down;
static float g_ptr_x = 640.0f, g_ptr_y = 360.0f;

static void pointer_update(const float *sticks, u64 now) {
  u64 t = armGetSystemTick();
  static u64 prev;
  float dt = prev ? (float)armTicksToNs(t - prev) / 1e9f : 0.016f;
  prev = t;
  if (dt > 0.1f)
    dt = 0.1f;
  if (!g_ptr_on) {
    if (g_ptr_down)
      touch(A7_TOUCH_UP, (int)g_ptr_x, (int)g_ptr_y, VF_CURSOR);
    g_ptr_down = 0;
    a7_cursor_set(g_ptr_x, g_ptr_y, 0, 0, 0);
    return;
  }
  float x = sticks[2], y = sticks[3];
  float m = sqrtf(x * x + y * y);
  if (m > 0.15f) {
    float speed = 250.0f + 1150.0f * (m - 0.15f) * (m - 0.15f) / (0.85f * 0.85f); /* px/s */
    float k = speed * dt * (g_h / 720.0f) / m;
    g_ptr_x += x * k;
    g_ptr_y -= y * k;
    g_ptr_x = g_ptr_x < 0 ? 0 : g_ptr_x > g_w - 1 ? (float)(g_w - 1) : g_ptr_x;
    g_ptr_y = g_ptr_y < 0 ? 0 : g_ptr_y > g_h - 1 ? (float)(g_h - 1) : g_ptr_y;
    if (g_ptr_down)
      touch(A7_TOUCH_MOVE, (int)g_ptr_x, (int)g_ptr_y, VF_CURSOR);
  }
  int zr = (now & HidNpadButton_ZR) != 0;
  if (zr && !g_ptr_down) {
    touch(A7_TOUCH_DOWN, (int)g_ptr_x, (int)g_ptr_y, VF_CURSOR);
    g_ptr_down = 1;
  } else if (!zr && g_ptr_down) {
    touch(A7_TOUCH_UP, (int)g_ptr_x, (int)g_ptr_y, VF_CURSOR);
    g_ptr_down = 0;
  }
  a7_cursor_set(g_ptr_x, g_ptr_y, 1, g_ptr_down, 0);
}

/* ------------------------------------------------------------ each frame */
void a7_input_poll(void) {
  float sticks[4] = {0};
  static u64 old;
  padUpdate(&g_pad); /* rt_pad_read only reads the state padUpdate took */
  u64 now = rt_pad_read(&g_pad, sticks);
  u64 down = now & ~old, up = old & ~now;

  if (down & HidNpadButton_L) {
    g_ptr_on = !g_ptr_on;
    debugPrintf("[input] pointer %s (L)\n", g_ptr_on ? "on: right stick moves, ZR taps" : "off");
    if (g_ptr_on && (now & HidNpadButton_ZR))
      key(AK_BUTTON_R2, 0); /* ZR becomes the finger */
  }
  for (unsigned i = 0; i < sizeof k_keys / sizeof k_keys[0]; i++) {
    if (down & k_keys[i].btn)
      key(k_keys[i].key, 1);
    if (up & k_keys[i].btn)
      key(k_keys[i].key, 0);
  }
  if (!g_ptr_on) {
    if (down & HidNpadButton_ZR)
      key(AK_BUTTON_R2, 1);
    if (up & HidNpadButton_ZR)
      key(AK_BUTTON_R2, 0);
  }
  pointer_update(sticks, now);

  /* Steering: tilt. A landscape phone reports gravity on Y when turned like
   * a wheel; 9.81 is a full tilt. The sign is negative: stick right (+x)
   * must be -Y for the car to go right (hardware, 2026-11-20: +Y steered the
   * other way). Motion steering goes through the same line. */
  if (g_a7_accel) {
    float x = sticks[0];
    if (fabsf(x) < 0.12f)
      x = 0.0f;
    x *= g_a7_cfg.steer_gain;
    if (g_a7_cfg.steer_mode == 1)
      x = motion_steer();
    else if (g_a7_cfg.steer_mode == 2) {
      float mo = motion_steer();
      x = fabsf(x) > fabsf(mo) ? x : mo;
    }
    g_a7_accel(g_jni_env, g_a7_glf_cls, 0.0f, -9.81f * x, 9.81f);
  }

  /* L + R + Minus held 2 s: ask for a thread dump (a7_main.c). */
  static uint64_t combo_since;
  static int combo_fired;
  const u64 combo = HidNpadButton_L | HidNpadButton_R | HidNpadButton_Minus;
  if (g_a7_dump_combo && (now & combo) == combo) {
    uint64_t t = armTicksToNs(armGetSystemTick());
    if (!combo_since)
      combo_since = t;
    else if (!combo_fired && t - combo_since > 2000000000ull) {
      g_a7_dump_request = 1;
      combo_fired = 1;
    }
  } else {
    combo_since = 0;
    combo_fired = 0;
  }

  poll_touch();
  old = now;
}
