# MachineModule 0.4.6 — macOS Universal with native ARM JIT

Native Apple Silicon now enables JIT for voices and master effects by default,
using the validated backend and the DMA/MACRI fixes from 0.4.5. Intel keeps JIT.
The app and VST3 remain Universal, targeting macOS 12.0 and later. Plugin IDs,
parameters and saved project format are unchanged.

The ARM JIT candidate passed 18 final automated checks on Apple M1, including
39,029,760 matching voice samples across OS 1.63 and X.13, five-minute renders
matching the interpreter, master references, X.13 expansions, processor routing,
MIDI and state restore. The user reports approximately 25% lower Ableton CPU
with the JIT candidate; this is not a controlled benchmark. See
ARM-JIT-VALIDATION-0.4.6.md for release checks and remaining limits.

The MIDI regression now distinguishes stock OS 1.63's channel mapping from
X.13's tonal transposition, and the MACRI regression covers native ARM JIT.

The 0.4.5 release and its source remain available as an interpreter-default
fallback. Build with `MACHINEMODULE_ENABLE_ARM64_JIT=OFF bash build-macos.sh /path/to/JUCE`
for that execution mode, or use the existing runtime interpreter overrides.

Locally ad-hoc signed, not notarized. Firmware and samples are user-supplied.
Mac binaries are included; Windows binaries have not been rebuilt for 0.4.6.
AGPL-3.0-only; see LICENSE and THIRD_PARTY_NOTICES.md.

---

# MachineModule 0.4.5 - Apple Silicon stability candidate

Native Apple Silicon defaults to original firmware interpreter execution for both voices and master effects, including DSP internal dispatch. This bypasses the unvalidated ARM JIT after an OS 1.63 freeze report on M2 Pro. Native testing subsequently identified and fixed a mixer DMA interrupt-queue stall; see the validation report below. CPU use can increase. Intel and Windows keep their existing JIT behavior.

Universal builds still contain x86_64 and arm64. The Mac script defaults to a macOS 12.0 deployment target for the user's Intel macOS 12.6 setup. Original firmware effects, plugin IDs, saved parameters and output layout are preserved.

See APPLE-SILICON-STABILITY.md for testing the existing app without rebuilding, applying the focused patch to an existing Mac repository, building the complete source and collecting a hang sample. See ARM-STABILITY-VALIDATION-0.4.5.md for completed Windows regression/interpreter checks and the later native validation in NATIVE-VALIDATION-2026-10-16.md.

## Local Mac validation and fixes — 2026-10-16

The mixer now disables unused serial-link DMA after firmware initialization,
preventing a full interrupt queue from stalling direct master-effect execution.
X.13 testing also exposed unimplemented MACRI instructions in a neighbor effect.
Both interpreter and JIT now implement multiply-accumulate immediate with rounding.
The new `md-macri-smoke` compares accumulator values, status flags and instruction
length against MACI followed by RND across registers, signs and boundary values.
X.13's affected neighbor effect now renders without unsupported-instruction messages.

The source archive contains source only; the separate macOS Universal ZIP contains
the locally ad-hoc signed standalone and VST3. See NATIVE-VALIDATION-2026-10-16.md
and DSP-READ-INVESTIGATION-2026-10-16.md for results and remaining limits.
Firmware and samples are user-supplied. AGPL-3.0-only; see LICENSE and THIRD_PARTY_NOTICES.md.

---

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
