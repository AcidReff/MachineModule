// The Machinedrum's main-processor side, for the voice DSP: per-track kit parameters, machine assignment,
// triggers and the control tick. The maths is the OS's own code run in MachineRunner (parameter smoothing
// $10002e0, LFO oscillator $1000088, LFO apply $1000332, machine coefficient functions); this class does what
// the OS's tick routine ($20ad9a) and slot pump ($10004d0) do around them. See docs/PROTOCOL.md.
//
// It also produces each track's inputs to the mixer DSP (DSP1): the 9 effect words (Y:$200+$40k) and the
// 5 mix words (Y:$100+5k), computed as the tick routine does ($20b1f6-$20b302).
//
// Not yet modelled: parameter locks and trigger groups (sequencer features).
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "MachineRunner.h"
#include "VoiceEngine.h"

namespace md::engine
{
	// TVoices: VoiceEngine (default) or any type with the same setSlot/renderBlock/Block/kSlotWords surface
	// (e.g. ParallelVoiceEngine, which splits the 16 slots across threads - see its header).
	template<class TVoices = VoiceEngine>
	class HostModel
	{
	public:
		static constexpr int kTracks = 16;
		static constexpr int kParams = 24;	// SYN1-8, AMD AMF EQF EQG FLTF FLTW FLTQ SRR DIST, VOL PAN DEL REV, LFOS LFOD LFOM

		HostModel(MachineRunner& _os, TVoices& _voices);

		// Assign a machine (0-191, MachineInfo::id). SYN1-8 take the machine's defaults. As on the MD, the voice
		// switches machine at its next trigger, where all 24 parameters take effect immediately (no glide).
		void setMachine(int _track, uint8_t _machineId);
		void setParam(int _track, int _param, int _value);	// 0-127, smoothed like the MD
		int param(int _track, int _param) const { return m_raw[_track][_param]; }
        void setTonal(int track,bool enabled) {m_os.setTonal(track,enabled);}
        void setParamImmediately(int track,int param,int value);
        void stopTrack(int track) {
            silenceVoice(track);m_trigger[track]=false;m_silenceNext[track]=true;
            m_activeOrder.erase(std::remove(m_activeOrder.begin(),m_activeOrder.end(),track),m_activeOrder.end());
            m_quiet[track]=0;
        }
		void setTempo(double _bpm);							// tempo factor for tempo-synced LFOs and E12/ROM retrig
		// velocity 1-127; accent: the sequencer's accented step (velocity becomes 128 + 2 x accent amount)
		void trigger(int _track, int _velocity = 100, bool _accent = false);
		void allSoundOff() {
			for(int t = 0; t < kTracks; ++t) {silenceVoice(t);m_silenceNext[t]=true;}
			m_trigger.fill(false);
			m_activeOrder.clear();
		}
		void setAccentAmount(int _amount) { m_accentAmount = std::clamp(_amount, 0, 127); }

		// Caps how many tracks can be simultaneously active (most-recently-triggered N; a new distinct
		// trigger past the cap steals the least-recently-triggered one, hard-cutting it to silence - not a
		// real MD behavior, which always runs all 16 track slots, but a performance safety valve for patterns
		// that never actually need all 16 at once). Default kTracks = disabled (every track its own voice, as
		// on real hardware). 1-kTracks; out-of-range clamps.
		// The voice budget, in units: most machines cost 1 and the ROM (sample) machines 2, after what one voice costs the
		// voice DSP on the Force (measured: ROM +397 us, the others +213 us on average per 128-frame block).
		void setMaxActiveVoices(int _n) { m_maxActive = std::clamp(_n, 1, kTracks * 2); }
		static int voiceCost(const uint8_t _machine) { return (_machine >= 128 && _machine < 160) || (_machine >= 176 && _machine < 192) ? 2 : 1; }
		int maxActiveVoices() const { return m_maxActive; }

		// Silence release: a sounding voice whose DSP output stays within +-_threshold (24-bit) for _blocks blocks in a row is
		// freed as a budget cut is (back to the empty machine; it stops costing DSP time and leaves the voice budget). Once
		// triggered, a voice otherwise costs its full DSP time for good, even after decaying to digital silence. 128 (-96 dBFS)
		// is below one LSB of the plugin's 16-bit output. Not the MD's behaviour (it runs every slot always): off by default,
		// so the bit-exact tests are unchanged. The next trigger starts the voice afresh.
		void setSilenceRelease(int _threshold, int _blocks) { m_releaseThreshold = _threshold; m_releaseBlocks = _blocks; }
		uint32_t silenceReleases() const { return m_releases; }	// voices freed this way so far (stats)

		// Track level (kit LEV, 0-127; smoothed by the OS's level slew $100029e), mute, output routing
		// (DSP1's per-track route word; 6 = the main outputs, the MD's default).
		void setLevel(int _track, int _level);
		void setMute(int _track, bool _mute) { m_mute[_track] = _mute; }
		void setRouting(int _track, int _route) { m_route[_track] = static_cast<uint8_t>(_route); }

		// Per-track inputs to the mixer DSP, as of the last tick.
		struct MixerInput
		{
			std::array<uint32_t, 9> fx{};	// AMD AMF EQF EQG FLTF FLTW FLTQ SRR DIST (smoothed + LFO, value << 7)
			std::array<uint32_t, 5> mix{};	// route, VOL gain, PAN << 16, REV send, DEL send
		};
		const MixerInput& mixerInput(int _track) const { return m_mixer[_track]; }

		// A track's LFO (LFO k belongs to track k; its speed, depth and mix are track parameters 21-23):
		// destination track and parameter (0-23), two shapes, type (bit 0 TRIG: restart on the track's trigger,
		// bit 1 HOLD: output only updated at a trigger; 0 = FREE).
		void setLfo(int _track, int _destTrack, int _destParam, int _shape1, int _shape2, int _type);

		// Control ticks: the MD runs its tick as fast as the ColdFire gets through it (~120 Hz measured in
		// gearmulator-md-mm, varying with load). Default: one tick every 11 blocks of 32 samples (125.3 Hz).
		void setBlocksPerTick(int _n) { m_blocksPerTick = _n; }

		// Render one 32-sample block of the 16 voices (runs a tick first when due).
		bool renderBlock(typename TVoices::Block& _out);

		// Optional stage timing (microseconds, accumulated): the OS tick + trigger updates, and the voice DSP.
		bool timingOn = false;
		double tickUs = 0, dspUs = 0, voiceLoopUs = 0, osCallsUs = 0;	// tickUs = voiceLoopUs (machine functions) + osCallsUs (smoothing, LFOs) + pokes
		void tick();
		void updateVoice(int _track);	// machine function on the voice's current array -> voice slot
		void updateMixer(int _track);	// the track's DSP1 words from its current array, level and velocity

		const uint16_t* voiceParams(int _track) const;	// the per-voice 24-value array after smoothing and LFO

	private:
		void silenceVoice(int _track);	// forces a voice to the OS's empty machine (GND--), for voice stealing

		MachineRunner& m_os;
		TVoices& m_voices;
		std::array<std::array<uint8_t, kParams>, kTracks> m_raw{};
		std::array<uint8_t, kTracks> m_machine{};
		std::array<int, kTracks> m_pendingMachine{};	// -1 = none
		std::array<bool, kTracks> m_trigger{};
		std::array<uint8_t, kTracks> m_velocity{};	// last trigger's velocity (0 until triggered, as on the MD)
		std::array<bool, kTracks> m_accent{};
		std::array<bool, kTracks> m_mute{};
		std::array<uint8_t, kTracks> m_route{};
		std::array<MixerInput, kTracks> m_mixer{};
		int m_accentAmount = 0;
		std::array<uint16_t, kParams> m_scratch{};
		int m_blocksPerTick = 11;
		int m_blockCount = 0;
		uint32_t m_tickCount = 0;
		int m_maxActive = kTracks * 2;
		std::array<bool, kTracks> m_silenceNext{};	// budget victims to silence on their next tick
		std::array<float, kTracks> m_costEma{};	// DSP instructions per block a track costs while it sounds (moving average), for group balancing
		std::vector<int> m_activeOrder;	// least- to most-recently-triggered
		int m_releaseThreshold = -1, m_releaseBlocks = 0;	// silence release, off (see setSilenceRelease)
		std::array<int, kTracks> m_quiet{};	// consecutive quiet blocks per sounding voice
		uint32_t m_releases = 0;
	};
}
