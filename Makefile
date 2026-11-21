#---------------------------------------------------------------------------------
# Asphalt 7: Heat -- Nintendo Switch wrapper (32-bit / AArch32, android32)
#
# Ships NO game code and NO game assets: the player's own APK is read at run
# time. Build with ./build.sh (the toolchain container).
#---------------------------------------------------------------------------------
TARGET               := a7_nx
# Pick an id no other android32 port uses (mc3_nx ...10C3, a6_nx ...10A6).
PORT_NPDM_PROGRAM_ID := 0x01000000000010A7
PORT_NPDM_VERSION    := 0.2.0
# Glitch + gameswf on the main thread: give it a large stack, as mc3 does.
PORT_NPDM_MAIN_STACK := 0x800000
# The engine reads its .bar archives with many small freads.
PORT_CFLAGS += -DRT_STDIO_READ_BUF=65536
# glf opens the 1 GB OBB for every file it reads from it (20 ms per open on
# the card): one handle per OBB, shared by every read-only fopen.
PORT_CFLAGS += -DRT_STDIO_SHARED_SUFFIX='".obb"'
# Long frames (loading) run boosted: without a watcher nothing polls during
# a frame that is one long call, and loads ran at normal clocks (hardware
# log 2026-11-20: no boosted frame after the start-up).
PORT_CFLAGS += -DRT_BOOST_WATCH_THREAD=1
# Guest threads on one core (source/dcr_sched.c): the runtime's 3-core
# scheduler self-test does not apply.
PORT_CFLAGS += -DRT_SCHED_SELFTEST=0

include runtime/runtime.mk

