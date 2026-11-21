/* port_config.h -- Asphalt 7: Heat for Switch (android32).
 *
 * com.gameloft.android.ANMP.GloftA7HM. The engine is Gameloft's 32-bit
 * Glitch build (ARMv7-A, VFPv3, softfp, SONAME libAndroid.so, GLES 2) on the
 * "glf" Java framework (com.gameloft.glf.GL2JNIActivity / GL2JNILib), the
 * same one Modern Combat 3 and Asphalt 8 use. No SWP instructions in it
 * (checked over .text), so nothing to rewrite as in a6_nx.
 *
 * The game data are two Google Play expansion files, opened by the engine
 * itself from the OBB folder:
 *   main.1060.com.gameloft.android.ANMP.GloftA7HM.obb   (~1.1 GB)
 *   patch.1110.com.gameloft.android.ANMP.GloftA7HM.obb  (~70 MB)
 * The runtime's path rules make /sdcard/Android/obb/<pkg>/ the game folder's
 * obb/ (dcr_path.c). MIT.
 */
#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

#define PORT_TITLE   "Asphalt 7: Heat"
#define PORT_NAME    "a7_nx"
#define PORT_PACKAGE "com.gameloft.android.ANMP.GloftA7HM"
#define PORT_VERSION "0.2.0"
/* First line of debug.log: name, title and version */
#define PORT_BANNER  PORT_NAME " v" PORT_VERSION ": " PORT_TITLE

/* The engine library. a7_setup.c looks for it in this order:
 *   1. <game folder>/libAndroid.so      (copied there by hand: always wins)
 *   2. lib/armeabi-v7a/libAndroid.so    in the APK
 *   3. lib/armeabi/libAndroid.so        in the APK
 *   4. any APK entry whose name ends in "libAndroid.so"
 * (the plan's own unpacking is not used: it knows one folder only). */
#define PORT_ABI_DIR "lib/armeabi-v7a/"
#define A7_LIB       "libAndroid.so"

/* The build the facts of this port were read from (JNI names, the Java
 * side): md5 c2aadf216032fa46faecb1e5de49ba06, 20723436 bytes. Its CRC-32 is
 * logged at every start ([a7] libAndroid.so: crc ...). 0 = accept any. */
#define A7_LIB_CRC32 0x00000000UL
#define A7_LIB_SIZE  20723436UL

/* The expansion files (Game.GetMainObbName / GetPatchObbName answer these;
 * on a phone the installer stored them in the "ExpansionPrefs" preferences). */
#define A7_OBB_MAIN  "main.1060." PORT_PACKAGE ".obb"
#define A7_OBB_PATCH "patch.1110." PORT_PACKAGE ".obb"
#define A7_OBB_DIR   "obb" /* in the game folder = /sdcard/Android/obb/<pkg> */

/* Sound: Gameloft's vox engine mixes everything itself and plays it through
 * OpenSL ES (vox::DriverAndroid::_InitOSL; it dlopen()s libOpenSLES.so),
 * served by the runtime's opensles.c on audout. */
#define RT_OPENSLES 1

/* libAndroid.so is 20 MB of code + 1 MB of data (p_vaddr + p_memsz of the
 * last LOAD: 0x1442c30): 32 MB is the runtime's default region already. */

/* Paths worth a line in debug.log (dcr_path_traced): the OBBs. */
#define RT_PATH_TRACE_EXTRA ".obb"

#endif /* PORT_CONFIG_H */
