// Reference for the native mixer-DSP code: runs the Machinedrum's mixer DSP (DSP1) program from the user's OS
// file in the dsp56300 emulator and calls its per-track effect function (P:$a4-$25d: AMD, EQ, filter, SRR,
// distortion) directly, one track and one 32-sample block at a time. The PC MasterFx also uses its master runner.
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../mdfw/Firmware.h"

namespace dsp56k { class DSP; class Memory; class Peripherals56303; class PeripheralsNop; class DefaultMemoryValidator; }

namespace md::mixref
{
	class MixerRef
	{
	public:
		static constexpr int kTracks = 16;
		static constexpr int kBlock = 32;
		using Block = std::array<int32_t, kBlock>;	// 24-bit signed

		explicit MixerRef(const fw::Firmware& _fw, bool useJit = false);
		~MixerRef();

		// The track's 9 effect words (AMD AMF EQF EQG FLTF FLTW FLTQ SRR DIST, value << 7), Y:$200+$40k.
		void setFx(int _track, const uint32_t* _fx);

		// One block through the track's effect chain (its state persists in DSP memory between calls).
		// _stopAt: stop at another P address instead of the function's end, to inspect an intermediate stage.
		bool runTrack(int _track, const Block& _in, Block& _out, uint32_t _stopAt = 0x25e);

		// The mix (P:$294-$341) on 16 processed track blocks and their 5 mix words (route, VOL, PAN, REV, DEL).
		// Results in X memory: main L/R $180, reverb send $1c0, delay send $600 (interleaved pairs), output
		// frames $400 (6 channels).
		bool runMix(const Block* _tracks, const uint32_t (*_mix)[5]);

		// The master effects (P:$342-$970: rhythm echo, gate box, EQ, dynamix), run on what runMix() left in X memory,
		// their parameters at Y:$150-$18c. Stops at the output stage ($971).
		bool runMaster();
		void resetMaster();

		uint32_t peekX(uint32_t _a) const;
		uint32_t peekY(uint32_t _a) const;
		void pokeX(uint32_t _a, uint32_t _v);
		void pokeY(uint32_t _a, uint32_t _v);
		uint64_t instructionsLastRun() const { return m_lastInstructions; }
		const std::string& fault() const { return m_fault; }
		dsp56k::DSP& dsp() { return *m_dsp; }

	private:
		bool runUntil(uint32_t _pc, uint64_t _max);

		std::unique_ptr<dsp56k::DefaultMemoryValidator> m_validator;
		std::unique_ptr<dsp56k::Memory> m_mem;
		std::unique_ptr<dsp56k::Peripherals56303> m_periphX;
		std::unique_ptr<dsp56k::PeripheralsNop> m_periphY;
		std::unique_ptr<dsp56k::DSP> m_dsp;
		uint64_t m_lastInstructions = 0;
		std::string m_fault;
		std::vector<uint32_t> m_initialX, m_initialY, m_initialExternal;
		bool m_useJit = false;
	};
}
