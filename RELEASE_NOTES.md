# MachineModule 0.4.4 — macOS Universal

Standalone app and VST3 for Intel (`x86_64`) and Apple Silicon (`arm64`), requiring macOS 12.6 or later. This release adds the Mac build of 0.4.4; the synthesis code, plugin IDs and state format are unchanged.

Download and extract `MachineModule-0.4.4-macOS-Universal.zip`. Open `Standalone/MachineModule.app`, select your own supported firmware, and configure CoreAudio/MIDI in Options. Copy the complete `VST3/MachineModule.vst3` bundle to `~/Library/Audio/Plug-Ins/VST3/` for your DAW and rescan. Start at 44.1 kHz, 512 samples and six voices.

The bundles are locally ad-hoc signed with the JIT entitlement, not notarized. If macOS blocks the downloaded app, use Open Anyway in the system privacy/security settings for this app.

Validation on Intel macOS 12.6 with user-supplied OS 1.63: standalone launched; all 47 tested machines produced audio; JIT matched the interpreter for 18,631,680 24-bit track samples; original master-effects and random-LFO checks passed. Both architecture slices and strict signatures were verified.

Apple Silicon runtime, interactive audio/MIDI and DAW hosting have not been verified. X.13 was not retested on Mac. Firmware and ROM samples are not included.

Build instructions: [MACOS-UNIVERSAL.md](MACOS-UNIVERSAL.md). AGPL-3.0-only; see LICENSE and THIRD_PARTY_NOTICES.md.

---

# MachineModule 0.4.4

Windows x64 standalone instrument and VST3 by AcidReff.

Voice synthesis now uses the DSP JIT fork used by MD Drums, with conservative instruction blocks that preserve audio output. Common synthesis paths compile on the firmware loader thread before playback. Full-plugin tests measured 13-38% lower total CPU use than 0.4.3, depending on firmware and output layout; all comparison audio hashes matched. See CPU-VALIDATION-0.4.4.md for measurements and limitations.

This update also includes the 0.4.3 firmware-call allocation and output-routing improvements. Supported features include OS 1.63 and verified UW X.13, synthesis expansions, tonal tuning, selectable Drum/Chromatic MIDI, original firmware master effects, individual track outputs, kit import and the RND LFO fix.

## Build and run

Use Visual Studio 2022 with Desktop development with C++, the Windows SDK, CMake tools and JUCE 8.0.8 sources. Run build-windows.ps1; see README.md for configuration and output paths. Copy the complete MachineModule.vst3 bundle when installing the plugin.

Supply your own supported firmware using FIRMWARE. Firmware and ROM samples are not included. Start at 44.1 kHz, a 512-sample buffer and six voices. Dense kits and small buffers can still overload a CPU.

## Validation

Warmed JIT matched the interpreter for over 39 million 24-bit track samples with OS 1.63 and X.13. Original master effects, X.13 expansions, RND LFO, MIDI channel/octave behavior, routing, sample rates, variable host blocks and all 548 saved parameters passed. These are automated engine/processor tests; ROM sample playback and every DAW were not tested.

AGPL-3.0-only. See LICENSE and THIRD_PARTY_NOTICES.md. Source is available in this repository. Not affiliated with Elektron or Akai.