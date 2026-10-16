# DSP read diagnostics investigation — 2026-10-16

Scope: OS 1.63, native ARM64, the existing 60-second stability workload.

## Findings

`Memory::get` reports reads past the emulated memory allocation and returns zero.
It checks the address before accessing host memory, so these messages do not
represent an out-of-bounds host read in this path.

Temporary stack traces located the reads in `VoiceEngine::runUntilTx`, through
the interpreter's parallel X/Y move instruction. The mixer is not the source.
Temporary instruction traces found six distinct reported PCs (the PC has already
advanced past the parallel move):

| Reported PC | Number of diagnostics |
| --- | ---: |
| 0x142181 | 29 |
| 0x142dde | 142 |
| 0x142ddf | 142 |
| 0x143522 | 57 |
| 0x143e85 | 9 |
| 0x144b3f | 3979 |

These are look-ahead fetches in pipelined voice routines. At loop exit their
fetched values or the calculations using them are discarded. For example,
instruction 0x142180 loads X memory into x0; x0 is overwritten at 0x142184
before it is used. Its parallel y0 load is also overwritten before use.

## Fallback-value experiment

A temporary diagnostic build changed the return value for **every** out-of-range
`Memory::get` call. All runs exercised the same firmware and 60-second workload:

| Fallback value | Result | Peak | Audio hash |
| --- | --- | ---: | --- |
| 0x000000 (normal release behavior) | PASS | 4617669 | e72f1ad39f22364f |
| 0x555555 | PASS | 4617669 | e72f1ad39f22364f |
| 0xaaaaaa | PASS | 4617669 | e72f1ad39f22364f |

The unchanged hash establishes that those read values do not affect the rendered
main output in this workload, consistent with the disassembly. Intel JIT also
produced this hash. Its generated memory reads bypass `Memory::get` and apply
their own address handling, explaining why absence of the messages on Intel
was not evidence of an ARM-specific fault.

An outside-sandbox run successfully enabled MMU mapping and still emitted
diagnostics with the same audio hash. The sandbox is not their sole cause.

## Release disposition and limits

No memory bounds checks or error messages were suppressed. All temporary tracing,
fallback-value changes and disassembly instrumentation were removed, and both
release architectures rebuilt. Existing behavior is retained.

This investigation supports treating the observed OS 1.63 diagnostics as harmless
discarded reads for this workload. It does not establish that every future memory
diagnostic is harmless. X.13 was subsequently located in Downloads and tested
as described below. The user's successful Ableton tests of both firmware versions
remain separate manual evidence. Native Intel hardware and other machine/parameter combinations
have not been exhaustively tested.

Raw traces and experiment logs are in `.build-deps/trace-*.log` and
`.build-deps/poison-*.log` in the workspace. The initial LOGF-based temporary trace
started the dependency's file logger and caused termination during process exit
after PASS; subsequent stderr-only diagnostic runs exited successfully. The
temporary logger call is absent from the restored release.

## X.13 follow-up

User-supplied firmware: `Machinedrum_SPS1-UW_OS_X.13.syx` in Downloads.
The normal 60-second ARM64 stability run reported 7230 memory-read messages
but matched Intel JIT's peak 4870572 and audio hash abebc36df40fb624.
Repeating with all out-of-range read results replaced by 0x555555 and 0xaaaaaa
produced that same peak and hash. This supports the same discarded-read
interpretation for X.13's stability workload. Temporary changes were removed.

The broader X.13 expansion check exposed a **separate, real defect**: MACRI
was unimplemented in both interpreter and JIT. The test's non-silence criterion
allowed it to print PASS despite repeated unsupported-instruction dumps during
neighbor effect 9. This was not one of the memory-read diagnostics.

Both backends now implement MACRI using immediate multiply-accumulate with
convergent rounding, matching existing MACI and RND semantics. The regression
executable `md-macri-smoke` checks source/destination registers, negation,
boundary immediates and accumulator values, status flags and immediate-word
consumption. ARM64 passed 448 interpreter cases; x86_64 via Rosetta passed
896 interpreter/JIT cases. Native ARM JIT remains disabled and unvalidated.

After the fix, the X.13 expansion test passed on ARM64 and x86_64 via Rosetta
with no unsupported-instruction or DSP error messages. Neighbor effect 9's peak
changed from 457 to 162740 on ARM64; Intel produced the same corrected peak.
The original master-reference comparisons, live controls, reset, impulse and
tempo checks also passed on native ARM64 before this instruction fix.

Raw X.13 logs are `.build-deps/x13-*.log`, `.build-deps/macri-*.log` and
`.build-deps/fixed-*-stability.log`. Firmware is excluded from distribution.
