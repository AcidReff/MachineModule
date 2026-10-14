#pragma once

#include <vector>

#include "types.h"

namespace dsp56k
{
	class Peripherals56303;
	class Peripherals56362;
	class Esxi;
	class IPeripherals;
	class DSP;

	class EsxiClock
	{
	public:
		enum class ClockSource
		{
			Instructions,
			Cycles
		};

		static constexpr uint32_t MaxEsais = 2;

		EsxiClock(IPeripherals& _peripherals);
		uint32_t exec() noexcept;

		void setPCTL(TWord _val);
		TWord getPCTL() const { return m_pctl; }

		void setSamplerate(uint32_t _samplerate);
		void setCyclesPerSample(uint32_t _cyclesPerSample);
		void setExternalClockFrequency(uint32_t _freq);

		void setEsaiDivider(Esxi* _esai, TWord _divider)
		{
			setEsaiDivider(_esai, _divider, _divider);
		}
		void setEsaiDivider(Esxi* _esai, TWord _dividerTX, TWord _dividerRX);
		// A nonzero fine period schedules an ESSI independently of the audio base
		// tick. Zero preserves the existing base-plus-divider clock.
		void setEsaiFinePeriod(Esxi* _esai, uint32_t _finePeriod);
		void setExactCycleDeadlineEnabled(bool _enabled) { m_exactCycleDeadlineEnabled = _enabled; }
		bool usesExactCycleDeadline() const;
		uint32_t getNextCycleDeadline() const { return m_nextCycleDeadline; }
		bool setEsaiCounter(const Esxi* _esai, int _counter)
		{
			return setEsaiCounter(_esai, _counter, _counter);
		}
		bool setEsaiCounter(const Esxi* _esai, int _counterTX, int _counterRX);
		bool shiftEsaiFineAnchor(const Esxi* _esai, int64_t _cycles);	// one-time fine-phase step (see .cpp)

		bool setSpeedPercent(uint32_t _percent = 100);

		auto getSpeedInHz() const			{ return m_speedHz; }
		auto getSpeedPercent() const		{ return m_speedPercent; }

		void setDSP(const DSP* _dsp);
		void setClockSource(ClockSource _clockSource);

		void restartClock();

		TWord getRemainingInstructionsForFrameSync() const;

		auto getDspInstructionCounter() const { return *m_dspInstructionCounter; }
		auto getLastClock() const { return m_lastClock; }
		auto getCyclesPerSample() const { return m_cyclesPerSample; }

	protected:
		const auto& getEsais() const { return m_esais; }
		const auto& getPeripherals() const { return m_periph; }

	private:
		void setClockSource(const DSP* _dsp, ClockSource _clockSource);

		void updateCyclesPerSample();

		const uint64_t* m_dspInstructionCounter = nullptr;
		uint64_t m_lastClock = 0;
		uint32_t m_nextCycleDeadline = 0;
		uint32_t m_cyclesPerSample = 2133;				// estimated cycles per sample before calculated

		ClockSource m_clockSource = ClockSource::Instructions;

		IPeripherals& m_periph;

		uint32_t m_fixedCyclesPerSample = 0;
		uint32_t m_samplerate = 0;
		TWord m_pctl = 0;
		uint32_t m_externalClockFrequency = 12000000;	// Hz

		uint64_t m_speedHz = 0;							// DSP clock speed in Hertz
		uint32_t m_speedPercent = 100;					// 100% = regular operation, overclock/underclock otherwise

		struct Clock
		{
			uint32_t divider = 0;
			int32_t counter = 0;
		};

		struct EsaiEntry
		{
			Esxi* esai = nullptr;

			Clock tx;
			Clock rx;

			// Nonzero selects the independent fine schedule; zero uses the base tick.
			uint32_t finePeriod = 0;
			uint64_t fineLastClock = 0;
		};

		std::vector<EsaiEntry> m_esais;

		// Cached "any entry has a non-zero finePeriod". Keeps exec()'s hot path free of the
		// fine-schedule loop (and provably byte-identical) whenever fine-link mode is unused.
		bool m_hasFineEsais = false;
		bool m_exactCycleDeadlineEnabled = false;
	};

	class EsaiClock : public EsxiClock
	{
	public:
		EsaiClock(Peripherals56362& _peripherals);
		template<bool ExpectedResult> TWord getRemainingInstructionsForTransmitFrameSync() const;
		template<bool ExpectedResult> TWord getRemainingInstructionsForReceiveFrameSync() const;
	};

	class EssiClock : public EsxiClock
	{
	public:
		EssiClock(Peripherals56303& _peripherals);
		template<bool ExpectedResult> TWord getRemainingInstructionsForTransmitFrameSync(uint32_t _esaiIndex) const;
		template<bool ExpectedResult> TWord getRemainingInstructionsForReceiveFrameSync(uint32_t _esaiIndex) const;
	};
}
