# Changelog

## 0.2.0 (2026-11-21)

First version that runs on hardware: menus, races and sound.

### Fixed
* Start-up crash in `Game.nativeInit`: it takes its JNIEnv from
  `NVThreadGetCurrentJNIEnv()`, which is NULL until `NVThreadInit(vm)`. Now called
  right after `JNI_OnLoad`.
* Crash in `GL2JNILib.setPaths`: the paths were set before glf's App existed. They
  are set by the engine itself from `getViewSettings` (`NewApp`, then `setupPaths`);
  the port's fallback now runs after it.
* Crash when leaving the game (`createDelayedLocalNotification` -> `strcmp(NULL)`):
  `C2DMAndroidUtils.nativeInit` was never called, so every push method ID was 0.
* Keys stuck after one press (brake/drift, look back): `nativeHandleKey` takes the
  `KeyEvent` action (0 = down, 1 = up), not a "pressed" flag.
* Steering was inverted.

### Added
* Sound: `android.media.AudioTrack` (`source/a7_audio.c`), resampled to 48 kHz
  stereo and played through audout. `write()` blocks like on a phone, which also
  stops the mixer thread from spinning on the shared core.
* The OBBs are opened once and shared by every read-only `fopen`
  (`RT_STDIO_SHARED_SUFFIX`): glf opened the 1 GB OBB for each file (20 ms each).
* CPU boost during long frames (loading): `RT_BOOST_WATCH_THREAD`.
* Shaders with `texture2D(sampler2D, vec3)` are rewritten when Mesa rejects them
  (`source/a7_shader.c`).
* `[pace]` in `debug.log`: late frames per 300 frames.
* "bad method ID" is logged once per caller.

## 0.1.0

Bring-up, not run on hardware.
