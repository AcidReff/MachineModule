#include "dsp56kEmu/assembler.h"
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/jit.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace dsp56k;

struct Rig {
    DefaultMemoryValidator validator;
    Memory memory{validator, 0x1000};
    PeripheralsNop x, y;
    DSP dsp{memory, &x, &y};
    Assembler assembler;

    Rig() {
        dsp.resetHW();
        auto config = dsp.getJit().getConfig();
        config.linkJitBlocks = false;
        config.maxInstructionsPerBlock = 1;
        dsp.getJit().setConfig(config);
    }
    TWord emit(const std::string& text, TWord pc) {
        const auto instruction = assembler.assemble(text.c_str());
        if(!instruction.success()) throw std::runtime_error("Assembly failed: " + text);
        for(unsigned i=0; i<instruction.wordCount; ++i) dsp.memWriteP(pc+i, instruction.word[i]);
        return pc+instruction.wordCount;
    }
    void run(TWord stop, bool jit) {
        dsp.setPC(0x100);
        for(int steps=0; dsp.getPC().toWord()!=stop; ++steps) {
            if(steps==8) throw std::runtime_error("MACRI did not consume its immediate correctly");
            if(jit) dsp.execJit(); else dsp.execInterpreter();
        }
    }
};

int main() { try {
    Rig reference, actual;
    unsigned cases=0;
    for(bool jit : {false, true}) {
#if !defined(__x86_64__) && !defined(_M_X64)
        if(jit) continue; // Native ARM JIT remains experimental and disabled.
#endif
        for(const auto* source : {"x0", "x1", "y0", "y1"})
        for(const auto* destination : {"a", "b"})
        for(bool negate : {false, true})
        for(unsigned value : {0u, 1u, 0x400000u, 0x7fffffu, 0x800000u, 0xfffffeu, 0xffffffu})
        for(uint64_t initial : {0ull, 0x123456800000ull, 0xffffff800000ull, 0x7fffffffffffull}) {
            std::ostringstream operands;
            operands << (negate ? "-" : "") << "#>$" << std::hex << value << ',' << source << ',' << destination;
            for(auto* rig : {&reference, &actual}) {
                rig->dsp.writeReg(Reg_SR, TReg24(0x300));
                rig->dsp.writeReg(Reg_A, TReg56(initial));
                rig->dsp.writeReg(Reg_B, TReg56(initial));
                rig->dsp.writeReg(Reg_X0, TReg24(0x765432));
                rig->dsp.writeReg(Reg_X1, TReg24(0xfedcba));
                rig->dsp.writeReg(Reg_Y0, TReg24(0x800000));
                rig->dsp.writeReg(Reg_Y1, TReg24(0x7fffff));
            }
            auto stop = reference.emit("maci " + operands.str(), 0x100);
            stop = reference.emit(std::string("rnd ") + destination, stop);
            reference.run(stop, false);
            actual.run(actual.emit("macri " + operands.str(), 0x100), jit);
            if(reference.dsp.regs().a.var != actual.dsp.regs().a.var ||
               reference.dsp.regs().b.var != actual.dsp.regs().b.var ||
               reference.dsp.getSR().toWord() != actual.dsp.getSR().toWord())
                throw std::runtime_error(std::string(jit ? "JIT" : "Interpreter") + " MACRI differs from MACI + RND: " + operands.str());
            ++cases;
        }
    }
    std::cout << "PASS: " << cases << " MACRI cases matched MACI + RND (accumulators, SR, instruction length)\n";
    return 0;
} catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; } }
