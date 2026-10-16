# MachineModule 0.4.6 — native ARM JIT validation

## Behavior

0.4.6 enables JIT for voices and original master effects by default on native
Apple Silicon. Intel retains JIT. The standalone app and VST3 are Universal
(`arm64` + `x86_64`), targeting macOS 12.0 and later. Plugin identifiers,
548 saved parameters and project state format are preserved.

The DMA interrupt-queue and MACRI fixes from 0.4.5 are retained. No further
DSP/JIT backend changes were required to pass the ARM JIT validation.
The experimental candidate's test-settings override is not included in the
release processor; normal firmware preference handling is retained.

## Candidate validation

Test host: Apple M1, native ARM64, macOS 26.7.1, Xcode 26.5, CMake 3.31.8,
JUCE 8.0.8. Source base: 0.4.5 commit `68c65da`, with ARM JIT enabled.

All 18 final automated candidate checks passed:

| Check | OS 1.63 | X.13 |
| --- | --- | --- |
| Voice JIT/interpreter, warm-up/retriggers/pitch/16 tracks | 18,631,680 matching track samples | 20,398,080 matching track samples |
| Original master mixer reference | 230,400 matching samples | 230,400 matching samples |
| Live master control/tempo changes | 153,600 matching samples | 153,600 matching samples |
| Master impulses, reset, tempo and silence | PASS | PASS |
| LFO modes and zero depth | PASS | PASS |
| X.13 expansions and neighbor effects | N/A | PASS |
| 60-second and 300-second stability vs interpreter | PASS | PASS |
| Processor sample rates, blocks, routing and state | PASS | PASS |
| MIDI channels, firmware-appropriate pitch, panic and tails | PASS | PASS |

The native ARM MACRI regression passed 896 interpreter/JIT cases, including
accumulator values, status flags and immediate-word consumption.
Processor tests covered 44.1/48/96 kHz, variable blocks, individual outputs,
effect returns, 6/16-voice stress and restoration of all 548 parameter values.
No hangs, crashes, unsupported instructions or memory-read diagnostics were
observed in the final JIT runs.

The MIDI test initially expected octave transposition on stock OS 1.63. An
interpreter control reproduced the failure, and the processor explicitly
enables that pitch behavior only for X.13. The test was corrected to assert
stock channel mapping without transposition while retaining X.13's octave
assertion. This correction changes test expectations, not MIDI product behavior.

| Firmware | Interpreter render time | JIT render time | Matching 300-second audio hash |
| --- | ---: | ---: | --- |
| OS 1.63 | 95.137 s | 35.057 s | `ab062cc86083e2b5` |
| X.13 | 113.588 s | 43.089 s | `73fc34fd1ed5570c` |

These are accelerated offline renders of five minutes of audio, not five-minute
real-time DAW sessions. Some reference timing overlapped setup work; these are
indicative throughput measurements, not controlled CPU/latency benchmarks.

## Ableton observation

The user tested the ARM JIT candidate in Ableton and reported approximately
25% lower CPU use. Exact firmware, project, buffer size and session duration
were not recorded for that comparison. Earlier user testing had confirmed
both OS 1.63 and X.13 with the interpreter-default build. The observation supports
the JIT candidate's practical benefit but does not establish a universal CPU gain
or exhaustive host/session stability.

## Fallback and preserved release

The 0.4.5 binaries, matching source and Git tag remain available and unchanged:
[0.4.5 release](https://github.com/AcidReff/MachineModule/releases/tag/v0.4.5).
Their Apple Silicon slice defaults to the interpreter.

To compile an interpreter-default ARM slice from 0.4.6:

```sh
MACHINEMODULE_ENABLE_ARM64_JIT=OFF bash build-macos.sh /path/to/JUCE-8.0.8
```

That option also disables internal ARM JIT dispatch. The existing runtime
interpreter overrides remain available for diagnostics.

## Limits

Native Intel hardware was not retested; x86_64 checks run through Rosetta on M1.
Other Apple Silicon models, older supported macOS versions, every patch, imported
samples, all DAWs and long real-time sessions have not been exhaustively tested.
Processor tests call shared processor code rather than hosting the VST3 in a DAW.
The final versioned Universal artifacts receive separate release checks below.
Windows binaries were not rebuilt. Mac bundles are locally ad-hoc signed with
the JIT entitlement, not notarized. Firmware and samples are not distributed.

## Final versioned Universal build — 2026-10-16

All **16 final release runtime checks passed** using the rebuilt 0.4.6 tools.
Each architecture (native ARM64 and x86_64 via Rosetta) passed:

- 896 MACRI interpreter/JIT regression cases.
- Voice JIT/interpreter comparisons for both OS 1.63 and X.13.
- 60-second stability renders for both firmware versions, with voices and
  master effects confirmed to use JIT.
- Original master-reference, impulse, reset, tempo and silence checks for both firmware versions.
- X.13 expansion and neighbor-effect checks.

The release stability hashes matched on both architectures and matched the
preserved 0.4.5 reference: OS 1.63 `e72f1ad39f22364f`, X.13 `abebc36df40fb624`.
All final release runs exited successfully without timeouts, unsupported
instructions or memory-read diagnostics.

Both app and VST3 declare version 0.4.6, include `arm64` and `x86_64`, and pass
strict local signature verification with the JIT entitlement. The preserved
0.4.5 binary/source ZIP checksums were rechecked and remained unchanged.

The five-minute, processor and MIDI coverage above applies to the preceding
candidate; the final rebuild reran the 16 checks listed here. It changes the
version/default build policy and includes the firmware-aware test corrections;
the DSP/JIT engine implementation is unchanged from that candidate.
