# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

A GStreamer plugin wrapping the Agora Linux RTC SDK, trimmed to the single **`agoraioudp`**
element and targeting **aarch64 (ARM64) only** with the **Agora Linux RTC SDK v4.4.32**
(SDK build 675674), vendored under `agora/sdk/agora_sdk_aarch64_4.4.32/`.

`agoraioudp` is bidirectional: its GStreamer pads carry encoded **H.264** video (sink =
publish to Agora, src = remote video out), while audio is bridged over local UDP
(`inport` = Opus in → Agora, `outport` = PCM out from Agora). The other elements
(`agorasink`/`agorasrc`/`agoraio`), other SDKs, and cross-compile tooling were removed.

## Architecture

Two layers, connected by a flat C ABI:

1. **`agora/libagorac/`** — C++ backend built as `libgstagorac.so`. `agorac.cpp` exposes an
   `extern "C"` API (`agorac.h`); the real work is in class `AgoraIo` (`agoraio.cpp`), which
   owns the Agora service/connection, publish/subscribe, and A/V sync. Sub-dirs: `observer/`
   (SDK callbacks), `helpers/` (encode/decode, log, utilities), `file_parser/`, `syncbuffer.cpp`.

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
  `/usr/local/lib` (the install defaults).

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
   `agoraio_set_video_dimensions()` into `AgoraIo::_videoWidth/_videoHeight/_videoFps`. Caps arrive
   before `agora_ctx` exists (created lazily on the first buffer), so the values are cached on the
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

## Conventions

- `libgstagorac.so` is a **build artifact** and is git-ignored (do not commit it). Vendored SDK
  `.so` files under `agora/sdk/` are tracked.
- SDK runtime droppings (`agora*.dat`, `agora_cache.db`, `common_resource/`, crash contexts) are
  git-ignored.
- A/V sync: `out-audio-delay`/`out-video-delay` (ms) compensate for the local device's audio-vs-video
  output latency; still needed (the SDK doesn't do this for you). Untouched by the upgrade.
