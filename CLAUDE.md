# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

A GStreamer plugin wrapping the Agora Linux RTC SDK, trimmed to the single **`agoraioudp`**
element and targeting **aarch64 (ARM64) only** with the **Agora Linux RTC SDK v4.4.32**
(SDK build 675674), vendored under `agora/sdk/agora_sdk_aarch64_4.4.32/`.

`agoraioudp` publishes encoded **H.264** video (sink pad, caps
`video/x-h264,stream-format=byte-stream,alignment=au`) and bridges audio over local UDP
(`inport` = Opus in → Agora, `outport` = PCM out from Agora). Remote video on the src pad
is **opt-in** (`receive-video=true`); by default the backend never subscribes to remote
video at all (this fork serves an intercom: video publish-only, audio bidirectional).
The other elements (`agorasink`/`agorasrc`/`agoraio`), other SDKs, cross-compile tooling,
and the ffmpeg/x264 transcode path were removed.

## Architecture

Two layers, connected by a flat C ABI:

1. **`agora/libagorac/`** — C++ backend built as `libgstagorac.so`. `agorac.cpp` exposes an
   `extern "C"` API (`agorac.h`); the real work is in class `AgoraIo` (`agoraio.cpp`), which
   owns the Agora service/connection and publish/subscribe. Sub-dirs: `observer/` (SDK
   callbacks), `helpers/` (log, utilities, context). `syncbuffer.cpp` is a thread-free
   delay buffer: pure pass-through unless an in/out delay property is set. Backend logging
   (`helpers/agoralog.cpp`) has two tiers: `logMessage` (debug, `/tmp/agora.log`, gated at
   runtime by the `verbose` property) and `logInfo` (connection lifecycle — connected /
   user joined/left / publish state — always printed to stdout with an `AgoraIO:` prefix so
   the journal captures them; used for join/leave diagnostics in production).

2. **`gst-agora/plugin-src/agoraioudp/gstagoraioudp.c`** — the C element. It fills an
   `agora_config_t` (`agora/libagorac/agoraconfig.h`), calls `agoraio_init(&config)` for an
   opaque `AgoraIoContext_t*`, then `agoraio_send_video` on its chain function.

**Key seam:** the C plugin never touches the SDK directly — everything crosses `agorac.h`.
To surface a new plugin property: element property → `agora_config_t` → `AgoraIo` ctor/`init`.
The video-dimensions path is the exception/model to copy: caps event →
`agoraio_set_video_dimensions()` → `AgoraIo` members → `EncodedVideoFrameInfo` (see gotcha #1).

## Building

Native aarch64 only. `install.sh` builds the backend via the **makefile** (`make`), not CMake.

```sh
cd build
./build_all_aarch64_4.4.32.sh     # system install: backend+SDK -> /usr/local/lib, plugin -> gstreamer dir (sudo)
./build_local_aarch64_4.4.32.sh   # in-place build, installs NOTHING (for testing); prints the env to use
```

- `install.sh <sdkdir>` runs `make` + `sudo make install`, copies **all** `$sdkdir/agora_sdk/*.so`
  to `/usr/local/lib` (glob — see gotcha #3), installs headers, `ldconfig`.
- The plugin is built with meson. `gst-agora/plugin-src/meson.build` builds only `agoraioudp`
  and links the backend + SDK via **`link_args`** (see gotcha #2). `gst-agora/meson_options.txt`
  exposes `agorac_lib` / `agora_sdk_lib` so the local build points at the repo copies instead of
  `/usr/local/lib` (the install defaults). The C ABI headers (`agorac.h`/`agoraconfig.h`) always
  come from the repo via `include_directories` — never from a stale `/usr/local/include` copy.

Run pipelines with (system install):
```sh
export GST_PLUGIN_PATH=/usr/local/lib/aarch64-linux-gnu/gstreamer-1.0
```
For a **local** build also set `LD_LIBRARY_PATH` to the vendored SDK dir + `agora/libagorac`
(the script prints the exact lines). Agora SDK log: `~/.agora/agorasdk.log` (encrypted).

## SDK 4.4.x gotchas (hard-won — do not relearn these)

1. **Custom encoded video tracks need non-zero `width`/`height`.** A normal remote subscriber
   renders **black** if `EncodedVideoFrameInfo.width/height` are 0 (older SDKs inferred them; 4.4.x
   does not). Dimensions/fps are pulled from the sink caps in `gstagoraioudp.c` and pushed via
   `agoraio_set_video_dimensions()` into `AgoraIo::_videoWidth/_videoHeight/_videoFps`. Caps can
   arrive before `agora_ctx` exists (created on NULL→READY), so the values are cached on the
   element and re-applied in `init_agora()`.
2. **meson must link the backend with `-Wl,--no-as-needed`.** With the default `--as-needed`,
   `libgstagorac.so` is dropped and the plugin fails to link (undefined `agoraio_*`). It's passed
   in `link_args`, after the plugin objects.
3. **`libaosl.so` is a new required runtime lib** in 4.4.x (`libagora_rtc_sdk.so` NEEDs it, plus
   `libagora-fdkaac.so`). `install.sh` globs `*.so` so it's always copied — do not revert to an
   explicit file list.
4. **Codec: send H.264.** `SenderOptions` defaults to **H.265** in 4.4.x, but the per-frame
   `EncodedVideoFrameInfo.codecType = VIDEO_CODEC_H264` (agoraio.cpp) is what the receiver uses, so
   that line is required; the track-level `SenderOptions.codecType` is not.
5. **Observer signatures are `SDK_BUILD_NUM`-guarded.** `observer/userobserver.{h,cpp}` uses
   `#if SDK_BUILD_NUM>=675674` for the 4.4.x forms of `onUserVideoTrackSubscribed`
   (`const VideoTrackInfo&`), `onLocalVideoTrackStateChanged` (`LOCAL_VIDEO_STREAM_REASON`), and
   `add/removeRenderer` (now take a `VIDEO_MODULE_POSITION`). Keep both branches if touching them.
6. **License check is a no-op** for this SDK build: `verifyLicense()` (`helpers/utilities.cpp`) only
   reads `certificate.bin` under `#if SDK_BUILD_NUM==110077`; for 4.4.x it returns 0. No cert file
   is needed.
7. **Region: mainland China is excluded** — `scfg.areaCode = AREA_CODE_OVS` in
   `AgoraIo::initAgoraService()`. This is a deliberate fork behaviour.
8. **`libaosl` spams syslog — two layers of defence, keep both.** The 4.4.x `libaosl.so`
   logs harmless `AOSL: Java VM not set ...` warnings via `vsyslog` on non-Android
   platforms, unconditionally (`aosl_set_log_level` does NOT gate them).
   `quietAoslLogging()` (called from `initAgoraService()` **and again in `disconnect()`**):
   (a) resolves `aosl_set_vlog_func` via `dlopen`/`dlsym` (no header is shipped; needs
   `-ldl`) and installs a no-op sink — but the SDK swaps that sink back around service
   init/release, so it cannot cover teardown; and (b) calls
   `setlogmask(~(EMERG|ALERT))` — the per-thread-detach variant is logged at **emergency
   priority**, which journald broadcasts (wall) to every terminal as each SDK thread exits
   on disconnect. The logmask is process-level libc state that libaosl cannot reset. The
   no-op sink ignores its args, so it's safe regardless of the real callback signature —
   don't "improve" it into reading the format string.

## Conventions

- `libgstagorac.so` is a **build artifact** and is git-ignored (do not commit it). Vendored SDK
  `.so` files under `agora/sdk/` are tracked.
- SDK runtime droppings (`agora*.dat`, `agora_cache.db`, `common_resource/`, crash contexts) are
  git-ignored.
- A/V sync: `out-audio-delay`/`out-video-delay` (ms) compensate for the local device's audio-vs-video
  output latency; still needed (the SDK doesn't do this for you).
- **Teardown is idempotent and runs on both EOS and READY→NULL** (`gst_agoraioudp_teardown`):
  the SDK is disconnected first (its callback threads stop), then the audio bridge pipelines are
  released. `agora_ctx` is guarded by `ctx_lock` against the streaming/appsink/SDK threads.
  `AgoraIo::disconnect()` joins its watchdog thread and waits (bounded, 2 s) for the SDK's
  disconnect event — do not reintroduce detached threads or fixed sleeps here; gst-launch
  teardown wedges were a real production failure.
- Media buffers are accessed in place with `gst_buffer_map` on all four A/V paths — no per-frame
  `malloc`/copy. The SDK copies internally on send.
