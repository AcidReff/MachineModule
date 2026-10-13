#include "Mixer.h"

#include <algorithm>
#include <cstdlib>

#include "Dsp56.h"

namespace md::engine
{
	using namespace d56;

	int32_t Mixer::solo(const int32_t _sample, const uint32_t _volWord)
	{
		return lim(asl(mpy(_sample, sx24(_volWord)), 4));
	}

	int Mixer::frameChannel(const int _route)
	{
		// P:$2c2-$2cb: base + 6, minus 4 when bit 0 is clear, minus route / 2 (a fraction borrows)
		const Acc b = acc(6 - ((_route & 1) ? 0 : 4));
		return a1(sub(b, asr(acc(_route), 1)));
	}

	void Mixer::process(const int32_t* const* _tracks, const std::array<uint32_t, 5>* _mix, Output& _out, const uint32_t _dryMute) const
	{
		for(auto& f : _out.frame) f.fill(0);

		// Per main-routed track, six gains (P:$2de-$2f9): L R, REV L R, DEL L R.
		std::array<std::array<int32_t, 6>, kTracks> g{};
		std::array<const int32_t*, kTracks> mainIn{};
		int n = 0;
		static const bool noSkip = std::getenv("MD_MIX_NOSKIP") != nullptr;	// verification: run every track
		for(int t = 0; t < kTracks; ++t)
		{
			// A track with no signal adds exactly nothing to any sum (0 x gain = 0, x + 0 = x, and lim() of an
			// already-limited output word is that word), so it is skipped: most of a kit is silent most of the time.
			if(!noSkip && std::all_of(_tracks[t], _tracks[t] + kBlock, [](const int32_t s) { return s == 0; }))
				continue;
			const auto& w = _mix[t];
			const int route = sx24(w[0]);
			const int32_t vol = sx24(w[1]);
			if(route != kRouteMain)
			{
				// P:$2c2-$2d9: into the output frame, sample x VOL << 4 added to what is there, limited
				const int ch = frameChannel(route);
				for(int i = 0; i < kBlock; ++i)
				{
					auto& o = _out.frame[i][ch];
					o = lim(add(asl(mpy(_tracks[t][i], vol), 4), acc(o)));
				}
				continue;
			}
			Acc a = acc(sx24(w[2]));
			if(a > acc(0x7ecccd)) a = acc(0x7fffff);
			const auto idx = static_cast<uint32_t>(lim(asr(a, 10)));
			const Acc gainR = mpy(m_t[0x148000 + idx], vol);	// sin
			const Acc gainL = mpy(m_t[0x14a000 + idx], vol);	// cos
			const int32_t l = lim(gainL), r = lim(gainR);
			const int32_t rev = sx24(w[3]), del = sx24(w[4]);
			const bool mute = (_dryMute >> t) & 1;
			g[n] = {mute ? 0 : l, mute ? 0 : r, lim(mpy(rev, l)), lim(mpy(rev, r)), lim(mpy(del, l)), lim(mpy(del, r))};
			mainIn[n++] = _tracks[t];
		}

		// P:$9de, once per pair: the sum over the main-routed tracks, << 3, limited
		Stereo* outs[3] = {&_out.main, &_out.rev, &_out.del};
		for(int p = 0; p < 3; ++p)
			for(int i = 0; i < kBlock; ++i)
			{
				Acc a = 0, b = 0;
				for(int k = 0; k < n; ++k)
				{
					a = add(a, mpy(g[k][2 * p], mainIn[k][i]));
					b = add(b, mpy(g[k][2 * p + 1], mainIn[k][i]));
				}
				(*outs[p])[i] = {lim(asl(a, 3)), lim(asl(b, 3))};
			}
	}
}
