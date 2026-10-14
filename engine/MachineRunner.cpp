#include "MachineRunner.h"

#include <cstdio>
#include <algorithm>
#include <cstring>
#include <stdexcept>

#include "mc68k/mc68k.h"
#include "mc68k/cpuState.h"
#include "Musashi/m68k.h"

namespace md::engine
{
	namespace
	{
		constexpr uint32_t kMainBase = 0x200000, kMainSize = 0x100000;		// main RAM (OS loaded here)
		constexpr uint32_t kSramBase = 0x01000000, kSramSize = 0x10000;		// ColdFire internal SRAM
		constexpr uint32_t kSramImage = 0x2622f4;							// the OS copies its fast routines from here
		constexpr uint32_t kScratch = 0x0100e000;							// our args/stack area (unused by the OS routines)
		constexpr uint32_t kParams = kScratch, kOut = kScratch + 0x100;
		constexpr uint32_t kStackTop = kSramBase + kSramSize - 0x10;
		constexpr uint32_t kReturnSentinel = 0x00000100;	// unmapped, never executed: we stop when PC gets here
		constexpr uint64_t kMaxInstructions = 2'000'000;
		constexpr size_t kRecordSize = 86;
	}

	class MachineCpu final : public mc68k::Mc68k
	{
	public:
		MachineCpu() : Mc68k(M68K_CPU_TYPE_MCF5206E), m_main(kMainSize, 0), m_sram(kSramSize, 0) {}

		uint8_t read8(const uint32_t _addr) override { if(const auto* p = ptr(_addr, 1)) return *p; bad(_addr, 1, false); return 0; }
		uint16_t read16(const uint32_t _addr) override { if(const auto* p = ptr(_addr, 2)) return static_cast<uint16_t>(p[0] << 8 | p[1]); bad(_addr, 2, false); return 0; }
		uint16_t readImm16(const uint32_t _addr) override { return read16(_addr); }
		void write8(const uint32_t _addr, const uint8_t _val) override { if(auto* p = ptr(_addr, 1)) *p = _val; else bad(_addr, 1, true); }
		void write16(const uint32_t _addr, const uint16_t _val) override
		{
			if(auto* p = ptr(_addr, 2)) { p[0] = static_cast<uint8_t>(_val >> 8); p[1] = static_cast<uint8_t>(_val); } else bad(_addr, 2, true);
		}
		void bad(const uint32_t _addr, const int _n, const bool _write)
		{
			if(m_badAccesses < 8 && std::getenv("MD_BADLOG")) std::fprintf(stderr, "bad %s%d @ %08x pc=%08x\n", _write ? "write" : "read", _n * 8, _addr, m68k_get_reg(getCpuState(), M68K_REG_PC));
			++m_badAccesses;
		}
		uint32_t exec() override { return execInstruction(); }	// CPU only: no on-chip peripherals

		void setAReg(const int _i, const uint32_t _v) { m68k_set_reg(getCpuState(), static_cast<m68k_register_t>(M68K_REG_A0 + _i), _v); }
		void setSR(const uint32_t _v) { m68k_set_reg(getCpuState(), M68K_REG_SR, _v); }

		uint8_t* ptr(const uint32_t _addr, const uint32_t _n)
		{
			if(_addr >= kMainBase && _addr + _n <= kMainBase + kMainSize) return &m_main[_addr - kMainBase];
			if(_addr >= kSramBase && _addr + _n <= kSramBase + kSramSize) return &m_sram[_addr - kSramBase];
			return nullptr;
		}

		std::vector<uint8_t> m_main, m_sram;
		uint32_t m_badAccesses = 0;
	};
}

#define MC68K_CLASS md::engine::MachineCpu
#include "mc68k/musashiEntry.h"

namespace md::engine
{
	MachineRunner::MachineRunner(std::vector<uint8_t> _osImage) : m_os(std::move(_osImage))
	{
		if(m_os.size() > kMainSize) throw std::runtime_error("OS image too large");
		m_index.fill(-1);
        m_x13=m_os.size()==921308 && std::memcmp(m_os.data()+0xe0629,"GNDSW",5)==0;
		m_cpu = std::make_unique<MachineCpu>();
		std::memcpy(m_cpu->m_main.data(), m_os.data(), m_os.size());
		if(m_os.size() > kSramImage - kMainBase)
		{
			const size_t n = std::min<size_t>(m_os.size() - (kSramImage - kMainBase), kSramSize);
			std::memcpy(m_cpu->m_sram.data(), m_os.data() + (kSramImage - kMainBase), n);
		}
		// Runtime data the OS's boot writes into its internal SRAM (read from the booted full-system emulation, not in the
		// OS file). The ROM machines' coefficient function divides by the word at $0100150c ($bb8); left at 0 it takes a
		// divide-by-zero, returns nothing, and every ROM track played the same, glitchy, all-zero slot (2026-09-29).
		{
			static const uint8_t kBootSram[16] = {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 0, 0, 0x0b, 0xb8};
			std::memcpy(m_cpu->m_sram.data() + 0x1500, kBootSram, sizeof kBootSram);
		}
		m_cpu->setSR(0x2700);	// supervisor, interrupts masked
		parseMachineTable();
	}

	MachineRunner::~MachineRunner() = default;

	bool MachineRunner::computeMaster(const std::array<uint8_t, 32>& parameters, double bpm)
	{
		for (uint32_t i=0;i<32;++i) poke16(0x1000d9c+2*i, uint16_t(parameters[i])<<7);
		poke32(0x100150c, static_cast<uint32_t>(std::clamp(bpm,30.0,300.0)*24.0+0.5));
		for (uint32_t group=0;group<4;++group) {
			poke32(0x265404,group);
			m_cpu->setAReg(7,kStackTop);
			m_cpu->setSR(0x2700);
			m_cpu->setPC(0x20b4a2);
			m_cpu->m_badAccesses=0;
			uint32_t instructions=0;
			while(m_cpu->getPC()!=0x20ba4c) {
				m_cpu->exec();
				if(++instructions>10000 || m_cpu->m_badAccesses) {m_fault="master coefficient routine failed";return false;}
			}
		}
		return true;
	}

	uint8_t MachineRunner::peek8(const uint32_t _a) const { const auto* p = m_cpu->ptr(_a, 1); return p ? *p : 0; }
	uint16_t MachineRunner::peek16(const uint32_t _a) const { const auto* p = m_cpu->ptr(_a, 2); return p ? static_cast<uint16_t>(p[0] << 8 | p[1]) : 0; }
	uint32_t MachineRunner::peek32(const uint32_t _a) const { return static_cast<uint32_t>(peek16(_a)) << 16 | peek16(_a + 2); }
	void MachineRunner::poke8(const uint32_t _a, const uint8_t _v) { m_cpu->write8(_a, _v); }
	void MachineRunner::poke16(const uint32_t _a, const uint16_t _v) { m_cpu->write16(_a, _v); }
	void MachineRunner::poke32(const uint32_t _a, const uint32_t _v) { poke16(_a, static_cast<uint16_t>(_v >> 16)); poke16(_a + 2, static_cast<uint16_t>(_v)); }

	void MachineRunner::parseMachineTable()
	{
		// The descriptor table (86-byte records) is found by its shape around the TRX-B2 record's name.
		auto valid = [&](const size_t _o)
		{
			if(_o + kRecordSize > m_os.size()) return false;
			const uint32_t fn = static_cast<uint32_t>(m_os[_o]) << 24 | m_os[_o + 1] << 16 | m_os[_o + 2] << 8 | m_os[_o + 3];
			if(fn < kOsBase || fn >= kOsBase + m_os.size()) return false;
			for(size_t k = 5; k < 10; ++k)
				if(m_os[_o + k] != 0 && (m_os[_o + k] < 32 || m_os[_o + k] >= 127)) return false;
			return true;
		};
		static const char kAnchor[] = "TRXB2PTCH";
		const auto found = std::search(m_os.begin(), m_os.end(), kAnchor, kAnchor + sizeof(kAnchor) - 1);
		if(found == m_os.end() || found - m_os.begin() < 5) throw std::runtime_error("machine table not found (not an MD OS 1.63 image?)");
		const auto* hit = m_os.data() + (found - m_os.begin());
		size_t start = static_cast<size_t>(hit - m_os.data()) - 5;
		while(start >= kRecordSize && valid(start - kRecordSize)) start -= kRecordSize;
        auto addMachine = [&](size_t o)
		{
			MachineInfo m;
			m.function = static_cast<uint32_t>(m_os[o]) << 24 | m_os[o + 1] << 16 | m_os[o + 2] << 8 | m_os[o + 3];
			m.id = m_os[o + 4];
			for(size_t k = 5; k < 10 && m_os[o + k]; ++k) m.name += static_cast<char>(m_os[o + k]);
			for(size_t p = 0; p < 8; ++p)
			{
				for(size_t k = 0; k < 4 && m_os[o + 10 + 4 * p + k]; ++k) m.params[p] += static_cast<char>(m_os[o + 10 + 4 * p + k]);
				m.defaults[p] = m_os[o + 42 + p];
			}
            if(m_index[m.id]>=0)m_machines[static_cast<size_t>(m_index[m.id])]=std::move(m);
            else {m_index[m.id]=static_cast<int>(m_machines.size());m_machines.push_back(std::move(m));}
        };
        for(size_t o=start;valid(o);o+=kRecordSize)addMachine(o);
        if(m_x13)for(size_t o=0xe0420;o<=0xe067a;o+=kRecordSize) {
            if(!valid(o))throw std::runtime_error("Invalid X.13 machine descriptor");
            addMachine(o);
        }
        std::sort(m_machines.begin(),m_machines.end(),[](const auto& a,const auto& b){return a.id<b.id;});
        for(size_t i=0;i<m_machines.size();++i)m_index[m_machines[i].id]=(int)i;
	}

	const MachineInfo* MachineRunner::machine(const uint8_t _id) const
	{
		return m_index[_id] < 0 ? nullptr : &m_machines[static_cast<size_t>(m_index[_id])];
	}

	int64_t MachineRunner::call(const uint32_t _address, const std::initializer_list<uint32_t> _args)
	{
		m_fault.clear();
		auto& cpu = *m_cpu;
		uint32_t sp = kStackTop;
		// C calling convention: arguments pushed right to left, then the return address
		// initializer_list already owns a contiguous argument range for this call;
		// reverse it directly instead of allocating on the audio thread.
		for(auto it = _args.end(); it != _args.begin();) { --it; sp -= 4; poke32(sp, *it); }
		sp -= 4; poke32(sp, kReturnSentinel);
		cpu.setAReg(7, sp);
		cpu.setPC(_address);
		cpu.m_badAccesses = 0;
		uint64_t n = 0;
		while(cpu.getPC() != kReturnSentinel)
		{
			cpu.exec();
			if(++n > kMaxInstructions) { m_fault = "instruction budget exceeded"; return -1; }
		}
		m_lastInstructions = n;
		if(cpu.m_badAccesses) { m_fault = "access outside main RAM / internal SRAM"; return -1; }
		return cpu.getDReg(0);
	}

	int MachineRunner::compute(const uint8_t _machineId, const uint16_t* _params, uint32_t* _out, const int _outCapacity, const bool _trigger, int track)
	{
		const auto* m = machine(_machineId);
		if(!m) { m_fault = "unknown machine"; return -1; }
        // X.13 derives the track's tuning flag from the OUTPUT pointer: 21 long
        // words per track at $10015b4. A generic scratch pointer is invalid.
        const uint32_t outAddress=m_x13?0x10015b4+84*uint32_t(std::clamp(track,0,15)):kOut;
		for(uint32_t k = 0; k < 24; ++k)
			poke16(kParams + 2 * k, k < 8 ? _params[k] : 0);
		for(uint32_t k = 0; k < (m_x13?21u:32u); ++k)
			poke32(outAddress + 4 * k, 0);
		poke32(outAddress, _trigger ? 1 : 0);
		const auto count = call(m->function, {outAddress, kParams});
		if(count < 0) return -1;
		for(int k = 0; k < _outCapacity; ++k)
			_out[k] = peek32(outAddress + 4 * static_cast<uint32_t>(k));
		return static_cast<int>(count);
	}
}

