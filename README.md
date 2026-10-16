# MachineModule
<img width="2242" height="1726" alt="image" src="https://github.com/user-attachments/assets/750d3e28-330a-4393-93cc-0916650cb107" />


Windows x64 and macOS Universal standalone instrument and VST3, ported from sd88me/mpc-vst-machinedrum. The Machinedrum sound engine is retained; a new JUCE desktop interface replaces the MPC wrapper. The supplied Monomodule project informed the Visual Studio/JUCE build setup. Neither original source folder is required to build this copy.

## Version 0.4.5 source update

The Universal Mac build contains native Apple Silicon and Intel code. Apple Silicon
uses the firmware interpreter by default; experimental ARM JIT remains disabled.
This update fixes a master-effect DMA interrupt-queue stall and implements the
MACRI DSP instruction used by an X.13 neighbor effect in both interpreter and JIT.

OS 1.63 and X.13 each passed 60-second native Apple Silicon and Intel-via-Rosetta
checks with matching audio hashes. X.13 expansion checks passed on both paths.
The user also tested both firmware versions in Ableton before the final MACRI fix;
DAW retesting of that fix and native Intel hardware testing remain pending.
See [runtime validation](NATIVE-VALIDATION-2026-10-16.md) and
[DSP read investigation](DSP-READ-INVESTIGATION-2026-10-16.md).
Build this version using [the Mac build script](MACOS-UNIVERSAL.md).

## macOS (Intel + Apple Silicon)

The previously published **0.4.4** macOS ZIP is available from [Releases](https://github.com/AcidReff/MachineModule/releases/tag/v0.4.4-macos). Requires **macOS 12.6 or later**. The ZIP contains a Universal standalone app and VST3 plugin.

1. Extract the ZIP and open `Standalone/MachineModule.app`.
2. Select **FIRMWARE** and choose your own supported Machinedrum OS `.syx` or flash `.bin`.
3. In **Options > Audio/MIDI Settings**, select your CoreAudio output and MIDI input. Start at 44.1 kHz, 512 samples and six voices.
4. For a DAW, copy the complete `VST3/MachineModule.vst3` bundle to `~/Library/Audio/Plug-Ins/VST3/`, then rescan plugins.

The bundles are locally ad-hoc signed with the JIT entitlement and are **not notarized**. If macOS blocks the downloaded app, use **Open Anyway** in System Settings/System Preferences > Privacy & Security/Security & Privacy for this app.

For 0.4.4, Intel standalone launch and OS 1.63 engine checks passed on macOS 12.6. For current 0.4.5 validation, see the report above and [Mac build instructions](MACOS-UNIVERSAL.md).

## Windows run

Open `dist/Standalone/Release/MachineModule.exe`.

1. Select **FIRMWARE** and choose your Machinedrum **OS 1.63 or UW X.13** `.syx` update or **8 MB `.bin` flash image**. Loading happens in the background. Your firmware path is remembered in `%APPDATA%/MachinedrumModulePC/firmware-path.txt` and in saved plugin state. Firmware is not embedded in the executable or projects.
2. In the standalone window, use **Options > Audio/MIDI Settings** to select your output device and enable your MIDI input. Start at 44.1 kHz and a 512-sample buffer. This build supports Windows Audio/DirectSound; the Steinberg ASIO SDK is not included.
3. Click pads 1-16 to select and audition a track, or send MIDI notes **36-51** (also **0-15**) on any channel. Note-offs do not stop drum tails. MIDI All Notes Off / All Sound Off silences the voices.
4. Choose the track's machine in the dropdown. The first eight knob labels and defaults follow the selected machine. The other controls cover AMP/FX, routing, level, and LFO settings.

The interface uses a red LCD, pixel-drawn text and dials, dotted parameter cells, and a brushed-metal bezel. SYN, AMP/EFX, ROUTE and LFO are arranged in four panels. Drag controls to edit, or double-click values to type. PAN and EQG display centered, signed values. LFO destinations display track/machine and parameter names; waveform graphics follow the firmware's shape order. The SPEED/DEPTH/SHMIX controls mirror LFOS/LFOD/LFOM in ROUTE. Track level, firmware, kit import, tempo, voice budget and output gain sit below the screen.

**Voices** is a CPU budget, initially 6 as in the MPC version. Synth voices cost one unit; ROM voices cost two. When exceeded, the oldest active voice is cut. Increase to 16 for all synthesis tracks or 32 for all ROM tracks if your CPU can keep up. The engine uses three persistent voice threads and releases silent voices. Dense kits and small audio buffers can still cause dropouts.

The engine runs at 44.1 kHz. Other output rates use linear interpolation; use 44.1 kHz for the most direct rendering. MIDI triggers are quantized to the engine's 32-sample blocks. Host tempo drives the LFO and Rhythm Echo; the Tempo slider is used when no host tempo is available.

## VST3

The packaged bundle is `dist/VST3/MachineModule.vst3`. Copy the **whole .vst3 folder**, including its `Contents` directory, to your DAW's VST3 location. The usual system location is `C:\Program Files\Common Files\VST3` (Windows may request administrator permission). Rescan plugins in your DAW and load **MachineModule** as an instrument. Version 0.3.1 accelerates the original master effects using the x64 JIT. Existing track parameter IDs and the state format are retained; 32 master parameters are appended.

The default output is stereo, including the built-in effects. Turn up a track's **DEL** or **REV** knob in ROUTE to hear delay or reverb. The original OS 1.63 DSP1 program now runs Rhythm Echo, Gate Box, master EQ and Dynamix. Both send returns reach Main in the standalone without extra routing. Click **MASTER FX** to edit all 32 controls; **TRACK EDIT** returns to the track controls. Delay TIME defaults to 32 (one beat); host/standalone tempo sets its timing. Dynamix initially favours the uncompressed signal. These starting settings are chosen for this port, not advertised as factory kit settings. The previous JUCE replacement effects have been removed.

In a DAW with multiple-output support, you can additionally enable stereo reverb/delay sends and 16 mono track outputs. Enabling a track's output removes that track from the main dry mix; it continues feeding the effects. Enabling the **Reverb send** or **Delay send** output switches that effect to external processing: its raw send appears on that bus and its built-in return is suppressed. Leave those two buses disabled to use the built-in effects. Parameters and the firmware path save with the DAW project.

## Kits and sample machines

**IMPORT KIT** reads Machinedrum kit `.syx` dumps. A file with multiple kits populates the kit selector. Selected kit parameters save in plugin state; the imported bank menu itself is local to that editor session.

TRX, EFM, E12, P-I and GND machines use the firmware. ROM sample machines additionally require an extracted `ROM_SAMPLES.bin` in the **same directory as the selected firmware**, in the upstream MPC project's `MDS1` format. The loader validates this file and enables the ROM machines when it is present. An OS flash `.bin` is not an extracted sample file. This port does not yet extract sample memory or factory kits by booting the full hardware emulator; use kit SysEx files and the original project's sample extraction tools if needed.

As in the MPC engine, there is no sequencer, RAM recording, or INP/MIDI/CTR machines. Kit import currently restores track settings; set master effects separately on MASTER FX. Unsupported machine assignments from imported kits remain silent. LFO destination tracks display 01-16 and parameter names are resolved from the destination machine. Underlying automation retains indices 0-15 / 0-23; modes 0/1/2 mean FREE/TRIG/HOLD.

## X.13 synthesis support (0.4.0)

The verified UW X.13 release adds GND-SN's four-oscillator expansion, GND-SW, GND-PU, and NFX-EV/CO/UC to the machine selector. The eight SYN labels/defaults come from the selected firmware. **TONAL**, shown beside MASTER FX when X.13 is loaded, switches the selected track's tuning using the firmware's own coefficient routines. Tonal flags are also read from imported kits and saved with plugin state.

Support is specific to OS 1.63 and the verified UW X.13 image, not arbitrary third-party releases. The loader verifies the X.13 image before accepting its deliberately outdated section checksums, decompresses its extension, and reproduces its voice-DSP code/table upload. No firmware bytes are included in the source or binaries.

X.13 uses one shared voice-DSP instance so NFX can read neighbouring tracks. Idle buffers are cleared and automatic silence release is disabled for this firmware; its CPU behaviour differs from stock 1.63's three-group optimization. The Voices budget still applies. Sequencer features, X firmware panel shortcuts, extended LFO selection, live audio inputs and RAM recording are outside this module's current interface.

`md-x13-smoke` exercises expanded oscillators on tracks 1, 8 and 16, all three NFX machines, coefficient routines, tonal tuning, reverb and delay using the user's X.13 SysEx. These are engine tests, not a physical-hardware audio comparison.

## Selectable MIDI mode (0.4.1)

The **MIDI MODE** selector above the track pads offers **DRUM** (the default) and **CHROMATIC**. The mode is automated and saved with the DAW project; older projects open in Drum mode.

In Drum mode, notes 36–51 (also 0–15) trigger tracks 1–16 on any MIDI channel. In Chromatic mode, MIDI channels 1–16 address tracks 1–16. Route your DAW's MIDI tracks to one MachineModule instance, choosing the channel for each instrument, then write notes in its piano roll.

With X.13 loaded, supported GND, TRX and EFM tonal machines automatically use the firmware's tonal tuning in Chromatic mode. MIDI note **60** plays the saved PTCH/PTC1 value; each semitone changes pitch by two parameter steps, clamped to 0–127. The actual frequency at note 60 depends on the machine and saved pitch. Playing notes leaves the saved pitch and TONAL setting unchanged. Stock firmware and machines without tonal support still respond by channel, at their saved pitch.

Each track is monophonic: a new note retriggers that track. Note-offs leave drum envelopes ringing; use the machine's decay controls to set their length. CC120/123 stops only its addressed track in Chromatic mode, and all tracks in Drum mode. The Voices budget still limits simultaneously active tracks. Existing audio-output routing is unchanged.

`md-midi-smoke` checks all 16 channel-to-track routes, an octave frequency change, channel silence commands, note-off tails, Drum mode, extreme notes and saved/legacy state using X.13. Version 0.4.0 source, standalone and VST3 are preserved in `../MachineModule-backups/0.4.0-before-chromatic-midi`.

## Random LFO fix (0.4.2)

RND now starts with nonzero, distinct random-generator state for each track. Previously the missing runtime initialization left the firmware's additive generator stuck at zero, so RND produced a constant value. The firmware still generates the waveform; this fixes its initial state without replacing its algorithm. Seeds are deterministic on engine creation, and are not reset by notes or shape changes.

To hear it, select RND for SHP1, set SHMIX to 0, choose the destination track and parameter (for example PTCH), and turn up DEPTH and SPEED. For SHP2 alone, set SHMIX to 127. FREE runs continuously, TRIG restarts on triggers, and HOLD updates the applied value on triggers.

`md-lfo-smoke` reproduced the stuck destination before the fix and checks both shape slots, FREE/TRIG/HOLD and zero depth on tracks 1, 8 and 16 with OS 1.63 and X.13. The previous source, standalone and VST3 are in `../MachineModule-backups/0.4.1-before-random-lfo-fix`.

## CPU optimization in 0.4.3

The DSP interpreter skips its indirect peripheral callback only when that callback would immediately return because its scheduled clock has not arrived. Pending interrupt handlers and due peripherals still execute through the original path. Firmware-call argument setup no longer allocates a temporary vector, and the plugin resolves output-bus buffers once per audio callback instead of once per sample. Synthesis, control rate, effects and voice budgets are unchanged.

`md-render-benchmark` measures the current stock/X.13 engine with six and sixteen voices, the original master effects, three trials and audio hashes. Each trial renders 3,000 blocks (2,176.87 ms of audio); process CPU includes all voice threads. Compare the reference and optimized logs for measurements on the same PC. The previous 0.4.2 source, standalone and complete VST3 are in `../MachineModule-backups/0.4.2-before-cpu-optimization-2`.

See [CPU validation](CPU-VALIDATION-0.4.3.md) for the processor routing A/B measurements and audio checks. Routing CPU savings ranged from inconclusive for stock main-only output to approximately 10% for all outputs; X.13 cases measured 3-7%. These figures depend on the workload and are not whole-version guarantees.

## Voice JIT in 0.4.4

Voice synthesis now uses native x64 JIT execution . Voice blocks are limited to one DSP instruction because longer blocks produced different noise-machine output in comparison tests. This still avoids interpreting each instruction. The original firmware synthesis, master effects, control rate, voice budget, and stock/X.13 voice grouping are preserved.

Common synthesis paths are compiled on the firmware loader thread before the engine becomes ready. Warm-up restores the initial voice memory and registers; it does not change the starting sound. ROM/RAM sample paths are not precompiled. Loading may take slightly longer, and previously unseen parameter-dependent code can still compile during playback.

`md-jit-smoke` checks warmed JIT against the explicit interpreter at 24-bit precision, including all machines below ID 80, retriggers, pitch/tonal changes, and simultaneous tracks. See [0.4.4 CPU validation](CPU-VALIDATION-0.4.4.md) for measured plugin CPU savings and regression results. The previous source, standalone, and VST3 are preserved in `../MachineModule-backups/0.4.3-before-voice-jit`.

For troubleshooting, `MACHINEMODULE_VOICE_INTERPRETER=1` before launching the standalone or DAW disables voice JIT. `MACHINEMODULE_INTERPRETER=1` separately disables master-effects JIT. Neither option is required for normal use. The new DSP fork does not provide the old JIT-failure flag; execution budgets stop a failed render instead of automatically switching the master to its interpreter.

## Build with Visual Studio 2022

Requirements: **Desktop development with C++**, Windows SDK, Visual Studio CMake tools, and JUCE sources. The local JUCE 8.0.8 installation at `C:\JUCE` was used for this build. Emulator dependencies are included under `libs/`.

Either open `build/MachinedrumModulePC.sln` in VS 2022, select **Release / x64**, and build `MachinedrumModule_Standalone` and `MachinedrumModule_VST3`, or run:

```powershell
powershell -ExecutionPolicy Bypass -File .\build-windows.ps1
# A different JUCE source location:
powershell -ExecutionPolicy Bypass -File .\build-windows.ps1 -JucePath D:\JUCE
```

You can also open this project folder in Visual Studio with its `vs2022-x64` CMake preset. The standalone build goes to `dist/Standalone/Release`. The VST3 build goes to `build/MachinedrumModule_artefacts/Release/VST3`; refresh the packaged copy under `dist/VST3` after rebuilding.

## Verification

`md-smoke` loads your firmware, renders every supported synthesis machine, requires non-silent output, and optionally writes a stereo WAV:

```powershell
.\build\Release\md-smoke.exe "path\to\firmware.bin" smoke.wav
```

`md-plugin-smoke` exercises the actual processor: firmware loading, MIDI, finite output at 44.1/48/96 kHz with block sizes 1/17/127/511, individual-output routing, a 16-track stress render, state restoration, and an optional offscreen editor snapshot:

```powershell
.\build\md-plugin-smoke_artefacts\Release\md-plugin-smoke.exe "path\to\firmware.bin" editor-preview.png
```

These checks passed with the supplied OS 1.63 flash image. They do not replace testing in your particular DAW/audio-device setup. See `smoke.log`, `plugin-smoke.log` and `master-probe.log` for the results. ROM playback and kit import need suitable sample/kit fixtures for end-to-end verification.

## Source and licensing

AGPL-3.0-only; see `LICENSE` and `THIRD_PARTY_NOTICES.md`. No Elektron firmware or extracted samples are distributed with this project. Not affiliated with Elektron or Akai.

## Original master DSP validation

Run `build/Release/md-master-probe.exe` with your firmware path. It compares the PC mixer/master path against the original DSP mixer and master routines across delay, reverb, EQ and dynamics settings, checks impulse responses and deterministic resets, and checks delay timing at 120 and 240 BPM. This is a DSP-routine comparison, not a recording comparison against physical hardware or a complete machine boot. Parameter changes are supplied directly to the firmware coefficient routine; the full machine's UI/sequencer scheduling is not emulated.

The original master adds CPU work. Start with the default six-voice budget and a 512-sample buffer; the 16-track stress test can exceed real-time on this PC. Version 0.3.0 source, standalone and VST3 are backed up together in the sibling folder `MachineModule-backups/0.3.0-before-cpu-optimization`. The older 0.2.0 binary backup is retained in `dist/previous-0.2.0`.

## CPU optimization in 0.3.1

The original master-effects DSP uses the x64 JIT; voice synthesis retains the reference interpreter. The master JIT is bounded to 32 instructions per block and four DO-loop iterations per dispatch. Firmware initialization uses the interpreter. Common effect paths are warmed on the firmware loader thread, and a permanent master stop instruction avoids invalidating compiled blocks every audio block. A reported JIT code-generation failure falls back to the interpreter.

Validation matched 230,400 samples through the original mixer/master chain and another 153,600 samples during randomized live effect and tempo changes. Impulse, reset, delay timing, routing, sample-rate and state checks also passed. The attempted voice JIT produced different TRX-SD samples, so that experiment was not enabled in the release.

`md-cpu-benchmark` compares the same 3,000-block workload with interpreted and JIT master effects, using three voice groups and three trials at each voice budget. Setup/warm-up is excluded. Process CPU time includes all worker threads; rendering time is elapsed time. Every A/B audio hash matched.

| Voice budget | Interpreter rendering | Optimized rendering | Process CPU reduction |
| --- | ---: | ---: | ---: |
| 6 | 2003.81 ms | 951.77 ms | 41.37% |
| 16 | 2656.40 ms | 1654.57 ms | 23.21% |

Each render produces 2176.87 ms of audio. These are measurements on this PC, not guarantees for every kit, buffer size or DAW. See `cpu-benchmark.log` and `opt-master-final.log`. Run `build/Release/md-cpu-benchmark.exe` with the firmware path to reproduce the comparison.

For troubleshooting only, setting the environment variable `MACHINEMODULE_INTERPRETER=1` before launching the standalone or DAW forces interpreted master effects. Leave it unset for normal optimized playback and for the benchmark. Existing project state and parameter IDs remain compatible.
