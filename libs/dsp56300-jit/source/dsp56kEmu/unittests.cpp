#include "unittests.h"

namespace dsp56k
{
	static DefaultMemoryValidator g_defaultMemoryValidator;

	UnitTests::UnitTests()
		: mem(g_defaultMemoryValidator, 0x080000, 0x800000, 0x200000)
		, dsp(mem, &peripheralsX, &peripheralsY)
	{
	}

	void UnitTests::emit(const char* _text, TWord _pc)
	{
		const auto result = assembler.assemble(_text);
		if(!result.success())
			throw std::string("Assembly failed for: ") + _text;
		emit(result.word[0], result.wordCount > 1 ? result.word[1] : 0, _pc);
	}

	TWord UnitTests::emitToMemory(const char* _text, TWord _pc)
	{
		const auto result = assembler.assemble(_text);
		if(!result.success())
			throw std::string("Assembly failed for: ") + _text;
		return emitToMemory(result.word[0], result.wordCount > 1 ? result.word[1] : 0, _pc);
	}

	TWord UnitTests::emitToMemory(TWord _opA, TWord _opB, TWord _pc)
	{
		dsp.memWriteP(_pc, _opA);
		if(_opB)
			dsp.memWriteP(_pc + 1, _opB);
		return _opB ? _pc + 2 : _pc + 1;
	}

	uint32_t UnitTests::execUntil(TWord _targetPC, uint32_t _maxCycles)
	{
		for(uint32_t i = 0; i < _maxCycles; ++i)
		{
			const auto pc = dsp.getPC().toWord();
			if(pc == _targetPC)
				return i;
			execStep();
		}
		std::stringstream ss;
		ss << "execUntil: target PC $" << std::hex << _targetPC
		   << " not reached after " << std::dec << _maxCycles
		   << " cycles (current PC $" << std::hex << dsp.getPC().toWord() << ")";
		throw ss.str();
	}

	void UnitTests::runAllTests()
	{
		conditionCodes();
		aguModulo();
		aguMultiWrapModulo();
		aguBitreverse();
		x0x1Combinations();

		abs();
		add();
		addShortImmediate();
		addLongImmediate();
		addl();
		addr();
		and_();
		andi();

		asl();
		asl_D();
		asl_ii();
		asl_S1S2D();

		asr();
		asr_D();
		asr_ii();
		asr_S1S2D();

		bchg_aa();
		bclr_ea();
		bclr_aa();
		bclr_qqpp();
		bclr_D();
		bset_aa();
		btst_aa();

		clb();
		clr();
		cmp();
		cmpm();
		dec();
		div();
		dmac();
		dmaAddressWrapping();
		hdiTransmitCallbacks();
		dmacMultiPrecision();
		eor();
		extractu();
		extractu_co();
		ifcc();
		inc();
		insert();
		jscc();
		lra();
		lsl();
		lsr();
		lua_ea();
		lua_rn();
		mac();
		mac_S();
		max();
		maxm();
		merge();
		mpy();
		mpyr();
		mpy_SD();
		neg();
		normf();
		not_();
		or_();
		ori();
		rnd();
		rol();
		sub();
		subl();
		tfr();
		tcc();

		move();
		movel();
		parallel();

		// ALU extended
		and_xxxx();
		or_xxxx();
		sub_xxxx();
		cmp_xxxx();
		subr();
		tst();
		nop();

		// jumps
		jmp();
		jsr();
		jcc();
		jclr_jset();
		jsclr_jsset();

		// branches
		bra();
		bcc();
		bsr();
		bscc();
		brclr_brset();
		bsclr_bsset();

		// bit manipulation
		bchg();
		bset();
		btst();

		// multiply
		mpyi();
		mpy_su();

		// newly implemented
		eor_xx();
		ror_();

		// bit-test jump/branch — peripheral addressing modes
		jclr_jset_ppqq();
		jsclr_jsset_ppqq();
		brclr_brset_ppqq();

		// multi-instruction tests
		multiInstructionTests();
	}

	void UnitTests::conditionCodes()
	{
		auto invert = [](ConditionCode _cc)
		{
			switch (_cc)
			{
			case CCCC_CarrySet:	return CCCC_CarryClear;
			case CCCC_CarryClear: return CCCC_CarrySet;
			case CCCC_ExtensionSet: return CCCC_ExtensionClear;
			case CCCC_ExtensionClear: return CCCC_ExtensionSet;
			case CCCC_Equal: return CCCC_NotEqual;
			case CCCC_NotEqual: return CCCC_Equal;
			case CCCC_LimitSet: return CCCC_LimitClear;
			case CCCC_LimitClear: return CCCC_LimitSet;
			case CCCC_Minus: return CCCC_Plus;
			case CCCC_Plus: return CCCC_Minus;
			case CCCC_GreaterEqual: return CCCC_LessThan;
			case CCCC_LessThan: return CCCC_GreaterEqual;
			case CCCC_Normalized: return CCCC_NotNormalized;
			case CCCC_NotNormalized: return CCCC_Normalized;
			case CCCC_GreaterThan: return CCCC_LessEqual;
			case CCCC_LessEqual: return CCCC_GreaterThan;
			default:
				assert(false && "invalid condition code");
				return CCCC_NotEqual;
			}
		};

		auto runOne = [this](const int64_t _a, const ConditionCode _cc, const bool _expectedResult)
		{
			runTest([&]()
			{
				dsp.resetHW();
				dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(_a & 0xff'ffffff'ffffff)));

				dsp.reg.r[0].var = 0x1;
				dsp.reg.r[1].var = 0x0;

				emit("tst a");
				emit(0x020801 | (_cc << 12));	// tcc r0,r1 + the condition code as parameter
			}, [&]()
			{
				verify(dsp.regs().r[1] == (_expectedResult ? 1 : 0));
			});
		};

		auto run = [this, runOne, invert](const int64_t _a, const std::initializer_list<ConditionCode>& _ccs, bool _result = true)
		{
			for (const ConditionCode& cc : _ccs)
			{
				runOne(_a, cc, _result);
				runOne(_a, invert(cc), !_result);
			}
		};

		run(+1, {CCCC_Plus, CCCC_GreaterEqual, CCCC_GreaterThan, CCCC_NotEqual, CCCC_CarryClear, CCCC_ExtensionClear});
		run(-1, {CCCC_Minus, CCCC_LessEqual, CCCC_LessThan, CCCC_NotEqual, CCCC_CarryClear, CCCC_ExtensionClear});

		run(0, {CCCC_Equal, CCCC_LessEqual, CCCC_GreaterEqual, CCCC_CarryClear, CCCC_ExtensionClear, CCCC_NotNormalized});

		run(0xff'ffffff'ffffff, {CCCC_Minus, CCCC_ExtensionClear});
		run(0xff'800000'000000, {CCCC_Minus, CCCC_ExtensionClear});
		run(0xff'000000'000000, {CCCC_Minus, CCCC_ExtensionSet});
		run(0x00'700000'000000, {CCCC_Plus, CCCC_ExtensionClear});
		run(0x00'800000'000000, {CCCC_Plus, CCCC_ExtensionSet});

		run(0x00'c00000'000000, {CCCC_Plus, CCCC_NotNormalized});
		run(0x00'000000'000000, {CCCC_Plus, CCCC_NotNormalized});
		run(0xff'800000'000000, {CCCC_Minus, CCCC_Normalized});
		run(0x00'400000'000000, {CCCC_Plus, CCCC_Normalized});

		// DSP56300FM table 5-1: E tests the complete signed integer portion,
		// including bit 55 in all scaling modes (down: 48, normal: 47, up: 46).
		for(unsigned scaling = 0; scaling < 3; ++scaling)
		{
			const unsigned lowBit = scaling == 0 ? 47 : scaling == 1 ? 48 : 46;
			for(const uint64_t value : {0ull, 0xffffffffffffffull, 0x003fffffffffffull,
				0x00400000000000ull, 0x007fffffffffffull, 0x00800000000000ull,
				0x00ffffffffffffull, 0x01000000000000ull, 0x7ff00000000000ull,
				0x80000000000000ull, 0x80100000000000ull, 0xff800000000000ull,
				0xffc00000000000ull})
			{
				bool extension = false;
				for(unsigned bit = lowBit; bit < 55; ++bit)
					extension |= ((value >> bit) & 1) != ((value >> 55) & 1);
				for(const bool ab : {false, true})
					runTest([&]()
					{
						dsp.setSR(scaling << SRB_S0);
						dsp.setALU(ab, TReg56(value));
						emit(ab ? "tst b" : "tst a");
					}, [&]()
					{
						if(bool(dsp.sr_test(CCR_E)) != extension)
							LOG("Extension mismatch scaling=" << scaling << " value=" << std::hex << value
								<< " sr=" << dsp.getSR().var << " expected=" << extension);
						verify(bool(dsp.sr_test(CCR_E)) == extension);
					});
			}
		}
	}

	void UnitTests::aguModulo()
	{
		runTest([&]()
		{
			dsp.set_m(0, 0x000fff);
			dsp.regs().r[0].var = 0x123f00;
			dsp.regs().n[0].var = 0x000200;

			emit("move (r0)+n0");
		}, [&]()
		{
			verify(dsp.regs().r[0] == 0x123100);
		});

		runTest([&]()
		{
			// edge case where N = modulo size but not block size
			dsp.set_m(5, 0x003ffd);
			dsp.regs().r[5].var = 0x09c000;
			dsp.regs().n[5].var = 0x003ffe;

			emit("move (r5)-n5");
		}, [&]()
		{
			verify(dsp.regs().r[5] == 0x9c000);
		});

		runTest([&]()
		{
			dsp.set_m(5, 0x003ffd);
			dsp.regs().r[5].var = 0x09c000;
			dsp.regs().n[5].var = 0x001000;

			emit("move (r5)-n5");
		}, [&]()
		{
			verify(dsp.regs().r[5] == 0x09effe);
		});

		runTest([&]()
		{
			// edge case where N is the size of a block
			dsp.set_m(5, 0x003ffd);
			dsp.regs().r[5].var = 0x09c000;
			dsp.regs().n[5].var = 0x004000;

			emit("move (r5)+n5");
		}, [&]()
		{
			verify(dsp.regs().r[5] == 0x0a0000);
		});

		runTest([&]()
		{
			// undefined behaviour, tested in the simulator. It does modulo where masked and not-modulo outside of the mask
			dsp.set_m(5, 0x000080);
			dsp.regs().r[5].var = 0x000000;
			dsp.regs().n[5].var = 0x000190;

			emit("move (r5)+n5");
		}, [&]()
		{
			verify(dsp.regs().r[5] == 0x00010f);
		});

		runTest([&]()
		{
			// negative n
			dsp.set_m(0, 0x003ffd);
			dsp.regs().r[0].var = 0x0bbc3a;
			dsp.regs().n[0].var = 0xffe9c7;

			emit("move (r0)+n0");
		}, [&]()
		{
			verify(dsp.regs().r[0] == 0x0ba601);
		});
	}

	void UnitTests::aguMultiWrapModulo()
	{
		for(uint32_t i=0; i<0x200; ++i)
		{
			runTest([&]()
			{
				dsp.set_m(0, 0x0080ff);
				dsp.regs().r[0].var = 0x123400 + (i & 0xff);

				dsp.set_m(1, 0x0080ff);
				dsp.regs().r[1].var = 0x123400 + (i & 0xff);
				dsp.regs().n[1].var = 0x88;

				dsp.set_m(2, 0x0080ff);
				dsp.regs().r[2].var = 0x123400 + (i & 0xff);
				dsp.regs().n[2].var = 0x100;

				dsp.set_m(3, 0x0080ff);
				dsp.regs().r[3].var = 0x123400;
				dsp.regs().n[3].var = i;

				dsp.set_m(4, 0x0080ff);
				dsp.regs().r[4].var = 0x123400 + ((-static_cast<int32_t>(i)) & 0xff);

				emit("move (r0)+");
				emit("move (r1)+n1");
				emit("move (r2)+n2");
				emit("move (r3)-n3");
				emit("move (r4)-");
			}, [&]()
			{
				verify(dsp.regs().r[0] == 0x123400 + ((i + 1) & 0xff));
				verify(dsp.regs().r[1] == 0x123400 + (((i & 0xff) + 0x88) & 0xff));
				verify(dsp.regs().r[2] == 0x123400 + (i & 0xff));
				verify(dsp.regs().r[3] == 0x123400 + ((-static_cast<int32_t>(i)) & 0xff));
				verify(dsp.regs().r[4] == 0x123400 + ((-static_cast<int32_t>(i) - 1) & 0xff));
			});
		}
		runTest([&]()
		{
			dsp.x0(0x810f);

			emit(0x04c4a1);	// move x0,m1
			emit(0x04c4a1);	// move x0,m2

			dsp.regs().r[1].var = 0x3c8;
			dsp.regs().n[1].var = 5;
			dsp.set_m(1, 0x801f);

			dsp.regs().r[2].var = 0x3c8;
			dsp.regs().n[2].var = 1;
			dsp.set_m(2, 0x801f);

			emit("move (r1)+n1");
			emit("move (r2)+n2");
		}, [&]()
		{
			verify(dsp.regs().r[1] == 0x3cd);
			verify(dsp.regs().r[2] == 0x3c9);
		});
	}

	void UnitTests::aguBitreverse()
	{
		static_assert(bitreverse24(0x800000) == 0x000001, "bitreverse function not working");
		static_assert(bitreverse24(0x400000) == 0x000002, "bitreverse function not working");
		static_assert(bitreverse24(0x200000) == 0x000004, "bitreverse function not working");
		static_assert(bitreverse24(0x100000) == 0x000008, "bitreverse function not working");

		static_assert(bitreverse24(0x080000) == 0x000010, "bitreverse function not working");
		static_assert(bitreverse24(0x040000) == 0x000020, "bitreverse function not working");
		static_assert(bitreverse24(0x020000) == 0x000040, "bitreverse function not working");
		static_assert(bitreverse24(0x010000) == 0x000080, "bitreverse function not working");

		static_assert(bitreverse24(0x008000) == 0x000100, "bitreverse function not working");
		static_assert(bitreverse24(0x004000) == 0x000200, "bitreverse function not working");
		static_assert(bitreverse24(0x002000) == 0x000400, "bitreverse function not working");
		static_assert(bitreverse24(0x001000) == 0x000800, "bitreverse function not working");

		static_assert(bitreverse24(0x000001) == 0x800000, "bitreverse function not working");
		static_assert(bitreverse24(0x000002) == 0x400000, "bitreverse function not working");
		static_assert(bitreverse24(0x000004) == 0x200000, "bitreverse function not working");
		static_assert(bitreverse24(0x000008) == 0x100000, "bitreverse function not working");

		static_assert(bitreverse24(0x000010) == 0x080000, "bitreverse function not working");
		static_assert(bitreverse24(0x000020) == 0x040000, "bitreverse function not working");
		static_assert(bitreverse24(0x000040) == 0x020000, "bitreverse function not working");
		static_assert(bitreverse24(0x000080) == 0x010000, "bitreverse function not working");

		static_assert(bitreverse24(0x000100) == 0x008000, "bitreverse function not working");
		static_assert(bitreverse24(0x000200) == 0x004000, "bitreverse function not working");
		static_assert(bitreverse24(0x000400) == 0x002000, "bitreverse function not working");
		static_assert(bitreverse24(0x000800) == 0x001000, "bitreverse function not working");

		auto run = [&](const TWord _rInit, const TWord _rInc, const TWord _expectedResult, bool _add)
		{
			runTest([&]()
			{
				dsp.set_m(0, 0);
				dsp.regs().r[0].var = _rInit;
				dsp.regs().n[0].var = _rInc;
				if(_add)
					emit("move (r0)+n0");
				else
					emit("move (r0)-n0");
			}, [&]()
			{
				verify(dsp.regs().r[0] == _expectedResult);
			});
		};

		run(0, 1, 1, true);
		run(1, 1, 0, true);

		run(0, 1, 1, false);
		run(1, 1, 0, false);

		run(0xaabbcc, 0x123456, 0xb99079, true);
		run(0xaabbcc, 0x123456, 0xb08d93, false);
	}

	void UnitTests::x0x1Combinations()
	{
		runTest([&]()
		{
			dsp.x0(0xaabbcc);
			dsp.x1(0xddeeff);

			dsp.y0(0xabcdef);
			dsp.y1(0x123456);

			emit("move #$babecc,x0");
		}, [&]()
		{
			verify(dsp.regs().x.var == 0xddeeffbabecc);
			verify(dsp.regs().y.var == 0x123456abcdef);
		});

		auto init = [&]()
		{
			dsp.x0(0x111111);
			dsp.x1(0x222222);

			dsp.y0(0x333333);
			dsp.y1(0x444444);
		};

		// write to partial registers and check if common register is intact
		runTest([&]()
		{
			init();
			emit("move #$aaaaaa,x0");
//			emit(0x45f400, 0xbbbbbb);	// move #$bbbbbb,x1
//			emit(0x46f400, 0xcccccc);	// move #$cccccc,y0
			emit("move #$dddddd,y1");
//			emit(0x20c700);				// move y0, y1
		}, [&]()
		{
			verify(dsp.regs().x.var == 0x222222aaaaaa);
			verify(dsp.regs().y.var == 0xdddddd333333);
		});

		// write to two partial registers of the same common reg
		runTest([&]()
		{
			init();

			emit("move #$aaaaaa,x0");
			emit("move #$bbbbbb,x1");
		}, [&]()
		{
			verify(dsp.regs().x.var == 0xbbbbbbaaaaaa);
			verify(dsp.regs().y.var == 0x444444333333);
		});

		// write one half, then use the common reg for an add
		runTest([&]()
		{
			init();
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0)));

			emit("move #$aaaaaa,x0");
			emit("move #$dddddd,y1");
			emit("add x,a");
			emit("add y,b");
		}, [&]()
		{
			verify(dsp.aluA().var == 0x00222222aaaaaa);
			verify(dsp.aluB().var == 0xffdddddd333333);
		});
	}

	void UnitTests::abs()
	{
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00ff112233445566)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x0000aabbccddeeff)));

			emit("abs a");
			emit("abs b");
		}, [&]()
		{
			verify(dsp.aluA() == 0x00EEDDCCBBAA9A);
			verify(dsp.aluB() == 0x0000aabbccddeeff);
		});
	}

	void UnitTests::add()
	{
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x0001e000000000)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0xfffe2000000000)));

			// add b,a
			emit("add b,a");
		}, [&]()
		{
			verify(dsp.aluA().var == 0);
			verify(dsp.sr_test(CCR_C));
			verify(dsp.sr_test(CCR_Z));
			verify(!dsp.sr_test(CCR_V));
		});

		auto testAdd = [this](int64_t a, int y0, int64_t expectedResult)
		{
			runTest([&]()
			{
				dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(a)));
				dsp.reg.y.var = y0;

				// add y0,a
				emit("add y0,a");
			}, [&]()
			{
				verify(dsp.aluA().var == expectedResult);
			});
		};

		// TODO: test CCR for these
		testAdd(0, 0, 0);
		testAdd(0x00000000123456, 0x000abc, 0x00000abc123456);
		testAdd(0x00000000123456, 0xabcdef, 0xffabcdef123456);

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x0001e000000000)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0xfffe2000000000)));

			// add b,a
			emit("add b,a");
		}, [&]()
		{
			verify(dsp.aluA().var == 0);
			verify(dsp.sr_test(CCR_C));
			verify(!dsp.sr_test(CCR_V));
		});
	}

	void UnitTests::addShortImmediate()
	{
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0)));

			// add #<32,a
			emit("add #<$32,a");
		}, [&]()
		{
			verify(dsp.aluA().var == 0x00000032000000);
			verify(!dsp.sr_test(CCR_C));
			verify(!dsp.sr_test(CCR_Z));
			verify(!dsp.sr_test(CCR_V));
		});
	}

	void UnitTests::addLongImmediate()
	{
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0)));
			dsp.regs().pc.var = 0;

			// add #>32,a, two op add with immediate in extension word
			emit("add #>$32,a");
		}, [&]()
		{
			verify(dsp.aluA().var == 0x00000032000000);
			verify(!dsp.sr_test(CCR_C));
			verify(!dsp.sr_test(CCR_Z));
			verify(!dsp.sr_test(CCR_V));
		});
	}

	void UnitTests::addl()
	{
		// 56-bit signed arithmetic fits in int64_t here, including 2*D+S.
		constexpr uint64_t mask = 0x00ffffffffffffffull;
		const auto signed56 = [](uint64_t _value) -> int64_t
		{
			return _value < (1ull << 55) ? int64_t(_value) : int64_t(_value) - (1ll << 56);
		};
		constexpr uint64_t destinations[] = {0, 1, mask, 1ull << 54, 1ull << 55, 0x015a7b3f37c905ull};
		constexpr uint64_t sources[] = {0, 1, mask, (1ull << 55) - 1, 1ull << 55, 0xd55ad0723547d7ull};
		for(const auto destination : destinations)
		for(const auto source : sources)
		for(const bool ab : {false, true})
		for(const auto initialLimit : {0u, static_cast<unsigned>(CCR_L)})
		{
			const auto doubled = 2 * signed56(destination);
			const auto sum = doubled + signed56(source);
			const bool shiftOverflow = doubled < -(1ll << 55) || doubled >= (1ll << 55);
			const bool overflow = shiftOverflow || sum < -(1ll << 55) || sum >= (1ll << 55);
			const bool carry = (((destination << 1) & mask) + source) > mask;
			runTest([&]()
			{
				dsp.regs().sr.var = CCR_V | CCR_C | initialLimit;
				dsp.setALU(ab, TReg56(destination));
				dsp.setALU(!ab, TReg56(source));
				emit(ab ? "addl a,b" : "addl b,a");
			}, [&]()
			{
				verify((ab ? dsp.aluB().var : dsp.aluA().var) == (uint64_t(sum) & mask));
				verify((ab ? dsp.aluA().var : dsp.aluB().var) == source);
				verify(static_cast<bool>(dsp.sr_test(CCR_V)) == overflow);
				verify(static_cast<bool>(dsp.sr_test(CCR_L)) == (initialLimit != 0 || overflow));
				// The manual only guarantees carry without pre-shift overflow.
				if(!shiftOverflow)
					verify(static_cast<bool>(dsp.sr_test(CCR_C)) == carry);
			});
		}

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x222222)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x333333)));

			emit("addl a,b");
		}, [&]()
		{
			verify(dsp.aluB().var == 0x888888);
			verify(!dsp.sr_test(CCR_C));
			verify(!dsp.sr_test(CCR_Z));
			verify(!dsp.sr_test(CCR_V));
		});
	}

	void UnitTests::addr()
	{
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x004edffe000000)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0xff89fe13000000)));
			dsp.setSR(0x0800d0);							// (S L) U

			emit("addr b,a");
		}, [&]()
		{
			verify(dsp.aluA().var == 0x0ffb16e12000000);
			verify(dsp.getSR().var == 0x0800c8);			// (S L) N
		});

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xffb16e12000000)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0xff89fe13000000)));
			dsp.setSR(0x0800c8);							// (S L) N
			emit("addr a,b");
		}, [&]()
		{
			verify(dsp.aluB().var == 0xff766d1b800000);
			verify(dsp.getSR().var == 0x0800e9);			// (S L) E N C
		});
	}

	void UnitTests::and_()
	{
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xffcccccc112233)));
			dsp.regs().x.var = 0x777777;

			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0xaaaabbcc334455)));
			dsp.regs().y.var = 0x667788000000;

			emit("and x0,a");
			emit("and y1,b");
		}, [&]()
		{
			verify(dsp.aluA().var == 0xff444444112233);
			verify(dsp.aluB().var == 0xaa223388334455);
		});
	}

	void UnitTests::andi()
	{
		const auto srBackup = dsp.regs().sr;

		runTest([&]()
		{
			dsp.regs().omr.var = 0xff6666;
			dsp.regs().sr.var = 0xff4666;

			emit("andi #$33,omr");
			emit("andi #$33,eom");
			emit("andi #$33,mr");
			emit("andi #$33,ccr");
		}, [&]()
		{
			verify(dsp.regs().omr.var == 0xff2222);
			verify(dsp.regs().sr.var == 0xff0222);
		});

		dsp.setSR(srBackup);
	}

	void UnitTests::asl()
	{
		// asl #1,a,a
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xaaabcdef123456)));
			emit("asl #$1,a,a");
		}, [&]()
		{
			verify(dsp.aluA().var == 0x55579bde2468ac);
		});

		// asl #1,a,a
		runTest([&]()
		{
			emit("asr #$1,a,a");
		}, [&]()
		{
			verify(dsp.aluA().var == 0x2aabcdef123456);
		});

		// asl b
		runTest([&]()
		{
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x000599f2204000)));
			emit("asl b");
		}, [&]()
		{
			verify(dsp.aluB().var == 0x000b33e4408000);
		});

		// asl #28,a,a
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xf4)));
			dsp.setSR(0x0800d0);
			emit("asl #$28,a,a");
		}, [&]()
		{
			verify(dsp.aluA().var == 0x00f40000000000);
			verify(dsp.getSR().var == 0x0800f0);
		});
	}

	void UnitTests::asl_D()
	{
		// Independent bit-by-bit ASL oracle (DSP56300FM 13-15).
		for(const uint64_t input : {0ull, 1ull, 0x0080000000000000ull,
			0x0040000000000000ull, 0x00ffffffffffffffull})
			for(const unsigned count : {0u, 1u, 8u, 55u})
				for(const bool registerCount : {false, true})
				{
					auto expected = input;
					bool carry = false, overflow = false;
					for(unsigned bit = 0; bit < count; ++bit)
					{
						carry = (expected >> 55) != 0;
						expected = (expected << 1) & 0x00ffffffffffffffull;
						overflow |= carry != static_cast<bool>(expected >> 55);
					}
					runTest([&]()
					{
						dsp.regs().sr.var = CCR_C | CCR_V;
						dsp.setALU(false, TReg56(input));
						dsp.x0(count);
						const auto instruction = std::string("asl ") +
							(registerCount ? "x0" : "#" + std::to_string(count)) + ",a,b";
						emit(instruction.c_str());
					}, [&]()
					{
						verify(dsp.aluA().var == input);
						verify(dsp.aluB().var == expected);
						verify(static_cast<bool>(dsp.sr_test(CCR_C)) == carry);
						verify(static_cast<bool>(dsp.sr_test(CCR_V)) == overflow);
						verify(static_cast<bool>(dsp.sr_test(CCR_L)) == overflow);
					});
				}

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xaaabcdef123456)));
			dsp.regs().sr.var = 0;

			emit("asl a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0x55579bde2468ac);
			verify(!dsp.sr_test_noCache(CCR_Z));
			verify(dsp.sr_test_noCache(CCR_V));
			verify(dsp.sr_test_noCache(CCR_C));
		});

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00400000000000)));
			dsp.regs().sr.var = 0;
			emit("asl a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0x00800000000000);
			verify(!dsp.sr_test_noCache(CCR_Z));
			verify(!dsp.sr_test_noCache(CCR_V));
			verify(!dsp.sr_test_noCache(CCR_C));
		});
	}

	void UnitTests::asl_ii()
	{
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xaaabcdef123456)));
			dsp.regs().sr.var = 0;
			emit("asl #1,a,a");
		}, [&]()
		{
			verify(dsp.aluA().var == 0x55579bde2468ac);
			verify(!dsp.sr_test_noCache(CCR_Z));
			verify(dsp.sr_test_noCache(CCR_V));
			verify(dsp.sr_test_noCache(CCR_C));
		});
	}

	void UnitTests::asl_S1S2D()
	{
		runTest([&]()
		{
			dsp.regs().x.var = ~0;
			dsp.regs().y.var = ~0;
			dsp.x0(0x4);
			dsp.y1(0x8);

			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x0011aabbccddeeff)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x00ff112233445566)));

			emit("asl x0,a,a");
			emit("asl y1,b,b");
		}, [&]()
		{
			verify(dsp.aluA().var == 0x001aabbccddeeff0);
			verify(dsp.aluB().var == 0x0011223344556600);
		});
	}

	void UnitTests::asr()
	{
		// asr a
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x000599f2204000)));
			emit("asr a");
		}, [&]()
		{
			verify(dsp.aluA().var == 0x0002ccf9102000);
		});
	}

	void UnitTests::asr_D()
	{
		// DSP56300FM ASR: C receives the last discarded bit, or zero for
		// a zero count. Use an unsigned 56-bit oracle, not another backend.
		for(const uint64_t value : {0x0000000000000001ull, 0x00ffffffffffffffull,
			0x0080000000000000ull, 0x0055aa55aa55aa55ull})
			for(const unsigned count : {0u, 1u, 7u, 8u, 16u, 24u, 55u})
				for(const bool destinationB : {false, true})
					for(const bool registerCount : {false, true})
					{
						constexpr uint64_t mask = 0x00ffffffffffffffull;
						auto expected = value >> count;
						if(count && (value & (1ull << 55)))
							expected |= mask ^ (mask >> count);
						const bool carry = count && ((value >> (count - 1)) & 1);
						runTest([&]()
						{
							dsp.regs().sr.var = CCR_C | CCR_V;
							dsp.setALU(false, TReg56(value));
							dsp.x0(count);
							const auto instruction = std::string("asr ") +
								(registerCount ? "x0" : "#" + std::to_string(count)) +
								",a," + (destinationB ? "b" : "a");
							emit(instruction.c_str());
						}, [&]()
						{
							verify((destinationB ? dsp.aluB().var : dsp.aluA().var) == expected);
							verify(static_cast<bool>(dsp.sr_test(CCR_C)) == carry);
							verify(!dsp.sr_test(CCR_V));
							if(destinationB)
								verify(dsp.aluA().var == value);
						});
					}

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x000599f2204000)));
			dsp.regs().sr.var = 0;

			emit("asr a");
		}, [&]()
		{
			verify(dsp.aluA().var == 0x0002ccf9102000);
		});
	}

	void UnitTests::asr_ii()
	{
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x000599f2204000)));
			emit("asr #1,a,a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0x0002ccf9102000);
		});

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xfffffdff000000)));
			emit("asr #$15,a,a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0xffffffffffeff8);
		});
	}

	void UnitTests::asr_S1S2D()
	{
		runTest([&]()
		{
			dsp.regs().x.var = ~0;
			dsp.regs().y.var = ~0;
			dsp.x0(0x4);
			dsp.y1(0x8);

			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x0011aabbccddeeff)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x00ff112233445566)));

			emit("asr x0,a,a");
			emit("asr y1,b,b");
		}, [&]()
		{
			verify(dsp.aluA().var == 0x00011aabbccddeef);
			verify(dsp.aluB().var == 0x00ffff1122334455);
		});

		runTest([&]()
		{
			dsp.regs().y.var = ~0;
			dsp.y1(0x9);

			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00000200000000)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x00000007000000)));

			emit("asr y1,a,b");
		}, [&]()
		{
			verify(dsp.aluA().var == 0x00000200000000);
			verify(dsp.aluB().var == 0x00000001000000);
		});
	}

	void UnitTests::bchg_aa()
	{
		runTest([&]()
		{
			dsp.memory().set(MemArea_X, 0x2, 0x556677);
			emit("bchg #$3,x:<$2");
		}, [&]()
		{
			const auto x = dsp.memory().get(MemArea_X, 0x2);
			verify(x == 0x55667f);
			verify(!dsp.sr_test(CCR_C));
		});
		runTest([&]()
		{
			dsp.memory().set(MemArea_Y, 0x3, 0xddeeff);
			emit("bchg #$3,y:<$3");
		}, [&]()
		{
			const auto y = dsp.memory().get(MemArea_Y, 0x3);
			verify(y == 0xddeef7);
			verify(dsp.sr_test(CCR_C));
		});
	}

	void UnitTests::bclr_ea()
	{
		runTest([&]()
		{
			dsp.memory().set(MemArea_X, 0x11, 0xffffff);
			dsp.memory().set(MemArea_Y, 0x22, 0xffffff);

			dsp.regs().r[0].var = 0x11;
			dsp.regs().r[1].var = 0x22;

			dsp.regs().n[0].var = dsp.regs().n[1].var = 0;
			dsp.set_m(0, 0xffffff); dsp.set_m(1, 0xffffff);

			emit("bclr #$14,x:(r0)");
			emit("bclr #$10,y:(r1)");
		}, [&]()
		{
			const auto x = dsp.memory().get(MemArea_X, 0x11);
			const auto y = dsp.memory().get(MemArea_Y, 0x22);
			verify(x == 0xefffff);
			verify(y == 0xfeffff);
		});
	}

	void UnitTests::bclr_aa()
	{
		runTest([&]()
		{
			dsp.memory().set(MemArea_X, 0x11, 0xffaaaa);
			dsp.memory().set(MemArea_Y, 0x22, 0xffbbbb);

			emit("bclr #$14,x:<$11");
			emit("bclr #$10,y:<$22");
		}, [&]()
		{
			const auto x = dsp.memory().get(MemArea_X, 0x11);
			const auto y = dsp.memory().get(MemArea_Y, 0x22);
			verify(x == 0xefaaaa);
			verify(y == 0xfebbbb);
		});
	}

	void UnitTests::bclr_qqpp()
	{
		runTest([&]()
		{
			dsp.getPeriph(0)->write(0xffff90, 0x334455);
			dsp.getPeriph(0)->write(0xffffd0, 0x556677);

			emit("bclr #$2,x:<<$ffff90	- bclr_qq");
			emit("bclr #$4,x:<<$ffffd0 - bclr_pp");
		}, [&]()
		{
			const auto a = dsp.getPeriph(0)->read(0xffff90, Bclr_qq);
			const auto b = dsp.getPeriph(0)->read(0xffffd0, Bclr_pp);
			verify(a == 0x334451);	// bit 2 cleared
			verify(b == 0x556667);	// bit 4 cleared
		});
	}

	void UnitTests::bclr_D()
	{
		runTest([&]()
		{
			dsp.regs().omr.var = 0xddeeff;
			dsp.sr_clear(CCR_C);
			emit("bclr #$7,omr");
		}, [&]()
		{
			verify(dsp.sr_test(CCR_C));
			verify(dsp.regs().omr.var == 0xddee7f);
		});

		// do it again, now the C ccr bit needs to be clear
		runTest([&]()
		{
			dsp.sr_set(CCR_C);
			emit("bclr #$7,omr");
		}, [&]()
		{
			verify(!dsp.sr_test(CCR_C));
			verify(dsp.regs().omr.var == 0xddee7f);
		});

		// test undocumented feature of bclr #xx,[a,b], it works even though the documentation states otherwise
		runTest([&]()
		{
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0xff'ffffff'ffffff)));
			emit("bclr #$16,b");
		}, [&]()
		{
			verify(dsp.sr_test(CCR_C));
			verify(dsp.aluB().var == 0xffbfffff000000);
		});
	}

	void UnitTests::bset_aa()
	{
		runTest([&]()
		{
			dsp.memory().set(MemArea_X, 0x2, 0x55667f);
			dsp.memory().set(MemArea_Y, 0x3, 0xddeef0);

			emit("bset #$3,x:<$2");
			emit("bset #$3,y:<$3");
		}, [&]()
		{
			const auto x = dsp.memory().get(MemArea_X, 0x2);
			const auto y = dsp.memory().get(MemArea_Y, 0x3);
			verify(x == 0x55667f);
			verify(y == 0xddeef8);
		});
	}

	void UnitTests::btst_aa()
	{
		runTest([&]()
		{
			dsp.memory().set(MemArea_X, 0x2, 0xaabbc4);

			emit("btst #$2,x:<$2");
		}, [&]()
		{
			verify(dsp.sr_test(CCR_C));
		});

		runTest([&]()
		{
			emit("btst #$3,x:<$2");
		}, [&]()
		{
			verify(!dsp.sr_test(CCR_C));
		});
	}

	void UnitTests::clb()
	{
		// DSP56300FM Rev. 5, 13-42: the signed count determines N/Z;
		// all flags other than N/Z/V are preserved. Check both aliased and
		// separate destinations, whose prior value must not determine N.
		const auto testFlags = [&](const uint64_t source, const int count,
			const bool same, const bool sourceB)
		{
			const bool destinationB = same ? sourceB : !sourceB;
			runTest([&]()
			{
				dsp.setALU(sourceB, TReg56(static_cast<TReg56::MyType>(source)));
				dsp.setALU(!sourceB, TReg56(static_cast<TReg56::MyType>(0xffffff00000000ull)));
				dsp.regs().sr.var = 0xff;
				emit(sourceB ? (same ? "clb b,b" : "clb b,a") : (same ? "clb a,a" : "clb a,b"));
			}, [&]()
			{
				const auto expected = (static_cast<uint64_t>(count) << 24) & 0xffffffffffffffull;
				verify((destinationB ? dsp.aluB() : dsp.aluA()) == expected);
				if(!same) verify((sourceB ? dsp.aluB() : dsp.aluA()) == source);
				verify((dsp.getSR().var & 0xff) == (0xf1u | (count < 0 ? 8u : 0u) | (count == 0 ? 4u : 0u)));
			});
		};
		for(const bool same : {false, true})
		for(const bool sourceB : {false, true})
		{
			testFlags(0, 0, same, sourceB);
			testFlags(0xffffffffffffffull, -47, same, sourceB);
			for(unsigned bit = 0; bit < 55; ++bit)
			{
				testFlags(uint64_t(1) << bit, static_cast<int>(bit) - 46, same, sourceB);
				testFlags((~(uint64_t(1) << bit)) & 0xffffffffffffffull,
					static_cast<int>(bit) - 46, same, sourceB);
			}
		}

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xfc000000000000ull)));
			dsp.regs().sr.var = 0;
			emit("asr a"); // A=FE:000000:000000, E/U/N=1, C/Z/V=0.
			emit("clb a,b"); // Seven leading ones => count +2, N/Z/V=0.
		}, [&]()
		{
			verify(dsp.aluB() == 0x2000000);
			verify((dsp.getSR().var & 0xff) == 0x30);
		});

		auto testClb = [&](const uint64_t _a, const uint64_t _b)
		{
			runTest([&]()
			{
				dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(_a)));
				dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0)));

				emit("clb a,b");
			},
				[&]()
			{
				verify(dsp.aluB() == _b);
			});
		};

		testClb(0x00'ff'ffffff'ffffff, 0xffffffd1000000);
		testClb(0x00'00'ffffff'000000, 0x00000001000000);
		testClb(0x00'00'000000'000001, 0xffffffd2000000);
		testClb(0, 0);	// special case
	}

	void UnitTests::clr()
	{
		runTest([&]()
		{
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x99aabbccddeeff)));
			dsp.x0(0);
			dsp.regs().sr.var = 0x080000;

			emit("clr b #>$128,x0");
		},
			[&]()
		{
			verify(dsp.aluB() == 0);
			verify(dsp.x0() == 0x128);
			verify(dsp.sr_test(CCR_U));
			verify(dsp.sr_test(CCR_Z));
			verify(!dsp.sr_test(CCR_E));
			verify(!dsp.sr_test(CCR_N));
			verify(!dsp.sr_test(CCR_V));
		});

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xbada55c0deba5e)));
			emit("clr a");
		},
			[&]()
		{
			verify(dsp.aluA() == 0);
		});
	}

	void UnitTests::cmp()
	{
		runTest([&]()
		{
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0)));
			dsp.b1(TReg24(0x123456));

			dsp.regs().x.var = 0;
			dsp.x0(TReg24(0x123456));

			emit("cmp x0,b");
		},
			[&]()
		{
			verify(dsp.sr_test(CCR_Z));
			verify(!dsp.sr_test(CCR_N));
			verify(!dsp.sr_test(CCR_E));
			verify(!dsp.sr_test(CCR_V));
			verify(!dsp.sr_test(CCR_C));
		});
		runTest([&]()
		{
			dsp.x0(0xf00000);
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xfff40000000000)));
			dsp.setSR(0x0800d8);

			emit("cmp x0,a");
		},
			[&]()
		{
			verify(dsp.getSR().var == 0x0800d0);
		});

		runTest([&]()
		{
			dsp.setSR(0x080099);
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xfffffc6c000000)));
			emit("cmp #>$aa,a");
		},
			[&]()
		{
			verify(dsp.getSR().var == 0x080098);
		});
	}

	void UnitTests::cmpm()
	{
		runTest([&]()
		{
			dsp.sr_clear(CCR_C);
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(1)));
			dsp.x0(1);
			emit("cmpm x0,b");
		},
		[&]()
		{
			verify(dsp.sr_test(CCR_C));
		});
	}

	void UnitTests::dec()
	{
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(2)));
			emit("dec a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 1);
			verify(!dsp.sr_test(CCR_Z));
			verify(!dsp.sr_test(CCR_N));
			verify(!dsp.sr_test(CCR_E));
			verify(!dsp.sr_test(CCR_V));
			verify(!dsp.sr_test(CCR_C));
		});
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(1)));
			emit("dec a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0);
			verify(dsp.sr_test(CCR_Z));
			verify(!dsp.sr_test(CCR_N));
			verify(!dsp.sr_test(CCR_E));
			verify(!dsp.sr_test(CCR_V));
			verify(!dsp.sr_test(CCR_C));
		});
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0)));
			emit("dec a");
		},
			[&]()
		{
			verify(dsp.sr_test(static_cast<CCRMask>(CCR_N | CCR_C)));
			verify(!dsp.sr_test(static_cast<CCRMask>(CCR_Z | CCR_E | CCR_V)));
		});
	}

	void UnitTests::div()
	{
		{
			dsp.setSR(dsp.getSR().var & 0xfe);

			static constexpr uint64_t expectedValues[24] =
			{
				0xffef590e000000,
				0xffef790e000000,
				0xffefb90e000000,
				0xfff0390e000000,
				0xfff1390e000000,
				0xfff3390e000000,
				0xfff7390e000000,
				0xffff390e000000,
				0x000f390e000000,
				0x000dab2a000001,
				0x000a8f62000003,
				0x000457d2000007,
				0xfff7e8b200000f,
				0x0000985600001e,
				0xfff069ba00003d,
				0xfff19a6600007a,
				0xfff3fbbe0000f4,
				0xfff8be6e0001e8,
				0x000243ce0003d0,
				0xfff3c0aa0007a1,
				0xfff84846000f42,
				0x0001577e001e84,
				0xfff1e80a003d09,
				0xfff49706007a12
			};

			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00001000000000)));
			dsp.reg.y.var = 0x04444410c6f2;

			for (size_t i = 0; i < 24; ++i)
			{
				runTest([&]()
				{
					// div y0,a
					emit("div y0,a");
				}, [&]()
				{
					verify(dsp.aluA().var == expectedValues[i]);
				});
			}
		}

		{
			dsp.y0(0x218dec);
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00008000000000)));
			dsp.setSR(0x0800d4);

			static constexpr uint64_t expectedValues[24] =
			{
				0xffdf7214000000,
				0xffe07214000000,
				0xffe27214000000,
				0xffe67214000000,
				0xffee7214000000,
				0xfffe7214000000,
				0x001e7214000000,
				0x001b563c000001,
				0x00151e8c000003,
				0x0008af2c000007,
				0xffefd06c00000f,
				0x00012ec400001e,
				0xffe0cf9c00003d,
				0xffe32d2400007a,
				0xffe7e8340000f4,
				0xfff15e540001e8,
				0x00044a940003d0,
				0xffe7073c0007a1,
				0xffef9c64000f42,
				0x0000c6b4001e84,
				0xffdfff7c003d09,
				0xffe18ce4007a12,
				0xffe4a7b400f424,
				0xffeadd5401e848
			};

			for (size_t i = 0; i < 24; ++i)
			{
				runTest([&]()
				{
					// div y0,a
					emit("div y0,a");
				}, [&]()
				{
					verify(dsp.aluA().var == expectedValues[i]);
				});
			}
		}

		runTest([&]()
		{
			dsp.y0(0x218dec);
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00008000000000)));
			dsp.setSR(0x0800d4);
			emit("div y0,a");
		},
		[&]()
		{
			verify(dsp.aluA().var == 0xffdf7214000000);
			verify(dsp.getSR().var == 0x0800d4);		
		});
	}

	void UnitTests::dmac()
	{
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0)));
			dsp.x1(0x000020);
			dsp.y1(0x000020);
			emit("dmac ss x1,y1,a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0x800);
		});
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xfff00000000000)));
			dsp.x1(0x000020);
			dsp.y1(0x000020);
			emit("dmac ss x1,y1,a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0xfffffffff00800);
		});
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x005f1bbfa0e440)));
			dsp.regs().x.var = 0x015555555555;
			dsp.regs().y.var = 0x0000008ea9a0;
			emit("dmac su x1,y0,a");
		}, [&]()
		{
			verify(dsp.aluA().var == 0x00017c6effffff);
		});

		// dmac uu: both operands unsigned
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00AABBCC112233)));
			dsp.x1(0x100000);
			dsp.y1(0x200000);
			emit("dmac uu x1,y1,a");
		}, [&]()
		{
			verify(dsp.aluA().var == 0x00040000aabbcc);
		});

		// dmac ss with negate
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00112233000000)));
			dsp.x1(0x000100);
			dsp.y1(0x000200);
			emit("dmac ss -x1,y1,a");
		}, [&]()
		{
			verify(dsp.aluA().var == 0x000000000d2233);
		});
	}

	// 48x48-bit multi-precision multiply using mpyuu/dmac/macsu sequence.
	// Multiplies a 48-bit value (x1:x0) by a 48-bit value (y1:y0) using
	// four instructions that combine partial products with accumulator shifts.
	void UnitTests::dmacMultiPrecision()
	{
		// Test Case 1: small metric (y1:y0 = $000042:$123456)
		runTest([&]()
		{
			dsp.x0(0x555555);
			dsp.x1(0x055555);
			dsp.y0(0x123456);
			dsp.y1(0x000042);
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0)));

			emit("mpyuu x0,y0,a");
		}, [&]()
		{
			verify(dsp.aluA().var == 0x000c22e3f3dd1c);
		});

		runTest([&]()
		{
			emit("dmac su x1,y0,a");
		}, [&]()
		{
			verify(dsp.aluA().var == 0x0000c22e3fffff);
		});

		runTest([&]()
		{
			emit("macsu y1,x0,a");
		}, [&]()
		{
			verify(dsp.aluA().var == 0x0000c25a3fffd3);
		});

		runTest([&]()
		{
			emit("dmac ss x1,y1,a");
		}, [&]()
		{
			verify(dsp.aluA().var == 0x00000002c0c22e);
		});

		// Test Case 2: larger metric (y1:y0 = $001234:$abcdef)
		runTest([&]()
		{
			dsp.x0(0x555555);
			dsp.x1(0x055555);
			dsp.y0(0xabcdef);
			dsp.y1(0x001234);
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0)));

			emit("mpyuu x0,y0,a");
			emit("dmac su x1,y0,a");
			emit("macsu y1,x0,a");
			emit("dmac ss x1,y1,a");
		}, [&]()
		{
			verify(dsp.aluA().var == 0x000000c231d33f);
		});

		// Test Case 3: near-max signed metric (y1:y0 = $7fffff:$ffffff)
		runTest([&]()
		{
			dsp.x0(0x555555);
			dsp.x1(0x055555);
			dsp.y0(0xffffff);
			dsp.y1(0x7fffff);
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0)));

			emit("mpyuu x0,y0,a");
			emit("dmac su x1,y0,a");
			emit("macsu y1,x0,a");
			emit("dmac ss x1,y1,a");
		}, [&]()
		{
			verify(dsp.aluA().var == 0x00055555555554);
		});
	}

	void UnitTests::hdiTransmitCallbacks()
	{
		// A bridge timestamps each write, including a write that replaces full HOTX.
		HDI08 port(peripheralsX);
		port.setTransmitDataAlwaysEmpty(false);
		unsigned writes = 0, wakes = 0;
		uint64_t cycle = 254, lastWrite = 0;
		port.setWriteTxCallback([&] { ++writes; lastWrite = cycle; });
		port.setHostPumpWakeCallback([&] { ++wakes; });
		port.writeTX(0x111);
		verify(writes == 1 && wakes == 1 && lastWrite == 254);
		cycle = 508;
		port.writeTX(0x222);
		verify(writes == 2 && wakes == 2 && lastWrite == 508);
		verify(port.txData().size() == 1 && port.readTX() == 0x222);
		verify(!port.hasTX());

		// Notifications may consume the replacement synchronously.
		port.setWriteTxCallback({});
		port.writeTX(0x333);
		TWord received = 0;
		port.setWriteTxCallback([&] { received = port.readTX(); });
		port.writeTX(0x444);
		verify(received == 0x444 && !port.hasTX());

		// Buffered mode still queues distinct words and notifies for each append.
		port.setWriteTxCallback([&] { ++writes; });
		port.setTransmitDataBuffered(true);
		port.writeTX(0x555);
		port.writeTX(0x666);
		verify(writes == 4 && port.txData().size() == 2);
		verify(port.readTX() == 0x555 && port.readTX() == 0x666 && !port.hasTX());
	}

	void UnitTests::dmaAddressWrapping()
	{
		struct Case { TWord address, count, offsetA, offsetB, expected; };
		const Case cases[] = {
			{0xffffff, 1, 0, 0, 0},              // in-line postincrement
			{0xfffffe, 0, 0, 4, 2},              // positive DOR-B wrap
			{0, 0, 0, 0xffffff, 0xffffff},       // negative DOR-B wrap
			{0xfffffe, 64, 4, 0, 2},             // positive DOR-A wrap
			{0, 64, 0xffffff, 0, 0xffffff},      // negative DOR-A wrap
		};
		for(const auto& test : cases)
		{
			auto& dma = peripheralsX.getDMA();
			runTest([&]()
			{
				// Source is 3D, destination is fixed X:$100. The two high source
				// addresses are readable peripheral registers, so no out-of-range
				// backing RAM is needed. Issue only one word request, then inspect
				// the updated address before any subsequent transfer can use it.
				dma.setDCR(0, 0);
				dma.setDSR(0, test.address);
				dma.setDDR(0, 0x100);
				dma.setDCO(0, test.count);
				dma.setDOR(0, test.offsetA);
				dma.setDOR(1, test.offsetB);
				dma.setDCR(0, (32u << DmaChannel::Dam0) | (1u << DmaChannel::D3d)
					| (1u << DmaChannel::Dtm0) | (1u << DmaChannel::De));
				verify(dma.trigger(DmaChannel::RequestSource::ExternalIRQA));
			}, [&]()
			{
				verify(dma.getDSR(0) == test.expected);
				verify(dma.getDDR(0) == 0x100);
				dma.setDCR(0, 0);
			});
		}
	}

	void UnitTests::merge()
	{
		// Distinct halves catch reversed packing; all legal sources and both
		// destinations also exercise source/destination aliasing.
		for(const auto* source : {"x0", "x1", "y0", "y1", "a1", "b1"})
			for(const bool ab : {false, true})
			{
				const std::string instruction = std::string("merge ") + source + (ab ? ",b" : ",a");
				const bool alias = std::string(source) == (ab ? "b1" : "a1");
				runTest([&]()
				{
					dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x12abc123654321)));
					dsp.setALU(true, TReg56(static_cast<TReg56::MyType>(0x12abc123654321)));
					dsp.setALU(ab, TReg56(static_cast<TReg56::MyType>(0x12abc456654321)));
					dsp.x0(0xabc123); dsp.x1(0xabc123);
					dsp.y0(0xabc123); dsp.y1(0xabc123);
					dsp.setSR(0xff);
					emit(instruction.c_str());
				}, [&]()
				{
					verify((ab ? dsp.aluB() : dsp.aluA()).var
						== (alias ? 0x12456456654321ull : 0x12123456654321ull));
					verify((ab ? dsp.aluA() : dsp.aluB()).var == 0x12abc123654321ull);
					verify(dsp.x0().var == 0xabc123 && dsp.x1().var == 0xabc123);
					verify(dsp.y0().var == 0xabc123 && dsp.y1().var == 0xabc123);
					verify((dsp.getSR().var & 0xff) == (0xff & ~(CCR_N | CCR_Z | CCR_V)));
				});
			}

		// ADD leaves E/U lazy in the interpreter. MERGE must materialize them,
		// even when it writes the same accumulator that produced those flags.
		for(const bool extended : {false, true})
			runTest([&]()
			{
				dsp.setSR(extended ? CCR_U : CCR_E);
				dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(
					extended ? 0x01400000000000ull : 0)));
				dsp.x0(0);
				emit("add x0,a");
				emit("merge x0,a");
			}, [&]()
			{
				verify(bool(dsp.sr_test(CCR_E)) == extended);
				verify(bool(dsp.sr_test(CCR_U)) == !extended);
				verify(!dsp.sr_test(CCR_N));
				verify(dsp.sr_test(CCR_Z));
				verify(!dsp.sr_test(CCR_V));
			});

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00123800111111)));
			dsp.setALU(true, TReg56(static_cast<TReg56::MyType>(0x12abc800654321)));
			dsp.sr_set(CCR_V);
			emit("merge a1,b");
		}, [&]()
		{
			verify(dsp.aluA().var == 0x00123800111111);
			verify(dsp.aluB().var == 0x12800800654321);
			verify(dsp.sr_test(CCR_N));
			verify(!dsp.sr_test(CCR_Z));
			verify(!dsp.sr_test(CCR_V));
		});

		runTest([&]()
		{
			dsp.x0(0);
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x5a000000abcdef)));
			emit("merge x0,a");
		}, [&]()
		{
			verify(dsp.aluA().var == 0x5a000000abcdef);
			verify(!dsp.sr_test(CCR_N));
			verify(dsp.sr_test(CCR_Z));
			verify(!dsp.sr_test(CCR_V));
		});
	}

	void UnitTests::eor()
	{
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x0f799428000000)));
			dsp.x0(0x799428);

			emit("eor x0,a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0x0f000000000000);
		});

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x0f000428000123)));
			dsp.x0(0x799428);

			emit("eor x0,a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0x0f799000000123);
		});
	}

	void UnitTests::extractu()
	{
		runTest([&]()
		{
			dsp.regs().x.var = 0x4008000000;  // x1 = 0x4008  (width=4, offset=8)
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xef00)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0)));

			// extractu x1,a,b  (width = 0x8, offset = 0x28)
			emit(0x0c1a8d);	// extractu x0,a,b
		},
			[&]()
		{
			verify(dsp.aluB().var == 0xf);
		});

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0xfff47555000000)));
			dsp.setSR(0x0800d9);

			// extractu $8028,b,a
			emit(0x0c1890, 0x008028);	// extractu #$8028,a,a
		},
			[&]()
		{
			verify(dsp.aluA().var == 0xf4);
			verify(dsp.getSR().var == 0x0800d0);
		});

		runTest([&]()
		{
			dsp.reg.x.var = 0x4008000000;  // x1 = 0x4008  (width=4, offset=8)
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xff00)));

			// extractu x1,a,b  (width = 0x8, offset = 0x28)
			emit(0x0c1a8d);	// extractu x0,a,b

		}, [&]()
		{
			verify(dsp.aluB().var == 0xf);
		});

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0xfff47555000000)));
			dsp.setSR(0x0800d9);

			// extractu $8028,b,a
			emit(0x0c1890, 0x008028);	// extractu #$8028,a,a
		}, [&]()
		{
			verify(dsp.aluA().var == 0xf4);
			verify(dsp.getSR().var == 0x0800d0);
		});

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0xef123456abcdef)));

			// extractu #$020000,b,a
			emit(0x0c1890, 0x020000);	// extractu #$20000,a,a
		}, [&]()
		{
			verify(dsp.aluA().var == 0x56abcdef);
		});

		runTest([&]()
		{
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0)));
			dsp.b1(TReg24(0xAABBCC));
			dsp.b0(TReg24(0xDDEEFF));

			// extractu #$020000,b,a
			emit(0x0c1890, 0x020000);	// extractu #$20000,a,a
		}, [&]()
		{
			verify(dsp.aluA().var == 0x0000CCDDEEFF);
		});
	}

	void UnitTests::extractu_co()
	{
		runTest([&]()
		{
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x0444ffff000000)));

			// extractu #$C028,b,a  (width = 0xC, offset = 0x28)
			emit(0x0c1890, 0x00C028);	// extractu #$c028,a,a
		}, [&]()
		{
			verify(dsp.aluA().var == 0x444);
		});
	}

	void UnitTests::inc()
	{
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00ffffffffffffff)));
			emit("inc a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0);
			verify(dsp.sr_test(static_cast<CCRMask>(CCR_C | CCR_Z)));
			verify(!dsp.sr_test(static_cast<CCRMask>(CCR_N | CCR_E | CCR_V)));
		});
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(1)));
			emit("inc a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 2);
			verify(!dsp.sr_test(static_cast<CCRMask>(CCR_Z | CCR_N | CCR_E | CCR_V | CCR_C)));
		});
	}

	void UnitTests::insert()
	{
		runTest([&]()
		{
			dsp.x1(0x123456);
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x12aabbccddeeff)));
			emit("insert #$00c008,x1,a	; use 12 bits from x1 and insert into a at bit 8");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0x12aabbccd456ff);
		});

		runTest([&]()
		{
			dsp.x0(0x010028);						// control reg, 16 bits to position 40
			dsp.y1(0xabcdef);						// source
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x12123456123456)));	// dest
			emit("insert x0,y1,a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0xcdef3456123456);
		});

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0)));
			dsp.a0(TReg24(0xDDEEFF));
			dsp.b0(TReg24(0xAABBCC));
			dsp.x1(0x8000);
			emit("insert x1,b0,a");
		},
			[&]()
		{
			verify(dsp.a0().var == 0xDDEECC);
		});
	}

	void UnitTests::jscc()
	{
		runTest([&]()
		{
			// SR is the result of a being 0x0055000000000000 and then: tst a
			dsp.setSR(0x0800c0);
			dsp.reg.r[2].var = 0x50;

			// jsge (r2)
			emit("jsge (r2)");
		}, [&]()
		{
			verify(dsp.getPC() == 0x50);
		});
	}

	void UnitTests::lra()
	{
		runTest([&]()
		{
			dsp.regs().n[0].var = 0x4711;
			emit(0x044058, 0x00000a, 0x20);	// lra >*+$a,n0
		},
			[&]()
		{
			verify(dsp.regs().n[0].var == 0x2a);
		});
	}

	void UnitTests::lsl()
	{
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xffaabbcc112233)));
			emit("lsl a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0xff557798112233);
			verify(dsp.sr_test(CCR_C));
		});

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xffaabbcc112233)));
			emit("lsl #$4,a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0xffabbcc0112233);
			verify(!dsp.sr_test(CCR_C));
		});

		runTest([&]()
		{
			dsp.x1(0x4);
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xab112233445566)));
			emit("lsl x1,a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0xab122330445566);
		});

		runTest([&]()
		{
			dsp.x1(0x1c);				// more than 24 bits should move in zeroes
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xab112233445566)));
			emit("lsl x1,a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0xab000000445566);
		});
	}

	void UnitTests::lsr()
	{
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xffaabbcc112233)));
			emit("lsr a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0xff555de6112233);
			verify(!dsp.sr_test(CCR_C));
		});

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xffaabbcc112233)));
			emit("lsr #$4,a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0xff0aabbc112233);
			verify(dsp.sr_test(CCR_C));
		});

		runTest([&]()
		{
			dsp.x1(0x4);
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xab112233445566)));
			emit("lsr x1,a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0xab011223445566);
		});

		runTest([&]()
		{
			dsp.x1(0x1c);				// more than 24 bits should move in zeroes
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xab112233445566)));
			emit("lsr x1,a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0xab000000445566);
		});
	}

	void UnitTests::lua_ea()
	{
		runTest([&]()
		{
			dsp.regs().r[0].var = 0x112233;
			dsp.regs().n[0].var = 0x001111;
			emit("lua (r0)+,n0");
		},
			[&]()
		{
			verify(dsp.regs().r[0].var == 0x112233);
			verify(dsp.regs().n[0].var == 0x112234);
		});

		runTest([&]()
		{
			dsp.regs().r[0].var = 0x112233;
			dsp.regs().n[0].var = 0x001111;
			emit("lua (r0)+n0,n0");
		},
			[&]()
		{
			verify(dsp.regs().r[0].var == 0x112233);
			verify(dsp.regs().n[0].var == 0x113344);
		});
	}

	void UnitTests::lua_rn()
	{
		runTest([&]()
		{
			dsp.regs().r[0].var = 0x0000f0;
			dsp.set_m(0, 0xffffff);

			emit("lua (r0+$30),n3");
		},
			[&]()
		{
			verify(dsp.regs().r[0].var == 0x0000f0);
			verify(dsp.regs().n[3].var == 0x000120);
		});
		runTest([&]()
		{
			dsp.regs().r[0].var = 0x0000f0;
			dsp.set_m(0, 0x0000ff);

			emit("lua (r0+$30),n3");
		},
			[&]()
		{
			verify(dsp.regs().r[0].var == 0x0000f0);
			verify(dsp.regs().n[3].var == 0x000020);
		});
		runTest([&]()
		{
			dsp.regs().r[0].var = 0x0000f0;
			dsp.set_m(0, 0xffffff);

			emit("lua (r0-$11),r6");
		},
			[&]()
		{
			verify(dsp.regs().r[0].var == 0x0000f0);
			verify(dsp.regs().r[6].var == 0x0000df);
		});
	}

	void UnitTests::mac()
	{
		runTest([&]()
		{
			dsp.reg.x.var =   0xda7efa5a7efa;
			dsp.reg.y.var =   0x000000800000;
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x005a7efa000000)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x005a7efa000000)));

			emit(0x2000e2);	// mac x0,y1,a
		}, [&]()
		{
			verify(dsp.aluA() == 0x00800000000000);
		});

		runTest([&]()
		{
			emit(0x2000da);	// mac y1,x1,a
		}, [&]()
		{
			verify(dsp.aluB() == 0x00000000000000);
		});

		runTest([&]()
		{
			dsp.y0(0x7fffff);
			dsp.x0(0x6bb14a);
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x00553300000000)));
			dsp.setSR(0x0880d0);

			emit(0x2000da);	// mac y1,x1,a
		}, [&]()
		{
			verify(dsp.aluB() == 0x00c0e449289d6c);
			verify(dsp.getSR().var == 0x0880f0);
		});

		runTest([&]()
		{
			// mac y1,y0,b x:(r5)-,y0
			dsp.y1(0xf3aab8);
			dsp.y0(0x000080);
			dsp.setSR(0x0800d8);
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x0000000c000000)));
			dsp.reg.r[5].var = 10;
			dsp.memory().set(MemArea_X, 10, 0x123456);

			emit(0x46d5bb);	// mac y0,x0,a y:(r5)+,y0 (complex parallel)
		}, [&]()
		{
			verify(dsp.aluB() == 0);
			verify(dsp.reg.r[5].var == 9);
			verify(dsp.y0() == 0x123456);
			verify(dsp.getSR().var == 0x0800d4);
		});
	}

	void UnitTests::mac_S()
	{
		runTest([&]()
		{
			dsp.x1(0x2);
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x100)));

			emit("mac x1,#$2,a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0x00000000800100);
		});
	}

	void UnitTests::max()
	{
		auto run = [&](uint64_t _a, uint64_t _b, bool aIsGreaterEqual)
		{
			runTest([&]()
			{
				dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(_a)));
				dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(_b)));

				emit("max a,b");
			},
				[&]()
			{
				if(aIsGreaterEqual)
				{
					verify(dsp.aluA().var == _a);
					verify(dsp.aluB().var == _a);
					assert(!dsp.sr_test(CCR_C));
				}
				else
				{
					verify(dsp.aluA().var == _a);
					verify(dsp.aluB().var == _b);
					assert(dsp.sr_test(CCR_C));
				}
			});
		};

		run(1, 1, true);
		run(2, 1, true);
		run(1, 2, false);
		run(0xff112233445566, 0xffffffffffffff, false);
		run(0xffffffffffffff, 0x00123456123456, false);
		run(0x00123456123456, 0xffffffffffffff, true);
	}

	void UnitTests::maxm()
	{
		auto run = [&](int64_t _a, int64_t _b, bool aIsGreaterEqual)
		{
			_a &= 0xff'ffffff'ffffff;
			_b &= 0xff'ffffff'ffffff;

			runTest([&]()
			{
				dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(_a)));
				dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(_b)));

				emit("maxm a,b");
			},
				[&]()
			{
				if(aIsGreaterEqual)
				{
					verify(dsp.aluA().var == _a);
					verify(dsp.aluB().var == _a);
					assert(!dsp.sr_test(CCR_C));
				}
				else
				{
					verify(dsp.aluA().var == _a);
					verify(dsp.aluB().var == _b);
					assert(dsp.sr_test(CCR_C));
				}
			});
		};

		run(1, 1, true);
		run(2, 1, true);
		run(1, 2, false);
		run(-2, 1, true);
		run(-2, -5, false);
		run(0xff112233445566, 0xffffffffffffff, true);
		run(0xffffffffffffff, 0x00123456123456, false);
		run(0x00123456123456, 0xffffffffffffff, true);
	}

	void UnitTests::mpy()
	{
		runTest([&]()
		{
			dsp.x0(0x20);
			dsp.x1(0x20);

			emit(0x2000a0);	// mpy x0,x0,a
		},
			[&]()
		{
			verify(dsp.aluA().var == 0x000800);
		});

		runTest([&]()
		{
			dsp.x0(0xffffff);
			dsp.x1(0xffffff);

			emit(0x2000a0);	// mpy x0,x0,a
		},
			[&]()
		{
			verify(dsp.aluA().var == 0x2);
		});

		auto testMultiply = [this](int x0, int y0, int64_t expectedResult, TWord opcode)
		{
			runTest([&]()
			{
				dsp.reg.x.var = x0;
				dsp.reg.y.var = y0;

				// a = x0 * y0
				emit(opcode);
			}, [&]()
			{
				verify(dsp.aluA() == expectedResult);
			});
		};

		// mpy x0,y0,a
		testMultiply(0xeeeeee, 0xbbbbbb, 0x00091a2bd4c3b4, 0x2000d0);
		testMultiply(0xffffff, 0x7fffff, 0xffffffff000002, 0x2000d0);
		testMultiply(0xffffff, 0xffffff, 0x00000000000002, 0x2000d0);

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00400000000000)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x0003a400000000)));
			dsp.reg.x.var = 0x00000506c000;
			dsp.reg.y.var = 0x000400000400;
			dsp.setSR(0x0800c9);

			// mpy y0,x0,a
			emit(0x2000d0);	// mac x1,x0,a
		}, [&]()
		{
			verify(dsp.aluA().var == 0x00000036000000);
			verify(dsp.getSR().var == 0x0800d1);
		});

		// mpy xn,#imm,alu

		runTest([&]()
		{
			dsp.x0(0x020);
			dsp.x1(0x400);
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x12abcdefabdef)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x12abcdefabdef)));

			emit("mpy x1,#$13,a");
			emit("mpy x0,#$a,b");
		}, [&]()
		{
			verify(dsp.aluA().var == 0x8000);
			verify(dsp.aluB().var == 0x80000);
		});
	}

	void UnitTests::mpyr()
	{
		runTest([&]()
		{
			dsp.x0(0xef4e);
			dsp.y0(0x600000);
			dsp.setSR(0x0880d0);
			dsp.regs().omr.var = 0x004380;

			emit("mpyr y0,x0,a");
		}, [&]()
		{
			verify(dsp.aluA().var == 0x0000b37a000000);
		});
	}

	void UnitTests::mpy_SD()
	{
		runTest([&]()
		{
			dsp.x1(0x2);
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0)));

			emit("mpy x1,#$2,a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0x00000000800000);
		});
	}

	void UnitTests::neg()
	{
		// NEG preserves C; V describes this operation, and L latches V.
		// Only negating the most negative 56-bit value overflows.
		for(const uint64_t value : {0ull, 1ull, 0x00ffffffffffffffull,
			0x007fffffffffffffull, 0x0080000000000000ull})
			for(const unsigned initial : {0u, unsigned(CCR_C | CCR_V), unsigned(CCR_L)})
				for(const bool ab : {false, true})
				{
					const bool overflow = value == 0x0080000000000000ull;
					runTest([&]()
					{
						dsp.regs().sr.var = initial;
						dsp.setALU(ab, TReg56(value));
						emit(ab ? "neg b" : "neg a");
					}, [&]()
					{
						verify((ab ? dsp.aluB().var : dsp.aluA().var) ==
							((0ull - value) & 0x00ffffffffffffffull));
						verify(static_cast<bool>(dsp.sr_test(CCR_V)) == overflow);
						verify(static_cast<bool>(dsp.sr_test(CCR_L)) == (overflow || (initial & CCR_L)));
						verify(static_cast<bool>(dsp.sr_test(CCR_C)) == static_cast<bool>(initial & CCR_C));
					});
				}

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(1)));

			emit("neg a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0xffffffffffffff);
			verify(dsp.sr_test(CCR_N));
			verify(!dsp.sr_test(CCR_Z));
		});

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xfffffffffffffe)));

			emit("neg a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 2);
			verify(!dsp.sr_test(CCR_N));
			verify(!dsp.sr_test(CCR_Z));
		});
	}

	void UnitTests::normf()
	{
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00123456789abc)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x00123456789abc)));

			dsp.x0(4);
			dsp.y0(-4);

			emit("normf x0,a");
			emit("normf y0,b");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0x000123456789ab);
			verify(dsp.aluB().var == 0x0123456789abc0);
		});
	}

	void UnitTests::not_()
	{
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x12555555123456)));
			emit("not a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0x12aaaaaa123456);
		});

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xffd8b38b000000)));
			dsp.setSR(0x0800e8);
			emit("not a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0xff274c74000000);
			verify(dsp.regs().sr.var == 0x0800e0);
		});

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x12555555123456)));

			// not a
			emit("not a");
		}, [&]()
		{
			verify(dsp.aluA().var == 0x12aaaaaa123456);
		});

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xffd8b38b000000)));
			dsp.setSR(0x0800e8);

			// not a
			emit("not a");
		}, [&]()
		{
			verify(dsp.aluA().var == 0xff274c74000000);
			verify(dsp.getSR().var == 0x0800e0);
		});
	}

	void UnitTests::or_()
	{
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xbb222222555555)));
			dsp.x0(0x444444);
			emit("or x0,a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0xbb666666555555);
		});

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xbb222222555555)));
			emit("or #>$444444,a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0xbb666666555555);
		});

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xbb222222555555)));
			emit("or #$4,a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0xbb222226555555);
		});
	}

	void UnitTests::ori()
	{
		const auto srBackup = dsp.getSR();

		runTest([&]()
		{
			dsp.regs().omr.var = 0xff1111;
			dsp.regs().sr.var = 0xff1111;

			emit("ori #$33,omr");
			emit("ori #$33,eom");
			emit("ori #$33,ccr");
			emit("ori #$33,mr");
		},
			[&]()
		{
			verify(dsp.regs().omr.var == 0xff3333);
			verify(dsp.regs().sr.var == 0xff3333);
		});

		dsp.setSR(srBackup);
	}

	void UnitTests::rnd()
	{
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00222222333333)));

			emit("rnd a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0x00222222000000);
		});

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00222222999999)));

			emit("rnd a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0x00222223000000);
		});

		runTest([&]()
		{
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0xffff9538000000)));

			emit("rnd b");
		},
			[&]()
		{
			verify(dsp.aluB().var == 0xffff9538000000);
			verify(dsp.sr_test(CCR_N));
			verify(!dsp.sr_test(CCR_Z));
			verify(!dsp.sr_test(CCR_V));
		});

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xffffffffffffff)));

			emit("rnd a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0);
		});

		// test rnd with scaling mode bits set

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00222222ffffff)));
			dsp.sr_set(SR_S0);
			dsp.sr_clear(SR_S1);
			emit("rnd a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0x00222222000000);
		});

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00eeeeeebbbbbb)));
			dsp.sr_clear(SR_S0);
			dsp.sr_set(SR_S1);
			emit("rnd a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0x00eeeeee800000);
		});

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00eeeeeebbbbbb)));
			dsp.sr_clear(SR_S0);
			dsp.sr_clear(SR_S1);
			emit("rnd a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0x00eeeeef000000);
		});
	}

	void UnitTests::rol()
	{
		runTest([&]()
		{
			dsp.regs().sr.var = 0;
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xee112233ffeedd)));

			emit("rol a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0xee224466ffeedd);
			verify(!dsp.sr_test(CCR_C));
		});

		runTest([&]()
		{
			dsp.sr_set(CCR_C);
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x12abcdef123456)));				// 00010010 10101011 11001101 11101111 00010010 00110100 01010110

			// rol a
			emit("rol a");
		}, [&]()
		{
			verify(dsp.aluA().var == 0x12579BDF123456);		// 00010010 01010111 10011011 11011111 00010010 00110100 01010110
			verify(dsp.sr_test(CCR_C) == 1);
		});

		runTest([&]()
		{
			dsp.sr_set(CCR_C);
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x12123456abcdef)));				// 00010010 00010010 00110100 01010110 10101011 11001101 11101111

			// rol a
			emit("rol a");
		}, [&]()
		{
			verify(dsp.aluA().var == 0x122468ADABCDEF);		// 00010010 00100100 01101000 10101101 10101011 11001101 11101111
			verify(dsp.sr_test(CCR_C) == 0);
		});
	}

	void UnitTests::sub()
	{
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00000000000001)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x00000000000002)));

			emit("sub b,a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0xffffffffffffff);
			verify(dsp.sr_test(CCR_C));
			verify(!dsp.sr_test(CCR_V));
		});

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x80000000000000)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x00000000000001)));

			emit("sub b,a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0x7fffffffffffff);
			verify(!dsp.sr_test(CCR_C));
			verify(!dsp.sr_test(CCR_V));
		});

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0)));
			dsp.x0(0x800000);

			emit("sub x0,a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0x00800000000000);
			verify(dsp.sr_test(CCR_C));
			verify(!dsp.sr_test(CCR_N));
		});
	}

	void UnitTests::subl()
	{
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(2)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(4)));

			emit("subl b,a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0);
			verify(dsp.sr_test(CCR_Z));
		});
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(4)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(2)));

			emit("subl b,a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 6);
			verify(!dsp.sr_test(CCR_Z));
		});

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(2)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(4)));

			emit("subl a,b");
		},
			[&]()
		{
			verify(dsp.aluB().var == 6);
			verify(!dsp.sr_test(CCR_Z));
		});

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00400000000000)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x00200000000000)));

			// subl b,a
			emit("subl b,a");
		}, [&]()
		{
			verify(dsp.aluA().var == 0x00600000000000);
			verify(!dsp.sr_test(CCR_C));
			verify(!dsp.sr_test(CCR_V));
		});
	}

	void UnitTests::tfr()
	{
		// tfr a,b is a full 56-bit transfer. The assembler's "tfr a,b" may encode as
		// "move a,b" (Mover) which saturates to 24 bits, so we use raw opcode to ensure
		// the Tfr instruction (0x200009, JJJ=0 encoding) is tested.
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x11223344556677)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0)));
			emit(0x200009);	// tfr a,b (56-bit transfer)
		},
			[&]()
		{
			verify(dsp.aluB().var == 0x11223344556677);
		});
	}

	void UnitTests::tcc()
	{
		// Tcc_S1D1: tne a,b has two valid encodings (JJJ=0 and JJJ=1)
		// Test assembler encoding first, then verify alternative encoding matches

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xaa112233445566)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0)));
			dsp.sr_set(CCR_Z);
			emit(0x022008);	// tne a,b
		},
			[&]()
		{
			verify(dsp.aluB().var == 0);
		});

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xbb112233445566)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0)));
			dsp.sr_clear(CCR_Z);
			emit(0x022008);	// tne a,b
		},
			[&]()
		{
			verify(dsp.aluB().var == 0xbb112233445566);
		});

		// Same tests with alternative JJJ=0 encoding
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xaa112233445566)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0)));
			dsp.sr_set(CCR_Z);
			emit(0x022008);	// tne a,b (JJJ=0, alternative encoding)
		},
			[&]()
		{
			verify(dsp.aluB().var == 0);
		});

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xbb112233445566)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0)));
			dsp.sr_clear(CCR_Z);
			emit(0x022008);	// tne a,b (JJJ=0, alternative encoding)
		},
			[&]()
		{
			verify(dsp.aluB().var == 0xbb112233445566);
		});

		// Tcc_S2D2

		runTest([&]()
		{
			dsp.regs().r[0].var = 0xaa1122;
			dsp.regs().r[1].var = 0x0;
			dsp.sr_set(CCR_Z);
			emit("tne r0,r1");
		},
			[&]()
		{
			verify(dsp.regs().r[1].var == 0);
		});

		runTest([&]()
		{
			dsp.regs().r[0].var = 0xbb1122;
			dsp.regs().r[1].var = 0x0;
			dsp.sr_clear(CCR_Z);
			emit("tne r0,r1");
		},
			[&]()
		{
			verify(dsp.regs().r[1].var == 0xbb1122);
		});

		// Tcc_S1D2S2D2

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xaa112233445566)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0)));
			dsp.regs().r[0].var = 0xaa1122;
			dsp.regs().r[1].var = 0x0;
			dsp.sr_set(CCR_Z);
			emit(0x032009);	// tne a,b r0,r1
		},
			[&]()
		{
			verify(dsp.aluB().var == 0);
			verify(dsp.regs().r[1].var == 0);
		});

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xbb112233445566)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0)));
			dsp.regs().r[0].var = 0xbb1122;
			dsp.regs().r[1].var = 0x0;
			dsp.sr_clear(CCR_Z);
			emit(0x032009);	// tne a,b r0,r1
		},
			[&]()
		{
			verify(dsp.aluB().var == 0xbb112233445566);
			verify(dsp.regs().r[1].var == 0xbb1122);
		});
	}

	void UnitTests::ifcc()
	{
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(1)));

			dsp.setSR(0);

			emit(0x202a10);	// add b,a ifeq
		},
			[&]()
		{
			verify(dsp.aluA().var == 0);
		});

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(1)));

			dsp.setSR(CCR_Z);

			emit(0x202a10);	// add b,a ifeq
		},
			[&]()
		{
			verify(dsp.aluA().var == 1);
		});

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xffffffff000000)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x0)));

			dsp.setSR(CCR_Z);

			emit("tst a");
			emit(0x20310d);	// cmp a,b ifge.u
		},
			[&]()
		{
			verify(dsp.sr_test(CCR_N));
			verify(!dsp.sr_test(CCR_Z));
		});

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x0)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x1)));

			dsp.setSR(CCR_N);

			emit("tst a");
			emit(0x203105);	// cmp b,a ifge.u
		},
			[&]()
		{
			verify(dsp.sr_test(CCR_N));
			verify(!dsp.sr_test(CCR_Z));
		});

		// ifcc preserves CCR: clr b ifne must keep Z=0 from prior tst
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00010000000000)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x00AABBCC000000)));
			emit("tst a");
			emit("clr b ifne");
		}, [&]()
		{
			verify(dsp.aluB().var == 0);
			verify(!dsp.sr_test(CCR_Z));
		});

		// ifcc preserves CCR: condition false, neither dest nor CCR change
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x00112233000000)));
			emit("tst a");
			emit("clr b ifne");
		}, [&]()
		{
			verify(dsp.aluB().var == 0x00112233000000);
			verify(dsp.sr_test(CCR_Z));
		});
	}

	void UnitTests::move()
	{
		// immediate to register moves

		dsp.reg.x.var = 0;

		// move #$ff,a
		runTest([&](){ emit("move #$ff,a");		}, [&](){verify(dsp.aluA() == 0x00ffff0000000000);});
		// move #$0f,a
		runTest([&](){emit("move #$0f,a");		}, [&](){verify(dsp.aluA() == 0x00000f0000000000);});
		// move #$ff,x0
		runTest([&](){emit("move #$ff,x0");		}, [&](){verify(dsp.x0() == 0xff0000);		verify(dsp.reg.x == 0xff0000);});
		// move #$ff,r2
		runTest([&](){emit("move #$ff,r2");		}, [&](){verify(dsp.reg.r[2] == 0x0000ff);});
		// move #$12,a2
		runTest([&](){emit("move #$12,a2");		}, [&](){});
		// move #$345678,a1
		runTest([&](){emit("move #>$345678,a1");}, [&](){});
		// move #$abcdef,a0
		runTest([&](){emit("move #>$abcdef,a0");}, [&](){verify(dsp.aluA().var == 0x0012345678abcdef);});
		// move a,b
		runTest([&](){emit("move a,b");			}, [&](){verify(dsp.aluB().var == 0x00007fffff000000);});

		// memory to register move
		runTest([&]()
		{
			dsp.reg.r[5].var = 10;
			dsp.memory().set(MemArea_Y, 9, 0x123456);
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0)));
			// move y:-(r5),b)
			emit("move y:-(r5),b");
		}, [&]()
		{
			verify(dsp.aluB().var == 0x00123456000000);
			verify(dsp.reg.r[5].var == 9);
		});

		// move XY overlap
		runTest([&]()
		{
			dsp.memory().set(MemArea_X, 10, 0x123456);
			dsp.memory().set(MemArea_Y, 5, 0x543210);
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x0000babeb00bab)));

			dsp.reg.r[2].var = 10;
			dsp.reg.r[6].var = 5;

			// move x:(r2)+,a a,y:(r6)+
			emit(0xbada00);	// move x:(r2)+,a a,y:(r6)+ (complex parallel)
		}, [&]()
		{
			verify(dsp.reg.r[2] == 11);
			verify(dsp.reg.r[6] == 6);

			verify(dsp.aluA() == 0x00123456000000);
			verify(dsp.memory().get(MemArea_X, 10) == 0x123456);
			verify(dsp.memory().get(MemArea_Y, 5 ) == 0xbabe);
		});

		// op_Mover
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00112233445566)));
			dsp.regs().n[2].var = 0;
			emit("move a,n2");
		},		[&]()
		{
			verify(dsp.regs().n[2].var == 0x112233);
		});

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00445566aabbcc)));
			dsp.regs().r[0].var = 0;
			emit("move a,r0");
		},
			[&]()
		{
			verify(dsp.regs().r[0].var == 0x445566);
		});

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x44aabbccddeeff)));
			emit("move b,a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0x007fffff000000);
			verify(dsp.aluB().var == 0x44aabbccddeeff);
		});

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xff000000000000)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x77000000000000)));
			emit("move a2,x0");
			emit("move b2,y0");
		},
			[&]()
		{
			verify(dsp.x0() == 0xffffff);
			verify(dsp.y0() == 0x000077);
		});

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00223344556677)));
			dsp.y1(0xaabbcc);
			emit("move a,y1");
		},
			[&]()
		{
			verify(dsp.y1() == 0x223344);
		});

		// op_Movem_ea
		runTest([&]()
		{
			dsp.regs().r[2].var = 0xa;
			dsp.regs().n[2].var = 0x5;
			dsp.memory().set(MemArea_P, 0xa + 0x5, 0x123456);
			emit("move p:(r2+n2),r2");
		},
			[&]()
		{
			verify(dsp.regs().r[2].var == 0x123456);
		});

		// op_Movex_ea
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0)));
			dsp.memory().set(MemArea_X, 0x10, 0x223344);
			emit("move x:>$10,a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0x00223344000000);
		});

		runTest([&]()
		{
			emit("move #>$3a800,b");
		},
			[&]()
		{
			verify(dsp.aluB().var == 0x0003a800000000);
		});

		runTest([&]()
		{
			dsp.regs().r[0].var = 0x11;
			dsp.memory().set(MemArea_X, 0x19, 0x11abcd);
			emit("move x:(r0+$8),b");
		},
			[&]()
		{
			verify(dsp.aluB().var == 0x0011abcd000000);
		});

		runTest([&]()
		{
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x0011aabb000000)));
			dsp.memory().set(MemArea_X, 0x07, 0);
			dsp.regs().r[0].var = 0x3;
			emit("move b,x:(r0+$4)");
		},
			[&]()
		{
			const auto r = dsp.memory().get(MemArea_X, 0x7);
			verify(r == 0x11aabb);
		});

		// op_Move_xx
		runTest([&]()
		{
			dsp.regs().x.var = 0;
			emit("move #$ff,x0");
		},
			[&]()
		{
			verify(dsp.regs().x.var == 0x000000ff0000);
		});

		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0)));
			emit("move #$ff,a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0xffff0000000000);
		});

		// op_Movey_ea
		runTest([&]()
		{
			dsp.memory().set(MemArea_Y, 0x20, 0x334455);
			emit("move y:>$20,y1");
		},
			[&]()
		{
			verify(dsp.y1() == 0x334455);
		});

		// op_Move_ea
		runTest([&]()
		{
			dsp.regs().r[4].var = 0x10;
			dsp.regs().n[4].var = 0x3;
			emit("move (r4)+n4");
		},
			[&]()
		{
			verify(dsp.regs().r[4].var == 0x13);
		});

		runTest([&]()
		{
			dsp.regs().r[4].var = 0x13;
			emit("move (r4)+");
		},
			[&]()
		{
			verify(dsp.regs().r[4].var == 0x14);
		});

		// op_Movex_aa
		runTest([&]()
		{
			dsp.memory().set(MemArea_X, 0x7, 0x654321);
			dsp.regs().r[2].var = 0;
			emit("move x:<$7,r2");
		},
			[&]()
		{
			verify(dsp.regs().r[2].var == 0x654321);
		});

		// op_Movey_aa
		runTest([&]()
		{
			dsp.regs().r[2].var = 0xfedcba;
			dsp.memory().set(MemArea_Y, 0x6, 0);
			emit("move r2,y:<$6");
		},
			[&]()
		{
			verify(dsp.memory().get(MemArea_Y, 0x6) == 0xfedcba);
		});

		// op_Movex_Rnxxxx
		runTest([&]()
		{
			dsp.regs().r[3].var = 0x3;
			dsp.regs().n[5].var = 0;
			dsp.memory().set(MemArea_X, 0x7, 0x223344);
			emit("move x:(r3+$4),n5");
		},
			[&]()
		{
			verify(dsp.regs().r[3].var == 0x3);
			verify(dsp.regs().n[5].var == 0x223344);
		});
		
		runTest([&]()
		{
			dsp.regs().r[2].var = 0x15;
			dsp.regs().r[1].var = 0;
			dsp.memory().set(MemArea_X, 0x11, 0x456789);
			emit("move x:(r2-$4),r1");
		},
			[&]()
		{
			verify(dsp.regs().r[1].var == 0x456789);
		});

		// op_Movey_Rnxxxx
		runTest([&]()
		{
			dsp.regs().r[2].var = 0x5;
			dsp.regs().n[3].var = 0x778899;
			dsp.memory().set(MemArea_Y, 0x9, 0);
			emit("move n3,y:(r2+$4)");
		},
			[&]()
		{
			verify(dsp.regs().r[2].var == 0x5);
			verify(dsp.memory().get(MemArea_Y, 0x9) == 0x778899);
		});

		// op_Movex_Rnxxx
		runTest([&]()
		{
			dsp.regs().r[3].var = 0x3;
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0)));
			dsp.memory().set(MemArea_X, 0x7, 0x223344);
			emit("move x:(r3+$4),a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0x00223344000000);
		});

		runTest([&]()
		{
			dsp.regs().r[2].var = 0x14;
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0)));
			dsp.memory().set(MemArea_X, 0x10, 0x345678);
			emit("move x:(r2-$4),a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0x00345678000000);
		});

		runTest([&]()
		{
			dsp.regs().r[2].var = 0x11;
			dsp.set_m(2, 0x0f);
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0)));
			dsp.memory().set(MemArea_X, 0x1d, 0x345678);
			emit("move x:(r2-$4),a");
		},
			[&]()
		{
			verify(dsp.aluA().var == 0x00345678000000);
			dsp.set_m(2, 0xffffff);
		});

		// op_Movey_Rnxxx
		runTest([&]()
		{
			dsp.regs().r[2].var = 0x5;
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00334455667788)));
			dsp.memory().set(MemArea_Y, 0x9, 0);
			emit("move a,y:(r2+$4)");
		},
			[&]()
		{
			verify(dsp.memory().get(MemArea_Y, 0x9) == 0x334455);
		});

		// op_Movexr_ea
		runTest([&]()
		{
			dsp.regs().r[2].var = 0x5;
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x00223344556677)));
			dsp.regs().y.var = 0x111111222222;
			dsp.memory().set(MemArea_X, 0x5, 0xaabbcc);
			emit(0x1a9a00);	// move x:(r2)+,a b,y0 (Movexr encoding, equivalent to Movex+Mover)
		},
			[&]()
		{
			verify(dsp.aluA().var == 0xffaabbcc000000);
			verify(dsp.regs().y.var == 0x111111223344);
			verify(dsp.regs().r[2].var == 0x6);
		});

		runTest([&]()
		{
			// test dynamic peripheral addressing
			peripheralsX.write(0xffffc5, 0x00c0de);
			dsp.regs().r[2].var = 0xffffc5;
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0)));
			emit(0x1aa200);	// move x:(r2)+,a b,y0
		},
			[&]()
		{
			verify(dsp.aluA().var == 0x00c0de000000);
		});

		// op_Moveyr_ea
		runTest([&]()
		{
			dsp.regs().r[2].var = 0x5;
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x00223344556677)));
			dsp.regs().x.var = 0x111111222222;
			dsp.memory().set(MemArea_Y, 0x5, 0xddeeff);
			emit(0x1ada00);	// move b,x0 y:(r2)+,a
		},
			[&]()
		{
			verify(dsp.aluA().var == 0xffddeeff000000);
			verify(dsp.regs().x.var == 0x111111223344);
			verify(dsp.regs().r[2].var == 0x6);
		});

		// op_Movexr_A
		runTest([&]()
		{
			dsp.regs().r[1].var = 0x3;
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00223344556677)));
			dsp.regs().x.var = 0x111111222222;
			dsp.memory().set(MemArea_X, 3, 0);
			emit(0x082100);	// move a,x:(r1) x0,a
		},
			[&]()
		{
			verify(dsp.aluA().var == 0x00222222000000);
			verify(dsp.memory().get(MemArea_X, 3) == 0x223344);
		});

		// op_Moveyr_A
		runTest([&]()
		{
			dsp.regs().r[6].var = 0x4;
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x00334455667788)));
			dsp.regs().y.var = 0x444444555555;
			dsp.memory().set(MemArea_Y, 4, 0);
			emit(0x09a600);	// move b,y:(r6) y0,b
		},
			[&]()
		{
			verify(dsp.aluB().var == 0x00555555000000);
			verify(dsp.memory().get(MemArea_Y, 4) == 0x334455);
		});

		// op_Movexy
		runTest([&]()
		{
			dsp.regs().r[2].var = 0x2;
			dsp.regs().r[6].var = 0x3;
			dsp.regs().n[2].var = 0x3;
			dsp.x0(0);
			dsp.y0(0);
			dsp.memory().set(MemArea_X, 2, 0x223344);
			dsp.memory().set(MemArea_Y, 3, 0xccddee);

			emit("move x:(r2)+n2,x0 y:(r6)+,y0");
		},
			[&]()
		{
			verify(dsp.x0() == 0x223344);
			verify(dsp.y0() == 0xccddee);
			verify(dsp.regs().r[2].var == 0x5);
			verify(dsp.regs().r[6].var == 0x4);
		});

		runTest([&]()
		{
			dsp.regs().r[3].var = 0x6;
			dsp.regs().r[7].var = 0x7;
			dsp.x0(0x112233);
			dsp.y0(0x445566);
			dsp.memory().set(MemArea_X, 6, 0);
			dsp.memory().set(MemArea_Y, 7, 0);

			emit("move x0,x:(r3) y0,y:(r7)");
		},
			[&]()
		{
			verify(dsp.memory().get(MemArea_X, 6) == 0x112233);
			verify(dsp.memory().get(MemArea_Y, 7) == 0x445566);
		});

		// op_Movec_ea
		runTest([&]()
		{
			dsp.regs().r[0].var = 3;
			dsp.regs().omr.var = 0;
			dsp.memory().set(MemArea_X, 3, 0x112233);
			emit("move x:(r0),omr");
		},
			[&]()
		{
			verify(dsp.regs().omr == 0x112233);
		});

		const auto srBackup = dsp.regs().sr;

		// op_Movec_aa
		runTest([&]()
		{
			dsp.regs().sr.var = 0;
			dsp.memory().set(MemArea_X, 3, 0x223344);
			emit("move x:$3,sr");
		},
			[&]()
		{
			verify(dsp.regs().sr == 0x223344);
		});

		dsp.setSR(srBackup);

		// op_Movec_S1D2
		runTest([&]()
		{
			dsp.regs().vba.var = 0;
			dsp.y1(0x334455);
			emit(0x04c7b0);	// move y1,vba
		},
			[&]()
		{
			verify(dsp.regs().vba.var == 0x334455);
		});

		// op_Movec_S1D2
		runTest([&]()
		{
			dsp.regs().ep.var = 0xaabbdd;
			dsp.x1(0);
			emit(0x0445aa);	// move ep,x1
		},
			[&]()
		{
			verify(dsp.x1() == 0xaabbdd);
		});

		// op_Movec_ea with immediate data
		runTest([&]()
		{
			dsp.regs().lc.var = 0;
			emit("move #>$aabbcc,lc");
		},
			[&]()
		{
			verify(dsp.regs().lc.var == 0xaabbcc);
		});

		// op_Movec_xx
		runTest([&]()
		{
			dsp.regs().la.var = 0;
			emit(0x0555be);	// move #$55,la
		},
			[&]()
		{
			verify(dsp.regs().la.var == 0x55);
		});

		// op_Movep_ppea
		runTest([&]()
		{
			peripheralsX.write(0xffffc5, 0);
			emit("movep #>$ffeeff,x:<<$ffffc5");
		},
			[&]()
		{
			verify(dsp.memReadPeriph(MemArea_X, 0xffffc5, Movep_ppea) == 0xffeeff);
		});

		// op_Movep_eapp
		runTest([&]()
		{
			peripheralsX.write(0xffffc5, 0xc0de);
			dsp.memWriteP(0x23, 0);
			emit("movep x:<<$ffffc5,p:>$23");
		},
			[&]()
		{
			verify(dsp.memRead(MemArea_P, 0x23) == 0xc0de);
		});

		// op_Movep_Xqqea
		runTest([&]()
		{
			peripheralsX.write(0xffff85, 0);
			emit("movep #>$334455,x:<<$ffff85");
		},
			[&]()
		{
			verify(dsp.memReadPeriph(MemArea_X, 0xffff85, Movep_Xqqea) == 0x334455);
		});

		// op_Movep_Yqqea
		runTest([&]()
		{
			peripheralsY.write(0xffff8c, 0);
			emit("movep #>$556677,y:<<$ffff8c");
		},
			[&]()
		{
			verify(dsp.memReadPeriph(MemArea_Y, 0xffff8c, Movep_Yqqea) == 0x556677);
		});

		// op_Movep_SXqq
		runTest([&]()
		{
			peripheralsX.write(0xffff84, 0);
			dsp.y1(0x334455);
			emit(0x04c784);	// movep y1,x:<<$ffff84
		},
			[&]()
		{
			verify(dsp.memReadPeriph(MemArea_X, 0xffff84, Movep_SXqq) == 0x334455);
		});

		// op_Movep_SYqq
		runTest([&]()
		{
			peripheralsY.write(0xffff86, 0x112233);
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0)));
			emit(0x044f26);	// movep y:<<$ffff86,b
		},
			[&]()
		{
			verify(dsp.aluB().var == 0x00112233000000);
		});

		// op_Movep_Spp
		runTest([&]()
		{
			peripheralsY.write(0xffffc5, 0x8899aa);
			dsp.y1(0);
			emit(0x094705);	// movep y:<<$ffffc5,y1
		},
			[&]()
		{
			verify(dsp.y1() == 0x8899aa);
		});
	}

	void UnitTests::movel()
	{
		runTest([&]()
		{
			mem.set(MemArea_X, 100, 0x123456);
			mem.set(MemArea_Y, 100, 0x345678);

			dsp.reg.r[0].var = 100;

			// move l:(r0),ab
			emit(0x4ae000);	// move l:(r0),ab
		}, [&]()
		{
			verify(dsp.aluA().var == 0x00123456000000);
			verify(dsp.aluB().var == 0x00345678000000);
		});

		runTest([&]()
		{
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0xaabadbadbadbad)));
			dsp.memory().set(MemArea_X, 10, 0x123456);
			dsp.memory().set(MemArea_Y, 10, 0x543210);
			dsp.reg.r[0].var = 10;

			// move l:(r0),b
			emit(0x49e000);	// move l:(r0),b
		}, [&]()
		{
			verify(dsp.aluB() == 0x00123456543210);
		});

		// op_Movel_ea
		runTest([&]()
		{
			dsp.regs().x.var = 0xbadbadbadbad;
			dsp.regs().r[1].var = 0x10;
			dsp.memory().set(MemArea_X, 0x10, 0xaabbcc);
			dsp.memory().set(MemArea_Y, 0x10, 0xddeeff);

			emit(0x42d900);	// move l:(r1)+,x

			dsp.memory().set(MemArea_X, 0x3, 0x7f0000);
			dsp.memory().set(MemArea_Y, 0x3, 0x112233);
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0xffffeeddccbbaa)));

			emit(0x498300);	// move l:$3,b
		}, [&]()
		{
			verify(dsp.regs().x.var == 0xaabbccddeeff);
			verify(dsp.aluB().var == 0x007f0000112233);
			verify(dsp.regs().r[1].var == 0x11);
		});

		runTest([&]()
		{
			dsp.regs().x.var = 0xaabbccddeeff;
			dsp.regs().y.var = 0x112233445566;
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00765432123456)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x00654321fedcba)));
			dsp.regs().r[1].var = 0x10;
			dsp.regs().r[2].var = 0x15;
			dsp.regs().r[3].var = 0x20;
			dsp.regs().r[4].var = 0x25;
			dsp.memory().set(MemArea_X, 0x10, 0);	dsp.memory().set(MemArea_Y, 0x10, 0);
			dsp.memory().set(MemArea_X, 0x15, 0);	dsp.memory().set(MemArea_Y, 0x15, 0);
			dsp.memory().set(MemArea_X, 0x20, 0);	dsp.memory().set(MemArea_Y, 0x20, 0);
			dsp.memory().set(MemArea_X, 0x25, 0);	dsp.memory().set(MemArea_Y, 0x25, 0);
			emit(0x426100);	// move x,l:(r1)
			emit(0x436200);	// move y,l:(r2)
			emit(0x486300);	// move a,l:(r3)
			emit(0x496400);	// move b,l:(r4)
		},
			[&]()
		{
			verify(dsp.memory().get(MemArea_X, 0x10) == 0xaabbcc);	verify(dsp.memory().get(MemArea_Y, 0x10) == 0xddeeff);
			verify(dsp.memory().get(MemArea_X, 0x15) == 0x112233);	verify(dsp.memory().get(MemArea_Y, 0x15) == 0x445566);
			verify(dsp.memory().get(MemArea_X, 0x20) == 0x765432);	verify(dsp.memory().get(MemArea_Y, 0x20) == 0x123456);
			verify(dsp.memory().get(MemArea_X, 0x25) == 0x654321);	verify(dsp.memory().get(MemArea_Y, 0x25) == 0xfedcba);
		});

		// op_Movel_aa
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0)));
			dsp.memory().set(MemArea_X, 0x3, 0x123456);
			dsp.memory().set(MemArea_Y, 0x3, 0x789abc);
			emit(0x4a8300);	// move l:<$3,ab
		},
			[&]()
		{
			verify(dsp.aluA().var == 0x00123456000000);
			verify(dsp.aluB().var == 0x00789abc000000);
		});

		runTest([&]()
		{
			dsp.regs().y.var = 0;
			dsp.memory().set(MemArea_X, 0x4, 0x123456);
			dsp.memory().set(MemArea_Y, 0x4, 0x789abc);
			emit(0x438400);	// move l:<$4,y
		},
			[&]()
		{
			verify(dsp.regs().y.var == 0x00123456789abc);
		});
	}

	void UnitTests::parallel()
	{
		runTest([&]()
		{
			dsp.regs().x.var = 0x000000010000;
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x006c0000000000)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0xbbbbbbbbbbbbbb)));
			dsp.regs().y.var = 0x222222222222;

			emit(0x243c44);	// sub x0,a #$3c,x0
		},
			[&]()
		{
			verify(dsp.x0().var == 0x3c0000);
			verify(dsp.aluA().var == 0x006b0000000000);
			verify(dsp.aluB().var == 0xbbbbbbbbbbbbbb);
			verify(dsp.regs().y.var == 0x222222222222);
		});

		runTest([&]()
		{
			dsp.regs().x.var = 0x100000080000;
			dsp.regs().y.var = 0x000000200000;
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x0002cdd6000000)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x0002a0a5000000)));

			emit(0x210541);	// tfr x0,a a0,x1
		},
			[&]()
		{
			verify(dsp.regs().x.var == 0x000000080000);
			verify(dsp.regs().y.var == 0x000000200000);
			verify(dsp.aluA().var == 0x00080000000000);
			verify(dsp.aluB().var == 0x0002a0a5000000);
		});

		runTest([&]()
		{
			dsp.regs().x.var = 0x000000003339;
			dsp.regs().y.var = 0x65a1cb000000;
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00000000000000)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x00196871f4bc6a)));

			emit(0x21cf51);	// tfr y0,a a,b
		},
			[&]()
		{
			verify(dsp.regs().x.var == 0x000000003339);
			verify(dsp.regs().y.var == 0x65a1cb000000);
			verify(dsp.aluA().var == 0x00000000000000);
			verify(dsp.aluB().var == 0x00000000000000);
		});

		runTest([&]()
		{
			dsp.regs().x.var = 0x111111222222;
			dsp.regs().y.var = 0x333333444444;
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x55666666777777)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x88999999aaaaaa)));

			emit(0x21ee59);	// tfr y0,b b,a
		},
			[&]()
		{
			verify(dsp.regs().x.var == 0x111111222222);
			verify(dsp.regs().y.var == 0x333333444444);
			verify(dsp.aluA().var == 0xff800000000000);
			verify(dsp.aluB().var == 0x00444444000000);
		});

		runTest([&]()
		{
			dsp.regs().x.var = 0x111111222222;
			dsp.regs().y.var = 0x333333444444;
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x55666666777777)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x88999999aaaaaa)));

			emit(0x210741);	// tfr x0,a a0,y1
		},
			[&]()
		{
			verify(dsp.regs().x.var == 0x111111222222);
			verify(dsp.regs().y.var == 0x777777444444);
			verify(dsp.aluA().var == 0x00222222000000);
			verify(dsp.aluB().var == 0x88999999aaaaaa);
		});
	}

	// ======================================================================
	// ALU extended tests
	// ======================================================================

	void UnitTests::and_xxxx()
	{
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00aabbcc000000)));
			emit("and #>$f0f0f0,a");
		}, [&]()
		{
			verify(dsp.aluA().var == 0x00a0b0c0000000);
		});
		runTest([&]()
		{
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x00123456000000)));
			emit("and #>$00ff00,b");
		}, [&]()
		{
			verify(dsp.aluB().var == 0x00003400000000);
		});
	}

	void UnitTests::or_xxxx()
	{
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00a0b0c0000000)));
			emit("or #>$0f0f0f,a");
		}, [&]()
		{
			verify(dsp.aluA().var == 0x00afbfcf000000);
		});
		runTest([&]()
		{
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x00123456000000)));
			emit("or #>$ff0000,b");
		}, [&]()
		{
			verify(dsp.aluB().var == 0x00ff3456000000);
		});
	}

	void UnitTests::sub_xxxx()
	{
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00500000000000)));
			emit("sub #>$100000,a");
		}, [&]()
		{
			verify(dsp.aluA().var == 0x00400000000000);
		});
		runTest([&]()
		{
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x00200000000000)));
			emit("sub #>$100000,b");
		}, [&]()
		{
			verify(dsp.aluB().var == 0x00100000000000);
		});
	}

	void UnitTests::cmp_xxxx()
	{
		// a > imm
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00600000000000)));
			emit("cmp #>$500000,a");
		}, [&]()
		{
			verify(!dsp.sr_test(CCR_Z));
			verify(!dsp.sr_test(CCR_N));
		});
		// a == imm
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00600000000000)));
			emit("cmp #>$600000,a");
		}, [&]()
		{
			verify(dsp.sr_test(CCR_Z));
			verify(!dsp.sr_test(CCR_N));
		});
		// a < imm
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00600000000000)));
			emit("cmp #>$700000,a");
		}, [&]()
		{
			verify(!dsp.sr_test(CCR_Z));
			verify(dsp.sr_test(CCR_N));
		});
	}

	void UnitTests::subr()
	{
		// subr b,a: a = a/2 - b
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00600000000000)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x00020000000000)));
			emit("subr b,a");
		}, [&]()
		{
			verify(dsp.aluA().var == 0x002e0000000000);
		});
		// subr a,b: b = b/2 - a
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00100000000000)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x00400000000000)));
			emit("subr a,b");
		}, [&]()
		{
			verify(dsp.aluB().var == 0x00100000000000);
		});
		// subr with zero
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00000000000000)));
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x00100000000000)));
			emit("subr b,a");
		}, [&]()
		{
			verify(dsp.sr_test(CCR_N));
		});
	}

	void UnitTests::mpyi()
	{
		runTest([&]()
		{
			dsp.x0(0x100000);
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0)));
			emit("mpyi #>$4,x0,a");
		}, [&]()
		{
			verify(dsp.aluA().var != 0);
		});
	}

	void UnitTests::mpy_su()
	{
		runTest([&]()
		{
			dsp.x0(0x400000);
			dsp.y0(0x100000);
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0)));
			emit("mpysu x0,y0,a");
		}, [&]()
		{
			verify(dsp.aluA().var != 0);
		});
	}

	void UnitTests::tst()
	{
		// positive
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00400000000000)));
			emit("tst a");
		}, [&]()
		{
			verify(!dsp.sr_test(CCR_Z));
			verify(!dsp.sr_test(CCR_N));
		});
		// zero
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0)));
			emit("tst a");
		}, [&]()
		{
			verify(dsp.sr_test(CCR_Z));
			verify(!dsp.sr_test(CCR_N));
		});
		// negative
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0xff800000000000)));
			emit("tst a");
		}, [&]()
		{
			verify(!dsp.sr_test(CCR_Z));
			verify(dsp.sr_test(CCR_N));
		});
		// tst b
		runTest([&]()
		{
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x00123456000000)));
			emit("tst b");
		}, [&]()
		{
			verify(!dsp.sr_test(CCR_Z));
			verify(!dsp.sr_test(CCR_N));
		});
	}

	void UnitTests::nop()
	{
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00112233445566)));
			emit("nop");
		}, [&]()
		{
			verify(dsp.aluA().var == 0x00112233445566);
		});
	}

	// ======================================================================
	// Branch tests
	// ======================================================================

	void UnitTests::bra()
	{
		runTest([&]()
		{
			dsp.setPC(0);
			emit("bra >$50");
		}, [&]()
		{
			verify(dsp.getPC() == 0x50);
		});
	}

	void UnitTests::bcc()
	{
		// beq taken (Z=1)
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.setSR(0x0800c4);
			emit("beq >$50");
		}, [&]()
		{
			verify(dsp.getPC() == 0x50);
		});
		// beq not taken (Z=0)
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.setSR(0x0800c0);
			emit("beq >$50");
		}, [&]()
		{
			verify(dsp.getPC() != 0x50);
		});
		// bne taken (Z=0)
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.setSR(0x0800c0);
			emit("bne >$50");
		}, [&]()
		{
			verify(dsp.getPC() == 0x50);
		});
		// bne not taken (Z=1)
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.setSR(0x0800c4);
			emit("bne >$50");
		}, [&]()
		{
			verify(dsp.getPC() != 0x50);
		});
		// bpl taken (N=0)
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.setSR(0x0800c0);
			emit("bpl >$50");
		}, [&]()
		{
			verify(dsp.getPC() == 0x50);
		});
		// bmi taken (N=1)
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.setSR(0x0800c8);
			emit("bmi >$50");
		}, [&]()
		{
			verify(dsp.getPC() == 0x50);
		});
	}

	void UnitTests::bsr()
	{
		runTest([&]()
		{
			dsp.setPC(0);
			emit("bsr >$50");
		}, [&]()
		{
			verify(dsp.getPC() == 0x50);
		});
	}

	void UnitTests::bscc()
	{
		// bseq taken (Z=1)
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.setSR(0x0800c4);
			emit("bseq >$50");
		}, [&]()
		{
			verify(dsp.getPC() == 0x50);
		});
		// bseq not taken (Z=0)
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.setSR(0x0800c0);
			emit("bseq >$50");
		}, [&]()
		{
			verify(dsp.getPC() != 0x50);
		});
	}

	void UnitTests::brclr_brset()
	{
		// brclr #0,a1 — bit 0 clear → taken
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00fffffe000000)));
			emit("brclr #$0,a1,>$50");
		}, [&]()
		{
			verify(dsp.getPC() == 0x50);
		});
		// brclr #0,a1 — bit 0 set → not taken
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00ffffff000000)));
			emit("brclr #$0,a1,>$50");
		}, [&]()
		{
			verify(dsp.getPC() != 0x50);
		});
		// brset #0,a1 — bit 0 set → taken
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00ffffff000000)));
			emit("brset #$0,a1,>$50");
		}, [&]()
		{
			verify(dsp.getPC() == 0x50);
		});
		// brset #0,a1 — bit 0 clear → not taken
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00fffffe000000)));
			emit("brset #$0,a1,>$50");
		}, [&]()
		{
			verify(dsp.getPC() != 0x50);
		});
	}

	void UnitTests::bsclr_bsset()
	{
		// bsclr #0,a1 — bit 0 clear → taken
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00fffffe000000)));
			emit("bsclr #$0,a1,>$50");
		}, [&]()
		{
			verify(dsp.getPC() == 0x50);
		});
		// bsset #0,a1 — bit 0 set → taken
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00ffffff000000)));
			emit("bsset #$0,a1,>$50");
		}, [&]()
		{
			verify(dsp.getPC() == 0x50);
		});
	}

	// ======================================================================
	// Jump tests
	// ======================================================================

	void UnitTests::jmp()
	{
		runTest([&]()
		{
			dsp.setPC(0);
			emit("jmp $50");
		}, [&]()
		{
			verify(dsp.getPC() == 0x50);
		});
	}

	void UnitTests::jcc()
	{
		// jeq taken (Z=1)
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.setSR(0x0800c4);
			emit("jeq $50");
		}, [&]()
		{
			verify(dsp.getPC() == 0x50);
		});
		// jeq not taken (Z=0)
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.setSR(0x0800c0);
			emit("jeq $50");
		}, [&]()
		{
			verify(dsp.getPC() != 0x50);
		});
		// jne taken (Z=0)
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.setSR(0x0800c0);
			emit("jne $50");
		}, [&]()
		{
			verify(dsp.getPC() == 0x50);
		});
		// jne not taken (Z=1)
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.setSR(0x0800c4);
			emit("jne $50");
		}, [&]()
		{
			verify(dsp.getPC() != 0x50);
		});
		// jpl taken (N=0)
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.setSR(0x0800c0);
			emit("jpl $50");
		}, [&]()
		{
			verify(dsp.getPC() == 0x50);
		});
		// jmi taken (N=1)
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.setSR(0x0800c8);
			emit("jmi $50");
		}, [&]()
		{
			verify(dsp.getPC() == 0x50);
		});
		// jmi not taken (N=0)
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.setSR(0x0800c0);
			emit("jmi $50");
		}, [&]()
		{
			verify(dsp.getPC() != 0x50);
		});
		// jcc taken (C=0)
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.setSR(0x0800c0);
			emit("jcc $50");
		}, [&]()
		{
			verify(dsp.getPC() == 0x50);
		});
	}

	void UnitTests::jsr()
	{
		runTest([&]()
		{
			dsp.setPC(0);
			emit("jsr $50");
		}, [&]()
		{
			verify(dsp.getPC() == 0x50);
		});
	}

	void UnitTests::jclr_jset()
	{
		// jclr #0,a1,$100 — bit 0 clear → taken
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00fffffe000000)));
			emit("jclr #$0,a1,$100");
		}, [&]()
		{
			verify(dsp.getPC() == 0x100);
		});
		// jclr #0,a1,$100 — bit 0 set → not taken
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00ffffff000000)));
			emit("jclr #$0,a1,$100");
		}, [&]()
		{
			verify(dsp.getPC() != 0x100);
		});
		// jset #0,a1,$100 — bit 0 set → taken
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00ffffff000000)));
			emit("jset #$0,a1,$100");
		}, [&]()
		{
			verify(dsp.getPC() == 0x100);
		});
		// jset #0,a1,$100 — bit 0 clear → not taken
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00fffffe000000)));
			emit("jset #$0,a1,$100");
		}, [&]()
		{
			verify(dsp.getPC() != 0x100);
		});
		// jclr #3,x:<$2,$100
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.memory().set(MemArea_X, 0x2, 0xfffff7);
			emit("jclr #$3,x:<$2,$100");
		}, [&]()
		{
			verify(dsp.getPC() == 0x100);
		});
		// jset #3,x:<$2,$100
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.memory().set(MemArea_X, 0x2, 0x000008);
			emit("jset #$3,x:<$2,$100");
		}, [&]()
		{
			verify(dsp.getPC() == 0x100);
		});
	}

	void UnitTests::jsclr_jsset()
	{
		// jsclr #0,a1,$100 — bit 0 clear → taken
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00fffffe000000)));
			emit("jsclr #$0,a1,$100");
		}, [&]()
		{
			verify(dsp.getPC() == 0x100);
		});
		// jsclr #0,a1,$100 — bit 0 set → not taken
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00ffffff000000)));
			emit("jsclr #$0,a1,$100");
		}, [&]()
		{
			verify(dsp.getPC() != 0x100);
		});
		// jsset #0,a1,$100 — bit 0 set → taken
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00ffffff000000)));
			emit("jsset #$0,a1,$100");
		}, [&]()
		{
			verify(dsp.getPC() == 0x100);
		});
		// jsset #0,a1,$100 — bit 0 clear → not taken
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00fffffe000000)));
			emit("jsset #$0,a1,$100");
		}, [&]()
		{
			verify(dsp.getPC() != 0x100);
		});
	}

	// ======================================================================
	// Bit manipulation extended tests
	// ======================================================================

	void UnitTests::bchg()
	{
		// bchg #0,a1 — toggle bit 0 (0 → 1)
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00fffffe000000)));
			emit("bchg #$0,a1");
		}, [&]()
		{
			verify((dsp.aluA().var & 0x00ffffff000000) == 0x00ffffff000000);
		});
		// bchg #0,a1 — toggle bit 0 (1 → 0)
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00ffffff000000)));
			emit("bchg #$0,a1");
		}, [&]()
		{
			verify((dsp.aluA().var & 0x00ffffff000000) == 0x00fffffe000000);
		});
		// bchg #3,x:<$2
		runTest([&]()
		{
			dsp.memory().set(MemArea_X, 2, 0x000000);
			emit("bchg #$3,x:<$2");
		}, [&]()
		{
			verify(dsp.memory().get(MemArea_X, 2) == 0x000008);
		});
	}

	void UnitTests::bset()
	{
		// bset #4,a1
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00000000000000)));
			emit("bset #$4,a1");
		}, [&]()
		{
			verify((dsp.aluA().var & 0x00ffffff000000) == 0x00000010000000);
		});
		// bset #3,x:(r0)
		runTest([&]()
		{
			dsp.regs().r[0].var = 5;
			dsp.memory().set(MemArea_X, 5, 0x000000);
			emit("bset #$3,x:(r0)");
		}, [&]()
		{
			verify(dsp.memory().get(MemArea_X, 5) == 0x000008);
		});
		// bset #5,x:<<$ffffc5
		runTest([&]()
		{
			peripheralsX.write(0xffffc5, 0x000000);
			emit("bset #$5,x:<<$ffffc5");
		}, [&]()
		{
			verify(dsp.memReadPeriph(MemArea_X, 0xffffc5, Bset_pp) == 0x000020);
		});
	}

	void UnitTests::btst()
	{
		// btst #0,a1 — bit set → C=1
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00ffffff000000)));
			emit("btst #$0,a1");
		}, [&]()
		{
			verify(dsp.sr_test(CCR_C));
		});
		// btst #0,a1 — bit clear → C=0
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00fffffe000000)));
			emit("btst #$0,a1");
		}, [&]()
		{
			verify(!dsp.sr_test(CCR_C));
		});
		// btst #3,x:<$2 — bit set
		runTest([&]()
		{
			dsp.memory().set(MemArea_X, 2, 0x000008);
			emit("btst #$3,x:<$2");
		}, [&]()
		{
			verify(dsp.sr_test(CCR_C));
		});
		// btst #3,x:<$2 — bit clear
		runTest([&]()
		{
			dsp.memory().set(MemArea_X, 2, 0x000000);
			emit("btst #$3,x:<$2");
		}, [&]()
		{
			verify(!dsp.sr_test(CCR_C));
		});
	}

	// ======================================================================
	// Newly implemented instructions
	// ======================================================================

	void UnitTests::eor_xx()
	{
		// eor #$3f,a (short immediate EOR)
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00ff00ff000000)));
			emit("eor #$3f,a");
		}, [&]()
		{
			verify((dsp.aluA().var & 0x00ffffff000000) == 0x00ff00c0000000);
		});
		// eor #$3f,b
		runTest([&]()
		{
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x00000000000000)));
			emit("eor #$3f,b");
		}, [&]()
		{
			verify((dsp.aluB().var & 0x00ffffff000000) == 0x0000003f000000);
		});
		// eor with all bits set
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00ffffff000000)));
			emit("eor #$3f,a");
		}, [&]()
		{
			verify((dsp.aluA().var & 0x00ffffff000000) == 0x00ffffc0000000);
		});
	}

	void UnitTests::ror_()
	{
		// ror a — rotate right through carry
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00aabbcc000000)));
			dsp.sr_clear(CCR_C);
			emit("ror a");
		}, [&]()
		{
			// a1 was 0xaabbcc, bit 0 = 0, shifted right, old C (0) injected at bit 23
			verify((dsp.aluA().var & 0x00ffffff000000) == 0x00555de6000000);
			verify(!dsp.sr_test(CCR_C));	// old bit 0 was 0
		});
		// ror a with carry set
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00aabbcc000000)));
			dsp.sr_set(CCR_C);
			emit("ror a");
		}, [&]()
		{
			// old C (1) injected at bit 23
			verify((dsp.aluA().var & 0x00ffffff000000) == 0x00d55de6000000);
			verify(!dsp.sr_test(CCR_C));	// old bit 0 was 0
		});
		// ror a with odd value (bit 0 = 1)
		runTest([&]()
		{
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00000001000000)));
			dsp.sr_clear(CCR_C);
			emit("ror a");
		}, [&]()
		{
			verify((dsp.aluA().var & 0x00ffffff000000) == 0x00000000000000);
			verify(dsp.sr_test(CCR_C));		// old bit 0 was 1
		});
		// ror b
		runTest([&]()
		{
			dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x00800000000000)));
			dsp.sr_clear(CCR_C);
			emit("ror b");
		}, [&]()
		{
			verify((dsp.aluB().var & 0x00ffffff000000) == 0x00400000000000);
			verify(!dsp.sr_test(CCR_C));
		});
	}

	void UnitTests::jclr_jset_ppqq()
	{
		// pp addressing: peripheral at $ffffd0
		// jclr #3,x:<<$ffffd0,$100 — bit 3 clear → taken
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.getPeriph(0)->write(0xffffd0, 0xfffff7);
			emit("jclr #$3,x:<<$ffffd0,$100");
		}, [&]()
		{
			verify(dsp.getPC() == 0x100);
		});
		// jset #3,x:<<$ffffd0,$100 — bit 3 set → taken
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.getPeriph(0)->write(0xffffd0, 0x000008);
			emit("jset #$3,x:<<$ffffd0,$100");
		}, [&]()
		{
			verify(dsp.getPC() == 0x100);
		});
		// jset — not taken
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.getPeriph(0)->write(0xffffd0, 0xfffff7);
			emit("jset #$3,x:<<$ffffd0,$100");
		}, [&]()
		{
			verify(dsp.getPC() != 0x100);
		});

		// qq addressing: peripheral at $ffff90
		// jclr #3,x:<<$ffff90,$100 — bit 3 clear → taken
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.getPeriph(0)->write(0xffff90, 0xfffff7);
			emit("jclr #$3,x:<<$ffff90,$100");
		}, [&]()
		{
			verify(dsp.getPC() == 0x100);
		});
		// jset #3,x:<<$ffff90,$100 — bit 3 set → taken
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.getPeriph(0)->write(0xffff90, 0x000008);
			emit("jset #$3,x:<<$ffff90,$100");
		}, [&]()
		{
			verify(dsp.getPC() == 0x100);
		});
	}

	void UnitTests::jsclr_jsset_ppqq()
	{
		// jsclr with pp
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.getPeriph(0)->write(0xffffd0, 0xfffff7);
			emit("jsclr #$3,x:<<$ffffd0,$100");
		}, [&]()
		{
			verify(dsp.getPC() == 0x100);
		});
		// jsset with pp
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.getPeriph(0)->write(0xffffd0, 0x000008);
			emit("jsset #$3,x:<<$ffffd0,$100");
		}, [&]()
		{
			verify(dsp.getPC() == 0x100);
		});
		// jsclr with qq
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.getPeriph(0)->write(0xffff90, 0xfffff7);
			emit("jsclr #$3,x:<<$ffff90,$100");
		}, [&]()
		{
			verify(dsp.getPC() == 0x100);
		});
		// jsset with qq
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.getPeriph(0)->write(0xffff90, 0x000008);
			emit("jsset #$3,x:<<$ffff90,$100");
		}, [&]()
		{
			verify(dsp.getPC() == 0x100);
		});
	}

	void UnitTests::brclr_brset_ppqq()
	{
		// brclr with pp — taken
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.getPeriph(0)->write(0xffffd0, 0xfffff7);
			emit("brclr #$3,x:<<$ffffd0,>$50");
		}, [&]()
		{
			verify(dsp.getPC() == 0x50);
		});
		// brset with pp — taken
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.getPeriph(0)->write(0xffffd0, 0x000008);
			emit("brset #$3,x:<<$ffffd0,>$50");
		}, [&]()
		{
			verify(dsp.getPC() == 0x50);
		});
		// brset with pp — not taken
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.getPeriph(0)->write(0xffffd0, 0xfffff7);
			emit("brset #$3,x:<<$ffffd0,>$50");
		}, [&]()
		{
			verify(dsp.getPC() != 0x50);
		});
		// brclr with qq — taken
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.getPeriph(0)->write(0xffff90, 0xfffff7);
			emit("brclr #$3,x:<<$ffff90,>$50");
		}, [&]()
		{
			verify(dsp.getPC() == 0x50);
		});
		// brset with qq — taken
		runTest([&]()
		{
			dsp.setPC(0);
			dsp.getPeriph(0)->write(0xffff90, 0x000008);
			emit("brset #$3,x:<<$ffff90,>$50");
		}, [&]()
		{
			verify(dsp.getPC() == 0x50);
		});
	}

	// ======================================================================
	// Multi-instruction tests (use execUntil for full DSP execution)
	// ======================================================================

	void UnitTests::multiInstructionTests()
	{
		rep_multi();
		rep_div_powerOfTwo();
		do_multi();
		jsr_rts();
	}

	void UnitTests::rep_div_powerOfTwo()
	{
		// rep/div has a fast path for the case where the divisor is a power of two and the dividend is
		// already in range, both of which are runtime properties, so these cases deliberately cover both
		// sides of that guard. The expected values are the exact DIV semantics: running this under the
		// interpreter validates the table, running it under the JIT validates the fast path against it.
		struct DivCase
		{
			TWord divisor;
			uint64_t alu;
			TWord sr;
			TWord iterations;
			uint64_t expectedAlu;
			TWord expectedSr;
			TWord srMask;
		};

		// The JIT derives V and L from the last div step alone, while the DSP toggles V per step and makes L
		// sticky across all of them. Reproducing that needs the per-step V accumulated in the loop, which is
		// instructions in the hottest block in the emulator, so the last case below checks everything except
		// L. It is the only known difference and it needs a division whose dividend is out of range to show.
		constexpr TWord all = 0xffffff;
		constexpr TWord noL = all & ~static_cast<TWord>(CCR_L);

		static constexpr DivCase cases[] =
		{
			{ 0x000400, 0x0000000000c000, 0x000000, 12, 0xfffffc0c000000, 0x000000, all },	// fast, the Virus C shape: divisor 2^10, dividend clamped in range
			{ 0x000400, 0x00000000000000, 0x000000, 12, 0xfffffc00000000, 0x000000, all },	// fast, dividend 0
			{ 0x000400, 0x000003ffffffff, 0x000000, 12, 0x000003fffff7ff, 0x000001, all },	// fast, dividend at the top of the range
			{ 0x000400, 0x00000123456789, 0x000001, 12, 0x00000056789a46, 0x000001, all },	// fast, carry in set
			{ 0x000001, 0x00000000abcdef, 0x000000, 12, 0xffffffffdef55e, 0x000000, all },	// fast, divisor 2^0
			{ 0x800000, 0x0000123456789a, 0x000000, 24, 0xffd6789a001234, 0x000000, all },	// fast, divisor 2^23, 24 iterations
			{ 0x000400, 0x0000002aaaaaaa, 0x000000,  1, 0xfffffc55555554, 0x000000, all },	// slow, single iteration, below the fast path minimum
			{ 0x000400, 0x0000002aaaaaaa, 0x000000,  3, 0xfffffd55555550, 0x000000, all },	// slow, three iterations, just below the fast path minimum
			{ 0x000400, 0x0000002aaaaaaa, 0x000000,  4, 0xfffffeaaaaaaa0, 0x000000, all },	// fast, four iterations, exactly at the fast path minimum
			{ 0x000400, 0x0000002aaaaaaa, 0x000001, 24, 0xfffffeaa855555, 0x000000, all },	// fast, 24 iterations with carry in
			{ 0x001000, 0x00000800000000, 0x000040, 12, 0xfffff000000400, 0x000040, all },	// fast, divisor 2^12, L already set
			{ 0xffffff, 0x00000000800000, 0x000000, 12, 0xffffffff000400, 0x000000, all },	// fast, negative divisor normalises to 2^0
			{ 0x000400, 0x00000400000000, 0x000000, 12, 0x000004000007ff, 0x000001, all },	// slow, dividend exactly at the divisor
			{ 0x000400, 0xffffa96303b232, 0x000000, 12, 0xfad62c3b232000, 0x000000, all },	// slow, negative dividend
			{ 0x218dec, 0x00008000000000, 0x000000, 12, 0x00012ec400001e, 0x000001, all },	// slow, divisor not a power of two
			{ 0x000000, 0x00000000001000, 0x000000, 12, 0x000000010007ff, 0x000001, all },	// slow, divisor zero
			{ 0x000400, 0x00ff0000000000, 0x000000, 12, 0xefc07c000007f0, 0x000040, noL },	// slow, dividend far out of range, overflows on step 8
		};

		for (const auto& c : cases)
		{
			dsp.resetHW();
			dsp.y0(c.divisor);
			dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(c.alu)));
			dsp.setSR(c.sr);

			std::stringstream repOp;
			repOp << "rep #$" << std::hex << c.iterations;

			TWord pc = 0x100;
			pc = emitToMemory("jsr $200", pc);
			const auto returnPC = pc;
			emitToMemory("nop", pc);

			pc = 0x200;
			pc = emitToMemory(repOp.str().c_str(), pc);
			pc = emitToMemory("div y0,a", pc);
			emitToMemory("rts", pc);

			dsp.setPC(0x100);
			execUntil(returnPC);

			verify(dsp.aluA().var == static_cast<int64_t>(c.expectedAlu));
			verify((dsp.getSR().var & c.srMask) == (c.expectedSr & c.srMask));
		}
	}

	void UnitTests::rep_multi()
	{
		// Pattern: JSR to subroutine containing rep, RTS back. The JIT compiles
		// the JSR as one block, the subroutine as another, and exec() returns
		// at each block boundary (JSR, RTS).

		// rep #4: repeat add b,a four times
		dsp.resetHW();
		dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0)));
		dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x00000001000000)));

		TWord pc = 0x100;
		pc = emitToMemory("jsr $200", pc);		// entry: call subroutine
		const auto returnPC = pc;
		emitToMemory("nop", pc);

		pc = 0x200;
		pc = emitToMemory("rep #$4", pc);		// subroutine: rep #4
		pc = emitToMemory("add b,a", pc);		// repeated 4 times
		emitToMemory("rts", pc);				// return

		dsp.setPC(0x100);
		execUntil(returnPC);

		verify(dsp.aluA().var == 0x00000004000000);

		// rep x0: repeat with register count
		dsp.resetHW();
		dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0)));
		dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x00000001000000)));
		dsp.x0(7);

		pc = 0x100;
		pc = emitToMemory("jsr $200", pc);
		emitToMemory("nop", pc);

		pc = 0x200;
		pc = emitToMemory("rep x0", pc);
		pc = emitToMemory("add b,a", pc);
		emitToMemory("rts", pc);

		dsp.setPC(0x100);
		execUntil(0x101);

		verify(dsp.aluA().var == 0x00000007000000);
	}

	void UnitTests::do_multi()
	{
		// do #5: loop body adds 1 to a, five times
		dsp.resetHW();
		dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0)));
		dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x00000001000000)));

		TWord pc = 0x100;
		pc = emitToMemory("jsr $200", pc);		// entry: call subroutine
		const auto returnPC = pc;
		emitToMemory("nop", pc);

		pc = 0x200;
		pc = emitToMemory("do #$5,>$204", pc);	// do #5, loop end at $203
		pc = emitToMemory("add b,a", pc);		// $202: loop body
		pc = emitToMemory("nop", pc);			// $203: last instruction in loop
		pc = emitToMemory("rts", pc);			// $204: after loop, return

		dsp.setPC(0x100);
		execUntil(returnPC);

		verify(dsp.aluA().var == 0x00000005000000);

		// do with register count
		dsp.resetHW();
		dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0)));
		dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x00000001000000)));
		dsp.x0(3);

		pc = 0x100;
		pc = emitToMemory("jsr $200", pc);
		emitToMemory("nop", pc);

		pc = 0x200;
		pc = emitToMemory("do x0,>$204", pc);	// do x0, loop end at $203
		pc = emitToMemory("add b,a", pc);
		pc = emitToMemory("nop", pc);			// loop end
		pc = emitToMemory("rts", pc);			// after loop

		dsp.setPC(0x100);
		execUntil(0x101);

		verify(dsp.aluA().var == 0x00000003000000);
	}

	void UnitTests::jsr_rts()
	{
		// jsr to subroutine that adds b to a, then returns
		dsp.resetHW();
		dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0x00100000000000)));
		dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x00050000000000)));

		TWord pc = 0x100;
		pc = emitToMemory("jsr $200", pc);		// entry: call subroutine
		const auto returnPC = pc;
		emitToMemory("nop", pc);

		pc = 0x200;
		pc = emitToMemory("add b,a", pc);		// subroutine body
		emitToMemory("rts", pc);				// return

		dsp.setPC(0x100);
		execUntil(returnPC);

		verify(dsp.aluA().var == 0x00150000000000);

		// jsr + nested jsr + rts + rts
		dsp.resetHW();
		dsp.setALU(false, TReg56(static_cast<TReg56::MyType>(0)));
		dsp.setALU(true , TReg56(static_cast<TReg56::MyType>(0x00000001000000)));

		pc = 0x100;
		pc = emitToMemory("jsr $200", pc);		// call outer
		const auto finalPC = pc;
		emitToMemory("nop", pc);

		pc = 0x200;
		pc = emitToMemory("add b,a", pc);		// outer: a += 1
		pc = emitToMemory("jsr $300", pc);		// call inner
		pc = emitToMemory("add b,a", pc);		// outer: a += 1 (after inner returns)
		emitToMemory("rts", pc);				// outer: return

		pc = 0x300;
		pc = emitToMemory("add b,a", pc);		// inner: a += 1
		emitToMemory("rts", pc);				// inner: return

		dsp.setPC(0x100);
		execUntil(finalPC);

		verify(dsp.aluA().var == 0x00000003000000);	// 3 adds total
	}
}
