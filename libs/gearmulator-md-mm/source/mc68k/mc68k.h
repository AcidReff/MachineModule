#pragma once

#include <array>
#include <string>

#include "endian.h"
#include "gpt.h"
#include "qsm.h"
#include "sim.h"

namespace mc68k
{
	struct CpuState;

	class Mc68k
	{
	public:
		static constexpr uint32_t CpuStateSize = 640;	// headroom for ColdFire control registers added to m68ki_cpu_core

		Mc68k();
		virtual ~Mc68k();

		virtual uint32_t exec();

		void injectInterrupt(uint8_t _vector, uint8_t _level);
		bool hasPendingInterrupt(uint8_t _vector, uint8_t _level) const;
		bool legacyPeripheralsExecQuiescent()
		{
			return m_gpt.execQuiescent() && m_sim.execQuiescent()
				&& m_qsm.execQuiescent();
		}
		// Withdraw a queued interrupt when a level-sensitive source deasserts
		// before acknowledgement, then recompute the active interrupt level.
		bool removePendingInterrupt(uint8_t _vector, uint8_t _level);

		virtual void onReset() {}
		virtual void onBgnd();
		virtual uint32_t onIllegalInstruction(uint32_t _opcode);

		virtual uint8_t read8(const uint32_t _addr)
		{
			const auto addr = static_cast<PeriphAddress>(_addr & g_peripheralMask);

			if(m_gpt.isInRange(addr))			return m_gpt.read8(addr);
			if(m_sim.isInRange(addr))			return m_sim.read8(addr);
			if(m_qsm.isInRange(addr))			return m_qsm.read8(addr);

			return 0;
		}

		virtual uint16_t read16(const uint32_t _addr)
		{
			const auto addr = static_cast<PeriphAddress>(_addr & g_peripheralMask);

			if(m_gpt.isInRange(addr))			return m_gpt.read16(addr);
			if(m_sim.isInRange(addr))			return m_sim.read16(addr);
			if(m_qsm.isInRange(addr))			return m_qsm.read16(addr);

			return 0;
		}

		virtual void write8(const uint32_t _addr, const uint8_t _val)
		{
			const auto addr = static_cast<PeriphAddress>(_addr & g_peripheralMask);

			if(m_gpt.isInRange(addr))			m_gpt.write8(addr, _val);
			else if(m_sim.isInRange(addr))		m_sim.write8(addr, _val);
			else if(m_qsm.isInRange(addr))		m_qsm.write8(addr, _val);
		}

		virtual void write16(uint32_t _addr, uint16_t _val)
		{
			const auto addr = static_cast<PeriphAddress>(_addr & g_peripheralMask);

			if(m_gpt.isInRange(addr))			m_gpt.write16(addr, _val);
			else if(m_sim.isInRange(addr))		m_sim.write16(addr, _val);
			else if(m_qsm.isInRange(addr))		m_qsm.write16(addr, _val);
		}

		virtual uint16_t readImm16(uint32_t _addr) = 0;

		virtual uint32_t readIrqUserVector(uint8_t _level);

		void reset();

		void setPC(uint32_t _pc);
		uint32_t getPC() const;
		
		uint32_t getAReg(uint32_t _index) const;
		uint32_t getDReg(uint32_t _index) const;

		virtual uint32_t getResetPC() { return 0; }
		virtual uint32_t getResetSP() { return 0; }

		uint32_t disassemble(uint32_t _pc, char* _buffer);

		uint64_t getCycles() const { return m_cycles; }
		
		Port& getPortE()	{ return m_sim.getPortE(); }
		Port& getPortF()	{ return m_sim.getPortF(); }
		Port& getPortGP()	{ return m_gpt.getPortGP(); }
		Port& getPortQS()	{ return m_qsm.getPortQS(); }

		Gpt& getGPT()		{ return m_gpt; }
		Qsm& getQSM()		{ return m_qsm; }
		Sim& getSim()		{ return m_sim; }

		CpuState* getCpuState();
		const CpuState* getCpuState() const;

		bool dumpAssembly(const std::string& _filename, uint32_t _first, uint32_t _count, bool _splitFunctions = true);
		
	protected:
		// Select the Musashi CPU type. The public constructor retains the 68020 default.
		explicit Mc68k(unsigned int _m68kCpuType);
		// Execute one processor instruction and account its cycles without
		// advancing the generic on-chip peripheral models.
		uint32_t execInstruction();

		void raiseIPL();

		std::array<uint8_t, CpuStateSize> m_cpuStateBuf;
		CpuState* m_cpuState;

		Gpt m_gpt;
		Sim m_sim;
		Qsm m_qsm;
		
		std::array<std::deque<uint8_t>, 8> m_pendingInterrupts;

		uint64_t m_cycles = 0;
	};
}
