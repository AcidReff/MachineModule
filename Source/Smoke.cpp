#include "FirmwareLoader.h"
#include <iostream>
#include <fstream>
#include <cmath>
#include <chrono>

int main(int argc,char** argv) {
    try {
        if(argc<2){std::cerr<<"Usage: md-smoke firmware.syx-or-bin [output.wav]\n";return 2;}
        auto fw=pc::loadFirmware(argv[1]);
        std::cout<<"OS version: "<<fw.firmware.version<<"; OS bytes: "<<fw.os.size()<<std::endl;
        md::engine::Engine engine(fw.firmware,std::move(fw.os));
        std::vector<int16_t> samples;
        int tested=0,failed=0;
        auto start=std::chrono::steady_clock::now();
        for(const auto& m:engine.os().machines()) {
            if(m.id<16 || m.id>=80)continue;
            auto& h=engine.host();h.setMachine(0,m.id);
            h.setParam(0,10,64);h.setParam(0,11,64);h.setParam(0,12,0);h.setParam(0,13,127);h.setParam(0,17,100);h.setParam(0,18,64);h.setLevel(0,100);h.trigger(0,110);
            int peak=0;
            for(int b=0;b<690;++b) {
                md::engine::Engine::Output out;
                if(!engine.render(out))throw std::runtime_error(engine.fault());
                for(auto frame:out.mix.main)for(auto s:frame) {
                    peak=std::max(peak,std::abs(s));
                    samples.push_back((int16_t)std::clamp(s/256,-32768,32767));
                }
            }
            std::cout<<(int)m.id<<" "<<m.name<<" peak="<<peak<<std::endl;
            ++tested;if(peak==0)++failed;
        }
        if(argc>2) {
            std::ofstream out(argv[2],std::ios::binary);
            auto u16=[&](uint16_t x){out.put((char)x);out.put((char)(x>>8));};
            auto u32=[&](uint32_t x){u16((uint16_t)x);u16((uint16_t)(x>>16));};
            out.write("RIFF",4);u32(36+(uint32_t)samples.size()*2);out.write("WAVEfmt ",8);u32(16);u16(1);u16(2);u32(44100);u32(176400);u16(4);u16(16);
            out.write("data",4);u32((uint32_t)samples.size()*2);out.write((const char*)samples.data(),samples.size()*2);
        }
        std::cout<<"Machines tested: "<<tested<<"; silent: "<<failed<<"; elapsed: "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<" s\n";
        return tested>0 && failed==0 ? 0:1;
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
}
