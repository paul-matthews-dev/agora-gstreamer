# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

This repo provides GStreamer plugins that wrap the Agora Linux RTC SDK, letting GStreamer
pipelines send media into and pull media out of an Agora real-time channel. It targets Linux
on x86_64 (Intel/AMD) and aarch64/arm64.

Four GStreamer elements are registered (each is a separate shared object installed into the
GStreamer plugin dir):

- **agorasink** — sink; forwards encoded H.264 video / Opus audio from a pipeline into an Agora channel.
- **agorasrc** — source; pulls encoded H.264 / raw audio out of an Agora channel into a pipeline.
- **agoraio** — combined src+sink using GStreamer app-style buffers (bidirectional).
- **agoraioudp** — bidirectional element that bridges Agora media to/from local UDP ports (`inport`/`outport`), used for the webcam/mic examples in the README.

## Architecture

The code is two layers:

1. **`agora/libagorac/`** — a C++ library (`libgstagorac.so`) that does all the real Agora SDK
   work. `agorac.cpp` exposes a flat `extern "C"` API (declared in `agorac.h`) so the C GStreamer
   plugins can call it. The main class is `AgoraIo` (`agoraio.cpp` / `agoraio.h`) — it owns the
   Agora service/connection, encode/decode, jitter buffering, and A/V sync. Supporting code:
   - `observer/` — Agora SDK callbacks (`pcmframeobserver`, `h264frameobserver`, `connectionobserver`, `userobserver`) that receive remote media and connection events.
   - `helpers/` — `agoraencoder`/`agoradecoder` (x264 + libavcodec + swscale), `agoralog`, `localconfig`, `utilities`, `uidtofile`.
   - `file_parser/` — H.264 / AAC / Opus elementary-stream parsers.
   - `syncbuffer.cpp` — A/V synchronization buffer.

2. **`gst-agora/plugin-src/<element>/gst<element>.c`** — thin C GStreamer element implementations.
   Each parses element properties into an `agora_config_t` struct (defined in
   `agora/libagorac/agoraconfig.h`), calls `agoraio_init(&config)` to get an opaque
   `AgoraIoContext_t*`, and in its chain function calls `agoraio_send_video` /
   `agoraio_send_audio[_with_duration]`. `gst_agorasink_chain(...)` in `gstagorasink.c` is the
   main data path / entrypoint to study first.

**Key seam:** the C plugins never touch the Agora SDK directly — everything crosses the
`extern "C"` boundary in `agorac.h`. When adding a plugin property that changes behavior, wire it
through: element property → `agora_config_t` field → `AgoraIo` constructor/`init` args.

`gst-agora/plugin-src/template/` and `shared/` (`agorah264parser`) are shared scaffolding.
`gst-agora/plugin-src/tools/make_element` generates a new element skeleton.

## Building

Build the C++ library first, then the plugins. The versioned build scripts in `build/` do both.
Run from the `build/` directory:

```sh
cd build
./build_all_4.2.30.sh              # x86_64, Agora SDK 4.2.30
./build_all_aarch64_4.2.32.sh      # native aarch64, Agora SDK 4.2.32
```

Each script:
1. `cd ../agora/libagorac && ./install.sh <path-to-agora-sdk>` — runs `make` + `sudo make install`,
   installs `libgstagorac.so`, the bundled `libagora_rtc_sdk.so` (and friends), and headers into
   `/usr/local/`, then `ldconfig`.
2. `cd ../gst-agora && meson build && ./install` — meson build + `sudo ninja install` of the four
   plugin `.so` files.

The Agora SDKs are vendored under `agora/sdk/<version>/`. `install.sh` takes the SDK dir as its
one argument and exports it as `AGORA_SDK_DIR` for CMake. `old_build/` holds obsolete scripts for
older SDK versions — prefer `build/`.

Note: `meson.build` links the plugins directly against absolute paths
`/usr/local/lib/libgstagorac.so` and `/usr/local/lib/libagora_rtc_sdk.so`, so step 1 must succeed
before step 2.

### Cross-compiling arm64 on x86

Use `arm64.cmake` toolchain files. Set `INSTALL_PATH` for where the lib is copied, and copy the
target's `/usr/include/aarch64-linux-gnu` onto the host first (see README "Cross compilation"
section). `build/build_all_aarch64_on_intel_4.2.32.sh` automates the cross build.

## Running

Always export the plugin path before running any pipeline:

```sh
export GST_PLUGIN_PATH=/usr/local/lib/x86_64-linux-gnu/gstreamer-1.0
```

Common element properties (see README for full pipeline examples):
`appid` (app id **or** token), `channel`, `userid` (optional), `remoteuserid` (subscribe to one
uid), `audio` (true/false — audio vs video pipeline), `verbose` (logging), plus sink extras like
`enforce-audio-duration`, `avc-to-annexb`, and `agoraioudp`'s `inport`/`outport`,
`out-audio-delay`/`out-video-delay` (A/V sync tuning, microseconds), and proxy options
(`proxy=true`, `proxytimeout`, `proxyips`) for firewalled networks.

Agora SDK runtime log: `~/.agora/agorasdk.log`.

## Tests

Test programs live in a `test/` directory (referenced by README but not tracked in git). Compile
all with `./c` and run a single test by its name, e.g. `./endtest2`. Proxy test code is in
`test/test_proxy.c`.
