// Harness structure adapted from shnolk/monomodule src/core/dsp/DspEngine.cpp (AGPL-3.0).
#include "VoiceEngine.h"
#include "MachineRunner.h"
#include "DspExecutionPolicy.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <stdexcept>

#include "dsp56kEmu/assembler.h"
#include "dsp56kEmu/audio.h"
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/jit.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"

namespace md::engine
{
	using namespace dsp56k;

	namespace
	{
		// Memory as gearmulator-md-mm models the Machinedrum: external SRAM from $020000 is shared by P, X and Y.
		// P is 2M words, not the DSP's 8M: the MD's map stays below $200000 (samples end at $18fc12), and the interpreter's
		// per-word opcode cache made the full size cost ~300 MB of RAM in MPC. Output is identical.
		constexpr TWord kSizeP = 0x200000, kSizeXY = 0x800000, kBridge = 0x020000;

		// DSP2 state after the ColdFire's first-stage loader, read from a running gearmulator-md-mm (OS 1.63):
		// the full SPS-1 UW memory map (AAR0-3) and OMR.
		constexpr TWord kAar[4] = {0x100539, 0x140639, 0x180539, 0x1c0639};
		constexpr TWord kOmr = 0x00498d;

		// Program addresses (OS 1.63 voice program, see docs/PROTOCOL.md)
		constexpr TWord kEntry = 0x24;			// after the vector table: init, then bra $64
		constexpr TWord kHostGroupTx = 0x73;	// movep a1,x:HTX every 4 voices (ColdFire pacing): removed
		constexpr TWord kRenderFn = 0xa8;		// r1 = y:(r0+$145c77): render fn address for the voice's machine
		constexpr TWord kAfterRenderFn = 0xaa;	// resumes normal flow after the render-fn lookup
		constexpr TWord kAfterRender = 0xb5;	// voice rendered into Y:(y:$140); original: frame sync + link DMA
		constexpr TWord kNextVoice = 0xd5;		// advance to the next voice
		constexpr TWord kEndOfBlock = 0xe2;		// all 16 voices done; original: RAM-R input position, bra $64
		constexpr TWord kBlockStart = 0x64;
		constexpr TWord kStub = 0x0c00;			// free internal P RAM (the program's internal P ends at $3e2)
		constexpr TWord kSkipStub = 0x0c20;	// silent-voice check, replaces kRenderFn's lookup
		constexpr TWord kSkipNormal = 0x0c30;	// real render fn lookup, resumed
		constexpr TWord kSkipSilent = 0x0c38;	// idle voice: flag 0, straight to the next voice

		constexpr uint64_t kMaxInstrInit = 200'000'000;
		constexpr uint64_t kMaxInstrBlock = 20'000'000;
	}

	VoiceEngine::VoiceEngine(const fw::Firmware& _fw, bool useJit) : m_fw(_fw), m_useJit(useJit && platformAllowsJit() && std::getenv("MACHINEMODULE_VOICE_INTERPRETER")==nullptr)
	{
		m_validator = std::make_unique<DefaultMemoryValidator>();
		m_mem = std::make_unique<Memory>(*m_validator, kSizeP, kSizeXY, kBridge);
		m_periphX = std::make_unique<Peripherals56303>();
		m_periphY = std::make_unique<PeripheralsNop>();
		m_dsp = std::make_unique<DSP>(*m_mem, m_periphX.get(), m_periphY.get());
		if constexpr(g_useJIT)
		{
			// As gearmulator-md-mm configures its DSPs: the program keeps code in the vector area.
			auto cfg = m_dsp->getJit().getConfig();
			cfg.dynamicFastInterrupts = true;
			cfg.aguSupportBitreverse = true;
			cfg.linkJitBlocks = false;
			// Longer blocks currently diverge from the interpreter on noise machines.
			// Single-instruction blocks retain native code execution with exact state updates.
			cfg.maxInstructionsPerBlock = 1;
			cfg.maxDoIterations = 4;
			m_dsp->getJit().setConfig(cfg);
		}
		else
		{
			// The interpreter lazily allocates its opcode cache on targets without JIT.
			m_useJit = false;
		}
		m_periphX->getHI08().setRXRateLimit(0);
		m_periphX->getHI08().setTransmitDataAlwaysEmpty(true);

		// The program's init starts both ESSI ports (ESSI0 = link to the mixer DSP, ESSI1 = codec ADC). The
		// harness replaces the link and does not use the ADC: feed silence and discard output, never block.
		for(auto* essi : {&m_periphX->getEssi0(), &m_periphX->getEssi1()})
		{
			essi->setReadRxCallback([](uint64_t& _frameIndex, Audio::RxFrame& _frame)
			{
				_frame.resize(2);
				_frame[0] = Audio::RxSlot{};
				_frame[1] = Audio::RxSlot{};
				++_frameIndex;
			});
			essi->setWriteTxCallback([](uint64_t& _frameIndex, const Audio::TxFrame&) { ++_frameIndex; });
		}
		reset();
	}

	VoiceEngine::~VoiceEngine() = default;

	void VoiceEngine::warmUp(const std::vector<uint8_t>& osImage)
	{
		if(!m_useJit) return;
		// Keep compiled blocks, but discard the warm-up's musical state. This uses the
		// snapshot approach from c0remusic/md-drums; no firmware data is persisted.
		const auto sizeP = Memory::calcPMemSize(kSizeP, kSizeXY, kBridge);
		const auto sizeXY = Memory::calcXYMemSize(kSizeXY, kBridge);
		auto* p = m_mem->getMemAreaPtr(MemArea_P);
		auto* x = m_mem->getMemAreaPtr(MemArea_X);
		auto* y = m_mem->getMemAreaPtr(MemArea_Y);
		const std::vector<TWord> savedP(p,p+sizeP), savedX(x,x+sizeXY), savedY(y,y+sizeXY);
		const auto regs = m_dsp->regs();
		MachineRunner coefficients(osImage);
		for(const auto& machine : coefficients.machines()) {
			if(machine.id>=80) continue; // ROM/RAM require user sample data.
			for(bool tonal : {false,true}) {
				if(tonal && !m_fw.x13) continue;
				coefficients.setTonal(0,tonal);
				uint16_t params[24]{}; uint32_t words[32]{};
				for(int i=0;i<8;++i) params[i]=uint16_t(machine.defaults[i])<<7;
				const int count=coefficients.compute(machine.id,params,words,32,true,0);
				if(count<0) throw std::runtime_error("Voice warm-up coefficient failure");
				words[0]=uint32_t(machine.id)+1;
				setSlot(0,words,std::clamp(count,1,32));
				Block output{};
				for(int block=0;block<4;++block)
					if(!renderBlock(output)) throw std::runtime_error("Voice warm-up: "+m_fault);
			}
		}
		std::copy(savedP.begin(),savedP.end(),p);
		std::copy(savedX.begin(),savedX.end(),x);
		std::copy(savedY.begin(),savedY.end(),y);
		m_dsp->regs()=regs;
		auto& hi=m_periphX->getHI08(); hi.clearRX(); while(hi.hasTX()) hi.readTX();
		m_lastInstructions=0; m_lastActive=0; m_voiceInstr.fill(0); m_fault.clear();
	}

	void VoiceEngine::loadImage(const fw::DspImage& _img)
	{
		for(const auto& r : _img.records)
		{
			for(size_t k = 0; k < r.words.size(); ++k)
			{
				const TWord a = r.addr + static_cast<TWord>(k);
				// P space, and anything in the shared external range, must go through memWriteP so the JIT sees it.
				if(r.space == fw::Space::P || a >= kBridge)
					m_dsp->memWriteP(a, r.words[k]);
				else
					m_dsp->memWrite(r.space == fw::Space::X ? MemArea_X : MemArea_Y, a, r.words[k]);
			}
		}
	}

	void VoiceEngine::installHarness()
	{
		Assembler as;
		auto emit = [&](TWord& _pc, const std::string& _text)
		{
			const auto r = as.assemble(_text.c_str());
			if(!r.success())
				throw std::runtime_error("voice harness: cannot assemble '" + _text + "'");
			m_dsp->memWriteP(_pc++, r.word[0]);
			if(r.wordCount > 1)
				m_dsp->memWriteP(_pc++, r.word[1]);
		};
		auto hex = [](TWord _v) { std::ostringstream o; o << "$" << std::hex << _v; return o.str(); };

		// 0) the silent-voice render ($10008f: 32 zeros into Y:(r7)) pads its time with a 32 x 50 nop loop so
		//    the hardware's block timing is even; the output is the same without it
		m_dsp->memWriteP(0x100093, 0x000000);
		m_dsp->memWriteP(0x100094, 0x000000);

		// 1) no per-group handshake word to the host
		m_dsp->memWriteP(kHostGroupTx, 0x000000);

		// 2) after each voice's render: send its 32 samples (Y:(y:$140)) to the host, then continue
		{
			TWord pc = kAfterRender;
			m_dsp->memWriteP(pc++, 0x0af080);	// jmp >stub
			m_dsp->memWriteP(pc++, kStub);
		}
		{
			TWord pc = kStub;
			emit(pc, "move y:>$140,r0");
			emit(pc, "move #>$ffffff,m0");
			emit(pc, "do #32," + hex(pc + 3));	// DO, not REP: REP over a peripheral access is unsafe in the JIT
			emit(pc, "movep y:(r0)+,x:<<$ffffc7");
			emit(pc, "nop");
			m_dsp->memWriteP(pc++, 0x0af080);	// jmp >next voice
			m_dsp->memWriteP(pc++, kNextVoice);
		}

		// 2b) skip the machine call for idle voices (current machine code 0 = never assigned, or 1 = the
		//     empty machine GND--, both of which render 32 zeros, docs/PROTOCOL.md "per voice, every
		//     tick"): redirect r1 (the render fn about to be jsr'd at $b4) to a fast clear instead of the
		//     real GND-- function. r0 (current machine code) and r7 (buffer pointer, mod 32) are live here.
		{
			TWord pc = kRenderFn;
			m_dsp->memWriteP(pc++, 0x0af080);	// jmp >kSkipStub
			m_dsp->memWriteP(pc++, kSkipStub);
		}
		{
			// jeq/jmp, not beq/bra: Bcc_xxxx's operand is a raw PC-relative displacement, not an address
			// (unlike jmp/jclr/jset elsewhere in this file); jeq is the short-absolute conditional form.
			TWord pc = kSkipStub;
			emit(pc, "move r0,a");
			emit(pc, "tst a");
			emit(pc, "jeq " + hex(kSkipSilent));
			emit(pc, "move #>1,b");	// (cmp a,b here never matched code 1: an empty-machine voice kept rendering)
			emit(pc, "sub b,a");
			emit(pc, "tst a");
			emit(pc, "jeq " + hex(kSkipSilent));
			emit(pc, "add b,a");	// a back to the machine code, as the compare left it
			emit(pc, "jmp " + hex(kSkipNormal));
		}
		{
			TWord pc = kSkipNormal;
			emit(pc, "movep #>1,x:<<$ffffc7");	// this voice will render: flag 1, its 32 samples follow
			emit(pc, "move y:(r0+$145c77),r1");
			m_dsp->memWriteP(pc++, 0x0af080);	// jmp >kAfterRenderFn
			m_dsp->memWriteP(pc++, kAfterRenderFn);
		}
		{
			// An idle voice sends flag 0 and nothing else - no 32-word clear, no 32-word transmit; the host fills its
			// zeros. Its buffer pointer (mod 32) ends where it started, so going straight to the next voice is the same
			// state the clear-and-send path left. A voice's 32 samples are only sent when its flag is 1.
			TWord pc = kSkipSilent;
            if(m_fw.x13) {
                // NFX reads neighbouring voice buffers, including idle tracks.
                emit(pc,"movep #>1,x:<<$ffffc7");
                m_dsp->memWriteP(pc++,0x0bf080); // jsr >$10008f
                m_dsp->memWriteP(pc++,0x10008f);
                emit(pc,"jmp "+hex(kStub));
            } else {
			emit(pc, "movep #>0,x:<<$ffffc7");
			m_dsp->memWriteP(pc++, 0x0af080);	// jmp >kNextVoice
			m_dsp->memWriteP(pc++, kNextVoice);
            }
		}

		// 3) after all 16 voices: wait for the host's "go" word (the host updates the voice slots meanwhile)
		{
			TWord pc = kEndOfBlock;
			emit(pc, "jclr #0,x:<<$ffffc3," + hex(kEndOfBlock));
			emit(pc, "movep x:<<$ffffc6,a");
			emit(pc, "jmp " + hex(kBlockStart));
		}
	}

	void VoiceEngine::writeP(const uint32_t _addr, const uint32_t* _words, const size_t _count)
	{
		for(size_t i = 0; i < _count && _addr + i < m_mem->sizeP(); ++i)
			m_mem->set(MemArea_P, static_cast<TWord>(_addr + i), _words[i] & 0xffffff);
	}

	uint32_t VoiceEngine::readP(const uint32_t _addr) const
	{
		return _addr < m_mem->sizeP() ? m_mem->get(MemArea_P, _addr) & 0xffffff : 0;
	}

	void VoiceEngine::reset()
	{
		m_fault.clear();
		m_dsp->resetHW();
		loadImage(m_fw.dspA);	// section 1 = voice program (docs/PROTOCOL.md)
		installHarness();
		for(int i = 0; i < 4; ++i)
			m_periphX->write(0xfffff9 - static_cast<TWord>(i), kAar[i]);
		m_dsp->regs().omr.var = kOmr;
		auto& hi = m_periphX->getHI08();
		hi.clearRX();
		while(hi.hasTX())
			hi.readTX();
		m_dsp->setPC(kEntry);

		// Init runs into the block loop; the first (silent) block comes out and the DSP then waits for "go".

		Block first;
		if(!readBlock(first, kMaxInstrInit))
			throw std::runtime_error("voice DSP init did not complete: " + m_fault);

		while(hi.hasTX())
			hi.readTX();
	}

	void VoiceEngine::setSlot(const int _voice, const uint32_t* _words, const int _count)
	{
		const TWord base = kSlotBase + kSlotStride * static_cast<TWord>(_voice);
		for(int k = 0; k < _count; ++k)
			m_dsp->memWrite(MemArea_Y, base + static_cast<TWord>(k), _words[k] & 0xffffff);
	}

	bool VoiceEngine::runUntilTx(const size_t _words, const uint64_t _maxInstructions)
	{
		auto& hi = m_periphX->getHI08();
		const auto start = m_dsp->getInstructionCounter();
		static const bool debug = std::getenv("MDV_DEBUG") != nullptr;
		uint64_t nextReport = start + 5'000'000;
		uint64_t dispatches = 0;
		while(hi.txData().size() < _words)
		{
			if(m_useJit) m_dsp->execJit();
			else m_dsp->execInterpreter();
			if(debug && m_dsp->getInstructionCounter() > nextReport)
			{
				std::fprintf(stderr, "  PC=$%06x instr=%llu tx=%zu\n", m_dsp->getPC().toWord(),
					static_cast<unsigned long long>(m_dsp->getInstructionCounter() - start), hi.txData().size());
				nextReport += 5'000'000;
			}
			if(++dispatches > _maxInstructions || m_dsp->getInstructionCounter() - start > _maxInstructions)
			{
				std::ostringstream o;
				o << "instruction budget exceeded at PC=$" << std::hex << m_dsp->getPC().toWord() << std::dec
				  << " with " << hi.txData().size() << "/" << _words << " words";
				m_fault = o.str();
				return false;
			}
		}
		return true;
	}

	// One block from the DSP, voice by voice: a flag word (1 = it rendered, its 32 samples follow; 0 = idle, all zeros).
	// Every voice's slot is read before its flag goes out, so once the 16th flag is in the DSP has consumed every slot.
	bool VoiceEngine::readBlock(Block& _out, const uint64_t _maxInstructions)
	{
		auto& hi = m_periphX->getHI08();
		m_lastActive = 0;
		uint64_t prev = m_dsp->getInstructionCounter();
		int idx = 0;
		for(auto& voice : _out)
		{
			if(!runUntilTx(1, _maxInstructions))
				return false;
			// The DSP reaches voice k's flag after finishing voice k-1's render: the instructions between two flags are
			// the previous voice's cost (an idle voice costs almost nothing).
			{
				const uint64_t now = m_dsp->getInstructionCounter();
				if(idx > 0) m_voiceInstr[static_cast<size_t>(idx - 1)] = static_cast<uint32_t>(now - prev);
				prev = now;
			}
			const TWord flag = hi.readTX() & 0xffffff;
			if(flag == 1) ++m_lastActive;
			if(flag != 1)
			{
				voice.fill(0);
				++idx;
				continue;
			}
			if(!runUntilTx(voice.size(), _maxInstructions))
				return false;
			for(auto& s : voice)
			{
				const TWord t = hi.readTX() & 0xffffff;
				s = static_cast<int32_t>(t << 8) >> 8;
			}
			++idx;
		}
		m_voiceInstr[kVoices - 1] = static_cast<uint32_t>(m_dsp->getInstructionCounter() - prev);
		return true;
	}

	bool VoiceEngine::renderBlock(Block& _out)
	{
		auto& hi = m_periphX->getHI08();
		const TWord go = 1;
		hi.writeRX(&go, 1);
		const auto start = m_dsp->getInstructionCounter();
		if(!readBlock(_out, kMaxInstrBlock))
			return false;
		m_lastInstructions = m_dsp->getInstructionCounter() - start;
		return true;
	}
}
