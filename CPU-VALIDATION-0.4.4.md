# MachineModule 0.4.4 CPU validation

Voice synthesis now uses the c0remusic/dsp56300-md-mm JIT fork at commit
f84b5b6c01fc7e4e74bdd9a54087f0fc4c6b08d7, as used by MD Drums 0.1.0.
The master also uses this library. Single-instruction voice blocks are deliberate:
32-instruction blocks differed from the interpreter on noise machines, even with
the optimizer disabled or interpreter initialization. Single-instruction JIT
passed the comparisons below. No DSP source modifications were needed.

Common synthesis paths are precompiled on the firmware loader thread. Memory and
registers are restored afterward, preserving initial musical state. Three stock
voice groups and one shared X.13 group remain unchanged. This avoids introducing
the shared-noise/state differences caused by changing the number of voice DSPs.
Firmware effects, control rate, output routing and voice budgets are unchanged.

## Full-plugin comparison with 0.4.3

The saved 0.4.3 processor benchmark executable and newly built 0.4.4 executable
each ran twice, in opposite A/B orders. The table reports their arithmetic means.
Each case processes 400 variable-size blocks (17/511 frames), six voices, MIDI
retriggers, RND LFO, parameter changes, and original master effects. CPU time sums
all process threads; wall time measures elapsed rendering. Loading and warm-up
are excluded. No builds or other regression tests ran during these measurements.

| Firmware | Outputs / sample rate | 0.4.3 CPU ms | 0.4.4 CPU ms | CPU reduction | Wall reduction |
| --- | --- | ---: | ---: | ---: | ---: |
| OS 1.63 | Main / 44.1 kHz | 4000.00 | 3257.81 | 18.6% | 0.3% |
| OS 1.63 | Sparse / 48 kHz | 4125.00 | 3148.44 | 23.7% | 15.8% |
| OS 1.63 | All / 96 kHz | 2320.31 | 1429.69 | 38.4% | 28.2% |
| X.13 | Main / 44.1 kHz | 2851.56 | 2328.13 | 18.4% | 15.4% |
| X.13 | Sparse / 48 kHz | 2804.69 | 2242.19 | 20.1% | 17.7% |
| X.13 | All / 96 kHz | 1343.75 | 1171.87 | 12.8% | 7.5% |

These are workload-specific measurements on this PC, not a promise for every DAW
or kit. Stock main-output wall time was effectively unchanged despite lower total
CPU use. Timing varied between runs. Different sample rates produce different
amounts of native 44.1 kHz DSP work for the fixed host-frame workload, so compare
versions within a row rather than comparing rows.

All 24 processor-case output hashes matched. Hashes by row:
75b8028d91397203, 42a21991b3dcccb4, dce8775d2d387e4d,
4db88e194e8708c9, 176413f542a7d8fc, 1848535589fb0dba.

## Audio and feature checks

- `md-jit-smoke`: warmed JIT and explicit interpreter matched 18,631,680 stock and
  20,398,080 X.13 24-bit track samples, plus their main mixes. Exercises all machine
  IDs below 80, retriggers, pitch changes, tonal changes and 16 simultaneous tracks.
- `md-smoke`: 47 synthesis machines rendered with no silent machines. Stock JIT
  WAV matched the saved 0.4.3 WAV exactly with the final single-instruction setting.
- `md-master-probe`: original DSP mixer/master comparison, live effect changes,
  impulse responses, resets and 120/240 BPM delay timing passed on both firmwares.
- `md-x13-smoke`: expanded oscillators, NFX, tonal coefficients and effects passed.
- `md-lfo-smoke`: both RND shape slots, FREE/TRIG/HOLD and zero depth passed on both
  firmwares.
- `md-midi-smoke`: 16-channel routing, octave tuning, panic, note-off tails, drum
  mode, extreme notes and saved/legacy state passed with X.13.
- `md-plugin-smoke`: routing, sample rates, variable host blocks and all 548 saved
  parameters passed on both firmwares.
- Release x64 standalone and VST3 compiled successfully with VS 2022 and JUCE 8.0.8.

ROM/RAM sample playback was not validated: no extracted ROM sample fixture was
available. These paths are not precompiled. Unseen parameter-dependent code can
still compile during playback. Dense kits or small buffers can still overload a
CPU; this change does not guarantee dropout-free playback at 16 voices.

## Reproduction and rollback

Build `md-jit-smoke`, `md-master-probe`, `md-x13-smoke`, `md-lfo-smoke`,
`md-midi-smoke`, `md-plugin-smoke` and `md-processor-benchmark`; pass the user's
firmware path as the first argument. Firmware is never distributed.

Local raw results: `jit-cpu-{stock,x13}-{ref,fast}-{1,2}.log` and
`jit-final-*.log`. The reference processor executable is
`build/Release/md-processor-043-reference.exe`.

Previous source, standalone and complete VST3 are preserved together in
`../MachineModule-backups/0.4.3-before-voice-jit`.
`MACHINEMODULE_VOICE_INTERPRETER=1` selects interpreted voices for troubleshooting;
`MACHINEMODULE_INTERPRETER=1` separately selects interpreted master effects.
