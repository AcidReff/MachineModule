#include "hdi08.h"

#include <iostream>

int testReceiveOrder(const bool littleEndian)
{
	mc68k::Hdi08 hdi;
	hdi.icr(littleEndian ? mc68k::Hdi08::Hlend : 0);
	hdi.setRxEmptyCallback([&](bool) { hdi.relatchRx(); });
	bool queuedSecond = false;
	hdi.setReadIsrCallback([&](uint8_t status)
	{
		// The DSP-side status callback can deliver another word synchronously.
		if(!queuedSecond)
		{
			queuedSecond = true;
			hdi.writeRx(0xbb3344);
		}
		return status;
	});
	hdi.writeRx(0xaa1122);
	auto readWord = [&]()
	{
		if(littleEndian)
		{
			const auto high = hdi.read8(mc68k::PeriphAddress::HdiTXL);
			const auto middle = hdi.read8(mc68k::PeriphAddress::HdiTXM);
			const auto low = hdi.read8(mc68k::PeriphAddress::HdiTXH);
			return (static_cast<uint32_t>(high) << 16)
				| (static_cast<uint32_t>(middle) << 8) | low;
		}
		const auto high = hdi.read16(mc68k::PeriphAddress::HdiUnused4);
		const auto low = hdi.read16(mc68k::PeriphAddress::HdiTXM);
		return (static_cast<uint32_t>(high) << 16) | low;
	};
	const auto first = readWord();
	const auto second = readWord();
	if(first != 0xaa1122 || second != 0xbb3344)
	{
		std::cerr << "receive callback reordered/lost words: " << std::hex
			<< first << ", " << second << '\n';
		return 1;
	}
	if(hdi.hostRxWordsAvailable() != 0)
		return 1;
	return 0;
}

int main()
{
	{
		mc68k::Hdi08 port;
		unsigned reads = 0, commands = 0;
		port.setWriteIrqCallback([&](uint8_t vector) { if(vector == 0x16) ++commands; });
		port.setReadCvrCallback([&](uint8_t value) { ++reads; return uint8_t(value | mc68k::Hdi08::Hc); });
		port.write8(mc68k::PeriphAddress::HdiCVR, 0x8b);
		if(commands != 1 || reads != 0
			|| port.read8(mc68k::PeriphAddress::HdiCVR) != 0x8b
			|| (port.read16(mc68k::PeriphAddress::HdiICR) & 0xff) != 0x8b || reads != 2)
		{
			std::cerr << "CVR callback not preserved across byte/word reads or reentered on write\n";
			return 1;
		}
		port.setReadCvrCallback(nullptr);
		if(port.read8(mc68k::PeriphAddress::HdiCVR) != 0x0b) return 1;
	}
	if(testReceiveOrder(false) || testReceiveOrder(true))
		return 1;
	std::cout << "HI08 receive callback preserves FIFO order in both byte orders\n";
	return 0;
}
