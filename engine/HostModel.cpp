#include "HostModel.h"

#include <chrono>
#include <cstdlib>
#include "ParallelVoiceEngine.h"

#include <algorithm>
#include <type_traits>
#include <vector>
#include <cmath>

namespace md::engine
{
	namespace
	{
		// OS 1.63 addresses (internal SRAM $1000000 = the OS's copy of image $2622f4)
		constexpr uint32_t kRaw = 0x1000ddc;		// smoothing targets: 16 tracks x 24 bytes (0-127)
		constexpr uint32_t kWork = 0x1000a4c;		// smoothed values: 16 x 24 words (value << 7, up to $3fff)
		constexpr uint32_t kVoiceParams = 0x10011cc;	// per-voice arrays (a6): 16 x 24 words, after LFO
		constexpr uint32_t kTempo = 0x100150c;		// BPM x 24
		constexpr uint32_t kSmooth = 0x10002e0, kLfoOsc = 0x1000088, kLfoApply = 0x1000332;
		constexpr uint32_t kLfo = 0x1000f8c, kLfoStride = 0x24;	// 16 LFO structs: bytes 0-4 settings, 5 = trigger flag
		constexpr uint32_t kLfoWave = 0x204c94;		// waveform/restart for one LFO (in the tick routine's trigger path)
		constexpr uint32_t kLfoApplyOne = 0x10001e8;	// apply one LFO to its destination in the voice arrays
		constexpr uint32_t kTrackStride = 0x30;
		constexpr uint32_t kLevel = 0x1000d7c;		// smoothed track levels: 16 words (value << 7); then 32 master FX
		constexpr uint32_t kLevelTarget = 0x1000f5c;	// their targets: 48 bytes (0-127)
		constexpr uint32_t kLevelSmooth = 0x100029e;	// new = (3 old + target << 7) >> 2, all 48

		bool isAudioMachine(const uint8_t _id) { return !(_id >= 0x60 && _id <= 0x7b); }	// MID/CTR: no audio
	}

	template<class TVoices>
	HostModel<TVoices>::HostModel(MachineRunner& _os, TVoices& _voices) : m_os(_os), m_voices(_voices)
	{
		setTempo(125.0);
		m_pendingMachine.fill(-1);
		m_route.fill(6);
		for(int t = 0; t < kTracks; ++t)
		{
			m_os.poke8(kLevelTarget + static_cast<uint32_t>(t), 100);
			m_os.poke16(kLevel + 2 * static_cast<uint32_t>(t), 100 << 7);
			m_machine[t] = 0;
			setMachine(t, 0);
			setLfo(t, t, 0, 0, 0, 0);
			// The firmware RND routine ($204f32) adds these two state words.
			// This partial-system host never loads the hardware's runtime kit state;
			// a zero pair stays zero forever. Seed once per engine, with different
			// deterministic nonzero values per track; the firmware advances them itself.
			const uint32_t lfo = kLfo + kLfoStride * static_cast<uint32_t>(t);
			m_os.poke32(lfo + 0x18, 0x13579bdu + 0x1021u * static_cast<uint32_t>(t));
			m_os.poke32(lfo + 0x1c, 0x2468aceu + 0x1f3u * static_cast<uint32_t>(t));
		}
	}

	template<class TVoices>
	void HostModel<TVoices>::setMachine(const int _track, const uint8_t _machineId)
	{
		const auto* m = m_os.machine(_machineId);
		if(!m) return;
		m_pendingMachine[_track] = _machineId;
		for(int p = 0; p < 8; ++p)
			m_raw[_track][p] = m->defaults[p];
	}

	template<class TVoices>
	void HostModel<TVoices>::setParam(const int _track, const int _param, const int _value)
	{
		m_raw[_track][_param] = static_cast<uint8_t>(std::clamp(_value, 0, 127));
	}

	template<class TVoices>
	void HostModel<TVoices>::setTempo(const double _bpm)
	{
		m_os.poke32(kTempo, static_cast<uint32_t>(std::lround(std::clamp(_bpm, 30.0, 300.0) * 24.0)));
	}

    template<class TVoices>
    void HostModel<TVoices>::setParamImmediately(int track,int param,int value) {
        setParam(track,param,value);
        const auto v=m_raw[track][param];
        m_os.poke8(kRaw+24*uint32_t(track)+uint32_t(param),v);
        m_os.poke16(kWork+kTrackStride*uint32_t(track)+2*uint32_t(param),uint16_t(v)<<7);
        m_os.poke16(kVoiceParams+kTrackStride*uint32_t(track)+2*uint32_t(param),uint16_t(v)<<7);
    }

	template<class TVoices>
	void HostModel<TVoices>::setLfo(const int _track, const int _destTrack, const int _destParam, const int _shape1, const int _shape2, const int _type)
	{
		const uint32_t base = kLfo + kLfoStride * static_cast<uint32_t>(_track);
		m_os.poke8(base + 0, static_cast<uint8_t>(std::clamp(_destTrack, 0, kTracks - 1)));
		m_os.poke8(base + 1, static_cast<uint8_t>(std::clamp(_destParam, 0, kParams - 1)));
		m_os.poke8(base + 2, static_cast<uint8_t>(std::clamp(_shape1, 0, 7)));
		m_os.poke8(base + 3, static_cast<uint8_t>(std::clamp(_shape2, 0, 7)));
		m_os.poke8(base + 4, static_cast<uint8_t>(std::clamp(_type, 0, 3)));
	}

	template<class TVoices>
	void HostModel<TVoices>::setLevel(const int _track, const int _level)
	{
		m_os.poke8(kLevelTarget + static_cast<uint32_t>(_track), static_cast<uint8_t>(std::clamp(_level, 0, 127)));
	}

	template<class TVoices>
	void HostModel<TVoices>::silenceVoice(const int _track)
	{
		uint32_t out[32];
		const uint16_t params[8] = {};	// GND-- (OS machine id 0) takes none
		const int n = m_os.compute(0, params, out, 32);
		if(n > 0)
		{
			out[0] = 1;	// trigger code for machine id 0: the voice goes idle (harness skip check) until retriggered
			m_voices.setSlot(_track, out, std::min(n, TVoices::kSlotWords));
		}
	}

	template<class TVoices>
	void HostModel<TVoices>::trigger(const int _track, const int _velocity, const bool _accent)
	{
		bool wasSounding = false;
		// Voice budget: the tracks that have sounded (and so keep costing DSP time until silenced) are kept in trigger
		// order, and a trigger that would take their total cost past the budget cuts the least-recently-triggered ones.
		{
			auto it = std::find(m_activeOrder.begin(), m_activeOrder.end(), _track);
			wasSounding = it != m_activeOrder.end();
			if(wasSounding)
				m_activeOrder.erase(it);
			const int self = voiceCost(m_pendingMachine[_track] >= 0 ? static_cast<uint8_t>(m_pendingMachine[_track]) : m_machine[_track]);
			auto total = [&] { int c = self; for(const int t : m_activeOrder) c += voiceCost(m_machine[t]); return c; };
			while(!m_activeOrder.empty() && total() > m_maxActive)
			{
				const int victim = m_activeOrder.front();
				m_activeOrder.erase(m_activeOrder.begin());
				m_trigger[victim] = false;	// cut before it ever sounded (triggered earlier in this same block)
				m_silenceNext[victim] = true;	// written by the victim's next updateVoice (a write here would be overwritten by it)
			}
			m_activeOrder.push_back(_track);
			m_quiet[static_cast<size_t>(_track)] = 0;
		}
		if constexpr(std::is_same_v<TVoices, ParallelVoiceEngine>)
		{
			// Balance the voice groups: a track that was not sounding takes the group with the least sounding cost; one
			// that was keeps its group (its DSP voice state lives there). The old group's copy is silenced.
			const int groups = m_voices.groupCount();
			if(groups > 1 && !wasSounding)
			{
				std::vector<int> cost(static_cast<size_t>(groups), 0);
				for(const int t : m_activeOrder)
					if(t != _track)
						cost[static_cast<size_t>(m_voices.groupOf(t))] += static_cast<int>(m_costEma[static_cast<size_t>(t)] > 0 ? m_costEma[static_cast<size_t>(t)] : 3000.f * static_cast<float>(voiceCost(m_machine[t])));
				int best = m_voices.groupOf(_track);
				for(int g = 0; g < groups; ++g)
					if(cost[static_cast<size_t>(g)] < cost[static_cast<size_t>(best)])
						best = g;
				if(best != m_voices.groupOf(_track))
				{
					uint32_t out[32];
					const uint16_t params[8] = {};
					const int n = m_os.compute(0, params, out, 32);
					if(n > 0)
					{
						out[0] = 1;
						m_voices.moveVoice(_track, best, out, std::min(n, TVoices::kSlotWords));
					}
				}
			}
		}
		m_trigger[_track] = true;
		m_velocity[_track] = static_cast<uint8_t>(std::clamp(_velocity, 1, 127));
		m_accent[_track] = _accent;
		// The OS's track trigger ($20cdf0) flags the track's own LFO; the tick's trigger path acts on it.
		m_os.poke8(kLfo + kLfoStride * static_cast<uint32_t>(_track) + 5, 1);
		// The tick routine's trigger path: a pending machine is applied now, loading the kit values straight into
		// the smoothing targets, the smoothed array and the voice array (no glide).
		if(m_pendingMachine[_track] >= 0)
		{
			m_machine[_track] = static_cast<uint8_t>(m_pendingMachine[_track]);
			m_pendingMachine[_track] = -1;
			for(int p = 0; p < kParams; ++p)
			{
				const auto v = static_cast<uint16_t>(m_raw[_track][p] << 7);
				m_os.poke8(kRaw + 24 * static_cast<uint32_t>(_track) + static_cast<uint32_t>(p), m_raw[_track][p]);
				m_os.poke16(kWork + kTrackStride * static_cast<uint32_t>(_track) + 2 * static_cast<uint32_t>(p), v);
				m_os.poke16(kVoiceParams + kTrackStride * static_cast<uint32_t>(_track) + 2 * static_cast<uint32_t>(p), v);
			}
		}
	}

	template<class TVoices>
	const uint16_t* HostModel<TVoices>::voiceParams(const int _track) const
	{
		auto* self = const_cast<HostModel*>(this);
		for(int p = 0; p < kParams; ++p)
			self->m_scratch[p] = m_os.peek16(kVoiceParams + kTrackStride * static_cast<uint32_t>(_track) + 2 * static_cast<uint32_t>(p));
		return m_scratch.data();
	}

	template<class TVoices>
	void HostModel<TVoices>::updateVoice(const int _track)
	{
		// As the tick routine's voice loop: the machine function on the voice's current array; word 0 = trigger
		// flag, which the slot pump turns into machine id + 1. MID/CTR machines have no audio and are not sent.
		// The tick routine's trigger path, before the machine function: the LFO's restart/hold for this
		// trigger, then this track's LFO applied to its destination straight away.
		if(m_trigger[_track])
		{
			m_os.call(kLfoWave, {static_cast<uint32_t>(_track)});
			m_os.call(kLfoApplyOne, {static_cast<uint32_t>(_track)});
		}
		const auto id = m_machine[_track];
		if(m_silenceNext[_track] && !m_trigger[_track])
		{
			silenceVoice(_track);	// budget cut: the voice goes back to machine 0 and stops costing DSP time
			m_silenceNext[_track] = false;
		}
		else if(isAudioMachine(id))
		{
			m_silenceNext[_track] = false;
			uint32_t out[32];
			const int n = m_os.compute(id, voiceParams(_track), out, 32, m_trigger[_track], _track);
			if(n >= 0)	// n == 0 (TRX CP/MA/CL: no parameter words) still carries the trigger word
			{
				out[0] = m_trigger[_track] ? static_cast<uint32_t>(id) + 1 : 0;
				m_voices.setSlot(_track, out, std::clamp(n, 1, static_cast<int>(TVoices::kSlotWords)));
			}
		}
		updateMixer(_track);
		m_trigger[_track] = false;
	}

	template<class TVoices>
	void HostModel<TVoices>::updateMixer(const int _track)
	{
		// The tick routine's DSP1 block ($20b1e2-$20b302). MID/CTR machines send nothing.
		auto& m = m_mixer[_track];
		if(!isAudioMachine(m_machine[_track]))
			return;
		const uint16_t* a6 = voiceParams(_track);
		for(int k = 0; k < 9; ++k)
			m.fx[k] = a6[8 + k];	// sent unchanged by $1000702
		m.mix[0] = m_route[_track];
		if(m_mute[_track])
		{
			m.mix[1] = m.mix[2] = m.mix[3] = m.mix[4] = 0;
			return;
		}
		// VOL gain = ((LEV^2 >> 8) x VEL >> 17) x (VOL^2 >> 17); VEL = the trigger's velocity, or 128 + 2 x accent
		const int32_t lev = m_os.peek16(kLevel + 2 * static_cast<uint32_t>(_track));
		const int32_t vel = m_accent[_track] ? 128 + 2 * m_accentAmount : m_velocity[_track];
		const auto sq = [](const uint16_t _v) { return static_cast<uint32_t>(_v) * _v; };
		const int32_t gain = static_cast<int32_t>((((lev * lev) >> 8) * vel) >> 17) * static_cast<int32_t>(sq(a6[17]) >> 17);
		m.mix[1] = static_cast<uint32_t>(gain) & 0xffffff;
		m.mix[2] = (static_cast<uint32_t>(a6[18]) << 9) & 0x1fffe00;
		m.mix[3] = sq(a6[20]) >> 5;	// REV
		m.mix[4] = sq(a6[19]) >> 5;	// DEL
	}

	template<class TVoices>
	void HostModel<TVoices>::tick()
	{
		for(int t = 0; t < kTracks; ++t)
			for(int p = 0; p < kParams; ++p)
				m_os.poke8(kRaw + 24 * static_cast<uint32_t>(t) + static_cast<uint32_t>(p), m_raw[t][p]);

		const auto tv0 = timingOn ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
		for(int t = 0; t < kTracks; ++t)
			updateVoice(t);
		const auto tv1 = timingOn ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};

		// After the voice loop the OS smooths the parameters, runs the LFOs and builds the next tick's arrays.
		m_os.call(kSmooth, {});
		m_os.call(kLfoOsc, {});
		m_os.call(kLfoApply, {});
		m_os.call(kLevelSmooth, {});
		if(timingOn)
		{
			const auto tv2 = std::chrono::steady_clock::now();
			voiceLoopUs += std::chrono::duration<double, std::micro>(tv1 - tv0).count();
			osCallsUs += std::chrono::duration<double, std::micro>(tv2 - tv1).count();
		}
		++m_tickCount;
	}

	template<class TVoices>
	bool HostModel<TVoices>::renderBlock(typename TVoices::Block& _out)
	{
		// Ticks on a fixed block schedule; a trigger between ticks updates just its voice, so it starts on this
		// block rather than waiting for the next tick.
		const auto t0 = timingOn ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
		if(m_blockCount++ % m_blocksPerTick == 0)
			tick();
		else
			for(int t = 0; t < kTracks; ++t)
				if(m_trigger[t])
					updateVoice(t);
		const auto t1 = timingOn ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
		const bool ok = m_voices.renderBlock(_out);
		if(timingOn)
		{
			const auto t2 = std::chrono::steady_clock::now();
			tickUs += std::chrono::duration<double, std::micro>(t1 - t0).count();
			dspUs += std::chrono::duration<double, std::micro>(t2 - t1).count();
		}
		if(ok)
			for(int t = 0; t < kTracks; ++t)	// what each sounding track costs the DSP (idle voices cost a few dozen instructions)
			{
				const auto x = static_cast<float>(m_voices.voiceInstructions(t));
				if(x > 500.f)
					m_costEma[static_cast<size_t>(t)] = m_costEma[static_cast<size_t>(t)] > 0 ? 0.95f * m_costEma[static_cast<size_t>(t)] + 0.05f * x : x;
			}
		if(ok && m_releaseThreshold >= 0)
			for(size_t i = 0; i < m_activeOrder.size();)
			{
				const int t = m_activeOrder[i];
				const auto& v = _out[static_cast<size_t>(t)];
				const bool quiet = !m_trigger[t] && std::all_of(v.begin(), v.end(), [&](const int32_t s) { return s <= m_releaseThreshold && s >= -m_releaseThreshold; });
				auto& q = m_quiet[static_cast<size_t>(t)];
				q = quiet ? q + 1 : 0;
				if(q >= m_releaseBlocks)
				{
					q = 0;
					m_activeOrder.erase(m_activeOrder.begin() + static_cast<std::ptrdiff_t>(i));
					m_silenceNext[t] = true;	// applied by its next updateVoice, as a budget cut
					++m_releases;
					continue;
				}
				++i;
			}
		return ok;
	}

	template class HostModel<VoiceEngine>;
	template class HostModel<ParallelVoiceEngine>;
}
