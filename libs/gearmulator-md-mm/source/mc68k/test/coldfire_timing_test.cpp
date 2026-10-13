#include "cpuState.h"
#include "mc68k.h"
#include "musashiEntry.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <initializer_list>

namespace
{
	class TestCpu final : public mc68k::Mc68k
	{
	public:
		explicit TestCpu(unsigned type) : Mc68k(type) {}
		using Mc68k::execInstruction;
		uint8_t read8(uint32_t a) override { return memory.at(a); }
		uint16_t read16(uint32_t a) override { return (read8(a) << 8) | read8(a + 1); }
		uint16_t readImm16(uint32_t a) override { return read16(a); }
		void write8(uint32_t a, uint8_t v) override { memory.at(a) = v; }
		void write16(uint32_t a, uint16_t v) override { write8(a, v >> 8); write8(a + 1, v); }
		std::array<uint8_t, 1024> memory{};
	};

	int failures = 0;
	void check(const char* label, uint32_t got, uint32_t expected)
	{
		if(got == expected) return;
		std::fprintf(stderr, "%s: got %u, expected %u\n", label, got, expected);
		++failures;
	}

	void instruction(const char* name, std::initializer_list<uint16_t> words,
		uint32_t cfCycles, uint32_t legacyCycles, uint16_t sr = 0x2700,
		uint32_t expectedPc = 0)
	{
		for(const auto type : {M68K_CPU_TYPE_MCF5206E, M68K_CPU_TYPE_68020})
		{
			TestCpu cpu(type);
			uint32_t address = 0x20;
			for(const auto word : words) { cpu.write16(address, word); address += 2; }
			cpu.setPC(0x20);
			m68k_set_reg(cpu.getCpuState(), M68K_REG_SR, sr);
			m68k_set_reg(cpu.getCpuState(), M68K_REG_SP, 0x3c0);
			m68k_set_reg(cpu.getCpuState(), M68K_REG_A0, 0x100);
			m68k_set_reg(cpu.getCpuState(), M68K_REG_D0, 100);
			m68k_set_reg(cpu.getCpuState(), M68K_REG_D1, 5);
			const auto cycles = cpu.execInstruction();
			check(name, cycles, type == M68K_CPU_TYPE_MCF5206E ? cfCycles : legacyCycles);
			if(expectedPc) check("branch PC", cpu.getPC(), expectedPc);
		}
	}

	void conditionalBranchMatrix()
	{
		// CFPRM Bcc condition table and MCF5206EUM table 3-11. Sweep X too:
		// it must neither affect the decision nor be changed by a branch.
		// https://www.nxp.com/docs/en/reference-manual/CFPRM.pdf
		// https://www.nxp.jp/docs/en/data-sheet/MCF5206EUM.pdf
		unsigned cases = 0;
		for(unsigned type : {M68K_CPU_TYPE_MCF5206E, M68K_CPU_TYPE_68020})
		{
			TestCpu cpu(type);
			for(unsigned flags = 0; flags < 32; ++flags)
			{
				const bool c = flags & 1, v = flags & 2, z = flags & 4, n = flags & 8;
				const std::array<bool, 14> conditions{
					!c && !z, c || z, !c, c, !z, z, !v, v,
					!n, n, n == v, n != v, !z && n == v, z || n != v};
				for(unsigned condition = 2; condition < 16; ++condition)
					for(bool word : {false, true})
						for(int displacement : {-16, -2, 2, 16})
						{
							const auto opcode = static_cast<uint16_t>(0x6000 | (condition << 8)
								| (word ? 0 : static_cast<uint8_t>(displacement)));
							cpu.write16(0x20, opcode);
							if(word) cpu.write16(0x22, static_cast<uint16_t>(displacement));
							cpu.setPC(0x20);
							m68k_set_reg(cpu.getCpuState(), M68K_REG_SR, 0x2700 | flags);
							m68k_set_reg(cpu.getCpuState(), M68K_REG_D0, 0x12345678);
							const bool taken = conditions[condition - 2];
							const unsigned expectedCycles = type == M68K_CPU_TYPE_68020
								? (taken ? 6 : word ? 6 : 4)
								: displacement < 0 ? (taken ? 2 : 3) : (taken ? 3 : 1);
							const unsigned expectedPc = taken ? 0x22 + displacement : word ? 0x24 : 0x22;
							const auto before = failures;
							check("Bcc matrix cycles", cpu.execInstruction(), expectedCycles);
							check("Bcc matrix PC", cpu.getPC(), expectedPc);
							check("Bcc matrix SR", m68k_get_reg(cpu.getCpuState(), M68K_REG_SR), 0x2700 | flags);
							check("Bcc matrix D0", m68k_get_reg(cpu.getCpuState(), M68K_REG_D0), 0x12345678);
							if(failures != before)
							{
								std::fprintf(stderr, "type=%u opcode=%04x CCR=%02x displacement=%d\n",
									type, opcode, flags, displacement);
								return;
							}
							++cases;
						}
			}
		}
		std::printf("Bcc matrix: %u executions, all conditions/CCR values, both widths/directions/CPU models passed\n", cases);
	}

	void shiftMatrix()
	{
		// MCF5206EUM table 3-8: register-count shifts take one core cycle.
		// CFPRM ASL/ASR: ColdFire always clears V; 68020 ASL detects overflow.
		// A bit-at-a-time oracle checks both encodings and counts beyond 32 bits.
		unsigned cases = 0;
		for(unsigned type : {M68K_CPU_TYPE_MCF5206E, M68K_CPU_TYPE_68020})
		{
			TestCpu cpu(type);
			for(bool immediate : {false, true})
				for(unsigned op : {0xe2a0, 0xe3a0, 0xe2a8, 0xe3a8}) // ASR, ASL, LSR, LSL D1,D0
					for(unsigned count = immediate ? 1 : 0; count < (immediate ? 9 : 66); ++count)
						for(uint32_t src : {0u, 1u, 0x40000000u, 0x80000000u, 0x80000001u, 0xffffffffu})
							for(bool initialX : {false, true})
							{
								const unsigned shift = count & 63;
								const bool left = op & 0x100;
								uint32_t result = src;
								bool carry = false, extend = initialX, overflow = false;
								for(unsigned bit = 0; bit < shift; ++bit)
								{
									const bool sign = result & 0x80000000u;
									carry = left ? sign : (result & 1);
									result = left ? result << 1 : result >> 1;
									if(op == 0xe2a0 && sign) result |= 0x80000000u;
									if(type == M68K_CPU_TYPE_68020 && op == 0xe3a0
										&& sign != bool(result & 0x80000000u)) overflow = true;
									extend = carry;
								}
								const unsigned expectedSr = 0x2700 | (extend ? 16 : 0)
									| (result & 0x80000000u ? 8 : 0) | (result == 0 ? 4 : 0)
									| (overflow ? 2 : 0) | (carry ? 1 : 0);
								const auto opcode = immediate ? (op & ~0xe20) | ((count & 7) << 9) : op;
								cpu.write16(0x20, opcode);
								cpu.setPC(0x20);
								m68k_set_reg(cpu.getCpuState(), M68K_REG_SR, 0x270f | (initialX ? 16 : 0));
								m68k_set_reg(cpu.getCpuState(), M68K_REG_D0, src);
								m68k_set_reg(cpu.getCpuState(), M68K_REG_D1, count);
								const auto before = failures;
								check("shift cycles", cpu.execInstruction(),
									type == M68K_CPU_TYPE_MCF5206E ? 1 : immediate
										? (op == 0xe3a0 ? 8 : op == 0xe2a0 ? 6 : 4)
										: shift + (op == 0xe3a0 ? 8 : 6));
								check("shift result", m68k_get_reg(cpu.getCpuState(), M68K_REG_D0), result);
								check("shift CCR", m68k_get_reg(cpu.getCpuState(), M68K_REG_SR), expectedSr);
								check("shift count", m68k_get_reg(cpu.getCpuState(), M68K_REG_D1), count);
								check("shift PC", cpu.getPC(), 0x22);
								if(before != failures)
								{
									std::fprintf(stderr, "type=%u shift opcode=%04x count=%u src=%08x X=%u\n",
										type, opcode, count, src, initialX);
									return;
								}
								++cases;
							}
		}
		std::printf("Shift matrix: %u executions, register/immediate counts/results/CCR, both CPU models passed\n", cases);
	}
}

int main()
{
	// MCF5206EUM tables 3-5..3-11; legacy expectations guard other products.
	instruction("MOVEQ", {0x702a}, 1, 2);
	instruction("MOVE.B load", {0x1010}, 3, 6);
	instruction("MOVE.B store", {0x1080}, 1, 4);
	instruction("MOVE.W load", {0x3010}, 3, 6);
	instruction("MOVE.L indexed load", {0x2030, 0x1800}, 3, 9);
	instruction("LEA indexed", {0x41f0, 0x1800}, 2, 9);
	instruction("NOP", {0x4e71}, 3, 2);
	instruction("TRAP", {0x4e40}, 15, 20);
	instruction("MOVE.L absolute", {0x2039, 0x0000, 0x0100}, 2, 6);
	instruction("MOVE.W register", {0x3200}, 1, 2);
	instruction("ADD.L register", {0xd081}, 1, 2);
	instruction("MULS.L register", {0x4c01, 0x0800}, 18, 43);
	instruction("DIVS.L register", {0x4c41, 0x0800}, 35, 84);
	instruction("LSL.L immediate", {0xe588}, 1, 4);
	instruction("MOVEM.L save two", {0x48d0, 0x0003}, 3, 16);
	instruction("MOVEM.L restore two", {0x4cd0, 0x0003}, 3, 20);
	instruction("MOVE supervisor SR", {0x46fc, 0x2700}, 1, 10);
	instruction("MOVE user SR", {0x46fc, 0x0000}, 7, 10);
	instruction("BRA", {0x6006}, 2, 10, 0x2700, 0x28);
	instruction("BSR.W", {0x6100, 0x0006}, 3, 7, 0x2700, 0x28);
	instruction("BNE forward taken", {0x6606}, 3, 6, 0x2700, 0x28);
	instruction("BNE forward untaken", {0x6606}, 1, 4, 0x2704, 0x22);
	instruction("BNE backward taken", {0x66fc}, 2, 6, 0x2700, 0x1e);
	instruction("BNE backward untaken", {0x66fc}, 3, 4, 0x2704, 0x22);
	instruction("BNE.W forward taken", {0x6600, 0x0006}, 3, 6, 0x2700, 0x28);
	instruction("BNE.W forward untaken", {0x6600, 0x0006}, 1, 6, 0x2704, 0x24);
	instruction("BNE.W backward taken", {0x6600, 0xfffc}, 2, 6, 0x2700, 0x1e);
	instruction("BNE.W backward untaken", {0x6600, 0xfffc}, 3, 6, 0x2704, 0x24);
	conditionalBranchMatrix();
	shiftMatrix();
	return failures ? 1 : 0;
}
