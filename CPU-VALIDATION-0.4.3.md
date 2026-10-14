# CPU validation - MachineModule 0.4.3

Changes: avoid no-op DSP peripheral dispatch, remove firmware-call argument allocations, and resolve output buffers once per callback. No synthesis/control-rate/voice-budget changes.

## Processor routing comparison

Two runs per firmware, opposite A/B execution orders. Reference uses the saved 0.4.2 Plugin.cpp with the current engine; this isolates the routing change, rather than claiming a whole-version speedup. Each case processes 400 variable-size blocks (17/511 frames), six voices, MIDI triggers, parameter changes and effects. Process CPU sums all threads; wall time is elapsed rendering time.

| Firmware | Outputs / rate | Reference CPU ms | Optimized CPU ms | CPU reduction | Wall reduction |
| --- | --- | ---: | ---: | ---: | ---: |
| stock | Main / 44.1 kHz | 3945.31 | 3968.75 | -0.6% | 5.7% |
| stock | Sparse / 48 kHz | 4000.00 | 3703.12 | 7.4% | 6.1% |
| stock | All / 96 kHz | 2406.25 | 2164.06 | 10.1% | 12.5% |
| x13 | Main / 44.1 kHz | 2929.69 | 2804.69 | 4.3% | 5.6% |
| x13 | Sparse / 48 kHz | 2507.81 | 2335.94 | 6.9% | 7.4% |
| x13 | All / 96 kHz | 1281.25 | 1242.19 | 3.0% | 3.1% |

CPU changes ranged from -0.6% to about 10%; stock main-only CPU savings were not established. Background load and scheduling affect results. Do not add these percentages to engine benchmark percentages.

## Correctness

All 24 processor-case hashes matched across both runs, all layouts and both firmware versions. Engine benchmark hashes also matched the original executable for six and sixteen voices on both firmware versions. Forty-seven synthesis machines per firmware rendered WAV files identical to the reference. Original master effects, resets/delay timing, X.13 expansions, RND LFO, MIDI channel/octave tests, multi-output routing, variable host block sizes/rates and all 548 saved parameters passed.

Engine-only timings were noisy, so no separate interpreter speedup percentage is claimed. Compiler-wide LTO was evaluated and not retained. ROM sample playback was not validated because no sample fixture was supplied.

Raw results are local cpu2-routing-*.log, cpu2-fast-*.log, cpu2-ref-*.log and cpu2-* regression logs. The previous complete release is preserved in MachineModule-backups/0.4.2-before-cpu-optimization-2.

Reproduce with md-processor-benchmark and md-render-benchmark; configure MACHINEMODULE_REFERENCE_PLUGIN to an old Plugin.cpp to build md-processor-reference for A/B routing checks. Firmware is user-supplied.
