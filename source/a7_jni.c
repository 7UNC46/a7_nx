/* a7_jni.c -- the Java side of Asphalt 7, answered in C.
 *
 * The engine builds its class names at run time ("com/gameloft/android/ANMP/
 * GloftA7HM" + "/Game"...) and looks the methods up by name and signature.
 * The list below is every Java method of the APK whose name AND signature
 * are strings in libAndroid.so (classes.dex, read with tools/dexnat.py and a
 * bytecode walk) -- the ones the engine can call -- with what the phone's
 * code returns where that matters (OBB names, folders, the language...).
 * A method missing here falls through to the runtime's default answer (0 /
 * null) and is logged once as "[jni] unhandled ..." in debug.log: that is
 * the to-do list during bring-up. MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <unistd.h>

#include "dcr_path.h"
#include "dcr_setup.h"
#include "jni.h"
#include "port_config.h"
#include "rt_applet.h"
#include "util.h"

#include "a7.h"

void a7_set_paths(void); /* a7_main.c */

/* ------------------------------------------------------------ helpers */
/* Strings handed to the engine are kept alive for good (the runtime's
 * DeleteLocalRef frees at once; Gameloft code reads after it). */
static jvalue ret_str(const char *s) {
  static struct { char *val; JObj *o; } cache[64];
  static int n;
  static Mutex lock;
  mutexLock(&lock);
  for (int i = 0; i < n; i++)
    if (!strcmp(cache[i].val, s)) {
      mutexUnlock(&lock);
      return jv_l(cache[i].o);
    }
  JObj *o = jni_str(s);
  if (o) {
    o->immortal = 1;
    if (n < 64 && (cache[n].val = strdup(s)))
      cache[n++].o = o;
  }
  mutexUnlock(&lock);
  return jv_l(o);
}

static jvalue ret_bytes(const void *p, int len) {
  JObj *a = jni_array('B', len);
  if (a && len > 0)
    memcpy(a->a.data, p, len);
  return jv_l(a);
}

static const char *arg_str(const jvalue *a, int i) { return a ? jni_utf(a[i].l) : ""; }

/* ------------------------------------------------------------ device facts */
#define A7_DEVICE_ID "00000000000000a7"
#define A7_MAC       "02:00:00:00:00:a7"
#define A7_MODEL     "Switch"
#define A7_MAKER     "Nintendo"
#define A7_FIRMWARE  "4.0.4" /* an ICS phone: what the game was made for */
#define A7_UA        "Mozilla/5.0 (Linux; U; Android 4.0.4; en-us; Switch) AppleWebKit/534.30 (KHTML, like Gecko)"
/* DeviceConfig.xml picks its profile by GPU name (gpucpu configs with
 * commonname "nvidia"): the Switch's is a Tegra X1. */
#define A7_GPU       "NVIDIA Tegra X1"
#define A7_CPU       "ARMv7 Processor rev 1 (v7l)"

static JNI_H_DECL(h_obb_dir) { return ret_str(A7_ANDROID_OBB); }          /* getSDFolder: no slash */
static JNI_H_DECL(h_obb_path) { return ret_str(A7_ANDROID_OBB "/"); }     /* Game.GetObbPath: with */
static JNI_H_DECL(h_obb_main) { return ret_str(A7_OBB_MAIN); }
static JNI_H_DECL(h_obb_patch) { return ret_str(A7_OBB_PATCH); }
static JNI_H_DECL(h_files_dir) { return ret_str(A7_ANDROID_FILES); }
static JNI_H_DECL(h_package) { return ret_str(PORT_PACKAGE); }
static JNI_H_DECL(h_device_id) { return ret_str(A7_DEVICE_ID); }
static JNI_H_DECL(h_mac) { return ret_str(A7_MAC); }
static JNI_H_DECL(h_model) { return ret_str(A7_MODEL); }
static JNI_H_DECL(h_maker) { return ret_str(A7_MAKER); }
static JNI_H_DECL(h_maker_model) { return ret_str(A7_MAKER " " A7_MODEL); }
static JNI_H_DECL(h_firmware) { return ret_str(A7_FIRMWARE); }
static JNI_H_DECL(h_user_agent) { return ret_str(A7_UA); }
static JNI_H_DECL(h_gpu) { return ret_str(A7_GPU); }
static JNI_H_DECL(h_cpu) { return ret_str(A7_CPU); }
static JNI_H_DECL(h_cpu_freq) { return jv_f(1785000.0f); } /* kHz, as cpufreq reports it */
static JNI_H_DECL(h_cores) { return jv_i(2); }
static JNI_H_DECL(h_memory) { return jv_i(3072); }           /* MB (DeviceConfig: "limit (MB)") */
static JNI_H_DECL(h_empty) { return ret_str(""); }
static JNI_H_DECL(h_lang_num) { return jv_i(a7_language()); }
static JNI_H_DECL(h_lang_code) { return ret_str(a7_language_code()); }
static JNI_H_DECL(h_country) { return ret_str("US"); }
static JNI_H_DECL(h_carrier) { return ret_str("none"); }
static JNI_H_DECL(h_tex_format) { return jv_i(g_a7_cfg.tex_format); }
static JNI_H_DECL(h_unique_bytes) { return ret_bytes(A7_DEVICE_ID, (int)strlen(A7_DEVICE_ID)); }

static JNI_H_DECL(h_time_ms) { return jv_j((jlong)(armTicksToNs(armGetSystemTick()) / 1000000ull)); }

static JNI_H_DECL(h_glgame_obj) { return jv_l(jni_singleton(A7_CLS_GLGAME)); } /* SUtils.getContext */

static JNI_H_DECL(h_gluid) {
  JObj *arr = jni_array('I', 4);
  if (arr) {
    jint *v = (jint *)arr->a.data;
    v[0] = 0x4137; v[1] = 0x4e58; v[2] = 0; v[3] = 1;
  }
  return jv_l(arr);
}

/* SUtils.getPreferenceString(key, file[, default]): the SharedPreferences the
 * installer wrote on a phone. */
static JNI_H_DECL(h_pref) {
  const char *key = arg_str(a, 0), *file = arg_str(a, 1);
  const char *val = NULL;
  if (!strcmp(key, "mainFileName"))
    val = A7_OBB_MAIN;
  else if (!strcmp(key, "patchFileName"))
    val = A7_OBB_PATCH;
  else if (!strcmp(key, "SDFolder"))
    val = A7_ANDROID_OBB;
  if (!val) {
    int nargs = m && strstr(m->sig, ";Ljava/lang/String;Ljava/lang/String;)") ? 3 : 2;
    val = nargs == 3 ? arg_str(a, 2) : "";
  }
  static int told;
  if (told < 24) {
    told++;
    debugPrintf("[a7] getPreferenceString(\"%s\", \"%s\") -> \"%s\"\n", key, file, val);
  }
  return ret_str(val);
}

/* ------------------------------------------------------------ the game */
static JNI_H_DECL(h_exit) {
  debugPrintf("[a7] %s.Exit: leaving\n", m && m->cls ? m->cls->name : "?");
  rt_request_exit();
  log_flush_ring();
  _exit(0);
  return jv_none();
}

static JNI_H_DECL(h_log_call) {
  static int told;
  if (told < 40) {
    told++;
    debugPrintf("[a7] %s.%s(\"%s\") ignored\n", m->cls->name, m->name,
                a && m->sig[1] == 'L' ? arg_str(a, 0) : "");
  }
  return jv_none();
}

static JNI_H_DECL(h_print_debug) {
  debugPrintf("[game] %s\n", arg_str(a, 0));
  return jv_none();
}

/* TODO: the on-screen keyboard (player name). swkbd, then the text back
 * through GLGame.nativeonEditorAction. */
static JNI_H_DECL(h_show_keyboard) {
  debugPrintf("[a7] Game.ShowKeyboard(%d, %d): no keyboard yet\n", a ? a[0].i : 0, a ? a[1].i : 0);
  return jv_none();
}

/* ------------------------------------------------------------ glf */
static JNI_H_DECL(h_setup_paths) {
  a7_set_paths();
  return jv_none();
}

static JNI_H_DECL(h_create_view) {
  debugPrintf("[a7] GL2JNILib.createView: the view is the port's window (a7_gl.c)\n");
  return jv_none();
}

static JNI_H_DECL(h_swap) {
  g_a7_swapped = 1;
  a7_gl_swap();
  return jv_none();
}

static JNI_H_DECL(h_view_settings) {
  /* setViewSettings(...5 ints): the EGL config the engine would like (bits,
   * depth, stencil, samples?). Logged: a7_gl.c makes RGBA8 D24S8. */
  debugPrintf("[a7] GL2JNILib.setViewSettings(%d, %d, %d, %d, %d)\n", a ? a[0].i : 0, a ? a[1].i : 0,
              a ? a[2].i : 0, a ? a[3].i : 0, a ? a[4].i : 0);
  return jv_none();
}

static JNI_H_DECL(h_set_context) {
  /* setCurrentContext(i): an extra (loader) context current on the calling
   * thread. None were made (a7_main.c): no. */
  static int told;
  if (told++ < 4)
    debugPrintf("[a7] GL2JNILib.setCurrentContext(%d): no extra contexts -> false\n", a ? a[0].i : 0);
  return jv_z(0);
}

/* GL2JNILib.getResource(name): Class.getResourceAsStream, i.e. an entry at
 * the top of the APK ("/name", or "name" relative to the class). */
static JNI_H_DECL(h_get_resource) {
  const char *name = arg_str(a, 0);
  while (*name == '/')
    name++;
  mz_zip_archive zip;
  memset(&zip, 0, sizeof zip);
  if (!*name || !dcr_apk_path()[0] || !mz_zip_reader_init_file(&zip, dcr_apk_path(), 0)) {
    debugPrintf("[a7] getResource(\"%s\"): no APK\n", name);
    return jv_l(NULL);
  }
  size_t len = 0;
  void *p = mz_zip_reader_extract_file_to_heap(&zip, name, &len, 0);
  mz_zip_reader_end(&zip);
  debugPrintf("[a7] getResource(\"%s\"): %s (%u bytes)\n", name, p ? "found" : "not in the APK", (unsigned)len);
  if (!p)
    return jv_l(NULL);
  jvalue r = ret_bytes(p, (int)len);
  free(p);
  return r;
}

/* ------------------------------------------------------------ sound
 * android.media.AudioTrack: a7_audio.c (Vox streams its mix through it). */
JNI_H_DECL(a7_at_min_buffer);
JNI_H_DECL(a7_at_init);
JNI_H_DECL(a7_at_play);
JNI_H_DECL(a7_at_pause);
JNI_H_DECL(a7_at_stop);
JNI_H_DECL(a7_at_flush);
JNI_H_DECL(a7_at_release);
JNI_H_DECL(a7_at_write);
JNI_H_DECL(a7_at_volume);
JNI_H_DECL(a7_at_state);
JNI_H_DECL(a7_at_play_state);
JNI_H_DECL(a7_at_rate);
JNI_H_DECL(a7_at_channels);
JNI_H_DECL(a7_at_head);
JNI_H_DECL(a7_at_max_volume);
JNI_H_DECL(a7_at_min_volume);

/* ------------------------------------------------------------ the tables */
#define GM  A7_CLS_GAME
#define GG  A7_CLS_GLGAME
#define SU  A7_CLS_SUTILS
#define DV  A7_CLS_DEVICE
#define SI  A7_CLS_SENDINFO
#define PN  A7_CLS_PUSH
#define SP  A7_CLS_SPLASH
#define GLF A7_CLS_GLF
#define GO  A7_CLS_GLOT
#define SO  A7_CLS_SOCIAL
#define FB  A7_CLS_FB
#define IAB A7_CLS_IAB
#define IAD A7_JPKG "iab/utils/Device"
#define IAS A7_JPKG "iab/utils/SUtils"
#define IAE A7_JPKG "iab/utils/Encrypter"
#define ENC A7_JPKG "GLUtils/Encrypter"
#define CT  A7_JPKG "GLUtils/ConnectionTimer"
#define CTI A7_JPKG "iab/utils/ConnectionTimer"
#define AT  "android/media/AudioTrack"

const JMethodDef jni_method_defs[] = {
    /* Game: the phone, the OBBs, the overlays the game opens */
    {GM, "getSDFolder", NULL, h_obb_dir},
    {GM, "GetObbPath", NULL, h_obb_path},
    {GM, "GetMainObbName", NULL, h_obb_main},
    {GM, "GetPatchObbName", NULL, h_obb_patch},
    {GM, "GetExtraObbName", NULL, h_empty},
    {GM, "GetPhoneGPUName", NULL, h_gpu},
    {GM, "GetPhoneCPUName", NULL, h_cpu},
    {GM, "GetPhoneCPUFreq", NULL, h_cpu_freq},
    {GM, "GetPhoneManufacturer", NULL, h_maker},
    {GM, "GetPhoneModel", NULL, h_model},
    {GM, "GetPhoneLanguage", NULL, h_lang_num},
    {GM, "GetTextureFormat", NULL, h_tex_format},
    {GM, "GetTrackingDeviceFirmware", NULL, h_firmware},
    {GM, "GetTrackingDeviceLanguage", NULL, h_lang_code},
    {GM, "GetTrackingDeviceModel", NULL, h_model},
    {GM, "GetGLDID", NULL, h_device_id},
    {GM, "GetCurrentTime", NULL, h_time_ms},
    {GM, "GetLocalIPAddr", NULL, jni_h_zero},
    {GM, "getAndFW", NULL, h_firmware},
    {GM, "getBase64IMEI", NULL, h_device_id},
    {GM, "getDeviceCountry", NULL, h_country},
    {GM, "getDeviceLang", NULL, h_lang_code},
    {GM, "getDeviceName", NULL, h_maker_model},
    {GM, "getDeviceUniqueId", NULL, h_unique_bytes},
    {GM, "getIMEI", NULL, h_device_id},
    {GM, "getIdentifier", NULL, h_device_id},
    {GM, "getMACAddress", NULL, h_mac},
    {GM, "getMac", NULL, h_mac},
    {GM, "getManufacture", NULL, jni_h_zero},
    {GM, "getNumberProcessors", NULL, h_cores},
    {GM, "getTotalMemory", NULL, h_memory},
    {GM, "getUserAgent", NULL, h_user_agent},
    {GM, "isDeviceOpened", NULL, jni_h_false},
    {GM, "isInForeground", NULL, jni_h_true},
    {GM, "isTablet", NULL, jni_h_false},
    {GM, "isWifiEnabled", NULL, jni_h_zero},
    {GM, "isWifiOr3GEnabled", NULL, jni_h_zero},
    {GM, "isXperiaPlay", NULL, jni_h_false},
    {GM, "removeTrafficCars", NULL, jni_h_false},
    {GM, "checkFreeCashReady", NULL, jni_h_false},
    {GM, "Exit", NULL, h_exit},
    {GM, "PrintDebug", NULL, h_print_debug},
    {GM, "ShowKeyboard", NULL, h_show_keyboard},
    {GM, "SetKeyboardText", NULL, h_log_call},
    {GM, "SetIfNativeSwapBuffer", NULL, jni_h_void},
    {GM, "HideLoadingText", NULL, jni_h_void},
    {GM, "Pause", NULL, jni_h_void},
    {GM, "TrackingLaunchGame", NULL, jni_h_void},
    {GM, "sendAppToBackground", NULL, jni_h_void},
    {GM, "openBrowser", NULL, h_log_call},
    {GM, "requestHttp", NULL, h_log_call},
    {GM, "launchGLLive", NULL, h_log_call},
    {GM, "launchIGP", NULL, h_log_call},
    {GM, "OpenIGPFreemium", NULL, h_log_call},
    {GM, "GLPopup", NULL, h_log_call},
    {GM, "ShowAlert", NULL, h_log_call},
    {GM, "ShowPlusOne", NULL, jni_h_void},
    {GM, "ShowSelectSNS", NULL, jni_h_void},
    {GM, "showAds", NULL, jni_h_void},
    {GM, "hideAds", NULL, jni_h_void},
    {GM, "showInterstitial", NULL, jni_h_void},
    {GM, "showFreeCash", NULL, jni_h_void},
    {GM, "loadFreeCash", NULL, jni_h_void},
    {GM, "showWelcomeScreen", NULL, jni_h_void},
    {GM, "notifyTrophy", NULL, jni_h_void},

    /* GLGame (the activity) */
    {GG, "getSDFolder", NULL, h_obb_dir},
    {GG, "getIMEI", NULL, h_device_id},
    {GG, "GetGLDID", NULL, h_device_id},
    {GG, "getUserAgent", NULL, h_user_agent},
    {GG, "Exit", NULL, h_exit},
    {GG, "isDeviceOpened", NULL, jni_h_false},
    {GG, "isHTCDevice", NULL, jni_h_false},
    {GG, "isHoneycombDevice", NULL, jni_h_false},
    {GG, "isIceCreamSandwich", NULL, jni_h_true},
    {GG, "isSamsung_I9250", NULL, jni_h_false},
    {GG, "isSamsung_P7100", NULL, jni_h_false},
    {GG, "useGeneration0", NULL, jni_h_false},
    {GG, "useGeneration1", NULL, jni_h_false},
    {GG, "useGeneration2", NULL, jni_h_false},
    {GG, "ShowAlert", NULL, h_log_call},
    {GG, "ShowPlusOne", NULL, jni_h_void},
    {GG, "ShowSelectSNS", NULL, jni_h_void},

    /* GLUtils.SUtils / Device / SendInfo / Encrypter / ConnectionTimer */
    {SU, "getContext", NULL, h_glgame_obj},
    {SU, "getPackage", NULL, h_package},
    {SU, "getPreferenceString", NULL, h_pref},
    {SU, "getSDFolder", NULL, h_obb_dir},
    {SU, "getSaveFolder", NULL, h_files_dir},
    {SU, "hasConnectivity", NULL, jni_h_zero},
    {SU, "initHasConnectivity", NULL, jni_h_zero},
    {SU, "release", NULL, jni_h_void},
    {SU, "shareInfo", NULL, jni_h_void},
    {DV, "getAndroidId", NULL, h_device_id},
    {DV, "getDeviceId", NULL, h_device_id},
    {DV, "getIMEI", NULL, h_device_id},
    {DV, "getUserAgent", NULL, h_user_agent},
    {SI, "getGLDID", NULL, h_device_id},
    {SI, "getGLUID", NULL, h_gluid},
    {SI, "getIMEI", NULL, h_device_id},
    {SI, "getLocaleCountry", NULL, h_country},
    {SI, "getLocaleLanguage", NULL, h_lang_code},
    {SI, "getManufacturerModel", NULL, h_maker_model},
    {SI, "getPhoneCarrier", NULL, h_carrier},
    {SI, "getPreferenceString", NULL, h_pref},
    {SI, "getSDFolder", NULL, h_obb_dir},
    {SI, "getSaveFolder", NULL, h_files_dir},
    {ENC, "getValue", NULL, h_empty},
    {CT, "stop", NULL, jni_h_void},

    /* glf: the window, swaps, paths */
    {GLF, "setupPaths", NULL, h_setup_paths},
    {GLF, "createView", NULL, h_create_view},
    {GLF, "SwapBuffer", NULL, h_swap},
    {GLF, "SwapBufferLoading", NULL, h_swap},
    {GLF, "setViewSettings", NULL, h_view_settings},
    {GLF, "setCurrentContext", NULL, h_set_context},
    {GLF, "getResource", NULL, h_get_resource},
    {GLF, "enableAccelerometer", NULL, jni_h_void},
    {GLF, "SetOrientationLocked", NULL, jni_h_void},
    {GLF, "isHTCDevice", NULL, jni_h_false},
    {GLF, "isHoneycombDevice", NULL, jni_h_false},
    {GLF, "isIceCreamSandwich", NULL, jni_h_true},
    {GLF, "isSamsung_I9250", NULL, jni_h_false},
    {GLF, "isSamsung_P7100", NULL, jni_h_false},
    {GLF, "useGeneration0", NULL, jni_h_false},
    {GLF, "useGeneration1", NULL, jni_h_false},
    {GLF, "useGeneration2", NULL, jni_h_false},

    /* glot (tracking) */
    {GO, "getDeviceCarrier", NULL, h_carrier},
    {GO, "getDeviceCountry", NULL, h_country},
    {GO, "getDeviceFirmware", NULL, h_firmware},
    {GO, "getDeviceLanguage", NULL, h_lang_code},
    {GO, "getDeviceName", NULL, h_maker_model},
    {GO, "getIdentifier", NULL, h_device_id},
    {GO, "getMac", NULL, h_mac},
    {GO, "getSystemUpTimeMillis", NULL, h_time_ms},
    {GO, "hasConnectivity", NULL, jni_h_false},

    /* Online services: off (no network, as in a6_nx) */
    {PN, "IsEnable", NULL, jni_h_false},
    {PN, "IsDontDisturbEnable", NULL, jni_h_false},
    {PN, "HasPushNotification", NULL, jni_h_zero},
    {PN, "GetRegistrationID", NULL, h_empty},
    {PN, "GetUserID", NULL, h_empty},
    {PN, "GetJanusToken", NULL, h_empty},
    {PN, "SendPush", NULL, h_empty},
    {PN, "SendPushToMyself", NULL, h_empty},
    {PN, "GetStatus", NULL, jni_h_zero},
    {PN, "DeletePush", NULL, jni_h_zero},
    {PN, "SetUserCredential", NULL, jni_h_zero},
    {PN, "SetCustomUserCredential", NULL, jni_h_zero},
    {PN, "SetOfflineDeviceCredential", NULL, jni_h_zero},
    {PN, NULL, NULL, jni_h_void}, /* the setters, CancelAll, GetBundleData (null) */
    {SP, "isActive", NULL, jni_h_zero},
    {SP, "isWSReady", NULL, jni_h_zero},
    {SP, NULL, NULL, jni_h_void}, /* welcome screens: downloadWS, showLocalWS... */
    {SO, "GetDataFromURL", NULL, jni_h_null},
    {FB, "IsLoggedIn", NULL, jni_h_false},
    {FB, "hasPermission", NULL, jni_h_false},
    {FB, "getAccessToken", NULL, h_empty},
    {FB, NULL, NULL, jni_h_void}, /* the rest: requests that never answer */
    {IAB, "getSDFolder", NULL, h_obb_dir},
    {IAD, "getDeviceId", NULL, h_device_id},
    {IAD, "getIMEI", NULL, h_device_id},
    {IAD, "getUserAgent", NULL, h_user_agent},
    {IAS, "getContext", NULL, h_glgame_obj},
    {IAS, "getPreferenceString", NULL, h_pref},
    {IAS, "release", NULL, jni_h_void},
    {IAE, "getValue", NULL, h_empty},
    {CTI, "stop", NULL, jni_h_void},

    /* android.media.AudioTrack (a7_audio.c) */
    {AT, "getMinBufferSize", NULL, a7_at_min_buffer},
    {AT, "<init>", NULL, a7_at_init},
    {AT, "play", NULL, a7_at_play},
    {AT, "pause", NULL, a7_at_pause},
    {AT, "stop", NULL, a7_at_stop},
    {AT, "flush", NULL, a7_at_flush},
    {AT, "release", NULL, a7_at_release},
    {AT, "write", NULL, a7_at_write},
    {AT, "setStereoVolume", NULL, a7_at_volume},
    {AT, "getState", NULL, a7_at_state},
    {AT, "getPlayState", NULL, a7_at_play_state},
    {AT, "getSampleRate", NULL, a7_at_rate},
    {AT, "getPlaybackRate", NULL, a7_at_rate},
    {AT, "getChannelCount", NULL, a7_at_channels},
    {AT, "getPlaybackHeadPosition", NULL, a7_at_head},
    {AT, "getMaxVolume", NULL, a7_at_max_volume},
    {AT, "getMinVolume", NULL, a7_at_min_volume},
    {AT, NULL, NULL, jni_h_zero}, /* setPlaybackRate, setNotificationMarker...: success */

    {NULL, NULL, NULL, NULL},
};

const JFieldDef jni_field_defs[] = {
    /* vox asks for the API level (OpenSL ES from 9); ICS, as the game targets. */
    {"android/os/Build$VERSION", "SDK_INT", NULL, 15, NULL},
    {"android/os/Build", "MODEL", NULL, 0, A7_MODEL},
    {"android/os/Build", "MANUFACTURER", NULL, 0, A7_MAKER},
    {"android/os/Build", "DEVICE", NULL, 0, "nx"},
    {NULL, NULL, NULL, 0, NULL},
};

/* GLGame is an Activity (SUtils.getContext hands it out as the Context). */
const char *const jni_class_supers[][2] = {
    {A7_CLS_GLGAME, A7_CLS_GLF_ACT},
    {A7_CLS_GLF_ACT, "android/app/Activity"},
    {"android/app/Activity", "android/content/Context"},
    {NULL, NULL},
};
const char *const jni_missing_classes[] = {NULL};
