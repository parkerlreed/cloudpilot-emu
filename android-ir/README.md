# Native Palm USB IR remotes (Android / SDL2)

Two standalone ARM64 Android apps run Cloudpilot directly as native C++ with
SDL2 rendering, touch, Graffiti and hardware buttons. Both use the native
Android USB receiver/transmitter. No WebView or WASM runtime is packaged.

| Application module | Android package | Palm backend | Saved file |
| --- | --- | --- | --- |
| `omniremote` | `local.omniremote.irtest` | m130 / OS 4 / 68k | `session.img` |
| `noviiremote` | `local.noviiremote.irtest` | Tungsten E2 / OS 5 / ARM | `session-e2.img` |

These are independent Android application modules, with their own manifests,
launcher activities, package IDs and assets. Shared SDL/USB code is in the
neutral `runtime/` directory and Java package `local.palmir`; neither app is a
build variant of the other. Log tags are `OmniRemote` and `NoviiRemote`, with
shared USB protocol diagnostics under `PalmUsbIR`.

OmniRemote retains its existing package ID and saved session. Novii formerly
used `local.omniremote.irtest.novii`; an existing installation of that legacy
package needs its `files/session-e2.img` copied into the new Novii package before
removing the old app. Back up `files/session.img` too if its earlier m130 session
is still needed. Android isolates private data between package IDs.
The supplied session asset is used only when the app has no saved session.
Back cancels an active capture and saves before closing. Backgrounding also
saves. A failed save keeps the app open. Android's status bar is hidden, all
four sensor orientations are supported, and connecting the `045c:0132` dongle
can open whichever app the user selects as its Android USB default.

The display contains the Palm screen, Graffiti and hardware buttons. The E2
has a five-way pad; the m130 has up/down. External keyboard controls are F1–F4,
arrows and Return where supported by the Palm. USB feedback uses brief Android
toasts. Palm sound uses SDL2 at 44.1 kHz stereo: the m130 speaker PWM
is synthesized into PCM with cycle-timed transitions, and the E2 supplies its
native PCM stream. Short clicks are preserved within one emulator frame.
Audio pauses and clears queued samples in the background, then resumes with
the app. Playback uses Android’s media volume.

Emulator operations, capture completion and saving stay on the SDL
thread; USB I/O runs on a separate Android worker. ROM/NAND backing buffers
and one emulator instance stay alive across SDL activity recreation.

## Build

Requirements: Java 17/21, Android SDK 36, NDK 29.0.14206865, SDK CMake 3.22.1,
Python 3 and Git. Set `JAVA_HOME` and create ignored `local.properties` with
`sdk.dir=/path/to/android-sdk`. This project uses the native cores from its parent Cloudpilot checkout.
`prepare-sdl.sh` fetches SDL2 release-2.32.10 into ignored `build/SDL2`.
`SDL2_SOURCE` may select an existing checkout of that exact release.
There is no Tauri or web frontend in this project.

OmniRemote, using an existing m130 session with the app installed:

```sh
JAVA_HOME=/path/to/jdk21 PALM_IMAGE=/path/to/omniremote-session.img ./build.sh
adb install -r omniremote/build/outputs/apk/debug/omniremote-debug.apk
adb shell am start -n local.omniremote.irtest/local.omniremote.OmniRemoteActivity
```

Novii, using a prepared E2 session:

```sh
JAVA_HOME=/path/to/jdk21 NOVII_SESSION=/path/to/novii-e2-session.img ./build.sh
adb install -r noviiremote/build/outputs/apk/debug/noviiremote-debug.apk
adb shell am start -n local.noviiremote.irtest/local.noviiremote.NoviiRemoteActivity
```

Native APK builds from prepared sessions do not require Emscripten, Node or
Yarn. For first-time Novii installation from NOR/NAND and the supplied zip,
the optional host preparation helper uses Emscripten 4.0.18 and Node:

```sh
JAVA_HOME=/path/to/jdk21 EMSDK=/path/to/emsdk \
  E2_NOR=/path/to/Palm-Tungsten-E2-nor.bin \
  E2_NAND=/path/to/Palm-Tungsten-E2-nand.bin \
  NOVII_ZIP=/path/to/NoviiRemote.zip ./build.sh
```

That helper builds an unoptimized host WASM module only to boot and prepare
the initial session. It is excluded from the APK. Original NOR/NAND files are
read without modification. Palm software, generated sessions, SDK locations,
WASM, APKs and build caches are excluded from Git. Keep ROM-containing APKs local.

## IR behavior

In OmniRemote choose **Internal IR**, then **Training → Train buttons**.
Skip its preliminary Test Remote screen to reach the native record call.
The bridge intercepts the supplied Pro 2.x `ORLib.prc` A807 call, delivers an
Android capture to its training buffer, and collects UART carrier envelopes
for the D552 USB transmitter.

In Novii choose **Start → TV** for the supplied Sony codebase. To learn:
**menu → Setup → Relearn → Single mode → Start Learning**, then tap the button
to teach. Finish with **done** and close Setup. Preparation verifies the
English Deluxe 3.73D build 20080411b PRC SHA-256 before adapting its seven ARM
IR calls. Signature-guarded private traps replace the device hardware loops;
loaded carrier data stays in guest RAM and survives session restore.

The dongle supplies envelope durations, not measured carrier. Novii learning
assumes about 38 kHz (38,462 Hz after its integer period representation).
For OmniRemote, set the intended carrier in Training Settings. Built-in Novii
codes use their stored carrier. Each action sends one complete waveform;
held-button repeat sequences are not implemented. Receiving and transmitting
share one USB worker. Other IR apps must release the dongle before use.

## Validation

Native ARM64 APK builds cover both backends. On-device checks verified SDL2
startup and rendering and Novii USB sends. The E2 core integration test accepts
a real phone NEC capture, saves it through Novii and replays all 68 durations
exactly at 38,462 Hz, including capture cancellation:

```sh
node test/novii-e2-roundtrip.cjs /path/to/cloudpilot-emu \
  /path/to/prepared-novii-welcome-session.img /path/to/capture.json
```

The optional integration test uses the host WASM module and your local Palm
software; the JSON capture contains a `pattern_us` array. The older m130 Novii
adapter and its test remain available as development tools, but the Novii APK
uses the E2 backend. No binary Palm software is committed.

The short-click PWM regression test runs without a ROM or Android device:

```sh
c++ -std=c++17 -O2 test/pwm-audio.cpp -o /tmp/test-pwm-audio
/tmp/test-pwm-audio
```

Launcher icons use each application's original color `tAIB` 1000 resource
from its user-supplied PRC, including Palm transparency. Novii uses its 64×44
high-density icon; OmniRemote uses its 32×32 color icon. The Android adaptive
icons preserve the original artwork with nearest-neighbor scaling, center
the visible artwork after trimming transparent Palm padding, and use
transparent backgrounds. Regenerate
them locally with Pillow:

```sh
python3 tools/extract-prc-icons.py --omni /path/to/Remote.prc \
  --novii /path/to/NoviiRemote.zip
```
