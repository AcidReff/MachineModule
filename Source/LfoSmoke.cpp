#include "FirmwareLoader.h"
#include <iostream>
#include <set>
#include <stdexcept>

static void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
int main(int argc,char** argv) {
    try {
        if(argc!=2)return 2;
        auto fw=pc::loadFirmware(argv[1]);
        md::engine::Engine engine(fw.firmware,fw.os);
        auto& host=engine.host();
        for(int track:{0,7,15})for(int slot:{0,1})for(int mode:{0,1,2}) {
            host.setMachine(track,1);
            host.setParam(track,0,64);host.setParam(track,1,127);
            host.setParam(track,21,100);host.setParam(track,22,100);
            host.setParam(track,23,slot?127:0);
            host.setLfo(track,track,0,slot?0:5,slot?5:0,mode);
            host.trigger(track,100);
            std::set<uint16_t> values;
            for(int n=0;n<400;++n) {
                if(mode&&n%10==0)host.trigger(track,100);
                host.tick();
                if(n>40)values.insert(host.voiceParams(track)[0]);
                require(engine.os().faultReason().empty(),"LFO firmware call failed");
            }
            std::cout<<"Track "<<track+1<<" shape "<<slot+1<<" mode "<<mode<<": "<<values.size()<<" distinct pitch values"<<std::endl;
            require(values.size()>8,"RND LFO is stuck or not reaching destination pitch");
            host.setParam(track,22,0);
            for(int n=0;n<100;++n)host.tick();
            require(host.voiceParams(track)[0]==(64<<7),"Zero depth did not restore base pitch");
            host.setLfo(track,track,0,0,0,0);
        }
        std::cout<<"RND shape slots, FREE/TRIG/HOLD and zero-depth checks passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
}
