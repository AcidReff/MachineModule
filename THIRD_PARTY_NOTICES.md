# Source provenance

- **sd88me/mpc-vst-machinedrum** — https://github.com/sd88me/mpc-vst-machinedrum — AGPL-3.0-only. `engine/`, `tools/mdfw/`, `tools/mdmix/MixerRef`, kit parsing, and sample-file format come from the user's supplied checkout (README identifies v0.3.4). Local changes: portable machine-table search, owned firmware lifetime, MIDI panic method, reference interpreter execution, master coefficient execution and master DSP/reset support. The original supplied folder is unchanged.
- **shnolk/monomodule** — https://github.com/shnolk/monomodule — AGPL-3.0. Original attribution in the firmware decoder and DSP harness is retained. Its JUCE/CMake Windows setup was used as a reference.
- **sd88me/dsp56300** — https://github.com/sd88me/dsp56300 — GPL-3.0; checkout `26558e5401fa134a013eca8dbb8c5ecde837ec55` (`recomp-fixes`). Includes asmjit at `3577608cab0bc509f856ebf6e41b2f9d9f71acc4` (see its bundled license). Local `dsp.h` changes allow MSVC's member-pointer size and avoid an out-of-bounds member-pointer copy in recompiler metadata. This dependency supplied the DSP implementation through 0.4.3; in 0.4.4 only its pinned asmjit dependency is compiled.
- **joelanders/gearmulator-md-mm** — https://github.com/joelanders/gearmulator-md-mm — checkout `8cea0524a75435122c20b669ca114c9ac6509ba2`. Only its mc68k dependency is compiled, not its full-system hardware emulator. mc68k checkout `ace95b3d0a5a332db147244762dda65f9a010b9f` includes Musashi; see bundled source notices/licenses.
- **JUCE 8.0.8** — https://github.com/juce-framework/JUCE — local `C:/JUCE` source, used under its AGPLv3 option. JUCE includes the VST3 wrapper/SDK and other third-party components; see `C:/JUCE/LICENSE.md` and its module notices.

The new PC wrapper, loader adapters, build files and tests are supplied under AGPL-3.0-only. Elektron's firmware remains user-supplied and is neither embedded nor copied into the distribution folder.

Version 0.4.5 locally changes `libs/dsp56300-jit/source/dsp56kEmu/dspconfig.h` to
select the firmware interpreter on native Apple Silicon unless experimental ARM64
JIT is explicitly enabled. Existing source copyright and GPL notices are retained.

Version 0.4.3 additionally changes dsp56300's `DSP::execInterpreter` to avoid an indirect peripheral callback before its scheduled clock, while preserving interrupt dispatch and the original due-peripheral path. The source retains the upstream license and notices.

- **c0remusic/dsp56300-md-mm** - https://github.com/c0remusic/dsp56300-md-mm - GPL-3.0; commit f84b5b6c01fc7e4e74bdd9a54087f0fc4c6b08d7. Version 0.4.4 compiles the vendored dsp56kBase, dsp56kEmu and vtuneSdk directories in libs/dsp56300-jit, with the same pinned asmjit above. Upstream source and license notices are retained.
- **c0remusic/gearmulator-md-drums** - https://github.com/c0remusic/gearmulator-md-drums/tree/md-drums-v0.1.0 - voice-JIT integration and memory/register warm-up restoration were used as references. The harness retains its existing AGPL attribution; no MD Drums binary or firmware is embedded.
