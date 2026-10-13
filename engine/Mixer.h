// The Machinedrum's mix (mixer DSP, P:$294-$343 and the routine at $9de it generates code for) as native
// C++, bit-exact with the DSP: pan law, VOL gain, reverb and delay sends for the tracks routed to the main
// outputs, and the individual outputs for the others. Master effects are not part of this (Machinedrum FX).
#pragma once
#include <array>
#include <cstdint>

#include "TrackFx.h"

namespace md::engine
{
	class Mixer
	{
	public:
		static constexpr int kTracks = 16;
		static constexpr int kBlock = 32;
		static constexpr int kRouteMain = 6;

		using Stereo = std::array<std::array<int32_t, 2>, kBlock>;	// [frame][L, R], 24-bit
		struct Output
		{
			Stereo main{};		// dry main mix (the master effects' input, X:$180)
			Stereo rev{};		// reverb send (X:$1c0)
			Stereo del{};		// delay send (X:$600)
			// The DSP's 6-channel output frames before the master section adds the main mix: tracks routed
			// elsewhere than the main outs land here (route 0 -> channel 2, 1 -> 5, 2 -> 1, 3 -> 4, 4 -> 0, 5 -> 3).
			std::array<std::array<int32_t, 6>, kBlock> frame{};
		};

		explicit Mixer(const TrackFx::Tables& _tables) : m_t(_tables) {}

		// _tracks: each track's 32 processed samples (TrackFx output); _mix: each track's 5 mix words
		// (route, VOL gain, PAN, REV, DEL; HostModel::MixerInput::mix).
		// _dryMute: bit t set = track t stays out of the dry main mix but still feeds the reverb and delay sends (a track read
		// by a Machinemodule Tap); 0 = the DSP's own behaviour, bit-exact.
		void process(const int32_t* const* _tracks, const std::array<uint32_t, 5>* _mix, Output& _out, uint32_t _dryMute = 0) const;

		static int frameChannel(int _route);

		// One track on its own (no pan, no sends): the individual-output formula, sample x VOL << 4 limited (24-bit).
		// _volWord: the track's mix word 1 (HostModel::MixerInput::mix[1]).
		static int32_t solo(int32_t _sample, uint32_t _volWord);

	private:
		const TrackFx::Tables& m_t;
	};
}
