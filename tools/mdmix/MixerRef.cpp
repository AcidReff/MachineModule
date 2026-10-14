#include "MixerRef.h"

#include <sstream>
#include <stdexcept>

#include "dsp56kEmu/audio.h"
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/jit.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"

namespace md::mixref
{
	using namespace dsp56k;

	namespace
	{
		constexpr TWord kSizeP = 0x200000, kSizeXY = 0x200000, kBridge = 0x020000;
		// DSP1 state after the ColdFire's first-stage loader, read from a running gearmulator-md-mm (OS 1.63)
		constexpr TWord kAar[4] = {0x100539, 0x140639, 0x180539, 0x1c0639};
		constexpr TWord kOmr = 0x00490d;

		constexpr TWord kEntry = 0x24;		// sr = $300, jsr $100000 (init), then unmask and wait for the link DMA
		constexpr TWord kAfterInit = 0x2d;
		constexpr TWord kTrackFn = 0xa4;	// per-track effect chain: r6 = Y:$200+$40k, r7 = input (Y), r5 = output (X)
		constexpr TWord kTrackParams = 0x200, kTrackStride = 0x40;
		constexpr TWord kInput = 0x600;		// the link buffer: Y:$600 + $20k
		constexpr TWord kOutput = 0x200;	// X:$200 + $20k (tracks routed to the main outs)
	}

	MixerRef::MixerRef(const fw::Firmware& _fw, bool useJit)
	{
		m_validator = std::make_unique<DefaultMemoryValidator>();
		m_mem = std::make_unique<Memory>(*m_validator, kSizeP, kSizeXY, kBridge);
		m_periphX = std::make_unique<Peripherals56303>();
		m_periphY = std::make_unique<PeripheralsNop>();
		m_dsp = std::make_unique<DSP>(*m_mem, m_periphX.get(), m_periphY.get());
		for(auto* essi : {&m_periphX->getEssi0(), &m_periphX->getEssi1()})
		{
			essi->setReadRxCallback([](uint64_t& _i, Audio::RxFrame& _f) { _f.resize(2); _f[0] = {}; _f[1] = {}; ++_i; });
			essi->setWriteTxCallback([](uint64_t& _i, const Audio::TxFrame&) { ++_i; });
		}

		m_dsp->resetHW();
		for(const auto& r : _fw.dspB.records)	// section 2 = mixer program
			for(size_t k = 0; k < r.words.size(); ++k)
			{
				const TWord a = r.addr + static_cast<TWord>(k);
				if(r.space == fw::Space::P || a >= kBridge)
					m_dsp->memWriteP(a, r.words[k]);
				else
					m_dsp->memWrite(r.space == fw::Space::X ? MemArea_X : MemArea_Y, a, r.words[k]);
			}
		for(int i = 0; i < 4; ++i)
			m_periphX->write(0xfffff9 - static_cast<TWord>(i), kAar[i]);
		m_dsp->regs().omr.var = kOmr;

		// Stops: a spin after init, and one at the per-track function's end ($25e: its timing pad + jmp $6f).
		m_dsp->memWriteP(kAfterInit, 0x0c0000 | kAfterInit);	// jmp <$2d
		m_dsp->memWriteP(0x25e, 0x0c025e);						// jmp <$25e
		m_dsp->setPC(kEntry);
		if(!runUntil(kAfterInit, 100'000'000))
			throw std::runtime_error("mixer DSP init did not complete: " + m_fault);
		for(uint32_t a=0;a<0x800;++a) {m_initialX.push_back(peekX(a));m_initialY.push_back(peekY(a));}
		for(uint32_t a=0x100000;a<0x148000;++a) m_initialExternal.push_back(m_mem->get(MemArea_P,a));
		if(useJit) {
			auto cfg=m_dsp->getJit().getConfig();
			cfg.dynamicFastInterrupts=true;cfg.aguSupportBitreverse=true;cfg.linkJitBlocks=false;
			cfg.maxInstructionsPerBlock=32;cfg.maxDoIterations=4;
			m_dsp->getJit().setConfig(cfg);
			m_dsp->memWriteP(0x971,0x0c0971);
			m_useJit=true;
		}
	}

	MixerRef::~MixerRef() = default;

	void MixerRef::resetMaster()
	{
		for(uint32_t a=0;a<m_initialX.size();++a) {pokeX(a,m_initialX[a]);pokeY(a,m_initialY[a]);}
		for(uint32_t a=0;a<m_initialExternal.size();++a) m_dsp->memWriteP(0x100000+a,m_initialExternal[a]);
		for(int i=0;i<8;++i) m_dsp->writeReg(static_cast<EReg>(Reg_M0+i),TReg24(0xffffff));
		m_dsp->writeReg(Reg_SR,TReg24(0x300));
		m_fault.clear();
	}

	uint32_t MixerRef::peekX(const uint32_t _a) const { return m_dsp->memory().get(MemArea_X, _a); }
	uint32_t MixerRef::peekY(const uint32_t _a) const { return m_dsp->memory().get(MemArea_Y, _a); }
	void MixerRef::pokeX(const uint32_t _a, const uint32_t _v) { m_dsp->memWrite(MemArea_X, _a, _v & 0xffffff); }
	void MixerRef::pokeY(const uint32_t _a, const uint32_t _v) { m_dsp->memWrite(MemArea_Y, _a, _v & 0xffffff); }

	void MixerRef::setFx(const int _track, const uint32_t* _fx)
	{
		for(int k = 0; k < 9; ++k)
			pokeY(kTrackParams + kTrackStride * static_cast<uint32_t>(_track) + static_cast<uint32_t>(k), _fx[k]);
	}

	bool MixerRef::runUntil(const uint32_t _pc, const uint64_t _max)
	{
		const auto start = m_dsp->getInstructionCounter();
		uint64_t dispatches = 0;
		while(m_dsp->getPC().toWord() != _pc)
		{
			if(m_useJit) {
				m_dsp->execJit();
			} else m_dsp->execInterpreter();
			if(++dispatches > _max || m_dsp->getInstructionCounter() - start > _max)
			{
				std::ostringstream o;
				o << "instruction budget exceeded at PC=$" << std::hex << m_dsp->getPC().toWord();
				m_fault = o.str();
				return false;
			}
		}
		m_lastInstructions = m_dsp->getInstructionCounter() - start;
		return true;
	}

	bool MixerRef::runMix(const Block* _tracks, const uint32_t (*_mix)[5])
	{
		// As the main loop leaves things: main-routed tracks' blocks at X:$200 upwards, the others from X:$3e0
		// downwards; routing words copied to X:$6c8; the frame buffer base in X:$640.
		TWord up = 0x200, down = 0x3e0;
		for(int t = 0; t < kTracks; ++t)
		{
			const TWord at = (_mix[t][0] & 0xffffff) == 6 ? up : down;
			if(at == up) up += 0x20; else down -= 0x20;
			for(int i = 0; i < kBlock; ++i)
				pokeX(at + static_cast<TWord>(i), static_cast<uint32_t>(_tracks[t][i]));
			pokeX(0x6c8 + static_cast<TWord>(t), _mix[t][0]);
			for(int k = 0; k < 5; ++k)
				pokeY(0x100 + 5 * static_cast<TWord>(t) + static_cast<TWord>(k), _mix[t][k]);
		}
		pokeX(0x640, 0x400);
		m_dsp->writeReg(Reg_SR, TReg24(m_dsp->getSR().toWord() | 0x300));
		const TWord stop = 0x342;
		const TWord saved = m_dsp->memory().get(MemArea_P, stop);
		m_dsp->memWriteP(stop, 0x0c0000 | stop);
		m_dsp->setPC(0x294);
		const bool ok = runUntil(stop, 10'000'000);
		m_dsp->memWriteP(stop, saved);
		return ok;
	}

	bool MixerRef::runMaster()
	{
		const TWord stop = 0x971;
		const TWord saved = m_dsp->memory().get(MemArea_P, stop);
		m_dsp->memWriteP(stop, 0x0c0000 | stop);
		m_dsp->setPC(0x342);
		const bool ok = runUntil(stop, 10'000'000);
		m_dsp->memWriteP(stop, saved);
		return ok;
	}

	bool MixerRef::runTrack(const int _track, const Block& _in, Block& _out, const uint32_t _stopAt)
	{
		const auto k = static_cast<TWord>(_track);
		for(int i = 0; i < kBlock; ++i)
			pokeY(kInput + 0x20 * k + static_cast<TWord>(i), static_cast<uint32_t>(_in[i]));

		for(int i = 0; i < 8; ++i)
			m_dsp->writeReg(static_cast<EReg>(Reg_M0 + i), TReg24(0xffffff));
		m_dsp->writeReg(Reg_R6, TReg24(kTrackParams + kTrackStride * k));
		m_dsp->writeReg(Reg_R7, TReg24(kInput + 0x20 * k));
		m_dsp->writeReg(Reg_R5, TReg24(kOutput + 0x20 * k));
		m_dsp->writeReg(Reg_SR, TReg24(m_dsp->getSR().toWord() | 0x300));	// interrupts masked: only this function runs

		const TWord saved = m_dsp->memory().get(MemArea_P, _stopAt);
		if(_stopAt != 0x25e)
			m_dsp->memWriteP(_stopAt, 0x0c0000 | _stopAt);
		m_dsp->setPC(kTrackFn);
		const bool ok = runUntil(_stopAt, 10'000'000);
		if(_stopAt != 0x25e)
			m_dsp->memWriteP(_stopAt, saved);

		for(int i = 0; i < kBlock; ++i)
		{
			const TWord v = peekX(kOutput + 0x20 * k + static_cast<TWord>(i));
			_out[i] = static_cast<int32_t>(v << 8) >> 8;
		}
		return ok;
	}
}
