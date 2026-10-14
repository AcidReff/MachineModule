#pragma once

#include <cstdint>

#include "jitasmjithelpers.h"
#include "jittypes.h"

#include "asmjit/core/jitruntime.h"

namespace dsp56k
{
	class DSP;

	class JitTrampoline
	{
	public:
		typedef void (*TExecLoopFunc)(DSP*, uint32_t) noexcept;				// DSP, iteration count
		typedef TWord (*TExecUntilCyclesFunc)(DSP*) noexcept;				// DSP, runs to its exclusive exec target cycle; invalid PC or 0xffffffff
		typedef void (*TExecOneFunc)(JitDspPtr*, TWord, TJitFunc) noexcept;	// DspRegs, PC, function to be called

		static constexpr uint32_t UnrollShift = 3;
		static constexpr uint32_t UnrollSize = 1<<UnrollShift;

		JitTrampoline(DSP& _dsp);

		void generateCode()
		{
			generateExecLoopFunc();
			generateExecUntilCyclesFunc();
			generateExecOneFunc();
		}

		void exec(DSP* _dsp, uint32_t _count) const noexcept
		{
			assert(((_count >> UnrollShift) << UnrollShift) == _count);
			m_funcExecLoop(_dsp, _count >> UnrollShift);
		}

		void execOne(JitDspPtr* _jit, const TWord _pc, const TJitFunc _func) const noexcept
		{
			m_funcExecOne(_jit, _pc, _func);
		}

		TWord execUntilCycles(DSP* _dsp) const noexcept
		{
			return m_funcExecUntilCycles(_dsp);
		}

	private:
		void generateExecLoopFunc();
		void generateExecUntilCyclesFunc();
		void generateExecOneFunc();

		DSP& m_dsp;
		asmjit::JitRuntime m_runtime;
		AsmJitLogger m_logger;
		AsmJitErrorHandler m_errorHandler;
		TExecLoopFunc m_funcExecLoop = nullptr;
		TExecUntilCyclesFunc m_funcExecUntilCycles = nullptr;
		TExecOneFunc m_funcExecOne = nullptr;
	};
}
