// DSP56300 fixed-point arithmetic, for native code that must match the Machinedrum's DSP code bit for bit.
// Accumulators are 56-bit two's complement (A2:A1:A0) held right-aligned in int64_t and kept sign-extended;
// data registers and memory words are 24-bit, held sign-extended in int32_t. No scaling mode, no rounding
// unless an instruction asks for it.
#pragma once
#include <cstdint>

namespace md::engine::d56
{
	using Acc = int64_t;

	constexpr int32_t sx24(const uint32_t _w) { return static_cast<int32_t>(_w << 8) >> 8; }
	constexpr uint32_t u24(const int32_t _v) { return static_cast<uint32_t>(_v) & 0xffffff; }
	constexpr Acc wrap(const Acc _a) { return static_cast<Acc>(static_cast<uint64_t>(_a) << 8) >> 8; }

	// move x,a: a 24-bit word into A1, A0 cleared, A2 sign extension
	constexpr Acc acc(const int32_t _v) { return static_cast<Acc>(_v) * (Acc(1) << 24); }
	// A1:A0 as a 48-bit long word (move y,a / l: moves)
	constexpr Acc acc48(const int32_t _hi, const uint32_t _lo) { return acc(_hi) + (_lo & 0xffffff); }

	// move a,x: A1 through the limiter (saturates when A2 is in use)
	constexpr int32_t lim(const Acc _a)
	{
		if(_a > 0x7fffffffffffLL) return 0x7fffff;
		if(_a < -0x800000000000LL) return -0x800000;
		return static_cast<int32_t>(_a >> 24);
	}
	constexpr int32_t a1(const Acc _a) { return sx24(static_cast<uint32_t>(_a >> 24)); }	// move a1,x: no limiter
	constexpr uint32_t a0(const Acc _a) { return static_cast<uint32_t>(_a) & 0xffffff; }	// move a0,x

	// mpy s1,s2,d (fractional: product shifted left one)
	constexpr Acc mpy(const int32_t _s1, const int32_t _s2) { return wrap(static_cast<Acc>(_s1) * _s2 * 2); }
	// mpysu / macsu: s1 signed, s2 unsigned; mpyuu: both unsigned
	constexpr Acc mpysu(const int32_t _s1, const int32_t _s2) { return wrap(static_cast<Acc>(_s1) * static_cast<Acc>(u24(_s2)) * 2); }
	constexpr Acc mpyuu(const int32_t _s1, const int32_t _s2) { return wrap(static_cast<Acc>(u24(_s1)) * static_cast<Acc>(u24(_s2)) * 2); }
	// dmac: d = (d >> 24) + product
	constexpr Acc dmac(const Acc _d, const Acc _product) { return wrap((_d >> 24) + _product); }

	constexpr Acc add(const Acc _a, const Acc _b) { return wrap(_a + _b); }
	constexpr Acc sub(const Acc _a, const Acc _b) { return wrap(_a - _b); }
	constexpr Acc asl(const Acc _a, const int _n) { return wrap(static_cast<Acc>(static_cast<uint64_t>(_a) << _n)); }
	constexpr Acc asr(const Acc _a, const int _n) { return _a >> _n; }

	// move #xx,a (8-bit immediate): into the top byte of A1, sign extended
	constexpr Acc accShortImm(const uint8_t _i) { return static_cast<Acc>(static_cast<int8_t>(_i)) * (Acc(1) << 40); }

	// clb s,d: the shift that normalises s (0 = normalised, negative = shift left), as a 24-bit value
	inline int32_t clb(const Acc _s)
	{
		if(_s == 0) return 0;
		const auto shifted = static_cast<int64_t>(static_cast<uint64_t>(_s) << 8);
		uint64_t v = static_cast<uint64_t>(shifted < 0 ? ~shifted : shifted) | 0xff;
		int bsr = 63;
		while(!(v & (uint64_t(1) << bsr))) --bsr;
		return bsr - 54;
	}
	// normf s,d: asr d by s when s >= 0, else asl by -s
	inline Acc normf(const int32_t _s, const Acc _d) { return _s >= 0 ? asr(_d, _s) : asl(_d, -_s); }

	// div s,d: one non-restoring division step; carry in/out through _c
	inline Acc div(const Acc _d, const int32_t _s, bool& _c)
	{
		const bool change = (_d < 0) != (_s < 0);
		const Acc shifted = wrap(static_cast<Acc>((static_cast<uint64_t>(_d) << 1) | (_c ? 1u : 0u)));
		const Acc r = change ? add(shifted, acc(_s)) : sub(shifted, acc(_s));
		_c = r >= 0;
		return r;
	}

	// Modulo addressing r = r + n with modifier m (m = size - 1, size a power of two here)
	constexpr uint32_t modAdd(const uint32_t _r, const int32_t _n, const uint32_t _m)
	{
		const uint32_t base = _r & ~_m;
		return base | ((_r + static_cast<uint32_t>(_n)) & _m);
	}
}
