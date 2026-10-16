#include "FirmwareLoader.h"
#include "MasterFx.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv) { try {
    if(argc<2) { std::cerr<<"Usage: md-stability-smoke firmware [seconds=30]\n"; return 2; }
    const int seconds=argc>2?std::atoi(argv[2]):30;
    if(seconds<5||seconds>600) throw std::runtime_error("Choose 5-600 seconds of audio");
    auto fw=pc::loadFirmware(argv[1]);
    md::engine::ParallelEngine engine(fw.firmware,fw.os,fw.firmware.x13?1:3);
    md::engine::MasterFx master(fw.firmware,fw.os);
    auto& host=engine.host();
    host.setMaxActiveVoices(6);host.setSilenceRelease(fw.firmware.x13?-1:128,138);
    std::vector<uint8_t> machines;
    for(const auto& m:engine.os().machines())if(m.id>=16&&m.id<80)machines.push_back(m.id);
    if(machines.empty())throw std::runtime_error("No synthesis machines");
    auto assign=[&](int track,uint8_t machine) {
        host.setMachine(track,machine);host.setParam(track,10,64);host.setParam(track,11,64);
        host.setParam(track,13,127);host.setParam(track,17,100);host.setParam(track,18,64);
        host.setParam(track,19,45);host.setParam(track,20,45);host.setLevel(track,100);
    };
    for(int t=0;t<16;++t)assign(t,machines[t%machines.size()]);
    auto params=md::engine::MasterFx::defaults;
    std::cout<<"voices="<<(engine.voices().usingJit()?"JIT":"interpreter")
             <<" master="<<(master.usingJit()?"JIT":"interpreter")
             <<" groups="<<engine.voices().groupCount()<<" audioSeconds="<<seconds<<std::endl;
    const int blocks=(seconds*44100+31)/32;
    uint64_t hash=14695981039346656037ull;int peak=0;
    const auto start=std::chrono::steady_clock::now();
    for(int b=0;b<blocks;++b) {
        // Exercise retriggers, machine swaps, tails and fully idle periods.
        const int phase=b%4800;
        if(phase==3600)host.allSoundOff();
        if(phase<3600&&b%137==0)for(int t=0;t<16;++t)host.trigger(t,80+(t*3)%47);
        if(b%997==0) {
            const int t=(b/997)%16;
            assign(t,machines[(b/997+17)%machines.size()]);
            if(phase<3600)host.trigger(t,110);
        }
        if(b%211==0)host.setParam((b/211)%16,0,(b/211*7)%128);
        const double bpm=105+15*((b/4096)%4);
        if(b%4096==0){params[8]=uint8_t(16+((b/4096)%4)*20);params[9]=uint8_t((b/4096)%2?32:0);}
        host.setTempo(bpm);
        if(!master.configure(params,bpm))throw std::runtime_error("Master coefficient failure");
        md::engine::ParallelEngine::Output out{};
        if(!engine.render(out))throw std::runtime_error("Voice render: "+engine.fault());
        if(!master.process(out.mix,false,false))throw std::runtime_error("Master render failure");
        for(auto pair:out.mix.main)for(auto sample:pair) {
            peak=std::max(peak,std::abs(sample));hash^=uint32_t(sample);hash*=1099511628211ull;
        }
        if(b>0&&b%6890==0)std::cout<<"Rendered "<<b*32/44100<<" seconds"<<std::endl;
    }
    if(peak==0)throw std::runtime_error("Silent stability render");
    std::cout<<"PASS: "<<seconds<<" seconds; peak="<<peak<<" hash="<<std::hex<<hash<<std::dec
             <<" wallSeconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<std::endl;
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;} }
