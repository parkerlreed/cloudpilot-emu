#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "$0")" && pwd)"
sdl_dir="${SDL2_SOURCE:-$project_dir/build/SDL2}"
revision=5d249570393f7a37e037abf22cd6012a4cc56a71
if [[ ! -d "$sdl_dir" ]]; then
    mkdir -p "$(dirname -- "$sdl_dir")"
    git clone --depth 1 --branch release-2.32.10 https://github.com/libsdl-org/SDL.git "$sdl_dir" >&2
fi
if [[ "$(git -C "$sdl_dir" rev-parse HEAD)" != "$revision" ]]; then
    echo 'SDL2 must be the pinned release-2.32.10 revision.' >&2
    exit 1
fi
printf '%s\n' "$sdl_dir"
