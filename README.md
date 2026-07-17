# agora-gstreamer (agoraioudp)

A GStreamer plugin that bridges a live [Agora](https://www.agora.io) RTC channel to a
local pipeline, built on the Agora Linux RTC SDK. This fork is trimmed to the single
**`agoraioudp`** element and targets **aarch64 (ARM64) Linux** with the **Agora Linux RTC
SDK v4.4.32** (SDK build 675674), which is vendored in-tree.

It is used to publish a camera feed from an ARM device (e.g. a Raspberry Pi field unit)
into an Agora channel and to pull the remote participant's media back out — all as
ordinary GStreamer pipeline segments.

## What it does

`agoraioudp` is a bidirectional element:

- **Video → Agora**: its sink pad accepts encoded **H.264** (byte-stream) and publishes it
  to the channel.
- **Video ← Agora** (opt-in, `receive-video=true`): its src pad emits the remote
  participant's encoded **H.264**, ready to feed `decodebin`. Off by default — remote
  video is not even subscribed, saving downlink bandwidth on publish-only devices.
- **Audio** is bridged over local UDP so it can live in a separate pipeline/process:
  - audio **from** Agora is sent as raw PCM (`S16LE`, 48 kHz, mono) to `host:outport`,
  - audio **to** Agora is read as Opus from `host:inport`.

Fork-specific behaviour:

- **Region: mainland China is excluded** (`AREA_CODE_OVS`) for every connection.
- **Codec: H.264** (matches `x264enc`/hardware H.264 output; the SDK is told per-frame).
- **Video dimensions/framerate are taken from the pipeline caps**, so eth/4G resolution
  switches are handled automatically.

## How it's put together

```
GStreamer pipeline (C plugin)          gst-agora/plugin-src/agoraioudp/gstagoraioudp.c
        |  extern "C" ABI (agorac.h)
libgstagorac.so  (C++ backend)         agora/libagorac/  -> class AgoraIo
        |
Agora Linux RTC SDK 4.x                agora/sdk/agora_sdk_aarch64_4.4.32/
```

The C element parses properties/caps and forwards frames across a flat C ABI into the C++
`AgoraIo` class, which owns the Agora service, connection, publish/subscribe and A/V sync.

## Requirements

- aarch64 (ARM64) Linux — Ubuntu 20.04/22.04 or Raspberry Pi OS (64-bit)

**Build dependencies** (toolchain + the dev headers the backend/plugin compile and link against):

```sh
sudo apt-get update
sudo apt-get install -y \
  build-essential meson ninja-build pkg-config git \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev
```

**Runtime dependencies** (the GStreamer element sets the pipelines use):

```sh
sudo apt-get install -y \
  gstreamer1.0-tools gstreamer1.0-plugins-base gstreamer1.0-plugins-good \
  gstreamer1.0-plugins-ugly gstreamer1.0-libav gstreamer1.0-alsa
```

- `gstreamer1.0-plugins-ugly` provides `x264enc`; `gstreamer1.0-libav` provides `avdec_h264`
  (to decode the remote video); `gstreamer1.0-alsa` is only needed for the ALSA audio bridge.
- `gstreamer1.0-plugins-bad` is **not** needed for the examples here — add it only if your
  pipeline uses `h264parse`, `rtmpsink`, etc.

The Agora SDK itself is already vendored under `agora/sdk/agora_sdk_aarch64_4.4.32/` — no
separate download is needed.

## Building

Two scripts are provided; both build the C++ backend (`libgstagorac.so`) and the
`agoraioudp` plugin.

### System install

Installs the backend + SDK libraries into `/usr/local/lib` and the plugin into the
GStreamer plugin dir (needs `sudo`):

```sh
cd build
./build_all_aarch64_4.4.32.sh
```

### Local / in-place build (no install)

Builds everything inside the repo and installs **nothing** — useful for testing without
touching an existing system install:

```sh
cd build
./build_local_aarch64_4.4.32.sh
```

It prints the exact `LD_LIBRARY_PATH` / `GST_PLUGIN_PATH` to use afterwards.

## Running

After a **system install**, point GStreamer at the plugin dir:

```sh
export GST_PLUGIN_PATH=/usr/local/lib/aarch64-linux-gnu/gstreamer-1.0
```

After a **local build**, also add the repo libraries to the loader path (the local build
script prints these):

```sh
export LD_LIBRARY_PATH=<repo>/agora/sdk/agora_sdk_aarch64_4.4.32/agora_sdk:<repo>/agora/libagorac
export GST_PLUGIN_PATH=<repo>/gst-agora/build_local/plugin-src
```

Verify the element loads:

```sh
gst-inspect-1.0 agoraioudp
```

### Example: publish a camera and display the remote video

```sh
gst-launch-1.0 -e \
  v4l2src device=/dev/video0 ! image/jpeg,width=1280,height=960,framerate=15/1 ! jpegdec ! \
  videoconvert ! x264enc key-int-max=30 tune=zerolatency bitrate=1500 speed-preset=veryfast ! \
  queue ! agoraioudp appid=YOUR_APPID channel=YOUR_CHANNEL userid=101 outport=7372 inport=7373 \
  receive-video=true ! queue ! decodebin ! queue ! autovideosink
```

> The sink pad requires `video/x-h264,stream-format=byte-stream,alignment=au`, so
> `x264enc` negotiates byte-stream automatically — even when a tee branch (e.g.
> `rtph264pay`) would otherwise prefer `avc`.

> Remote video is **not** subscribed by default (an intercom-style deployment publishes
> video and only receives audio). Set `receive-video=true` to subscribe to the loudest
> speaker's video and push it out of the src pad; leave it off to save downlink
> bandwidth if the src pad is unused.

### Example: audio bridge (companion pipelines)

Audio out of Agora to the speaker (reads `outport`):

```sh
gst-launch-1.0 -v udpsrc port=7372 ! audio/x-raw,format=S16LE,channels=1,rate=48000,layout=interleaved ! \
  audioconvert ! autoaudiosink
```

Audio from the mic into Agora (writes `inport`). Default (`audio-pcm=false`, Opus —
pre-encoded audio bypasses ALL of the SDK's audio processing):

```sh
gst-launch-1.0 -v alsasrc ! audioconvert ! opusenc ! udpsink host=127.0.0.1 port=7373
```

With `audio-pcm=true` on the element, send raw PCM instead and the SDK applies its full
audio processing — **AEC** (echo of the `outport` playback is cancelled), **ANS** noise
suppression, **AGC** gain control — plus local voice-activity (VAD) logging:

```sh
gst-launch-1.0 -v alsasrc ! audioconvert ! audioresample ! \
  audio/x-raw,format=S16LE,channels=1,rate=48000 ! udpsink host=127.0.0.1 port=7373
```

## Element properties

| Property | Default | Description |
|---|---|---|
| `appid` | — | Agora App ID **or** a token |
| `channel` | — | Agora channel name |
| `userid` | — | Agora user id (optional; auto-assigned if unset) |
| `mode` | `3` | `1` = local loopback test (no SDK), `2` = video only (no audio bridge), `3` = video + audio |
| `audio` | `false` | Treat this element's pads as audio instead of video |
| `receive-video` | `false` | Subscribe to remote video and push it out of the src pad |
| `audio-pcm` | `false` | `inport` carries raw PCM (S16LE, 48 kHz, mono) instead of Opus; enables the SDK's AEC/ANS/AGC and VAD reporting |
| `agora-params` | — | Raw `setParameters` JSON applied after connect (e.g. `{"che.audio.aec.fixed_delay":80}`) |
| `host` | `127.0.0.1` | UDP host for the audio bridge |
| `outport` | `7374` | UDP port audio **from** Agora is sent to (PCM S16LE) |
| `inport` | `7373` | UDP port audio **to** Agora is read from (Opus) |
| `out-audio-delay` / `out-video-delay` | `0` | Delay (ms) on media **from** Agora → pipeline, for A/V sync |
| `in-audio-delay` / `in-video-delay` | `0` | Delay (ms) on media **from** pipeline → Agora |
| `proxy` | `false` | Route via the Agora cloud proxy (firewalled networks) |
| `proxyips` | — | Comma-separated proxy signalling IPs |
| `proxytimeout` | `10000` | Timeout (ms) before falling back to the proxy |
| `verbose` | `false` | Verbose logging |

### A/V synchronisation

The SDK delivers audio and video in sync over the network, but a device's local audio and
video output paths can have different latency. Use `out-audio-delay` / `out-video-delay`
(milliseconds) to compensate on playback.

### Firewalled networks (proxy)

Add `proxy=true` to route through the Agora cloud proxy if a direct connection can't be
established within the timeout. Optionally supply `proxytimeout=` and
`proxyips=<ip1,ip2>`. Whitelist the relevant region IP:ports per Agora's cloud-proxy docs.

## Notes

- Runtime files created by the SDK (`agora*.dat`, `agora_cache.db`, `common_resource/`,
  crash-context dumps) are git-ignored.
- The Agora SDK writes its own log to `~/.agora/agorasdk.log` (encrypted).
- The SDK's `libaosl` layer would otherwise spam **syslog** with a harmless
  `AOSL: Java VM not set ...` warning on non-Android platforms. The backend silences all
  `libaosl` logging at startup (via `aosl_set_vlog_func`), so nothing reaches the system
  journal; the RTC SDK's own `agorasdk.log` is unaffected.
