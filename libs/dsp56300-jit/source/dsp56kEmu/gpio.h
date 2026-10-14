#pragma once

#include <functional>

#include "types.h"

namespace dsp56k
{
	class Gpio
	{
	public:
		using CallbackDspWrite = std::function<void()>;
		using CallbackConfigChanged = std::function<void()>;

		virtual ~Gpio() = default;

		void dspWrite(const TWord _data)
		{
			if(_data == m_dspWrite)
				return;

			m_dspWrite = _data;
			m_callbackDspWrite();
		}

		void hostWrite(const TWord _data)
		{
			m_hostWrite = _data;
		}

		TWord dspRead() const
		{
			// reading the data register returns the pin state: the host-driven level for
			// input pins, the DSP's own output latch for output pins
			const auto hostIn = m_hostInputSource ? m_hostInputSource() : m_hostWrite;
			return ((hostIn & ~m_direction) | (m_dspWrite & m_direction)) & m_control;
		}

		TWord hostRead() const
		{
			// Host will only get a bit if configured as output
			return m_dspWrite & m_direction & m_control;
		}

		void setCallbackDspWrite(CallbackDspWrite&& _callback)
		{
			m_callbackDspWrite = std::move(_callback);
		}

		// External logic (e.g. a clock wired to an input pin) can supply the host-side pin
		// state dynamically instead of latching it via hostWrite. Called from the DSP thread.
		void setHostInputSource(std::function<TWord()>&& _source)
		{
			m_hostInputSource = std::move(_source);
		}

		void setCallbackDirChanged(CallbackConfigChanged&& _callback)
		{
			m_callbackConfigChanged = std::move(_callback);
		}

		const TWord& getDirection() const
		{
			return m_direction;
		}

		virtual void setDirection(const TWord _dir)
		{
			if(_dir == m_direction)
				return;
			m_direction = _dir;
			m_callbackConfigChanged();
		}

		virtual const TWord& getControl() const
		{
			return m_control;
		}

		virtual void setControl(TWord _control)
		{
			if(_control == m_control)
				return;
			m_control = _control;
			m_callbackConfigChanged();
		}

	protected:
		TWord m_direction = 0;	// bitmask, bit clear = Host to DSP, bit set = DSP to Host
		TWord m_control = 0;	// bitmask, bit enabled = GPIO enabled

	private:
		TWord m_dspWrite = 0;
		TWord m_hostWrite = 0;

		CallbackDspWrite m_callbackDspWrite = []{};
		CallbackConfigChanged m_callbackConfigChanged = []{};
		std::function<TWord()> m_hostInputSource;
	};

	class EssiPort final : public Gpio
	{
		/*
		 * DSP56303 ESSI ports C and D (PCRx/PRRx/PDRx):
		 * Port Control 0 and Port Direction 0 = GPIO input
		 * Port Control 0 and Port Direction 1 = GPIO output
		 * Port Control 1                      = pin is configured for ESSI
		 * i.e. the base class' "GPIO enabled" mask is the INVERSE of the control register.
		 */
	public:
		EssiPort()
		{
			// reset state: PCR = 0 = all pins are GPIO
			Gpio::setControl(0xffffff);
		}

		void setControl(const TWord _control) override
		{
			m_essiControl = _control;
			Gpio::setControl(~_control & 0xffffff);
		}

		const TWord& getControl() const override
		{
			return m_essiControl;
		}

	private:
		TWord m_essiControl = 0;
	};

	class EsaiPortC final : public Gpio
	{
		/*
		 * Port Control 0 and Port Direction 0 = GPIO disconnected
		 * Port Control 1 and Port Direction 0 = GPIO is input
		 * Port Control 0 and Port Direction 1 = GPIO is output
		 * Port Control 1 and Port Direction 1 = pin is configured for ESAI
		 */
	public:
		enum PortBits
		{
			SCKR,	// 0
			FSR,	// 1
			HCKR,	// 2
			SCKT,	// 3
			FST,	// 4
			HCKT,	// 5
			SDO5,	// 6
			SDO4,	// 7
			SDO3,	// 8
			SDO2,	// 9
			SDO1,	// 10
			SDO0,	// 11
		};

		void setControl(const TWord _control) override
		{
			m_esaiControl = _control;
			Gpio::setControl(m_esaiControl ^ getDirection());
		}

		void setDirection(const TWord _dir) override
		{
			m_direction = _dir;
			Gpio::setControl(m_esaiControl ^ _dir);
		}

		const TWord& getControl() const override
		{
			return m_esaiControl;
		}

	private:
		TWord m_esaiControl = 0;
	};
}
