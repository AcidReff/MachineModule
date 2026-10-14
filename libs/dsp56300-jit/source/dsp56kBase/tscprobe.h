#pragma once

// Diagnostic TSC probes: where a DSP's host time goes inside execUntilCycles.
// Compiled only with the CMake option DSP56K_TSC_PROBES, and active only on a
// thread that installs a probe::Ctx (the MD/MM pair worker). Each rdtsc charges
// the ticks since the previous one to the innermost open scope, so the self
// times of all scopes add up exactly to the wall time under the context, and a
// scope never counts a nested scope's time. Probe cost itself is estimated by
// calibrate() and taken out at report time.

#ifdef DSP56K_TSC_PROBES

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>

#if !(defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__))
#error "DSP56K_TSC_PROBES needs an x86 TSC"
#elif defined(_MSC_VER)
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

namespace dsp56k::probe
{
	enum Cat : uint8_t
	{
		Outside,		// no scope open: the worker between chunks (gates, host transport, notify)
		Park,			// root: the worker waiting for a gate to open
		Exec,			// root: execUntilCycles of the DSP a chunk runs; self = native JIT + dispatch
		CatchUp,		// the other DSP run nested in a link delivery; self = its blocks + loop
		Periph,			// execPeripherals: ESSI clocks, link/codec callbacks, HDI08, timers, DMA;
						// at level 2 only what the categories below leave (delay reset, external interrupts)
		MmioWrite,		// peripheral register writes from JIT code
		Intr,			// interrupt dispatch, including the vector block's own execution
		Compile,		// JIT block emission
		Invalidate,		// JIT invalidation on P writes
		ModeCheck,		// JIT mode switch check
		Wait,			// WAIT
		EsxiClock,		// level 2: serial clock scheduling, without the slots it runs
		EssiTx0,		// level 2: an ESSI transmit slot, its frame callbacks included
		EssiTx1,
		EssiRx0,		// level 2: an ESSI receive slot, its frame callbacks included
		EssiRx1,
		Hdi08,			// level 2: HDI08 service
		Timers,			// level 2: timers
		Dma,			// level 2: DMA channels
		EssiHostTx,		// level 2: an ESSI handing a transmitted frame to the host
		EssiHostProbe,	// level 2: an ESSI asking the host whether a word is on the wire
		EssiHostRx,		// level 2: an ESSI taking a received frame from the host
		EssiDmaRequest,	// level 2: DMA transfers an ESSI slot requests
		Calib,
		CalibParent,
		CatCount
	};

	inline const char* catName(const uint32_t _cat)
	{
		static constexpr const char* names[CatCount] = {"outside", "park", "exec", "catchUp", "periph", "mmioWrite",
			"intr", "compile", "invalidate", "modeCheck", "wait", "esxiClock", "essiTx0", "essiTx1", "essiRx0", "essiRx1",
			"hdi08", "timers", "dma", "essiHostTx", "essiHostProbe", "essiHostRx", "essiDmaRequest", "calib", "calibParent"};
		return _cat < CatCount ? names[_cat] : "?";
	}

	struct Bucket
	{
		uint64_t self = 0;			// ticks charged while this was the innermost scope
		uint64_t calls = 0;			// scope entries
		uint64_t childEnters = 0;	// scopes opened while this one was innermost
	};

	// Per-DSP event counters, written only by the thread running that DSP, no
	// atomics: they are read by the same thread at report time.
	struct Counters
	{
		std::array<uint64_t, 4> checks{};		// dispatch callback invocations: periph, interrupts, preventInterrupt, nop
		uint64_t modeChecks = 0;				// Jit::checkModeChange calls
		uint64_t periphDue = 0;					// execPeripherals runs
		uint64_t intrEmpty = 0;					// execInterrupts with an empty queue
		uint64_t intrMasked = 0;				// execInterrupts with a masked vector
		std::array<uint64_t, 2> nopCalls{};		// skipNopLoop calls [nested, own]
		std::array<uint64_t, 2> nopSkipped{};	// cycles skipped
		std::array<uint64_t, 2> pollCalls{};	// skipPollLoop calls [nested, own]
		std::array<uint64_t, 2> pollActed{};	// calls that skipped anything
		std::array<uint64_t, 2> pollSkipped{};	// cycles skipped
		std::array<uint64_t, 2> essiTxSlots{};	// per ESSI: transmit slots past the clock gate and TE
		std::array<uint64_t, 2> essiTxFrames{};	// frames handed to the host
		std::array<uint64_t, 2> essiRxSlots{};	// receive slots past the clock gate and RE
		std::array<uint64_t, 2> essiRxIdle{};	// of which found no word on the wire
	};

	struct Ctx
	{
		uint8_t level = 1;			// 0: roots only (Exec, CatchUp), 1: the exec path, 2: also inside the peripherals
		uint8_t curCat = Outside;
		uint8_t curDsp = 0;
		uint8_t rootDsp = 0;		// DSP of the open Exec root; any other DSP counts as nested
		uint64_t last = 0;

		Bucket acc[2][2][CatCount];						// [dsp][nested][cat]
		uint64_t untimedCalls[2][CatCount]{};			// [dsp][cat] entries below the level: rates without rdtsc
		uint64_t mmioReads[2][2][128]{};				// [dsp][nested][register & 0x7f]
		uint64_t cycles[2][2]{};						// [dsp][nested] executed DSP cycles

		double kIn = 0.0;		// probe ticks charged to a child per entry
		double kOut = 0.0;		// probe ticks charged to the parent per child entry

		Bucket& bucket(const uint32_t _dsp, const uint32_t _cat)
		{
			// Worker-level categories belong to no DSP run
			const uint32_t nested = _cat <= Park ? 0 : (_dsp != rootDsp ? 1 : 0);
			return acc[_dsp & 1][nested][_cat];
		}

		uint32_t nestedFor(const uint32_t _dsp) const { return _dsp != rootDsp ? 1 : 0; }
	};

	inline thread_local Ctx* t_ctx = nullptr;

	inline uint64_t now() { return __rdtsc(); }

	struct Token
	{
		Ctx* ctx = nullptr;
		uint8_t cat = 0;
		uint8_t dsp = 0;
		uint8_t rootDsp = 0;
	};

	inline Token enter(const uint32_t _minLevel, const uint32_t _cat, const uint32_t _dsp)
	{
		Ctx* const c = t_ctx;
		if(!c)
			return {};
		if(c->level < _minLevel)
		{
			++c->untimedCalls[_dsp & 1][_cat];
			return {};
		}
		const uint64_t t = now();
		auto& parent = c->bucket(c->curDsp, c->curCat);
		parent.self += t - c->last;
		++parent.childEnters;
		const Token token{c, c->curCat, c->curDsp, c->rootDsp};
		c->curCat = static_cast<uint8_t>(_cat);
		c->curDsp = static_cast<uint8_t>(_dsp & 1);
		if(_cat == Exec)
			c->rootDsp = c->curDsp;
		++c->bucket(c->curDsp, _cat).calls;
		c->last = t;
		return token;
	}

	inline void leave(const Token& _token)
	{
		Ctx* const c = _token.ctx;
		if(!c)
			return;
		const uint64_t t = now();
		c->bucket(c->curDsp, c->curCat).self += t - c->last;
		c->curCat = _token.cat;
		c->curDsp = _token.dsp;
		c->rootDsp = _token.rootDsp;
		c->last = t;
	}

	struct Scope
	{
		Scope(const uint32_t _minLevel, const uint32_t _cat, const uint32_t _dsp) : token(enter(_minLevel, _cat, _dsp)) {}
		~Scope() { leave(token); }
		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;
		Token token;
	};

	// Index of the Counters arrays split [nested, own]: the same rule as the
	// timed buckets, a DSP other than the open root is nested.
	inline uint32_t ownIndex(const uint32_t _dsp)
	{
		const Ctx* const c = t_ctx;
		return c ? 1u - c->nestedFor(_dsp) : 1u;
	}

	inline void countMmioRead(const uint32_t _dsp, const uint32_t _offset)
	{
		Ctx* const c = t_ctx;
		if(c)
			++c->mmioReads[_dsp & 1][c->nestedFor(_dsp)][_offset & 0x7f];
	}

	// Measures the probe cost with empty scopes under a parent, then resets the
	// accumulators. Keeps the lowest of a few rounds: an optimistic cost, so
	// corrected figures are upper bounds.
	inline void calibrate(Ctx& _ctx)
	{
		Ctx* const prev = t_ctx;
		t_ctx = &_ctx;
		double bestIn = 1e30, bestOut = 1e30;
		for(int round = 0; round < 4; ++round)
		{
			_ctx = Ctx{_ctx.level};
			_ctx.last = now();
			constexpr uint32_t n = 65536;
			{
				Scope parent(0, CalibParent, 0);
				for(uint32_t i = 0; i < n; ++i)
					Scope s(0, Calib, 0);
			}
			const auto& calib = _ctx.acc[0][0][Calib];
			const auto& par = _ctx.acc[0][0][CalibParent];
			bestIn = std::min(bestIn, static_cast<double>(calib.self) / n);
			bestOut = std::min(bestOut, static_cast<double>(par.self) / n);
		}
		const uint8_t level = _ctx.level;
		_ctx = Ctx{level};
		_ctx.kIn = bestIn;
		_ctx.kOut = bestOut;
		_ctx.last = now();
		t_ctx = prev;
	}
}

#define DSP_PROBE_SCOPE(_level, _cat, _dsp) ::dsp56k::probe::Scope dspProbeScope((_level), ::dsp56k::probe::_cat, (_dsp))
// A category computed at run time, such as one per ESSI
#define DSP_PROBE_SCOPE_CAT(_level, _cat, _dsp) ::dsp56k::probe::Scope dspProbeScope((_level), (_cat), (_dsp))
#define DSP_PROBE_COUNT(_expr) _expr

#else

#define DSP_PROBE_SCOPE(_level, _cat, _dsp)
#define DSP_PROBE_SCOPE_CAT(_level, _cat, _dsp)
#define DSP_PROBE_COUNT(_expr)

#endif
