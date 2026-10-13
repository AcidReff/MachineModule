#include "TrackFx.h"

#include "Dsp56.h"

namespace md::engine
{
	using namespace d56;

	namespace
	{
		constexpr uint32_t kSine = 0x148000;	// 32768 words, one sine period

		int32_t lim48hi(const Acc _a, uint32_t& _lo)
		{
			if(_a > 0x7fffffffffffLL) { _lo = 0xffffff; return 0x7fffff; }
			if(_a < -0x800000000000LL) { _lo = 0; return -0x800000; }
			_lo = a0(_a);
			return a1(_a);
		}
	}

	TrackFx::Tables::Tables(const fw::Firmware& _fw) : m_ext(0x10000, 0), m_xInt(0x800, 0)
	{
		for(const auto& r : _fw.dspB.records)
			for(size_t k = 0; k < r.words.size(); ++k)
			{
				const uint32_t a = r.addr + static_cast<uint32_t>(k);
				if(a >= kBase && a < kBase + 0x10000)
					m_ext[a - kBase] = sx24(r.words[k]);
				else if(r.space == fw::Space::X && a < 0x800)
					m_xInt[a] = sx24(r.words[k]);
			}

		// The boot code's sine: s[n+1] = 2c s[n] - s[n-1] in 48-bit double precision, c = cos(2 pi / 32768).
		int32_t hi[2] = {0, 0x648};
		uint32_t lo[2] = {0, 0x7ed46b};
		const int32_t cHi = 0x7fffff, cLo = sx24(0xd88586);
		uint32_t out = kSine - kBase;
		m_ext[out++] = 0;
		m_ext[out++] = 0x648;
		int r = 1;
		for(int n = 0; n < 0x7ffe; ++n)
		{
			const int32_t y1 = hi[r];
			const int32_t y0 = sx24(lo[r]);
			r ^= 1;
			Acc a = mpyuu(cLo, y0);
			a = dmac(a, mpysu(cHi, y0));
			a = add(a, mpysu(y1, cLo));
			a = dmac(a, mpy(cHi, y1));
			a = asl(a, 1);
			a = sub(a, acc48(hi[r], lo[r]));
			hi[r] = lim48hi(a, lo[r]);
			m_ext[out++] = lim(a);
		}
	}

	void TrackFx::init(State& _s)
	{
		_s.y.fill(0);
		_s.y[5] = 0x3fff;
		_s.y[1] = _s.y[2] = _s.y[3] = 0x2000;
		_s.y[0x25] = kSine;
	}

	void TrackFx::setParams(State& _s, const uint32_t* _fx9)
	{
		for(int k = 0; k < 9; ++k)
			_s.y[k] = sx24(_fx9[k]);
	}

	// P:$a4-$ba. Filter cutoffs and resonances from FLTF, FLTW, FLTQ.
	void TrackFx::prepFilter(State& _s)
	{
		auto& y = _s.y;
		Acc b = asr(acc(y[6]), 7);				// FLTQ
		Acc a = asr(acc(y[4]), 3);				// FLTF << 4
		const int32_t q = lim(b);
		if(a == 0) b = 0;
		y[0x22] = lim(a);
		y[0x23] = lim(b);
		b = asr(acc(y[5]), 3);					// FLTW << 4
		a = add(a, b);
		if(a > acc(0x7ff)) a = acc(0x7ff);
		y[0x24] = lim(a);
		a = acc(q);
		if(b >= acc(0x7ef)) a = 0;
		y[0x26] = lim(a);
	}

	// P:$bb-$d1. Amplitude modulation by the sine table: out = (1 - d) in + d (in x sin), d = AMD, rate AMF.
	// Output at X:$c-$2b (the DSP's X:$b gets a stale word).
	void TrackFx::amd(State& _s, const int32_t* _in)
	{
		auto& y = _s.y;
		auto& x = m_x;
		const int32_t n1 = lim(asr(acc(y[1]), 3));	// AMF << 4
		const Acc b0 = asl(acc(y[0]), 9);			// AMD << 16
		uint32_t r1 = static_cast<uint32_t>(y[0x25]);
		auto sine = [&]() { const int32_t v = m_t[r1]; r1 = modAdd(r1, n1, 0x7fff); return v; };

		const Acc aDepth = add(accShortImm(0x80), b0);
		const int32_t dMinus1 = lim(aDepth);		// x1 = d - 1
		const int32_t d = lim(b0);					// y1
		x[0xb] = lim(b0);
		int32_t s = sine();
		for(int j = 0; j < kBlock; ++j)
		{
			const int32_t ringed = lim(mpy(_in[j], s));
			s = sine();
			x[0xc + j] = lim(add(mpy(-dMinus1, _in[j]), mpy(d, ringed)));
		}
		r1 = modAdd(r1, -n1, 0x7fff);
		y[0x25] = static_cast<int32_t>(r1);
	}

	// P:$d2-$133. EQ: a peaking biquad (EQF frequency, EQG gain) on the AMD output. The coefficients come from
	// the OS tables indexed by EQG and EQF; the gain normalisation uses the DSP's 24-step division. The filter
	// runs in place on X:$8-$2b (history in X:$8-$b and state words $12-$16).
	void TrackFx::eq(State& _s)
	{
		auto& y = _s.y;
		auto& x = m_x;
		std::array<int32_t, 5> c{};		// Y:$50-$54, addressed by r5 modulo 5
		int r5 = 0;
		auto pushC = [&](const int32_t _v) { c[r5] = _v; r5 = (r5 + 1) % 5; };
		auto popC = [&]() { const int32_t v = c[r5]; r5 = (r5 + 1) % 5; return v; };

		Acc b = asr(acc(y[3]), 7);										// EQG
		Acc a = acc(y[2]);
		uint32_t r2 = u24(lim(b));
		a = asr(a, 3);													// EQF << 4
		int32_t x0 = m_t[r2 + 0x143476];
		int32_t x1 = m_t[r2 + 0x143576];
		{ const Acc oa = a; a = add(a, acc(x0)); b = acc(a1(oa)); }
		uint32_t r0 = 8;
		b = add(b, acc(x1));
		uint32_t r1 = u24(a1(a));
		int32_t y1 = y[0x16];
		uint32_t r3 = u24(a1(b));
		x[r0++] = y1;
		y1 = y[0x14];
		x[r0++] = y1;
		x0 = m_t[r1 + 0x14221d];
		x1 = m_t[r3 + 0x142c76];
		{ const int32_t ox1 = x1; a = mpy(x1, x0); x0 = ox1; }
		b = wrap(-mpy(x1, x0));
		a = sub(a, acc(0x400000));
		{ const Acc ob = b; b = asr(b, 1); pushC(lim(ob)); }
		{ const Acc oa = a; b = add(b, a); pushC(lim(oa)); }
		b = wrap(-b);
		a = asr(acc(y[2]), 3);
		int32_t y0 = lim(b);
		x0 = m_t[r2 + 0x1434f6];
		x1 = m_t[r2 + 0x1435f6];
		{ const Acc oa = a; a = add(a, acc(x0)); b = acc(a1(oa)); }
		y1 = y[0x13];
		b = add(b, acc(x1));
		r2 = u24(a1(a));
		x[r0++] = y1;
		r3 = u24(a1(b));
		y1 = y[0x12];
		x0 = m_t[r2 + 0x14221d];
		x1 = m_t[r3 + 0x142c76];
		{ const int32_t ox1 = x1; a = wrap(-mpy(x1, x0)); x0 = ox1; }
		b = mpy(x1, x0);
		r1 = 0x80;
		x[r0++] = y1;
		b = asr(b, 1);
		r0 = r1;
		y1 = 0x400000;
		{ const Acc ob = b; b = add(b, acc(y1)); x[r1++] = lim(ob); }
		{ const Acc oa = a; b = add(b, a); x[r1++] = lim(oa); }
		a = acc(clb(b));
		b = normf(a1(a), b);
		x[r1++] = y1;
		{ const Acc ob = b; b = acc(y0); x0 = lim(ob); }
		b = normf(a1(a), b);
		b = asr(b, 3);
		bool carry = false;
		for(int i = 0; i < 24; ++i)
			b = div(b, x0, carry);
		b = acc(sx24(a0(b)));
		b = asl(b, 1);
		x0 = x[r0++];
		y1 = x[0x2b];
		y[0x12] = y1;
		y0 = lim(b);
		{ b = mpy(y0, x0); x0 = x[r0++]; }
		{ a = mpy(y0, x0); x0 = x[r0++]; }
		{ const Acc ob = b; b = mpy(y0, x0); pushC(lim(ob)); }
		pushC(lim(a));
		pushC(lim(b));
		y1 = x[0x2a];
		y[0x13] = y1;

		// the filter loop
		r0 = 8;
		r2 = 9;
		a = acc48(y[0x14], u24(y[0x15]));
		x0 = x[r0++]; y0 = popC();
		a = add(a, mpy(y0, x0)); x1 = x[r0++]; y1 = popC();
		b = acc(x[r2]);
		for(int i = 0; i < kBlock; ++i)
		{
			{ a = add(a, mpy(y1, x1)); x[r2++] = lim(b); }
			{ a = add(a, mpy(y1, x1)); x0 = x[r0++]; y0 = popC(); }
			{ a = add(a, mpy(y0, x0)); x0 = x[r0++]; y0 = popC(); }
			{ a = add(a, mpy(y0, x0)); x0 = x[r0]; r0 -= 3; y0 = popC(); }
			{ a = add(a, mpy(y0, x0)); x0 = x[r0++]; y0 = popC(); }
			{ b = a; x1 = x[r0++]; y1 = popC(); }
			{ const Acc oa = a; a = add(a, mpy(y0, x0)); x1 = lim(oa); }
		}
		x[r2--] = lim(b);
		y[0x14] = lim(b);
		y[0x15] = static_cast<int32_t>(a0(b));
		x0 = x[r2];
		y[0x16] = x0;
	}

	void TrackFx::storeL(const uint32_t _addr, const Acc _a)
	{
		if(_a > 0x7fffffffffffLL) { m_x[_addr] = 0x7fffff; m_y[_addr] = sx24(0xffffff); return; }
		if(_a < -0x800000000000LL) { m_x[_addr] = -0x800000; m_y[_addr] = 0; return; }
		m_x[_addr] = a1(_a);
		m_y[_addr] = sx24(a0(_a));
	}

	Acc TrackFx::loadL(const uint32_t _addr) const { return acc48(m_x[_addr], u24(m_y[_addr])); }

	// P:$134-$19f. Filter, first section (high-pass at y[$24] = FLTF + FLTW, resonance y[$26]): coefficients
	// from the OS tables, ramped linearly across the block (X:$648 / X:$668 ramp shapes, rising or falling
	// cutoff), then a 2-pole recursion with 48-bit state (y[$17]/y[$27], y[$18], y[$a]/y[$b], y[$c]).
	void TrackFx::filter1(State& _s)
	{
		auto& y = _s.y;
		auto& x = m_x;
		auto& ys = m_y;

		uint32_t r2 = u24(y[0x26]);
		Acc a = acc(y[0x24]);
		int32_t x0 = m_t[r2 + 0x141f00];
		int32_t x1 = m_t[r2 + 0x141f80];
		Acc b;
		{ const Acc oa = a; a = add(a, acc(x0)); b = acc(a1(oa)); }
		b = add(b, acc(x1));
		uint32_t r3 = u24(a1(a));
		r2 = u24(a1(b));
		x1 = m_t[r3 + 0x1402aa];
		int32_t y1 = m_t[r2 + 0x141700];
		int32_t y0;
		{ const int32_t oy1 = y1; a = mpy(y1, x1); x0 = oy1; }
		b = wrap(-mpy(x0, y1));
		r3 = 6;
		{ const Acc ob = b; a = asl(a, 1); x1 = lim(ob); }
		a = add(a, acc(-0x800000));
		y1 = y[0x18];
		x[r3++] = y1;
		{ const Acc oa = a; a = add(a, acc(x1)); x0 = lim(oa); }
		a = wrap(-a);
		x[0x35] = x0;
		y0 = y[0x2b];
		{ const Acc ob = b; b = sub(b, acc(y0)); x0 = lim(ob); }
		y[0x2b] = x0;
		x[0x36] = y0;
		y1 = y[0x2c];
		{ const Acc oa = a; a = sub(a, acc(y1)); x0 = lim(oa); }
		y[0x2c] = x0;
		x[0x37] = y1;
		x[0x32] = lim(b);
		x[0x33] = lim(a);
		a = acc(x[0x35]);
		x1 = y[0x2a];
		{ const Acc oa = a; a = sub(a, acc(x1)); x0 = lim(oa); }
		y[0x2a] = x0;
		x[0x38] = x1;
		x[0x34] = lim(a);

		// coefficient ramps into Y:$80, $a0, $c0
		uint32_t r4 = 0x80, r5 = 0xa0, r7 = 0xc0;
		uint32_t r0 = 0x648;
		if(acc(y[0x24]) < acc(y[0x29])) r0 = 0x668;
		a = acc(x[0x36]); b = acc(x[0x37]); y0 = x[0x32]; y1 = x[0x33];
		uint32_t r1 = r0;
		x0 = m_t.xInternal(r0++);
		for(int i = 0; i < kBlock; ++i)
		{
			{ const Acc oa = a; a = add(a, mpy(y0, x0)); ys[r4++] = lim(oa); }
			{ const Acc ob = b; b = add(b, mpy(x0, y1)); x0 = m_t.xInternal(r0++); ys[r5++] = lim(ob); }
		}
		a = acc(x[0x38]); x1 = x[0x34];
		x0 = m_t.xInternal(r1++);
		for(int i = 0; i < kBlock; ++i)
		{
			const Acc oa = a; a = add(a, mpy(x1, x0)); x0 = m_t.xInternal(r1++); ys[r7++] = lim(oa);
		}

		// the recursion
		r0 = 6; r1 = 8; r4 = 0x80; r5 = 0xa0; r7 = 0xc0;
		x0 = y[0x17];
		x[r3++] = x0;
		a = acc(x0);
		a = acc48(a1(a), u24(y[0x27]));
		x0 = y[0xc]; x[r3++] = x0;
		x0 = y[0xa]; x[r3++] = x0;
		b = acc(x0);
		b = acc48(a1(b), u24(y[0xb]));
		r2 = 8;
		x0 = x[r0++]; y0 = ys[r4];
		{ a = add(a, mpy(y0, x0)); x0 = x[r0++]; y0 = ys[r7]; }
		for(int i = 0; i < kBlock; ++i)
		{
			{ a = add(a, mpy(y0, x0)); x0 = x[r0--]; y0 = ys[r5]; }
			{ a = add(a, mpy(y0, x0)); x0 = x[r1++]; y0 = ys[r4++]; }
			{ b = add(b, mpy(y0, x0)); x0 = x[r1++]; y0 = ys[r7++]; }
			storeL(r2++, a);
			{ b = add(b, mpy(y0, x0)); x0 = x[r1--]; y0 = ys[r5++]; }
			{ b = add(b, mpy(y0, x0)); x0 = x[r0++]; y0 = ys[r4]; }
			{ a = add(a, mpy(y0, x0)); x0 = x[r0++]; y0 = ys[r7]; }
			storeL(r3++, b);
		}
		a = loadL(--r3);
		y1 = x[--r3];
		y[0xc] = y1;
		y[0xa] = a1(a);
		y[0xb] = sx24(a0(a));
		a = loadL(--r2);
		y1 = x[--r2];
		y[0x18] = y1;
		y[0x17] = a1(a);
		y[0x27] = sx24(a0(a));
	}

	// P:$1a0-$21e. Filter, second section (low-pass at y[$22] = FLTF, resonance y[$23]): coefficients ramped
	// into Y:$a0, $c0 and $60, then a 2-pole recursion with 48-bit state (y[$d]/y[$e], y[$f], y[$19]/y[$1a],
	// y[$1b], y[$10], y[$11]).
	void TrackFx::filter2(State& _s)
	{
		auto& y = _s.y;
		auto& x = m_x;
		auto& ys = m_y;

		uint32_t r2 = u24(y[0x23]);
		Acc a = acc(y[0x22]);
		int32_t x0 = m_t[r2 + 0x141f00];
		int32_t x1 = m_t[r2 + 0x141f80];
		Acc b;
		{ const Acc oa = a; a = add(a, acc(x0)); b = acc(a1(oa)); }
		uint32_t r0 = 0;
		b = add(b, acc(x1));
		uint32_t r1 = u24(a1(a));
		x1 = y[0x1b];
		x[r0++] = x1;
		r2 = u24(a1(b));
		x1 = m_t[r1 + 0x1402aa];
		int32_t y1 = m_t[r2 + 0x141700];
		int32_t y0;
		{ const int32_t oy1 = y1; a = mpy(y1, x1); x0 = oy1; }
		b = wrap(-mpy(x0, y1));
		{ const Acc oa = a; a = asl(a, 1); x1 = lim(oa); }
		a = sub(a, b);
		a = sub(a, acc(-0x800000));
		a = asr(a, 3);
		x[0x3c] = lim(a);
		a = acc(x1);
		a = add(a, acc(sx24(0xc00000)));
		y0 = y[0x2f];
		{ const Acc ob = b; b = sub(b, acc(y0)); x0 = lim(ob); }
		y[0x2f] = x0;
		x[0x36] = y0;
		y1 = y[0x2e];
		{ const Acc oa = a; a = sub(a, acc(y1)); x0 = lim(oa); }
		y[0x2e] = x0;
		x[0x37] = y1;
		x[0x32] = lim(b);
		x[0x33] = lim(a);

		uint32_t r5 = 0xa0, r7 = 0xc0;
		r1 = 0x648;
		if(acc(y[0x22]) < acc(y[0x2d])) r1 = 0x668;
		a = acc(x[0x36]); b = acc(x[0x37]); y0 = x[0x32]; y1 = x[0x33];
		r2 = r1;
		x0 = m_t.xInternal(r1++);
		for(int i = 0; i < kBlock; ++i)
		{
			{ const Acc oa = a; a = add(a, mpy(y0, x0)); ys[r5++] = lim(oa); }
			{ const Acc ob = b; b = add(b, mpy(x0, y1)); x0 = m_t.xInternal(r1++); ys[r7++] = lim(ob); }
		}
		a = acc(x[0x3c]);
		y1 = y[0x30];
		{ const Acc oa = a; a = sub(a, acc(y1)); x0 = lim(oa); }
		y[0x30] = x0;
		b = acc(y1);
		{ const Acc oa = a; a = wrap(-a); y1 = lim(oa); }
		a = asl(a, 1);
		r7 = 0x60;
		{ const Acc oa = a; a = b; y0 = lim(oa); }
		a = wrap(-a);
		a = asl(a, 1);
		x0 = m_t.xInternal(r2++);
		for(int i = 0; i < kBlock; ++i)
		{
			{ const Acc ob = b; b = add(b, mpy(x0, y1)); ys[r7++] = lim(ob); }
			{ const Acc oa = a; a = add(a, mpy(y0, x0)); x0 = m_t.xInternal(r2++); ys[r7++] = lim(oa); }
		}

		// the recursion
		x0 = y[0xf];
		x1 = y[0x11];
		b = acc(y[0x19]);
		a = acc(y[0xd]);
		b = acc48(a1(b), u24(y[0x1a]));
		a = acc48(a1(a), u24(y[0xe]));
		x[r0++] = lim(b);
		x[r0++] = x0;
		x[r0++] = lim(a);
		x[r0++] = x1;
		x1 = y[0x10];
		x[r0] = x1;
		r0 = 2;
		const int32_t n0 = -3;
		r1 = 0;
		r2 = 4;
		uint32_t r3 = 2;
		r5 = 0xa0; r7 = 0xc0;
		x1 = x[0x25]; y[0x10] = x1;
		x1 = x[0x24]; y[0x11] = x1;
		uint32_t r4 = 0x60;
		x0 = x[r0++]; y0 = ys[r5];
		{ a = add(a, mpy(y0, x0)); x0 = x[r0++]; y0 = ys[r7]; }
		a = add(a, mpy(y0, x0));
		for(int i = 0; i < kBlock; ++i)
		{
			{ a = add(a, mpy(y0, x0)); x0 = x[r0++]; y1 = ys[r4++]; }
			{ a = add(a, mpy(x0, y1)); x0 = x[r0++]; y0 = ys[r4]; }
			{ a = add(a, mpy(y0, x0)); x0 = x[r0]; r0 = static_cast<uint32_t>(static_cast<int32_t>(r0) + n0); }
			{ a = add(a, mpy(x0, y1)); x0 = x[r1++]; y0 = ys[r5++]; }
			{ b = add(b, mpy(y0, x0)); x0 = x[r1++]; y0 = ys[r7++]; }
			{ b = add(b, mpy(y0, x0)); storeL(r2++, a); }
			{ b = add(b, mpy(y0, x0)); x0 = x[r1++]; }
			{ b = add(b, mpy(x0, y1)); x0 = x[r1++]; y0 = ys[r4++]; }
			{ b = add(b, mpy(y0, x0)); x0 = x[r1]; r1 = static_cast<uint32_t>(static_cast<int32_t>(r1) + n0); }
			{ b = add(b, mpy(x0, y1)); x0 = x[r0++]; y0 = ys[r5]; }
			{ a = add(a, mpy(y0, x0)); x0 = x[r0++]; y0 = ys[r7]; }
			{ a = add(a, mpy(y0, x0)); storeL(r3++, b); }
		}
		--r2; x1 = x[r2]; x0 = ys[r2];
		y[0xd] = x1; y[0xe] = x0;
		x0 = x[--r2]; y[0xf] = x0;
		--r2; x1 = x[r2]; x0 = ys[r2];
		y[0x19] = x1; y[0x1a] = x0;
		x0 = x[--r2]; y[0x1b] = x0;
	}

	// P:$21f-$233. SRR: sample and hold on a phase accumulator (y[$1c]; held sample y[$1d]) whose period comes
	// from SRR^2. Input X:$2-$21 (the filter's output), output Y:$3-$22.
	void TrackFx::srr(State& _s)
	{
		auto& y = _s.y;
		auto& ys = m_y;
		Acc a = add(acc(y[7]), acc(0x4000));
		Acc b = acc(y[0x1c]);
		int32_t x0 = lim(a);
		const int32_t y1 = y[0x1d];
		a = mpy(x0, x0);
		x0 = 0x10;
		a = sub(a, acc(0x20));
		a = asl(a, 2);
		a = add(a, acc(x0));
		uint32_t r4 = 2;
		const int32_t x1 = lim(a);
		a = acc(y1);
		for(int i = 0; i < kBlock; ++i)
		{
			b = add(b, acc(x0));
			const int32_t in = m_x[r4];
			const bool ge = b >= acc(x1);
			ys[r4++] = lim(a);
			if(ge) { a = acc(in); b = sub(b, acc(x1)); }
		}
		ys[r4] = lim(a);
		y[0x1c] = lim(b);
		m_held = lim(a);	// stored to y[$1d] by the distortion's prologue, as on the DSP
	}

	// P:$234-$25d. Distortion: the sample times a DIST-dependent gain, shifted left 9 and saturated by the
	// limiter, then a first-order filter (coefficients from the OS tables and constants; state y[$1e]-$21).
	// Input Y:$3-$22, output the track's 32 samples.
	void TrackFx::dist(State& _s, int32_t* _out)
	{
		auto& y = _s.y;
		auto& x = m_x;
		auto& ys = m_y;
		Acc b = asr(acc(y[8]), 6);				// DIST x 2
		uint32_t r4 = 3;
		const int32_t n0 = lim(b);
		y[0x1d] = m_held;
		uint32_t r2 = 0x3c;
		int32_t x1 = m_t[static_cast<uint32_t>(0x143777 + n0)];
		x[r2++] = x1;
		x1 = m_t[static_cast<uint32_t>(0x143878 + n0)];
		b = mpy(x1, 0x7fdf3b);
		int32_t y0 = y[0x1e];
		y[0x1e] = lim(b);
		x1 = sx24(0xffbe77);
		int32_t x0 = y[0x1f];
		x[r2--] = x1;
		Acc a = acc48(y[0x20], u24(y[0x21]));
		int32_t y1;
		x1 = x[r2++]; y1 = ys[r4++];
		{ const Acc ob = b; b = mpy(y1, x1); x1 = x[r2--]; y1 = lim(ob); }
		b = asl(b, 9);
		{ const Acc oa = a; a = add(a, wrap(-mpy(y0, x0))); x0 = lim(oa); }
		{ const int32_t ox0 = x0; x0 = lim(b); a = add(a, mpy(x1, ox0)); }
		{ a = add(a, mpy(x0, y1)); x1 = x[r2++]; y0 = ys[r4++]; }
		int o = 0;
		for(int i = 0; i < kBlock - 1; ++i)
		{
			{ b = mpy(x1, y0); x1 = x[r2--]; }
			b = asl(b, 9);
			{ const Acc oa = a; a = add(a, wrap(-mpy(x0, y1))); _out[o++] = lim(oa); y0 = lim(oa); }
			{ const int32_t oy0 = y0; x0 = lim(b); a = add(a, mpy(x1, oy0)); }
			{ a = add(a, mpy(x0, y1)); x1 = x[r2++]; y0 = ys[r4++]; }
		}
		y[0x1f] = lim(b);
		y[0x20] = lim(a);
		y[0x21] = sx24(a0(a));
		_out[o] = lim(a);
	}

	void TrackFx::process(State& _s, const int32_t* _in, int32_t* _out)
	{
		prepFilter(_s);
		amd(_s, _in);
		if(stopAfter == Stop::AfterAmd)
			return;
		eq(_s);
		if(stopAfter == Stop::AfterEq)
			return;
		filter1(_s);
		if(stopAfter == Stop::AfterFilter1)
			return;
		filter2(_s);
		if(stopAfter == Stop::AfterFilter2)
			return;
		srr(_s);
		if(stopAfter == Stop::AfterSrr)
			return;
		dist(_s, _out);
	}
}
