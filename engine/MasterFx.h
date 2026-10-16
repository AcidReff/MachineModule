#pragma once
#include "MachineRunner.h"
#include "Mixer.h"
#include "../tools/mdmix/MixerRef.h"
#include <cstdlib>

namespace md::engine {
// Executes the user's original DSP1 master program. No firmware is embedded.
class MasterFx {
public:
    // SRAM/kit order: Gate Box, Rhythm Echo, EQ, Dynamix.
    inline static constexpr std::array<uint8_t,32> defaults{
        0,0,80,40,0,127,127,100,
        32,0,40,40,0,127,0,100,
        64,64,64,64,64,64,64,90,
        0,0,127,0,0,0,0,127};
    inline static constexpr const char* names[32]={
        "DVOL","PRED","DEC","DAMP","HP","LP","GATE","LEV",
        "TIME","MOD","MFRQ","FB","FLTF","FLTW","MONO","LEV",
        "LF","LG","HF","HG","PF","PG","PQ","GAIN",
        "ATCK","REL","TRHD","RTIO","KNEE","HP","OUTG","MIX"};
    MasterFx(const fw::Firmware& fw, const std::vector<uint8_t>& os, bool useJit=true)
        : cpu(os),dsp(fw,useJit && std::getenv("MACHINEMODULE_INTERPRETER")==nullptr) {
        // Firmware loading runs on a background thread. Exercise the common
        // master branches there so their initial JIT compilation precedes audio.
        if(dsp.usingJit()) {
            for(int variant=0;variant<8;++variant) {
                auto p=defaults;
                p[8]=uint8_t(variant*18);p[9]=uint8_t(variant*16);
                p[6]=variant%2?0:127;p[26]=variant%2?0:127;
                p[27]=variant%2?127:0;p[31]=variant%2?0:127;
                if(!configure(p,120))throw std::runtime_error("Master DSP warm-up configuration failed");
                for(int b=0;b<128;++b) {
                    Mixer::Output block{};
                    for(int i=0;i<32;++i){int32_t v=((b*32+i)%97<48?1:-1)*2000000;
                        block.main[i]={v,-v};block.rev[i]={v/4,-v/4};block.del[i]={v/4,-v/4};}
                    if(!process(block,false,false))throw std::runtime_error("Master DSP warm-up failed");
                }
            }
            reset();
        }
    }
    void reset(){dsp.resetMaster();valid=false;}
    bool usingJit() const { return dsp.usingJit(); }
    bool configure(const std::array<uint8_t,32>& params,double bpm) {
        if(valid && params==last && bpm==lastTempo)return true;
        if(!cpu.computeMaster(params,bpm))return false;
        for(auto map:{std::array<uint32_t,3>{0x1001af4,0x150,9},{0x1001b18,0x17a,11},
                      {0x1001b44,0x170,10},{0x1001b74,0x185,8}})
            for(uint32_t i=0;i<map[2];++i)dsp.pokeY(map[1]+i,cpu.peek32(map[0]+4*i));
        // Start at the selected delay time after loading/reset. Subsequent edits
        // retain the firmware's tape-style movement towards the new time.
        if(!valid){dsp.pokeY(0x15a,dsp.peekY(0x150));dsp.pokeY(0x15b,dsp.peekY(0x151));dsp.pokeY(0x15d,0);}
        last=params;lastTempo=bpm;valid=true;return true;
    }
    bool process(Mixer::Output& block,bool externalReverb,bool externalDelay) {
        for(int i=0;i<32;++i)for(int c=0;c<2;++c) {
            dsp.pokeX(0x180+2*i+c,uint32_t(block.main[i][c]));
            dsp.pokeX(0x1c0+2*i+c,externalReverb?0:uint32_t(block.rev[i][c]));
            dsp.pokeX(0x600+2*i+c,externalDelay?0:uint32_t(block.del[i][c]));
        }
        if(!dsp.runMaster())return false;
        for(int i=0;i<32;++i)for(int c=0;c<2;++c)
            block.main[i][c]=int32_t(dsp.peekX(0x180+2*i+c)<<8)>>8;
        return true;
    }
private:
    MachineRunner cpu;
    mixref::MixerRef dsp;
    std::array<uint8_t,32> last{};
    double lastTempo=0;
    bool valid=false;
};
}


