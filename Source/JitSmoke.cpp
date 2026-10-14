#include "FirmwareLoader.h"
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv) { try {
    if(argc!=2) return 2;
    auto fw=pc::loadFirmware(argv[1]);
    md::engine::Engine reference(fw.firmware,fw.os,false), fast(fw.firmware,fw.os,true);
    fast.voices().warmUp(fw.os);
    uint64_t samples=0;
    auto compare=[&](int id,int block) {
        md::engine::Engine::Output a{},b{};
        if(!reference.render(a)||!fast.render(b)) throw std::runtime_error("Render failed");
        if(a.tracks!=b.tracks || a.mix.main!=b.mix.main) {
            std::cerr<<"Mismatch machine="<<id<<" block="<<block<<std::endl;
            throw std::runtime_error("JIT/interpreter audio differs");
        }
        samples+=16*32;
    };
    for(const auto& machine:reference.os().machines()) {
        if(machine.id>=80) continue;
        for(auto* e:{&reference,&fast}) {
            auto& h=e->host();h.allSoundOff(); h.setMachine(0,machine.id);
            h.setParam(0,10,64);h.setParam(0,11,64);h.setParam(0,13,127);
            h.setParam(0,17,100);h.setParam(0,18,64);h.setLevel(0,100);h.trigger(0,110);
        }
        for(int block=0;block<690;++block) {
            if(block==200||block==400) for(auto* e:{&reference,&fast}) {
                e->host().setParam(0,0,block==200?100:20);
                if(fw.firmware.x13)e->host().setTonal(0,block==200);
                e->host().trigger(0,block==200?64:127);
            }
            compare(machine.id,block);
        }
    }
    for(auto* e:{&reference,&fast}) for(int t=0;t<16;++t) {
        auto& h=e->host();h.setMachine(t,uint8_t(16+t%14));h.setLevel(t,100);
        h.setParam(t,13,127);h.setParam(t,17,100);h.trigger(t,110);
    }
    for(int block=0;block<1200;++block) {
        if(block%53==0)for(auto* e:{&reference,&fast})e->host().trigger((block/53)%16,90);
        compare(255,block);
    }
    std::cout<<"JIT + warm-up matches interpreter: "<<samples<<" 24-bit track samples; retriggers, pitch changes, tonal and 16 tracks passed"<<std::endl;
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;} }
