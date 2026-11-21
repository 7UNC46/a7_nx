/* a7_setup.c -- what the first start needs besides the APK: the engine
 * library in the game folder, and the two expansion files where the engine
 * looks for them.
 *
 * The library: the runtime's own unpacking (port_setup_plan.libs) knows one
 * folder of the APK (PORT_ABI_DIR) and stops with an error when the library
 * is not there. Asphalt 7 builds differ (lib/armeabi/, lib/armeabi-v7a/,
 * repacks that carry it elsewhere), so this step looks itself, in the order
 * port_config.h gives, and a libAndroid.so copied into the game folder by
 * hand always wins (it is never overwritten).
 *
 * The OBBs: the engine opens them itself (Game.GetObbPath + GetMainObbName,
 * see a7_jni.c): /sdcard/Android/obb/<pkg>/<name>, which is the game
 * folder's obb/. OBBs put straight into the game folder are moved there
 * (same SD card: a rename, instant). Nothing is unpacked: 1.1 GB stays one
 * file. MIT.
 */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "dcr_path.h"
#include "dcr_setup.h"
#include "error.h"
#include "port_config.h"
#include "rt_boot.h"
#include "util.h"

#include "a7.h"

#define STAMP_LIB "lib:" A7_LIB /* not A7_LIB: that key is the runtime's own */

/* ------------------------------------------------------------ the library */
static int find_lib(mz_zip_archive *zip) {
  static const char *const k_where[] = {"lib/armeabi-v7a/" A7_LIB, "lib/armeabi/" A7_LIB};
  for (unsigned i = 0; i < sizeof k_where / sizeof k_where[0]; i++) {
    int idx = mz_zip_reader_locate_file(zip, k_where[i], NULL, 0);
    if (idx >= 0) {
      debugPrintf("[a7] the APK has %s\n", k_where[i]);
      return idx;
    }
  }
  const size_t ln = strlen(A7_LIB);
  unsigned n = mz_zip_reader_get_num_files(zip);
  for (unsigned i = 0; i < n; i++) {
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(zip, i, &st) || st.m_is_directory)
      continue;
    size_t l = strlen(st.m_filename);
    if (l >= ln && !strcmp(st.m_filename + l - ln, A7_LIB)) {
      debugPrintf("[a7] the APK has %s (not in a lib/ folder)\n", st.m_filename);
      return (int)i;
    }
  }
  return -1;
}

void a7_setup_lib(RtSetupCtx *ctx) {
  char dst[DCR_PATH_MAX];
  rt_root_path(dst, sizeof dst, A7_LIB);
  long have = rt_file_size(dst);
  unsigned long crc = 0, size = 0;
  int stamped = rt_setup_stamp_get(STAMP_LIB, &crc, &size);

  /* Copied by hand (no stamp of ours, or not the size we unpacked): keep it. */
  if (have > 0 && (!stamped || (unsigned long)have != size)) {
    debugPrintf("[a7] %s in the game folder (%ld bytes): using it, the APK's is not looked at\n", A7_LIB, have);
    return;
  }

  int idx = find_lib(ctx->apk);
  if (idx < 0) {
    if (have > 0)
      return; /* unpacked before; this APK has none: keep what is there */
    fatal_error("%s has no %s, and the game folder has none either.\n\n%s", ctx->apk_path, A7_LIB,
                port_apk_help());
  }
  mz_zip_archive_file_stat st;
  mz_zip_reader_file_stat(ctx->apk, (mz_uint)idx, &st);
  if (have == (long)st.m_uncomp_size && stamped && crc == (unsigned long)st.m_crc32)
    return; /* made from this APK before */

  debugPrintf("[a7] unpacking %s from the APK (%lu KB)\n", A7_LIB, (unsigned long)(st.m_uncomp_size >> 10));
  if (rt_setup_extract_entry(ctx->apk, idx, dst, "Unpacking the game's library", ctx->p0, ctx->p1) != 0)
    fatal_error("Could not write %s (SD card full or read-only?).", dst);
  rt_setup_stamp_set(STAMP_LIB, (unsigned long)st.m_crc32, (unsigned long)st.m_uncomp_size);
}

/* ------------------------------------------------------------ the OBBs */
/* <root>/obb/<name>: there, or moved there from <root>/<name>. */
static long place_obb(const char *name, int required) {
  char rel[128], in_obb[DCR_PATH_MAX], in_root[DCR_PATH_MAX];
  snprintf(rel, sizeof rel, A7_OBB_DIR "/%s", name);
  rt_root_path(in_obb, sizeof in_obb, rel);
  rt_root_path(in_root, sizeof in_root, name);

  long size = rt_file_size(in_obb);
  long loose = rt_file_size(in_root);
  if (loose > 0 && size != loose) {
    rt_mkdirs_for(in_obb);
    if (size >= 0)
      remove(in_obb); /* an older copy: the one put in the folder now wins */
    if (rename(in_root, in_obb) == 0) {
      debugPrintf("[a7] %s moved into %s/\n", name, A7_OBB_DIR);
      size = loose;
    } else
      debugPrintf("[a7] could not move %s into %s/ (it stays where it is)\n", name, A7_OBB_DIR);
  }
  if (size > 0)
    debugPrintf("[a7] %s/%s: %ld MB\n", A7_OBB_DIR, name, size >> 20);
  else if (required)
    fatal_error("The game data %s is missing.\n\n"
                "Copy the two OBB files of your own Asphalt 7 to\n"
                "  sd:" PORT_ROOT_PATH "/" A7_OBB_DIR "/\n"
                "(on a phone they are in Android/obb/" PORT_PACKAGE "/):\n"
                "  " A7_OBB_MAIN "\n"
                "  " A7_OBB_PATCH,
                name);
  else
    debugPrintf("[a7] %s/%s: not there (the game may still start without it)\n", A7_OBB_DIR, name);
  return size;
}

void a7_setup_obbs(RtSetupCtx *ctx) {
  (void)ctx;
  place_obb(A7_OBB_MAIN, 1);
  place_obb(A7_OBB_PATCH, 0);
}
