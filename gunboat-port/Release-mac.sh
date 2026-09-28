#!/usr/bin/env bash
# Builds the macOS release package: release/Gunboat-<version>-macos-universal.zip with Gunboat.app and
# the controller mapping tool Gunboat Controller.app (universal: Apple silicon and Intel, macOS 11+,
# SDL3 linked in statically, ad-hoc signed), an empty Game folder with its README, the player README
# and the licenses. No game files: the player adds their own to Game, beside Gunboat.app. The sound
# effects editor is a development tool and is not in the package. Needs Xcode's command line tools,
# CMake 3.24+ and Ninja (brew install cmake ninja). SDL3 is built from its source into deps/ the first
# time (as the Linux workflow does); SDL_PREFIX points at another static SDL3 instead.
set -euo pipefail
source="$(cd "$(dirname "$0")" && pwd)"
build="${BUILD_DIR:-$source/build-mac}"
sdl_version=3.4.16
sdl_src="$source/deps/SDL3-$sdl_version"
sdl_prefix="${SDL_PREFIX:-$source/deps/sdl3-mac}"
archs="arm64;x86_64"
min_macos=11.0

if [ ! -f "$sdl_prefix/lib/libSDL3.a" ]; then
    mkdir -p "$source/deps"
    if [ ! -d "$sdl_src" ]; then
        curl -fsSL -o "$source/deps/sdl.tar.gz" \
            "https://github.com/libsdl-org/SDL/releases/download/release-$sdl_version/SDL3-$sdl_version.tar.gz"
        tar -C "$source/deps" -xzf "$source/deps/sdl.tar.gz"
    fi
    cmake -S "$sdl_src" -B "$source/deps/sdl-build-mac" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
        -DCMAKE_OSX_ARCHITECTURES="$archs" -DCMAKE_OSX_DEPLOYMENT_TARGET=$min_macos \
        -DCMAKE_INSTALL_PREFIX="$sdl_prefix"
    cmake --build "$source/deps/sdl-build-mac"
    cmake --install "$source/deps/sdl-build-mac"
fi

cmake -S "$source" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$sdl_prefix" \
    -DCMAKE_OSX_ARCHITECTURES="$archs" -DCMAKE_OSX_DEPLOYMENT_TARGET=$min_macos
cmake --build "$build" --target gunboat gunboat_controller

version="$("$build/gunboat" --version | awk '{print $NF}')"
name="Gunboat-$version-macos-universal"
out="$source/release"
# Staged outside the source tree: a synced folder (iCloud's Documents) tags .app folders with Finder
# attributes, which break the signatures. Only the finished zip is copied to release/.
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
stage="$work/$name"
top="$stage/Gunboat"
mkdir -p "$top/Game" "$top/licenses" "$out"

# The port's own icon (dist/gunboat.png, 256 x 256) as an .icns.
iconset="$work/gunboat.iconset"
rm -rf "$iconset"
mkdir -p "$iconset"
for s in 16 32 128 256; do
    sips -z $s $s "$source/dist/gunboat.png" --out "$iconset/icon_${s}x${s}.png" >/dev/null
done
for s in 16 32 128; do
    sips -z $((s * 2)) $((s * 2)) "$source/dist/gunboat.png" --out "$iconset/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$iconset" -o "$work/gunboat.icns"
rm -rf "$iconset"

# make_app <bundle name> <executable> <bundle id>
make_app() {
    local app="$top/$1.app"
    mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
    strip -x -o "$app/Contents/MacOS/$2" "$build/$2"
    cp "$work/gunboat.icns" "$app/Contents/Resources/gunboat.icns"
    cat > "$app/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>$1</string>
    <key>CFBundleDisplayName</key><string>$1</string>
    <key>CFBundleExecutable</key><string>$2</string>
    <key>CFBundleIdentifier</key><string>$3</string>
    <key>CFBundleIconFile</key><string>gunboat</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>$version</string>
    <key>CFBundleVersion</key><string>$version</string>
    <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
    <key>LSMinimumSystemVersion</key><string>$min_macos</string>
    <key>LSApplicationCategoryType</key><string>public.app-category.games</string>
    <key>NSHighResolutionCapable</key><true/>
    <key>GCSupportsControllerUserInteraction</key><true/>
</dict>
</plist>
EOF
    xattr -cr "$app"                  # codesign refuses files with extended attributes
    codesign --force --sign - "$app"  # ad-hoc: Apple silicon runs only signed code
}
make_app "Gunboat" gunboat io.github.plasticcog.gunboat
make_app "Gunboat Controller" gunboat_controller io.github.plasticcog.gunboat-controller
rm -f "$work/gunboat.icns"

sed "s/@VERSION@/$version/g" "$source/dist/README.txt" > "$top/README.txt"
cp "$source/dist/Game/README.txt" "$top/Game/README.txt"
cp "$source/THIRD_PARTY.md" "$top/"
cp "$source/licenses/test-drive-3-sdl3-MIT.txt" "$top/licenses/"
cp "$source/vendor/nuked-opl3/LICENSE" "$top/licenses/Nuked-OPL3-LGPL-2.1.txt"
cp "$source/vendor/imgui/LICENSE.txt" "$top/licenses/Dear-ImGui-MIT.txt"
cp "${SDL_LICENSE:-$sdl_src/LICENSE.txt}" "$top/licenses/SDL3-zlib.txt"

# No game file may be in the package.
if find "$top/Game" -type f ! -name README.txt | grep -q .; then
    echo "Game folder not empty" >&2
    exit 1
fi

codesign --verify --deep --strict "$top/Gunboat.app" "$top/Gunboat Controller.app"
(cd "$stage" && ditto -c -k --keepParent Gunboat "$work/$name.zip")  # ditto keeps the bundles' signatures
rm -f "$out/$name.zip"
cp "$work/$name.zip" "$out/$name.zip"
echo "Release package: $out/$name.zip"
(cd "$stage" && find Gunboat -type f -print0 | xargs -0 stat -f "%10z  %N")
