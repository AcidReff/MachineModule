// Runs the Machinedrum OS's own per-machine coefficient functions (compiled C in the ColdFire OS image,
// section 0 of the user's .syx) in a 68k emulator. Each is count = fn(uint32_t* out, const uint16_t* params):
// the 8 SYN parameters in, out[1..12] = the voice DSP's slot words. The machine descriptor table gives each
// machine's function. See docs/PROTOCOL.md, "Host model: the ColdFire side".
#pragma once
#include <array>
#include <algorithm>
#include <initializer_list>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace md::engine
{
	class MachineCpu;

	struct MachineInfo
	{
		uint8_t id = 0;
		std::string name;
		std::array<std::string, 8> params;
		std::array<uint8_t, 8> defaults{};
		uint32_t function = 0;
	};

	class MachineRunner
	{
	public:
		static constexpr uint32_t kOsBase = 0x200000;

		explicit MachineRunner(std::vector<uint8_t> _osImage);	// decompressed section 0
		~MachineRunner();

		const std::vector<MachineInfo>& machines() const { return m_machines; }
		const MachineInfo* machine(uint8_t _id) const;
        void setTonal(int track,bool enabled) {if(m_x13)poke8(0x2e1ab4+uint32_t(std::clamp(track,0,15)),enabled?1:0);}

		// Call a machine's coefficient function. params: at least the 8 SYN values as 16-bit (value << 7, up to
		// $3fff); out receives the words the OS writes (out[0] is left 0: it is the caller's trigger word).
		// Returns the word count the function reports, or -1 on a fault.
		// _trigger: the OS sets out[0] (the trigger flag) before calling a machine function, and some of them (TRX XT/CP/MA/CL/XC)
		// start with `tst.l (out)` and return no words at all when it is 0: they only compute on a trigger tick.
        int compute(uint8_t _machineId, const uint16_t* _params, uint32_t* _out, int _outCapacity, bool _trigger = false, int track = 0);

		// Call any OS routine (C calling convention, up to 4 32-bit arguments). Returns d0, or -1 on a fault.
		int64_t call(uint32_t _address, std::initializer_list<uint32_t> _args);
		// OS 1.63 master coefficient section, stopped before the sequencer resumes.
		bool computeMaster(const std::array<uint8_t, 32>& parameters, double bpm);

		// OS memory, as the ColdFire sees it: main RAM $200000-$2fffff (initialised from the OS image, so its
		// initialised data is right), internal SRAM $1000000-$100ffff (the OS's copy of its fast routines from
		// image $2622f4, and their data). Big-endian.
		uint8_t peek8(uint32_t _addr) const;
		uint16_t peek16(uint32_t _addr) const;
		uint32_t peek32(uint32_t _addr) const;
		void poke8(uint32_t _addr, uint8_t _v);
		void poke16(uint32_t _addr, uint16_t _v);
		void poke32(uint32_t _addr, uint32_t _v);

		uint64_t lastInstructions() const { return m_lastInstructions; }
		const std::string& faultReason() const { return m_fault; }

	private:
		void parseMachineTable();

		std::vector<uint8_t> m_os;
		std::unique_ptr<MachineCpu> m_cpu;
		std::vector<MachineInfo> m_machines;
		std::array<int, 256> m_index{};
		uint64_t m_lastInstructions = 0;
		std::string m_fault;
        bool m_x13 = false;
	};
}
