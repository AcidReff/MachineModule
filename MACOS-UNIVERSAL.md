# MachineModule Universal Mac build

The build script targets both Intel (`x86_64`) and Apple Silicon (`arm64`) in one
standalone `.app` and one VST3 bundle. You can build both architectures on your
Intel Mac. macOS selects the native architecture when launching the result.
See [Apple's Universal binary guide](https://developer.apple.com/documentation/apple-silicon/building-a-universal-macos-binary).

Both architecture slices compiled successfully with Xcode 14.2, CMake 3.31.6 and
JUCE 8.0.8. The standalone launched on an Intel Mac running macOS 12.6.
With user-supplied OS 1.63, all four Intel engine checks passed: 47 machines
produced audio; JIT matched the interpreter for 18,631,680 24-bit track samples;
original master effects and random LFO checks passed. Both bundle signatures
passed strict verification. Both slices specify macOS 12.6 as their minimum.

Apple Silicon runtime, interactive CoreAudio/MIDI and DAW hosting remain
unverified. An M-series Mac is needed to test the ARM64 slice and its DSP JIT.
These checks do not establish that every DAW or audio device works.

## Mac build prerequisites

1. The app targets macOS **12.6 or later**. The local build uses **Xcode 14.2** on macOS 12.6. Download a compatible Xcode from
   [Apple Developer Downloads](https://developer.apple.com/download/all/), place
   it in Applications and open it once to install its components. Check
   [Apple's Xcode requirements](https://developer.apple.com/xcode/system-requirements)
   if you use another macOS version.
2. Install **CMake 3.22 or newer**, preferably 3.31.x, using a compatible macOS
   universal installer from [CMake](https://cmake.org/download/).
3. Obtain the **JUCE 8.0.8 source**. Its libraries are compiled for both
   architectures during this build; a separate prebuilt JUCE library is not needed.

In Terminal:

```bash
sudo xcode-select --switch /Applications/Xcode.app/Contents/Developer
export PATH="/Applications/CMake.app/Contents/bin:$PATH"
xcodebuild -version
cmake --version
mkdir -p "$HOME/Developer"
git clone --depth 1 --branch 8.0.8 https://github.com/juce-framework/JUCE.git "$HOME/Developer/JUCE"
```

Skip the clone if you already have JUCE 8.0.8. Pass its actual folder to the script.

## Build the app and VST3

Copy and extract `MachineModule-Mac-Universal-Source.zip` onto your Mac. Open
Terminal in the extracted source folder containing `build-macos.sh`, then run:

```bash
bash build-macos.sh "$HOME/Developer/JUCE"
```

The script uses:

```text
CMAKE_OSX_ARCHITECTURES=x86_64;arm64
CMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH=NO
CMAKE_OSX_DEPLOYMENT_TARGET=12.6
```

A separate `build-macos-universal` cache avoids reusing the old Intel-only cache.
Both architecture-specific DSP backends are included in the dependency source.
The script applies local ad-hoc signatures with the JIT entitlement, checks code
signatures, and verifies both architectures using `lipo` before packaging.
These local signatures are for testing; this is not a notarized distribution.

The signing entitlement is `com.apple.security.cs.allow-jit`; no JIT callback
allowlist is enabled because the bundled asmjit uses `pthread_jit_write_protect_np`.
See [Apple's JIT porting guidance](https://developer.apple.com/documentation/apple-silicon/porting-just-in-time-compilers-to-apple-silicon).

Outputs:

- `dist/macOS-Universal/Standalone/MachineModule.app`
- `dist/macOS-Universal/VST3/MachineModule.vst3`
- `macos-universal-build.log`

Open the app, select your own supported Machinedrum firmware and configure the
CoreAudio/MIDI device in Options. Start at 44.1 kHz, 512 samples and six voices.
Copy the whole VST3 bundle to `~/Library/Audio/Plug-Ins/VST3/` for a VST3-capable
DAW and rescan. This configuration produces standalone and VST3, not Logic's AU.

## Verify both architectures

The build fails if either architecture is missing. You can also inspect them:

```bash
lipo -archs dist/macOS-Universal/Standalone/MachineModule.app/Contents/MacOS/MachineModule
lipo -archs dist/macOS-Universal/VST3/MachineModule.vst3/Contents/MacOS/MachineModule
```

Both commands should show `x86_64 arm64` (order may differ).

On your Intel Mac, run the firmware checks from the source folder:

```bash
FIRMWARE="$HOME/Documents/elektron_sps1-1uw_os1.63.bin"
for tool in md-smoke md-jit-smoke md-master-probe md-lfo-smoke; do
    arch -x86_64 "./build-macos-universal/Release/$tool" "$FIRMWARE" || break
done
```

Repeat with your X.13 file and its expansion check:

```bash
FIRMWARE="$HOME/Documents/Machinedrum_SPS1-UW_OS_X.13.syx"
arch -x86_64 ./build-macos-universal/Release/md-jit-smoke "$FIRMWARE"
arch -x86_64 ./build-macos-universal/Release/md-x13-smoke "$FIRMWARE"
```

On an **M-series Mac**, copy the source/build folder with the Universal tools and
run the same commands with `arch -arm64` instead. `md-jit-smoke` must report exact
JIT/interpreter matches. It is not possible to run the ARM64 checks on Intel.
Also test native standalone sound, original master effects, Chromatic MIDI and
VST3 project save/restore on each architecture. Firmware and samples are supplied
by the user and are not distributed with this source package.

If a Mac build fails, send `macos-universal-build.log`. If a check fails, send its
terminal output and the Mac architecture. Do not treat a successful compilation
alone as proof that ARM64 synthesis is correct.
