#!/bin/bash
set -euo pipefail

# Universal macOS personal-test build. Run: bash build-macos.sh /path/to/JUCE
project_dir="$(cd "$(dirname "$0")" && pwd)"
juce_dir="${1:-${JUCE_DIR:-$project_dir/../JUCE}}"
build_dir="$project_dir/build-macos-universal"
output_dir="$project_dir/dist/macOS-Universal"
entitlements="$project_dir/macos/Jit.entitlements"

if [ "$(uname -s)" != "Darwin" ]; then
    echo "Run this Universal build script on a Mac (Intel or Apple Silicon)." >&2
    exit 1
fi
if ! command -v cmake >/dev/null 2>&1; then
    echo "Install CMake 3.22 or newer. See MACOS-UNIVERSAL.md." >&2
    exit 1
fi
if ! xcodebuild -version >/dev/null 2>&1; then
    echo "Install full Xcode and select it with xcode-select. See MACOS-UNIVERSAL.md." >&2
    exit 1
fi
if [ ! -f "$juce_dir/CMakeLists.txt" ]; then
    echo "JUCE source not found: $juce_dir. Pass your JUCE 8.0.8 folder as the first argument." >&2
    exit 1
fi
juce_dir="$(cd "$juce_dir" && pwd)"
exec > >(tee "$project_dir/macos-universal-build.log") 2>&1
trap 'echo "Build failed. See macos-universal-build.log in the source folder." >&2' ERR

cmake -S "$project_dir" -B "$build_dir" -G Xcode \
    "-DJUCE_DIR=$juce_dir" \
    "-DCMAKE_OSX_ARCHITECTURES=x86_64;arm64" \
    "-DCMAKE_OSX_DEPLOYMENT_TARGET=${MACOSX_DEPLOYMENT_TARGET:-12.0}" \
    -DCMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH=NO \
    -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO \
    -DMACHINEMODULE_ENABLE_ARM64_JIT=OFF \
    -DBUILD_TESTING=OFF

cmake --build "$build_dir" --config Release --parallel 4 \
    --target MachinedrumModule_Standalone MachinedrumModule_VST3 \
             md-smoke md-jit-smoke md-master-probe md-lfo-smoke md-x13-smoke md-stability-smoke md-macri-smoke

products="$build_dir/MachinedrumModule_artefacts/Release"
app="$products/Standalone/MachineModule.app"
plugin="$products/VST3/MachineModule.vst3"
if [ ! -d "$app" ] || [ ! -d "$plugin" ]; then
    echo "Expected app/plugin bundles are missing; inspect the build output." >&2
    exit 1
fi

# Local ad-hoc signatures require no signing certificate or Developer Program.
sign_and_verify() {
    local bundle="$1"
    local executable="$2"
    /usr/bin/lipo "$executable" -verify_arch x86_64 arm64
    /usr/bin/lipo -archs "$executable"
    /usr/bin/codesign --force --sign - --timestamp=none \
        --entitlements "$entitlements" "$bundle"
    /usr/bin/codesign --verify --strict "$bundle"
}
sign_and_verify "$app" "$app/Contents/MacOS/MachineModule"
sign_and_verify "$plugin" "$plugin/Contents/MacOS/MachineModule"
for tool in md-smoke md-jit-smoke md-master-probe md-lfo-smoke md-x13-smoke md-stability-smoke md-macri-smoke; do
    sign_and_verify "$build_dir/Release/$tool" "$build_dir/Release/$tool"
done
mkdir -p "$output_dir/Standalone" "$output_dir/VST3"
/usr/bin/ditto "$app" "$output_dir/Standalone/MachineModule.app"
/usr/bin/ditto "$plugin" "$output_dir/VST3/MachineModule.vst3"

printf '\nStandalone: %s\nVST3: %s\n' \
    "$output_dir/Standalone/MachineModule.app" \
    "$output_dir/VST3/MachineModule.vst3"
echo "Universal build verified: x86_64 + arm64. Run the checks in MACOS-UNIVERSAL.md on both Mac types."
