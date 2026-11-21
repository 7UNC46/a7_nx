/* a7_config.c -- config.ini for Asphalt 7 (the runtime's INI engine, rt_cfg.c,
 * reads and writes it; this is the table). MIT. */
#include <string.h>
#include <switch.h>

#include "rt_cfg.h"
#include "util.h"

#include "a7.h"

A7Config g_a7_cfg = {.fps60 = 0, .steer_gain = 0.6f, .touch = 1, .motion_gain = 1.5f, .tex_format = 4};

int g_a7_cooperative = 0;
int g_a7_dump_combo = 1;
int g_a7_trace_files = 0;
static int g_boost = 1;
static int g_lang_choice = 0; /* index into A7_LANGS */
static int g_fps_choice = 1;  /* index into "60,30" */
static int g_tex_choice = 3;  /* index into "pvrtc,atc,dxt,etc1" */

/* Game.GetPhoneLanguage's numbers (classes.dex): en 0, fr 1, de 2, es 3,
 * it 4, pt / br 5, ja 6, ko 7, zh 8, ru 9. The choices after "auto" are in
 * that order, so choice - 1 is the engine's number. */
#define A7_LANGS "auto,en,fr,de,es,it,pt,ja,ko,zh,ru"
static const char *const k_codes[] = {"en", "fr", "de", "es", "it", "pt", "ja", "ko", "zh", "ru"};

static const CfgOpt k_opts[] = {
    CFG_ROW_RESOLUTION("auto",
                       "Rendering resolution: auto (1080 if docked when the game starts, 720 in\n"
                       "# handheld), 1080 or 720."),
    {"display", "frame_rate", "30",
     "60 or 30 frames a second.",
     CFG_CHOICE, "60,30", &g_fps_choice, 0, 0, 0, 0},
    {"display", "texture_format", "etc1",
     "Which compressed textures the game is told the GPU takes (Game.GetTextureFormat):\n"
     "# etc1 (every Android GPU), dxt, atc or pvrtc. Change it only if textures are\n"
     "# missing or garbled.",
     CFG_CHOICE, "pvrtc,atc,dxt,etc1", &g_tex_choice, 0, 0, 0, 0},
    {"controls", "steer_gain", "0.6",
     "How far the left stick tilts the 'phone' (0.2 - 1.5).",
     CFG_FLOAT, NULL, &g_a7_cfg.steer_gain, 0.2f, 1.5f, 0, 0},
    {"controls", "steering", "stick",
     "stick: the left stick steers. motion: tilt the Switch (handheld), the Pro\n"
     "# Controller or the Joy-Con grip like a steering wheel. both: either.",
     CFG_CHOICE, "stick,motion,both", &g_a7_cfg.steer_mode, 0, 0, 0, 0},
    {"controls", "motion_gain", "1.5",
     "Motion steering: how strongly a tilt steers (0.5 - 4).",
     CFG_FLOAT, NULL, &g_a7_cfg.motion_gain, 0.5f, 4.0f, 0, 0},
    {"controls", "motion_invert", "false",
     "Motion steering goes the wrong way: set this to true.",
     CFG_BOOL, NULL, &g_a7_cfg.motion_invert, 0, 0, 0, 0},
    {"controls", "touch_screen", "true",
     "The touch screen works in handheld.",
     CFG_BOOL, NULL, &g_a7_cfg.touch, 0, 0, 0, 0},
    {"game", "language", "auto",
     "Game language: auto (the Switch's), en, fr, de, es, it, pt, ja, ko, zh or ru.",
     CFG_CHOICE, A7_LANGS, &g_lang_choice, 0, 0, 0, 0},
    CFG_ROW_BOOST("CPU at 1785 MHz while the game loads (frames over 50 ms), normal otherwise.",
                  &g_boost),
    CFG_ROW_GL_SELFTEST(NULL),
    {"debug", "cooperative", "false",
     "false: the game's threads are time-sliced every 10 ms. true: they take turns\n"
     "# only when one waits.",
     CFG_BOOL, NULL, &g_a7_cooperative, 0, 0, 0, 0},
    {"debug", "trace_files", "false",
     "Write every file the game opens to debug.log, with the result (slow;\n"
     "# for bug reports: which file the game cannot open).",
     CFG_BOOL, NULL, &g_a7_trace_files, 0, 0, 0, 0},
    {"debug", "hang_dump", "true",
     "Hold L + R + Minus for 2 s: the game pauses 12 s and debug.log gets\n"
     "# where every thread is (for bug reports of hangs).",
     CFG_BOOL, NULL, &g_a7_dump_combo, 0, 0, 0, 0},
    CFG_ROW_BOOT_LOG("Show the start-up log on screen.", NULL),
    CFG_ROW_LOG_JNI("Write every Java call the game makes to debug.log (slow; for bring-up).",
                    NULL),
};

static void apply(void) {
  g_a7_cfg.fps60 = g_fps_choice == 0;
  g_a7_cfg.tex_format = g_tex_choice + 1; /* 1 PVRTC, 2 ATC, 3 DXT, 4 ETC1 */
  debugPrintf("[config] resolution %s, %d fps, textures %d, steer %.2f, touch %s, boost %s\n",
              rt_config_get("display", "resolution"), g_a7_cfg.fps60 ? 60 : 30, g_a7_cfg.tex_format,
              (double)g_a7_cfg.steer_gain, g_a7_cfg.touch ? "on" : "off", g_boost ? "on" : "off");
  static const char *const k_steer[] = {"left stick", "motion", "stick and motion"};
  debugPrintf("[config] steering: %s (motion gain %.1f%s)\n", k_steer[g_a7_cfg.steer_mode % 3],
              (double)g_a7_cfg.motion_gain, g_a7_cfg.motion_invert ? ", inverted" : "");
}

static const CfgTable k_table = {
    .opts = k_opts,
    .nopts = CFG_COUNT(k_opts),
    .version = 1,
    .apply = apply,
};

void dcr_config_load(void) { rt_config_load(&k_table); }

/* The game's number for the chosen language; auto: the console's. */
int a7_language(void) {
  static int cached = -1;
  if (cached >= 0)
    return cached;
  if (g_lang_choice > 0)
    return cached = g_lang_choice - 1;
  int lang = 0; /* English when the console's is not in the game */
  u64 code = 0;
  SetLanguage sl;
  if (R_SUCCEEDED(setInitialize())) {
    if (R_SUCCEEDED(setGetSystemLanguage(&code)) && R_SUCCEEDED(setMakeLanguage(code, &sl))) {
      switch (sl) {
      case SetLanguage_FR: case SetLanguage_FRCA: lang = 1; break;
      case SetLanguage_DE: lang = 2; break;
      case SetLanguage_ES: case SetLanguage_ES419: lang = 3; break;
      case SetLanguage_IT: lang = 4; break;
      case SetLanguage_PT: case SetLanguage_PTBR: lang = 5; break;
      case SetLanguage_JA: lang = 6; break;
      case SetLanguage_KO: lang = 7; break;
      case SetLanguage_ZHCN: case SetLanguage_ZHHANS: lang = 8; break;
      case SetLanguage_RU: lang = 9; break;
      default: lang = 0; break;
      }
    }
    setExit();
  }
  debugPrintf("[config] language %d (%s, the console's)\n", lang, k_codes[lang]);
  return cached = lang;
}

const char *a7_language_code(void) { return k_codes[a7_language()]; }

/* dcr_path.c / bionic_io.c / bionic_stdio.c: which paths to log ([io] fopen
 * -> its FILE* or 0x0, [io] open -> fd, [io] stat). The APK and the OBBs
 * always; everything with [debug] trace_files. */
int dcr_path_traced(const char *p) {
  if (!p)
    return 0;
  return g_a7_trace_files || strstr(p, ".apk") != NULL || strstr(p, ".obb") != NULL;
}
