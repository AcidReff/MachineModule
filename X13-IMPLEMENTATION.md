# X.13 integration, MachineModule 0.4.0

Supported: stock UW OS 1.63 and the user's verified UW X.13 release, as SysEx
or an 8 MiB flash image. No firmware, extracted instructions or samples are
embedded or distributed. The local `build/firmware-analysis` directory is
private diagnostic output and is not part of the distribution or source backups.

## Firmware-derived integration points

The X.13 flash payload is 0xf4ac4 bytes starting at flash 0x4000. Its FNV-1a-64
fingerprint is 0x33c3853f38437ba0. It retains obsolete checksums in its first
three compressed sections. The loader permits these only when the complete
known payload matches, rather than trusting the version label.

The boot stub at flash 0x2be00 decompresses the extension at 0xe96e0 into RAM
0x2c0000. After its eight-byte header, the stream expands to 134876 bytes.
The main OS plus extension occupies 921308 bytes including the intervening gap.

The OS routine at 0x2c039e uploads 0xb08 24-bit voice-DSP words from 0x2dbd6c
to DSP P:0x13b600 and replaces initialize/trigger/render dispatch entries.
`installX13VoiceProgram` reads those words and pointer values from the user's
decompressed firmware. Expanded machine descriptors occupy 0x2e0420 through
0x2e067a; they replace GND-SN and add GND-SW/PU, NFX-EV/CO/UC and the input
compressor definitions. Input machines remain excluded by the instrument UI.

X.13 coefficient routines infer the track from the output address, using
21 long words per track at 0x10015b4. The tuning flags are at 0x2e1ab4.
The original generic scratch output pointer cannot be used for these routines.
The smoothing/LFO SRAM routines used by HostModel and the master coefficient
code remain at the stock addresses. The master DSP program used here is shared.

NFX requires shared voice buffers, so the plugin uses one voice-DSP instance
for X.13. Idle voices clear their firmware output buffers, and automatic silence
release is disabled. Stock 1.63 retains three voice groups and silence release.

## Verification on this PC

- `x13-expansions.log`: GND-SN/SW/PU on tracks 1, 8 and 16, all three NFX
  machines, coefficient calls in normal/tonal modes, tonal pitch changes,
  reverb and delay.
- `x13-plugin-smoke.log`: routing, 44.1/48/96 kHz, varied block sizes,
  547-parameter state restore including tonal, all 64 imported kit slots,
  X.13 -> stock -> X.13 with the editor open, and machine-menu updates.
- `stock-040-engine-smoke.log`: 47 stock synthesis machines, none silent.
- `stock-040-plugin-smoke.log`: stock plugin audio, routing, effects and state.
- `x13-flash-smoke.log`: the same expanded-engine checks using a flash image.
- `x13-damaged-test.log`: a modified image is rejected.

X.13 stress rendering took 1372 ms at a six-voice budget and 2712 ms at sixteen
for 2133 ms of audio. Stock took 850 / 1708 ms in separate runs. These are
workload-specific wall-clock measurements, not a guarantee for other kits.

This is a synthesis module integration, not a full X.13 hardware emulation.
There is no physical-hardware or full-boot audio equivalence claim. Sequencing,
RAM recording, live inputs, extended LFO selections and panel shortcuts are not
implemented. Other community firmware releases require their own validation.
