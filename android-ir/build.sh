#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "$0")" && pwd)"
export CLOUDPILOT_SOURCE="${CLOUDPILOT_SOURCE:-$(cd -- "$project_dir/.." && pwd)}"
export SDL2_SOURCE="$(bash "$project_dir/prepare-sdl.sh")"
cd "$project_dir"
if [[ -n "${NOVII_SESSION:-}${NOVII_ZIP:-}" ]]; then
    if [[ -n "${NOVII_SESSION:-}" ]]; then
        cp "$NOVII_SESSION" noviiremote/src/main/assets/session.img
    else
        # Optional first-time ROM/database preparation on the host. The APK
        # itself contains only native ARM64 code and SDL2.
        : "${EMSDK:?Set EMSDK for first-time E2 session preparation, or supply NOVII_SESSION}"
        : "${E2_NOR:?Set E2_NOR to your Tungsten E2 NOR image}"
        : "${E2_NAND:?Set E2_NAND to your Tungsten E2 NAND image}"
        source "$EMSDK/emsdk_env.sh"
        make -C "$CLOUDPILOT_SOURCE/src/common" -j8 emscripten
        make -C "$CLOUDPILOT_SOURCE/src/uarm" -j8 uarm_web.wasm
        python3 tools/prepare-novii.py --e2 "$NOVII_ZIP" build/novii-e2-databases
        node tools/install-novii-e2.cjs "$CLOUDPILOT_SOURCE" "$E2_NOR" "$E2_NAND" \
            build/novii-e2-databases noviiremote/src/main/assets/session.img
    fi
    ./gradlew :noviiremote:assembleDebug
else
    : "${PALM_IMAGE:?Set PALM_IMAGE to your own m130 session with OmniRemote installed}"
    cp "$PALM_IMAGE" omniremote/src/main/assets/session.img
    ./gradlew :omniremote:assembleDebug
fi
