#pragma once

#include <vector>
#include <functional>
#include <atomic>

#include "opcodetypes.h"
#include "types.h"
#include "dsp56kBase/ringbuffer.h"
#include "utils.h"
#include "dma.h"

namespace dsp56k
{
	class IPeripherals;
	class Disassembler;

	class HDI08
	{
	public:
		explicit HDI08(IPeripherals& _peripheral);

		enum Addresses
		{
			HCR		= 0xFFFFC2,					// Host Control Register (HCR)
			HSR		= 0xFFFFC3,					// Host Status Register (HSR)
			HPCR	= 0xFFFFC4,					// Host Port Control Register (HPCR)
			HBAR	= 0xFFFFC5,					// Host Base Address Register (HBAR)
			HORX	= 0xFFFFC6,					// Host Receive Register (HORX)
			HOTX	= 0xFFFFC7,					// Host Transmit Register (HOTX)
			HDDR	= 0xFFFFC8,					// Host Data Direction Register (HDDR)
			HDR		= 0xFFFFC9					// Host Data Register (HDR)
		};

		enum HostStatusRegisterBits
		{
			HSR_HRDF,						// Receive Data Full
			HSR_HTDE,						// Transmit Data Empty
			HSR_HCP,						// Host Command Pending
			HSR_HF0,						// Host Flag 0
			HSR_HF1,						// Host Flag 1
			HSR_DMA = 7,					// DMA Status
		};

		enum HostPortControlRegisterBits
		{
			HPCR_HEN = 6,					// HostEnable
		};
		
		enum HostControlRegisterBits
		{
			HCR_HRIE,					// Host Receive Interrupt Enable
			HCR_HTIE,					// Host Transmit Interrupt Enable
			HCR_HCIE,					// Host Command Interrupt Enable
			HCR_HF2,					// HCR Host Flags 2,3 (HF2,HF3) Bits 3-4
			HCR_HF3,
			HCR_HDM0,					// HCR Host DMA Mode Control Bits (HDM0, HDM1, HDM2) Bits 5-7
			HCR_HDM1,
			HCR_HDM2,
		};

		using CallbackTx = std::function<void()>;
		using CallbackHostPumpWake = std::function<void()>;
		using CallbackRx = std::function<void()>;
		using CallbackHostStateChanged = std::function<void()>;

		TWord readStatusRegister();

		TWord readControlRegister() const
		{
			return m_hcr.load(std::memory_order_acquire);
		}

		TWord readPortControlRegister() const
		{
			return m_hpcr;
		}

		void writeControlRegister(TWord _val);

		void writeStatusRegister(const TWord _val);

		void writePortControlRegister(const TWord _val);

		bool hasTX() const
		{
			return !m_dataTX.empty();
		}
		TWord readTX();
		void writeTX(TWord _val);

		uint32_t exec() noexcept;

		TWord readRX(Instruction _inst);

		void writeRX(const std::vector<TWord>& _data)		{ writeRX(_data.data(), _data.size()); }
		void writeRX(const TWord* _data, size_t _count);
		void clearRX();

		const auto& rxData() const { return m_dataRX; }
		const auto& txData() const { return m_dataTX; }

		bool hasRXData() const {return !m_dataRX.empty();}

		void setPendingHostFlags01(uint32_t _pendingHostFlags)
		{
			m_pendingHostFlags01.store(static_cast<int32_t>(_pendingHostFlags), std::memory_order_release);
		}

		bool hasPendingHostFlags01() const
		{
			return m_pendingHostFlags01.load(std::memory_order_acquire) >= 0;
		}

		void setHostFlags(uint8_t _flag0, uint8_t _flag1);
		void setHostFlagsWithWait(uint8_t _flag0, uint8_t _flag1);
		bool needsToWaitForHostFlags(uint8_t _flag0, uint8_t _flag1) const;
		void waitUntilBufferEmpty() const;

		void reset();

		bool dataRXFull() const;

		void terminate();

		TWord readHDR() const;
		void writeHDR(TWord _val);

		TWord readHDDR() const;
		void writeHDDR(TWord _val);

		void setTransmitDataAlwaysEmpty(bool _alwaysEmpty)
		{
			m_transmitDataAlwaysEmpty = _alwaysEmpty;
		}

		// Buffered TX queues words up to the ring capacity. The default retains
		// the original single-latch replacement behavior.
		void setTransmitDataBuffered(bool _buffered)
		{
			m_transmitDataBuffered = _buffered;
		}

		static void setSymbols(Disassembler& _disasm);

		void injectTXInterrupt();

		bool txInterruptEnabled() const
		{
			return dsp56k::bittest<TWord, HCR_HTIE>(m_hcr.load(std::memory_order_acquire));
		}

		bool rxInterruptEnabled() const
		{
			return dsp56k::bittest<TWord, HCR_HRIE>(m_hcr.load(std::memory_order_acquire));
		}

		// Called after every HOTX write, including replacement of a full latch.
		void setWriteTxCallback(const CallbackTx& _callback)
		{
			m_callbackTx = _callback;
		}

		void setHostPumpWakeCallback(const CallbackHostPumpWake& _callback)
		{
			m_callbackHostPumpWake = _callback;
		}

		void setRXRateLimit(uint32_t _rateLimit)
		{
			m_rxRateLimit = _rateLimit;
		}

		void setReadRxCallback(const CallbackRx& _callback)
		{
			m_callbackRx = _callback;

			if(!m_callbackRx)
				m_callbackRx = [] {};
		}

		void setHostStateChangedCallback(const CallbackHostStateChanged& _callback)
		{
			m_callbackHostStateChanged = _callback;

			if(!m_callbackHostStateChanged)
				m_callbackHostStateChanged = [] {};
		}

		// Optional DSP56303 host-port arbitration. Disabled by default. It
		// preserves command priority, retained HRX reads, and native CVR command
		// dispatch across an asynchronous host/DSP boundary.
		void setHostCommandArbitration(bool _enable);

		// Queue a CVR host command for normal interrupt dispatch.
		void writeHostCommand(TWord _vba);

		// Notify the port when an interrupt vector is serviced.
		void onInterruptDispatched(TWord _vba);

		bool hostCommandArbitration() const { return m_hostCommandArbitration; }

		// HC clears on acceptance, not on interrupt return. A command queued
		// behind an active handler has not been accepted yet either.
		bool hostCommandPending() const
		{
			return m_hostCommandArbitration &&
				(m_hostCommandPending.load(std::memory_order_acquire) ||
				 m_hostCommandHasQueued.load(std::memory_order_acquire));
		}
		uint64_t hostCommandAcceptedCycle() const
		{
			return m_hostCommandAcceptedCycle.load(std::memory_order_acquire);
		}

		// A command remains busy through interrupt return.
		bool hostCommandBusy() const
		{
			return m_hostCommandArbitration &&
				(m_hostCommandPending.load(std::memory_order_acquire) ||
				 m_hostCommandInFlight.load(std::memory_order_acquire));
		}

	private:
		// Suppress mainline HRX consumption while a command can preempt it.
		bool hostCommandHoldActive() const;

		// Detect interrupt return and release a queued command.
		void pollHostCommandCompletion();

		// Publish and inject a host-command vector.
		void dispatchHostCommandNow(TWord _vba);

		bool dmaTriggerReceive() const;
		bool dmaTriggerTransmit() const;
		bool hasDmaReceiveTrigger() const;

		TWord m_hsr = 0;
		std::atomic<TWord> m_hcr{0};
		TWord m_hpcr = 0;
		RingBuffer<TWord, 8192, true> m_dataRX;
		RingBuffer<TWord, 8192, true> m_dataTX;
		IPeripherals& m_periph;
		std::atomic<uint32_t> m_pendingTXInterrupts;
		uint64_t m_lastRXClock = 0;
		TWord m_hdr = 0;
		TWord m_hddr = 0;
		bool m_transmitDataAlwaysEmpty = true;
		bool m_transmitDataBuffered = false;
		CallbackTx m_callbackTx;
		CallbackHostPumpWake m_callbackHostPumpWake;
		CallbackRx m_callbackRx = [] {};
		CallbackHostStateChanged m_callbackHostStateChanged = [] {};
		uint32_t m_rxRateLimit;		// minimum number of instructions between two RX interrupts
		bool m_waitServeRXInterrupt = false;
		std::atomic<int32_t> m_pendingHostFlags01{-1};

		DmaChannel::RequestSource m_dmaReqSourceReceive;
		DmaChannel::RequestSource m_dmaReqSourceTransmit;
		Dma* m_dma;

		// Host-port arbitration state shared by the host and DSP threads.
		bool	m_hostCommandArbitration = false;	// config gate (A1-A4)
		std::atomic<bool>	m_hostCommandPending{false};	// A2: HCP/HRDF hold, A4: command armed
		std::atomic<TWord>	m_hostCommandVba{0};			// A2/A4: the pending host-command vector (2xHV)
		TWord	m_lastRXValue = 0;					// A3: last valid word presented in HRX

		// Completion state is DSP-thread-only; the phase and one queued command
		// are shared atomically with the host thread.
		TWord	m_hcReturnSsIndex = 0;				// stack index at dispatch; handler has returned at <=
		bool	m_hcEntered = false;				// observed the handler raise the stack (long interrupt)
		std::atomic<bool>	m_hostCommandInFlight{false};	// command vector dispatched; handler not yet returned
		std::atomic<bool>	m_hostCommandHasQueued{false};	// a second command is waiting behind the in-flight one
		std::atomic<TWord>	m_hostCommandQueuedVba{0};
		std::atomic<uint64_t> m_hostCommandAcceptedCycle{0};
	};
}
