#include "cpuState.h"
#include "mc68k.h"
#include "musashiEntry.h"

#include <array>
#include <cstdint>
#include <cstdio>

namespace
{
	class ColdFireTestCpu final : public mc68k::Mc68k
	{
	public:
		ColdFireTestCpu() : Mc68k(M68K_CPU_TYPE_MCF5206E) {}

		uint8_t read8(const uint32_t _address) override
		{
			return _address < m_code.size() ? m_code[_address] : 0;
		}

		uint16_t read16(const uint32_t _address) override
		{
			return static_cast<uint16_t>(read8(_address)) << 8 | read8(_address + 1);
		}

		uint16_t readImm16(const uint32_t _address) override
		{
			return read16(_address);
		}

	private:
		// REMS.L D2,D3:D1 followed by the equal-register DIVS.L D2,D1 form.
		const std::array<uint8_t, 8> m_code{0x4c, 0x42, 0x18, 0x03,
			0x4c, 0x42, 0x18, 0x01};
	};
}

int main()
{
	{
		ColdFireTestCpu cpu;
		cpu.setPC(0);
		m68k_set_reg(cpu.getCpuState(), M68K_REG_D1, 4417);
		m68k_set_reg(cpu.getCpuState(), M68K_REG_D2, 10);
		m68k_set_reg(cpu.getCpuState(), M68K_REG_D3, 0xdeadbeef);

		cpu.exec();
		if(cpu.getDReg(1) != 4417 || cpu.getDReg(3) != 7)
		{
			std::fprintf(stderr, "ColdFire REMS changed dividend: D1=%u D3=%u\n",
				cpu.getDReg(1), cpu.getDReg(3));
			return 1;
		}

		cpu.exec();
		if(cpu.getDReg(1) != 441 || cpu.getDReg(3) != 7)
		{
			std::fprintf(stderr, "ColdFire DIVS quotient mismatch: D1=%u D3=%u\n",
				cpu.getDReg(1), cpu.getDReg(3));
			return 1;
		}
	}

	/* Keep the overflow case separate so it starts from a known status word. */
	{
		ColdFireTestCpu cpu;
		cpu.setPC(0);
		m68k_set_reg(cpu.getCpuState(), M68K_REG_D1, 0x80000000);
		m68k_set_reg(cpu.getCpuState(), M68K_REG_D2, 0xffffffff);
		m68k_set_reg(cpu.getCpuState(), M68K_REG_D3, 0xdeadbeef);
		m68k_set_reg(cpu.getCpuState(), M68K_REG_SR, 0x1f);

		cpu.exec();
		const auto status = m68k_get_reg(cpu.getCpuState(), M68K_REG_SR);
		if(cpu.getDReg(1) != 0x80000000 || cpu.getDReg(3) != 0xdeadbeef
			|| (status & 0x1f) != 0x12)
		{
			std::fprintf(stderr, "ColdFire REMS overflow mismatch: D1=%08x D3=%08x SR=%04x\n",
				cpu.getDReg(1), cpu.getDReg(3), status);
			return 1;
		}
	}

	return 0;
}
