#include "FirmwareLoader.h"
#include "MasterFx.h"
#include <iostream>
#include <cmath>
static void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
int main(int argc,char** argv) {
    try {
        if(argc!=2)return 2;
        auto fw=pc::loadFirmware(argv[1]);require(fw.firmware.x13,"Expected verified X.13 firmware");
        md::engine::Engine engine(fw.firmware,fw.os);
        md::engine::MasterFx effects(fw.firmware,fw.os);
        auto& host=engine.host();
        md::engine::MachineRunner coefficients(fw.os);
        for(const auto& machine:coefficients.machines())if(machine.id<80) {
            for(int track:{0,7,15})for(bool tonal:{false,true}) {
                coefficients.setTonal(track,tonal);
                uint16_t params[24]{};uint32_t words[32]{};
                for(int i=0;i<8;++i)params[i]=machine.defaults[i]<<7;
                require(coefficients.compute(machine.id,params,words,32,true,track)>=0,"X.13 coefficient routine failed");
            }
        }
        coefficients.setTonal(0,false);
        uint16_t params[24]{};params[0]=64<<7;params[1]=96<<7;
        uint32_t normal[32]{},tonal[32]{};
        require(coefficients.compute(4,params,normal,32,true,0)>0,"Default pitch failed");
        coefficients.setTonal(0,true);
        require(coefficients.compute(4,params,tonal,32,true,0)>0,"Tonal pitch failed");
        require(normal[1]!=tonal[1],"Tonal tuning did not change pitch");
        std::cout<<"Coefficient routines and tonal tuning passed"<<std::endl;
        for(int id:{1,4,5}) {
            coefficients.setTonal(0,false);
            const auto* machine=coefficients.machine((uint8_t)id);
            uint16_t base[24]{};for(int i=0;i<8;++i)base[i]=machine->defaults[i]<<7;
            uint32_t original[32]{};
            const int count=coefficients.compute((uint8_t)id,base,original,32,true,0);
            for(int parameter:{4,5,6,7}) {
                uint16_t edited[24]{};std::copy(base,base+24,edited);
                edited[parameter]=(parameter<6||id==1?64:32)<<7;
                uint32_t changed[32]{};
                require(coefficients.compute((uint8_t)id,edited,changed,32,true,0)==count,"Expanded parameter coefficient count changed");
                require(!std::equal(original,original+count,changed),"Expanded oscillator parameter had no effect");
            }
        }
        std::cout<<"Additional oscillators, waveform and unison controls passed"<<std::endl;
        for(int id:{1,4,5,7,8,9})require(engine.os().machine((uint8_t)id)!=nullptr,"Missing expanded machine");
        for(int id:{1,4,5})for(int track:{0,7,15}) {
            host.allSoundOff();host.setMachine(track,(uint8_t)id);
            host.setParam(track,10,64);host.setParam(track,11,64);host.setParam(track,13,127);
            host.setParam(track,17,100);host.setParam(track,18,64);host.setLevel(track,100);host.trigger(track,110);
            int peak=0;
            for(int b=0;b<140;++b){md::engine::Engine::Output out;require(engine.render(out),"Expanded voice render failed");if(b>30)for(auto v:out.tracks[track])peak=std::max(peak,std::abs(v));}
            std::cout<<engine.os().machine((uint8_t)id)->name<<" track="<<track+1<<" peak="<<peak<<" CPU fault="<<engine.os().faultReason()<<std::endl;
            require(peak>100,"Expanded oscillator silent");require(engine.os().faultReason().empty(),"Coefficient CPU fault");
        }
        for(int id:{7,8,9}) {
            host.allSoundOff();
            for(int t=0;t<3;++t){host.setMachine(t,t==1?(uint8_t)id:4);host.setParam(t,10,64);host.setParam(t,11,64);host.setParam(t,13,127);host.setParam(t,17,100);host.setParam(t,18,64);host.setLevel(t,100);host.trigger(t,110);}
            int peak=0;
            for(int b=0;b<200;++b){md::engine::Engine::Output out;require(engine.render(out),"Neighbor effect render failed");if(b>30)for(auto v:out.tracks[1])peak=std::max(peak,std::abs(v));}
            std::cout<<"NFX "<<id<<" peak="<<peak<<std::endl;require(peak>100,"Neighbor effect silent");
        }
        for(int effect:{0,1}) {
            effects.reset();require(effects.configure(md::engine::MasterFx::defaults,120),"X.13 master configuration failed");
            int peak=0;
            for(int b=0;b<1800;++b){md::engine::Mixer::Output out{};if(b==0)(effect?out.del:out.rev)[0]={1000000,1000000};require(effects.process(out,false,false),"X.13 master render failed");for(auto v:out.main)for(auto s:v)peak=std::max(peak,std::abs(s));}
            std::cout<<(effect?"Delay":"Reverb")<<" peak="<<peak<<std::endl;require(peak>100,"Master effect silent");
        }
        std::cout<<"X.13 checks passed"<<std::endl;return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
}
