# Universal build runtime validation — 2026-10-16

Firmware: user-supplied `Elektron_SPS1-1UW_OS1.63.syx` (excluded from distribution).
Host: Apple Silicon, macOS 26.7.1; Xcode 26.5; JUCE 8.0.8; CMake 3.31.8.

The initial ARM64 stability and master-effects runs stalled. Process samples
showed the master interpreter blocked while adding a DMA interrupt to its full
1024-entry interrupt queue. The mixer calls firmware routines directly with
interrupts masked, but firmware initialization had left serial-link DMA enabled.
`MixerRef` now disables those unused DMA channels after initialization. The
host supplies mixer input blocks directly; serial-link transfers are unnecessary.

After rebuilding and signing both architectures:

| Check | Result |
| --- | --- |
| ARM64 stability, 60 audio seconds | PASS; peak 4617669; hash e72f1ad39f22364f; 19.879 wall seconds |
| x86_64 stability via Rosetta, 60 audio seconds | PASS; same peak and hash; 12.392 wall seconds |
| ARM64 master effects | PASS; 230400 reference samples and 153600 live-control samples matched; impulse/reset/tempo/silence checks passed |
| x86_64 master effects via Rosetta | PASS; same comparisons and impulse checks |
| ARM64 LFO | PASS; RND slots, FREE/TRIG/HOLD and zero-depth checks |
| Standalone and VST3 architecture/signature checks | PASS; x86_64 + arm64; local ad-hoc signatures |

The ARM64 stability run emitted 4358 DSP memory-read error diagnostics despite
passing and matching the Intel output hash. A subsequent investigation traced
them to unused look-ahead loads in the OS 1.63 voice firmware. Replacing all
out-of-range read results with either 0x555555 or 0xaaaaaa left the complete
60-second audio hash unchanged. See `DSP-READ-INVESTIGATION-2026-10-16.md`.
This is not proof of correct playback for every patch or firmware version.
Native Intel hardware, interactive standalone audio and DAW/VST3 behavior remain
untested. The binaries are not notarized.

Raw logs and samples are retained in the workspace's `.build-deps` folder.

## X.13 and MACRI follow-up

X.13 was located in Downloads. Its master-effects reference checks passed on
native ARM64. Expansion testing found missing MACRI support in neighbor effect 9;
this has been implemented in both backends. The updated expansion test passes on
ARM64 and x86_64 via Rosetta with no unsupported-instruction or DSP error messages.
The dedicated MACRI regression passes 448 ARM64 interpreter cases and 896 x86_64
interpreter/JIT cases. See the DSP read investigation report for details.

After the fix, all four 60-second stability runs passed:

| Firmware | Architecture | Peak | Audio hash |
| --- | --- | ---: | --- |
| OS 1.63 | native ARM64 | 4617669 | e72f1ad39f22364f |
| OS 1.63 | x86_64 via Rosetta | 4617669 | e72f1ad39f22364f |
| X.13 | native ARM64 | 4870572 | abebc36df40fb624 |
| X.13 | x86_64 via Rosetta | 4870572 | abebc36df40fb624 |

X.13's stability memory-read diagnostics also passed the fallback-value experiment:
zero, 0x555555 and 0xaaaaaa all produced the same peak and audio hash.
These stability scenarios do not exercise X.13's neighbor effect 9; the separate
expansion test and MACRI regression cover that fix. DAW testing of the updated
MACRI build remains pending; the user's earlier Ableton tests preceded that fix.
