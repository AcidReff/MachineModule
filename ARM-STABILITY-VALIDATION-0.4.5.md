# 0.4.5 Apple Silicon stability candidate validation

Reported failure: OS 1.63 produces sound for a few seconds, then hangs standalone
and VST3 on an M2 Pro. The user reports the Universal build works on Intel macOS
12.6. The exact Mac source revision, native process architecture, hang sample and
root cause are not yet confirmed.

## Mitigation

Native Apple Silicon uses the original firmware interpreter for voice and master
DSP execution. A compile-time DSP configuration guard also disables internal JIT
dispatch. Wrapper mode selection and master warm-up honor the effective mode.
The experimental CMake switch can restore ARM JIT for investigation; the normal
Universal script explicitly disables it. Intel/Windows retain their previous JIT.

This is a candidate mitigation, not a confirmed native ARM64 fix. Original firmware
synthesis/effects, parameters, plugin IDs, output layout and three stock / one X.13
voice groups are preserved. Higher Apple Silicon CPU use is a tradeoff.

## Completed checks on Windows

- Release standalone, VST3 and diagnostic tools built with VS 2022 / JUCE 8.0.8.
- Stock 30-second audio stress: JIT, explicitly interpreted, and a separate build
  with `DSP56K_FORCE_INTERPRETER=ON` all passed and produced main-output hash
  `a85b93f7a3a42bec`. Exercises six active voices, three DSP workers, all 16 track
  slots, retriggers, machine changes, idle periods, effects and tempo changes.
- All 47 stock synthesis machines passed in both interpreter configurations.
  Their rendered WAVs matched the saved release reference, SHA-256:
  `EA429619D30488C93A189AEB62730A1E24B0C57A464951965198562F29B567F5`.
- Original master DSP comparisons, live changes, resets and 120/240 BPM delay
  timing passed in the compile-time interpreter build.
- X.13 five-second stress hashes matched JIT and compile-time interpreter:
  `644a3f19aad6aa23`. X.13 expanded oscillators, NFX, tonal coefficients and effects
  passed with the compile-time interpreter.
- Plugin routing/sample rates/host blocks and all 548 saved parameters passed.
- Chromatic MIDI all-channel routing, octave tuning, panic, tails, Drum mode and
  state tests passed with X.13.
- Existing stock JIT/warm-up comparison passed 18,631,680 matching 24-bit samples.
  With JIT disabled, the comparison now explicitly returns 77 / SKIP rather than
  reporting a false JIT pass.
- Both Mac shell scripts passed Bash syntax checks; the diagnostic launcher
  correctly rejects non-Mac execution.
- Focused five-file hotfix applied cleanly to a copy of the previous source and
  reproduced the tested engine/DSP files, leaving build files untouched.

These Windows tests validate the interpreter route and regressions, not Apple
Silicon execution. Timing under background compilation is not a CPU benchmark.

## Pending on the tester's Mac

1. Existing-app test with BOTH interpreter environment variables.
2. Rebuild or focused hotfix; confirm native ARM64 mode, then repeat the original
   playing pattern for at least two minutes in standalone and VST3.
3. Native `md-stability-smoke` 60-second audio render and original master probe.
4. Activity Monitor process sample if the freeze persists, plus architecture,
   macOS version, firmware/machine/kit and reproduction steps.

See APPLE-SILICON-STABILITY.md. No Mac binary was compiled in this Windows
workspace. External Mac-only source changes cannot be inferred from this checkout;
use the focused patch to preserve them where applicable.

Local raw results: `arm-safety-*.log`. The separate interpreter-only validation
build is `build/arm-safe-core-validation/out` and is not distributed.

The previous source, Windows standalone and complete VST3 are in
`../MachineModule-backups/0.4.4-before-apple-silicon-stability`. Mac binaries were
built externally and were unavailable for this backup.
