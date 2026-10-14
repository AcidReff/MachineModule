#include "dsp.h"
#include "interrupts.h"
#include "hdi08.h"

namespace dsp56k
{
	bool is56303(IPeripherals& _peripherals)
	{
		return dynamic_cast<Peripherals56303*>(&_peripherals) != nullptr;
	}

	Dma* getDma(IPeripherals& _peripherals)
	{
		if(auto* p = dynamic_cast<Peripherals56303*>(&_peripherals))
			return &p->getDMA();

		if(auto* p = dynamic_cast<Peripherals56362*>(&_peripherals))
			return &p->getDMA();

		return nullptr;
	}

	HDI08::HDI08(IPeripherals& _peripheral)
	: m_periph(_peripheral)
	, m_pendingTXInterrupts(0)
	, m_rxRateLimit(200)
	, m_dmaReqSourceReceive (is56303(_peripheral) ? DmaChannel::RequestSource::Hi08ReceiveDataFull : DmaChannel::RequestSource::HostReceiveData)
	, m_dmaReqSourceTransmit(is56303(_peripheral) ? DmaChannel::RequestSource::Hi08TransmitDataEmpty : DmaChannel::RequestSource::HostTransmitData)
	, m_dma(getDma(_peripheral))
	{
	}

	bool HDI08::hostCommandHoldActive() const
	{
		if(!m_hostCommandArbitration || !m_hostCommandPending.load(std::memory_order_acquire))
			return false;

		// The hold suppresses only mainline execution. Interrupt handlers already
		// suspend the mainline and must retain access to their own input data
		// (DSP56303UM section 6.6.1).
		if(m_periph.getDSP().getProcessingMode() != DSP::Default)
			return false;

		// A masked command waits without holding HRX, avoiding a deadlock against
		// a command that cannot yet dispatch.
		return !m_periph.getDSP().isInterruptMasked(m_hostCommandVba.load(std::memory_order_relaxed));
	}

	TWord HDI08::readStatusRegister()
	{
		pollHostCommandCompletion();

		// A dispatchable host command takes priority over a mainline HRX read.
		const bool hrdf = !m_dataRX.empty() && !hostCommandHoldActive();
		dsp56k::bitset<TWord, HSR_HRDF>(m_hsr, hrdf ? 1 : 0);

		// Derive HCP on the DSP thread to avoid a cross-thread HSR update.
		if(m_hostCommandArbitration)
			dsp56k::bitset<TWord, HSR_HCP>(m_hsr, m_hostCommandPending.load(std::memory_order_acquire) ? 1 : 0);

		// Apply pending host flags, if applicable
		const auto hf01 = m_pendingHostFlags01.load(std::memory_order_acquire);

		if(hf01 < 0)
			return m_hsr;

		m_pendingHostFlags01.store(-1, std::memory_order_release);

		m_hsr &= ~0x18;
		m_hsr |= static_cast<uint32_t>(hf01);

		m_callbackHostStateChanged();

		return m_hsr;
	}

	void HDI08::setHostCommandArbitration(const bool _enable)
	{
		m_hostCommandArbitration = _enable;

		// Reset the serializer when arbitration is reconfigured.
		m_hostCommandAcceptedCycle.store(0, std::memory_order_release);
		m_hostCommandPending.store(false, std::memory_order_release);
		m_hostCommandInFlight.store(false, std::memory_order_release);
		m_hostCommandHasQueued.store(false, std::memory_order_release);
		m_hcEntered = false;

		if(_enable)
		{
			m_periph.getDSP().setInterruptServicedCallback([this](const TWord _vba)
			{
				onInterruptDispatched(_vba);
			});
		}
	}

	void HDI08::writeHostCommand(const TWord _vba)
	{
		// A CVR write raises HCP and dispatches through the normal interrupt path
		// (DSP56303UM sections 6.6.1 and 6.7.2).
		if(!m_hostCommandArbitration)
		{
			m_periph.getDSP().injectExternalInterrupt(_vba);
			return;
		}

		// Serialize one additional command while a previous command is busy.
		if(hostCommandBusy())
		{
			// Publish the value before its availability flag.
			m_hostCommandQueuedVba.store(_vba, std::memory_order_relaxed);
			m_hostCommandHasQueued.store(true, std::memory_order_release);
			return;
		}

		dispatchHostCommandNow(_vba);
	}

	void HDI08::dispatchHostCommandNow(const TWord _vba)
	{
		// Publish the vector value before the pending flag and before interrupt
		// injection, so the DSP thread observes a coherent command state. HCP is
		// derived on the DSP thread to avoid a cross-thread read-modify-write.
		m_hostCommandVba.store(_vba, std::memory_order_relaxed);
		m_hostCommandPending.store(true, std::memory_order_release);
		m_periph.getDSP().injectExternalInterrupt(_vba);
	}

	void HDI08::onInterruptDispatched(const TWord _vba)
	{
		if(!m_hostCommandPending.load(std::memory_order_acquire) ||
			_vba != m_hostCommandVba.load(std::memory_order_relaxed))
			return;
		// Servicing the vector clears HCP and moves the command to in-flight.
		// The stack level provides an on-thread interrupt-return signal.
		m_hostCommandAcceptedCycle.store(m_periph.getDSP().getCycles(), std::memory_order_release);
		m_hostCommandPending.store(false, std::memory_order_release);

		m_hcReturnSsIndex = m_periph.getDSP().ssIndex();
		m_hcEntered = false;
		m_hostCommandInFlight.store(true, std::memory_order_release);
	}

	void HDI08::pollHostCommandCompletion()
	{
		if(!m_hostCommandInFlight.load(std::memory_order_relaxed))
			return;

		auto& dsp = m_periph.getDSP();
		const auto mode = dsp.getProcessingMode();
		const auto ss = dsp.ssIndex();

		if(!m_hcEntered)
		{
			// A raised stack confirms entry into a long interrupt.
			if(ss > m_hcReturnSsIndex)
			{
				m_hcEntered = true;
			}
			else if(mode == DSP::Default)
			{
				// A short interrupt completes within execInterrupt.
				m_hostCommandInFlight.store(false, std::memory_order_release);
				if(m_hostCommandHasQueued.load(std::memory_order_acquire))
				{
					m_hostCommandHasQueued.store(false, std::memory_order_relaxed);
					dispatchHostCommandNow(m_hostCommandQueuedVba.load(std::memory_order_relaxed));
				}
			}
			return;
		}

		// A long interrupt completes at its original stack level in default mode.
		if(mode == DSP::Default && ss <= m_hcReturnSsIndex)
		{
			m_hostCommandInFlight.store(false, std::memory_order_release);
			if(m_hostCommandHasQueued)
			{
				m_hostCommandHasQueued = false;
				dispatchHostCommandNow(m_hostCommandQueuedVba);
			}
		}
	}

	uint32_t HDI08::exec() noexcept
	{
		DSP_PROBE_SCOPE(2, Hdi08, m_periph.getDSP().getProbeId());
		pollHostCommandCompletion();

		if (!bittest(m_hpcr, HPCR_HEN))
			return IPeripherals::MaxDelayCycles;

		// DMA service is cycle-stealing and is not subject to the RX interrupt
		// rate limit (DSP56300FM section 10).
		if(m_hostCommandArbitration && !m_dataRX.empty() && hasDmaReceiveTrigger() && !rxInterruptEnabled())
		{
			dmaTriggerReceive();
			return m_dataRX.empty() ? IPeripherals::MaxDelayCycles : 0;
		}

		if (!m_waitServeRXInterrupt && !m_dataRX.empty() && (rxInterruptEnabled() || hasDmaReceiveTrigger()))
		{
			const auto clock = m_periph.getDSP().getInstructionCounter();

			const auto d = clock - m_lastRXClock;
			if(d >= m_rxRateLimit)
			{
				if(rxInterruptEnabled())
					m_periph.getDSP().injectInterrupt(Vba_Host_Receive_Data_Full);
				m_lastRXClock = clock;
				m_waitServeRXInterrupt = true;
				dmaTriggerReceive();
//				LOG("Wait serve interrupt");
				return 0;
			}
			return static_cast<uint32_t>(m_rxRateLimit - d);
		}
		if(m_transmitDataAlwaysEmpty)
		{
			if (m_pendingTXInterrupts > 0)
			{
				const auto interruptEnabled = txInterruptEnabled();
				const auto dmaTriggered = dmaTriggerTransmit();

				if(interruptEnabled || dmaTriggered)
				{
					--m_pendingTXInterrupts;
					dsp56k::bitset<TWord, HSR_HTDE>(m_hsr, 1);

					if(interruptEnabled)
						m_periph.getDSP().injectInterrupt(Vba_Host_Transmit_Data_Empty);
				}
//				LOG("HTDE=1");
			}
		}
		else
		{
			if (m_dataTX.empty())
			{
				const auto hadHTDE = bittest(m_hsr, HSR_HTDE);
				const auto injectInterrupt = txInterruptEnabled() && !hadHTDE;
				dsp56k::bitset<TWord, HSR_HTDE>(m_hsr, 1);
//				if(!hadHTDE)
//					LOG("HTDE=1");
				if (injectInterrupt)
				{
//					LOG("Inject HTDE");
					m_periph.getDSP().injectInterrupt(Vba_Host_Transmit_Data_Empty);
				}
				if(!hadHTDE)
				{
					dmaTriggerTransmit();
				}
			}
		}

		return IPeripherals::MaxDelayCycles;
	}

	TWord HDI08::readRX(const Instruction _inst)
	{
		m_periph.setDelayCycles(0);

		pollHostCommandCompletion();

		// Preserve the retained HRX value while a command has priority.
		if(hostCommandHoldActive())
			return m_lastRXValue;

		if (m_dataRX.empty())
		{
			LOG("Empty read, PC=" << HEX(m_periph.getDSP().getPC().toWord()) << ", processingMode=" << m_periph.getDSP().getProcessingMode());
			m_waitServeRXInterrupt = false;
			// Under arbitration, an empty HRX read returns its retained value.
			return m_hostCommandArbitration ? m_lastRXValue : 0;
		}

		TWord res;

		switch (_inst)
		{
		case Btst_pp:
		case Btst_D:
		case Btst_qq:
		case Btst_ea:
		case Btst_aa:
			res = m_dataRX.front();
//			LOG("HDI08 RX = " << HEX(res) << " (non-pop because op is bit test)");
			break;
		default:
			res = m_dataRX.pop_front();
			m_waitServeRXInterrupt = false;
			m_callbackRx();
//			LOG("HDI08 RX = " << HEX(res) << " (pop)");
			break;
		}

		m_lastRXValue = res;	// A3: remember the value presented in HRX
		return res;
	}

	void HDI08::writeRX(const TWord* _data, const size_t _count)
	{
		for (size_t i = 0; i < _count; ++i)
		{
			m_dataRX.waitNotFull();
			const auto d = _data[i] & 0x00ffffff;
//			LOG("Write RX: " << HEX(d));
			m_dataRX.push_back(d);
		}
		m_periph.setDelayCycles(0);
	}

	void HDI08::clearRX()
	{
		m_dataRX.clear();
		m_periph.setDelayCycles(0);
	}

	void HDI08::setHostFlags(const uint8_t _flag0, const uint8_t _flag1)
	{
		m_pendingHostFlags01.store(-1, std::memory_order_release);

		dsp56k::bitset<TWord, HSR_HF0>(m_hsr, _flag0);
		dsp56k::bitset<TWord, HSR_HF1>(m_hsr, _flag1);

//		LOG("Write HostFlags, HSR " << HEX(m_hsr));
	}

	void HDI08::setHostFlagsWithWait(const uint8_t _flag0, const uint8_t _flag1)
	{
		const auto hsr = m_hsr;
		const auto target = (_flag0 ? 1:0) | (_flag1 ? 2:0);
		if (((hsr>>3)&3) == target) 
			return;
		waitUntilBufferEmpty();
		setHostFlags(_flag0, _flag1);
	}

	void HDI08::waitUntilBufferEmpty() const
	{
		while (hasRXData())
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	}

	bool HDI08::needsToWaitForHostFlags(const uint8_t _flag0, const uint8_t _flag1) const
	{
		const auto target = (_flag0?1:0) | (_flag1?2:0);
		const auto hsr = m_hsr;
		if (((hsr>>3)&3)==target) 
			return false;
		return hasRXData();
	}

	void HDI08::reset()
	{
		m_hcr.store(0, std::memory_order_relaxed);
		m_hpcr = 0;
		m_hsr = 0;
		bitset<TWord, HSR_HTDE>(m_hsr, 1);
		m_hddr = 0;
		// m_hdr is not affected by reset
	}

	bool HDI08::dataRXFull() const
	{
		return m_dataRX.full();
	}

	void HDI08::terminate()
	{
		while(!m_dataRX.full())
			m_dataRX.push_back(0);
	}

	TWord HDI08::readHDR() const
	{
//		LOG("Read HDR: " << HEX(m_hdr));
		return m_hdr;
	}

	void HDI08::writeHDR(TWord _val)
	{
//		LOG("Write HDR: " << HEX(_val));
		m_hdr = _val;
	}

	TWord HDI08::readHDDR() const
	{
		LOG_DIAGNOSTIC("Read HDDR: " << HEX(m_hdr));
		return m_hddr;
	}

	void HDI08::writeHDDR(TWord _val)
	{
		LOG_DIAGNOSTIC("Write HDDR: " << HEX(_val));
		m_hddr = _val;
	}

	void HDI08::setSymbols(Disassembler& _disasm)
	{
		constexpr std::pair<int,const char*> symbols[] =
		{
			// HDI08
			{HCR,	"M_HCR"},
			{HSR,	"M_HSR"},
			{HPCR,	"M_HPCR"},
			{HBAR,	"M_HBAR"},
			{HORX,	"M_HORX"},
			{HOTX,	"M_HOTX"},
			{HDDR,	"M_HDDR"},
			{HDR,	"M_HDR"}
		};

		for (const auto& symbol : symbols)
			_disasm.addSymbol(Disassembler::MemX, symbol.first, symbol.second);

		_disasm.addBitSymbol(Disassembler::MemX, HSR, HSR_HRDF, "HSR_HRDF");
		_disasm.addBitSymbol(Disassembler::MemX, HSR, HSR_HTDE, "HSR_HTDE");
		_disasm.addBitSymbol(Disassembler::MemX, HSR, HSR_HCP , "HSR_HCP");
		_disasm.addBitSymbol(Disassembler::MemX, HSR, HSR_HF0 , "HSR_HF0");
		_disasm.addBitSymbol(Disassembler::MemX, HSR, HSR_HF1 , "HSR_HF1");
		_disasm.addBitSymbol(Disassembler::MemX, HSR, HSR_DMA , "HSR_DMA");

		_disasm.addBitSymbol(Disassembler::MemX, HPCR, HPCR_HEN, "HPCR_HEN");

		_disasm.addBitSymbol(Disassembler::MemX, HCR, HCR_HRIE, "HCR_HRIE");
		_disasm.addBitSymbol(Disassembler::MemX, HCR, HCR_HTIE, "HCR_HTIE");
		_disasm.addBitSymbol(Disassembler::MemX, HCR, HCR_HCIE, "HCR_HCIE");

		_disasm.addBitSymbol(Disassembler::MemX, HCR, HCR_HF2, "HCR_HF2");
		_disasm.addBitSymbol(Disassembler::MemX, HCR, HCR_HF3, "HCR_HF3");

		_disasm.addBitSymbol(Disassembler::MemX, HCR, HCR_HDM0, "HCR_HDM0");
		_disasm.addBitSymbol(Disassembler::MemX, HCR, HCR_HDM1, "HCR_HDM1");
		_disasm.addBitSymbol(Disassembler::MemX, HCR, HCR_HDM2, "HCR_HDM2");

		_disasm.addSymbol(Disassembler::MemP, Vba_Host_Receive_Data_Full, "int_hdi08_receiveDataFull");
		_disasm.addSymbol(Disassembler::MemP, Vba_Host_Command, "int_hdi08_hostCommand");
		_disasm.addSymbol(Disassembler::MemP, Vba_Host_Transmit_Data_Empty, "int_hdi08_transmitDataEmpty");
	}

	void HDI08::injectTXInterrupt()
	{
		++m_pendingTXInterrupts;
	}

	uint32_t HDI08::readTX()
	{
		m_dataTX.waitNotEmpty();
		if(!m_transmitDataAlwaysEmpty)
			m_periph.setDelayCycles(0);
		return m_dataTX.pop_front();
	}

	void HDI08::writeTX(const TWord _val)
	{
		if(!m_transmitDataAlwaysEmpty && !m_dataTX.empty() && (!m_transmitDataBuffered || m_dataTX.full()))
		{
			m_dataTX.front() = _val;
			// A replacement is a new production event too. Host bridges must
			// observe it so the retained word cannot inherit an older timestamp.
			if(m_callbackTx)
				m_callbackTx();
			if(m_callbackHostPumpWake)
				m_callbackHostPumpWake();
			return;
		}

		m_dataTX.waitNotFull();
		m_dataTX.push_back(_val);

//		LOG("Write HDI08 HOTX " << HEX(_val));
//		LOG("HTDE=0");
		dsp56k::bitset<TWord, HSR_HTDE>(m_hsr, 0);

		if(m_transmitDataAlwaysEmpty)
			injectTXInterrupt();

		if(m_callbackTx)
			m_callbackTx();
		if(m_callbackHostPumpWake)
			m_callbackHostPumpWake();

		m_periph.setDelayCycles(0);
	}

	void HDI08::writeControlRegister(TWord _val)
	{
//		LOG("Write HDI08 HCR " << HEX(_val) << ", pc=" << HEX(m_periph.getDSP().getPC().toWord()));

		const auto hadTXInterrupt = txInterruptEnabled();
		const auto hadRXInterrupt = rxInterruptEnabled();
		m_hcr.store(_val, std::memory_order_release);

		m_callbackHostStateChanged();

		const auto hasTXInterrupt = txInterruptEnabled();
		const auto hasRXInterrupt = rxInterruptEnabled();

		if(!hadTXInterrupt && hasTXInterrupt && dsp56k::bittest<TWord, HSR_HTDE>(m_hsr))
		{
			if(m_transmitDataAlwaysEmpty)
				++m_pendingTXInterrupts;
			else
				dsp56k::bitset<TWord, HSR_HTDE>(m_hsr, 0);	// force inject
		}

		m_periph.setDelayCycles(0);

		return;

		if(!hadTXInterrupt && hasTXInterrupt)
		{
			LOG_DIAGNOSTIC("HTDE interrupt enabled");
		}
		else if(hadTXInterrupt && !hasTXInterrupt)
		{
			LOG_DIAGNOSTIC("HTDE interrupt disabled");
		}

		if(!hadRXInterrupt && hasRXInterrupt)
		{
			LOG_DIAGNOSTIC("RX interrupt enabled");
		}
		else if(hadRXInterrupt && !hasRXInterrupt)
		{
			LOG_DIAGNOSTIC("RX interrupt disabled");
		}
	}

	void HDI08::writeStatusRegister(const TWord _val)
	{
//		LOG("Write HDI08 HSR " << HEX(_val));
		m_hsr = _val;
		m_periph.setDelayCycles(0);
	}

	void HDI08::writePortControlRegister(const TWord _val)
	{
		LOG_DIAGNOSTIC("Write HDI08 HPCR " << HEX(_val));
		m_hpcr = _val;
		m_periph.setDelayCycles(0);
	}

	bool HDI08::dmaTriggerReceive() const
	{
		if(!m_dma)
			return false;
		return m_dma->trigger(m_dmaReqSourceReceive);
	}

	bool HDI08::dmaTriggerTransmit() const
	{
		if(!m_dma)
			return false;
		return m_dma->trigger(m_dmaReqSourceTransmit);
	}

	bool HDI08::hasDmaReceiveTrigger() const
	{
		if(!m_dma)
			return false;
		return m_dma->hasTrigger(m_dmaReqSourceReceive);
	}
};
