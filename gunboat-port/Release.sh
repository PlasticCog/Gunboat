#!/usr/bin/env bash
# Builds the Linux release package: release/Gunboat-<version>-linux-x86_64.tar.gz with gunboat and the
# sound effects editor gunboat_sfx_editor (stripped; SDL3 linked in when it was built static), an
# empty Game folder with its README, the player README and the licenses. No game files: the player
# adds their own to Game. Needs CMake 3.24+, Ninja, a C++17 compiler and SDL3 where CMake finds it
# (CMAKE_ARGS="-DCMAKE_PREFIX_PATH=..."); SDL_LICENSE is SDL's LICENSE.txt. The GitHub workflow
# .github/workflows/release-linux.yml builds it this way on Ubuntu 22.04.
set -euo pipefail
source="$(cd "$(dirname "$0")" && pwd)"
build="${BUILD_DIR:-$source/build-linux}"
# shellcheck disable=SC2086
cmake -S "$source" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release ${CMAKE_ARGS:-}
cmake --build "$build" --target gunboat gunboat_sfx_editor

version="$("$build/gunboat" --version | awk '{print $NF}')"
name="Gunboat-$version-linux-x86_64"
out="$source/release"
stage="$out/$name"
top="$stage/Gunboat"
rm -rf "$stage"
mkdir -p "$top/Game" "$top/licenses"

strip -o "$top/gunboat" "$build/gunboat"
strip -o "$top/gunboat_sfx_editor" "$build/gunboat_sfx_editor"
sed "s/@VERSION@/$version/g" "$source/dist/README.txt" > "$top/README.txt"
cp "$source/dist/Game/README.txt" "$top/Game/README.txt"
cp "$source/THIRD_PARTY.md" "$top/"
cp "$source/licenses/test-drive-3-sdl3-MIT.txt" "$top/licenses/"
cp "$source/vendor/nuked-opl3/LICENSE" "$top/licenses/Nuked-OPL3-LGPL-2.1.txt"
cp "$source/vendor/imgui/LICENSE.txt" "$top/licenses/Dear-ImGui-MIT.txt"
cp "${SDL_LICENSE:-$source/deps/SDL3-3.4.16/LICENSE.txt}" "$top/licenses/SDL3-zlib.txt"

# No game file may be in the package.
if find "$top/Game" -type f ! -name README.txt | grep -q .; then
    echo "Game folder not empty" >&2
    exit 1
fi

rm -f "$out/$name.tar.gz"
tar -C "$stage" -czf "$out/$name.tar.gz" Gunboat
echo "Release package: $out/$name.tar.gz"
(cd "$stage" && find Gunboat -type f -exec ls -l {} \; | awk '{printf "%10s  %s\n", $5, $9}')
