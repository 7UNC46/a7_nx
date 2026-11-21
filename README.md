<div align="center">

<img src="launcher/icon.jpg" alt="a7_nx" width="160">

# a7_nx

**Asphalt 7: Heat on Nintendo Switch**

An unofficial Nintendo Switch wrapper for the Android version of
**Asphalt 7: Heat**, built on the a6_nx runtime.

</div>

---

## Status

**Version 0.2.0: playable.** Menus, races and sound work on hardware; a race
can be played from the start to the results screen.

What works:

* Start-up, menus, a quick race from the start to the results, saving the profile
* Sound (the game's Java `AudioTrack` stream, played through audout)
* Steering with the left stick, brake/drift, look back
* Touch in handheld mode, and a pointer (L) for every menu the keys miss

Known issues:

* Some menus: the D-pad does not reach the buttons in the middle (use touch or the pointer)
* Loading a race takes about 20 s; small frame drops in races
* All game threads share one CPU core (a race in Glitch otherwise), which limits the frame rate.
  If 60 fps stutters, try 30 fps (`config.ini`)
* Some shaders use `texture2D(sampler2D, vec3)`, which Mesa rejects: they are rewritten
  when they fail to compile (`source/a7_shader.c`; still to confirm on hardware)

`debug.log` shows what to look at: `[jni] unhandled ...` (Java calls still to answer),
`[pace]` (frames late per 300 frames), `[frame]` (frames over 120 ms, with the CPU use of
each thread), `[shader]`.

The port was made with one build of the library:
`libAndroid.so`, md5 `c2aadf216032fa46faecb1e5de49ba06`, 20 723 436 bytes. That build came from
a modified APK. Other builds of the same version (main 1060, patch 1110) should work, because the port
finds everything by name and not by address.

---

## Files on the SD card

```text
sd:/switch/a7_nx/
├── a7_nx.nro
├── a7.apk                     your APK (any name)
├── libAndroid.so              only if the APK has none in lib/
└── obb/
    ├── main.1060.com.gameloft.android.ANMP.GloftA7HM.obb
    └── patch.1110.com.gameloft.android.ANMP.GloftA7HM.obb
```

OBBs placed directly in `a7_nx/` are moved into `obb/` at the first start.
A `libAndroid.so` in the folder always takes precedence over the one in the APK.

1. In the homebrew menu (sphaira), install a forwarder for **Asphalt 7: Heat**.
2. Start it from the HOME menu. Then send `debug.log` (and `crash.log` with
   `a7_nx.elf` if the game closed).

---

## Controls

| Input | Action |
| --- | --- |
| Left Stick | Steer (sent as the phone's tilt) |
| ZR | Brake / drift |
| D-Pad | Menus; in a race, the Xperia Play keys (one of them: look back) |
| A / B | Confirm / back |
| X / Y / R / ZL | Xperia Play keys (what each does in a race: still to map) |
| + / - | Start (pause) / select |
| L | Pointer on/off: Right Stick moves it, ZR taps |
| Touchscreen | Touch in handheld mode |

`config.ini` (written at the first start): `fps` (30/60), `steer_mode`
(stick, motion, both), `motion_invert`, `boost`, resolution and texture format.

--- | --- |
| Left Stick | Steer (as the phone's tilt) |
| D-Pad, A, B | Android D-pad, centre, back |
| X / Y / R / ZL / ZR | Android gamepad keys (Xperia Play layout) |
| + / - | Start / select |
| L | Pointer on/off: Right Stick moves it, ZR taps |
| Touchscreen | Touch in handheld mode |

---

## Build

As a6_nx: Docker, the vita2hos AArch32 image, libnx32 and mesa32
(`tools/setup_deps.sh`, once), then

```bash
./build.sh
launcher/build.sh      # -> launcher/a7_nx.nro
```

---

## Tools

* `tools/needed_from_so.py <dir with libAndroid.so>`: the import list (then
  `python3 runtime/tools/gen_imports.py`)
* `tools/dexnat.py`, `tools/dexmethods.py`: the APK's native and Java methods
* `tools/dexcalls.py <classes.dex> <regex>`: which Java code calls a method
* `tools/dexflow.py <classes.dex> 'La/b/C;' [method]`: what a method does, in order

---

## Credits

**Asphalt 7: Heat**: Gameloft

Based on [a6_nx](https://github.com/7UNC46/a6_nx) (7UNC46). The runtime comes from
[mc3_nx](https://github.com/Kaays/mc3_nx) (Kaays) and
[a8retry_nx](https://github.com/aks796/a8retry_nx) (aks796). The loader derives
from the Android `.so` loader work by Andy Nguyen (TheOfficialFloW) and fgsfds,
ported to AArch32 with reference to vita2hos. MIT-licensed, see `LICENSE`.

---

## Disclaimer

This is an unofficial fan project, not affiliated with Nintendo or Gameloft.
Asphalt is a trademark of Gameloft.

***No game code or assets are included. You need your own copy of the game.***
