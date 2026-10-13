#include "Firmware.h"
#include "MachineRunner.h"
#include <fstream>
#include <iostream>
#include "mc68k/Musashi/m68k.h"
int main(int argc,char** argv) {
    try {
        if(argc<3)return 2;
        auto bytes=md::fw::readFile(argv[1]);
        if(argc>3) {
            const auto start=std::stoul(argv[2],nullptr,16),end=std::stoul(argv[3],nullptr,16);
            const auto base=argc>4?std::stoul(argv[4],nullptr,16):0x200000;
            for(auto p=start;p<end;) {char line[256]{};auto n=m68k_disassemble_raw(line,(unsigned)p,bytes.data()+p-base,bytes.data()+p-base,M68K_CPU_TYPE_68020);std::cout<<std::hex<<p<<" "<<line<<std::endl;p+=n?n:2;}
            return 0;
        }
        md::fw::FlashImage flash;
        if(bytes.size()==0x800000){flash.base=0x4000;flash.bytes.assign(bytes.begin()+0x4000,bytes.end());}
        else flash=md::fw::parseSysex(bytes);
        // Diagnostic unpacking: list patched sections even when legacy checksums differ.
        md::fw::Container container;
        size_t pos=0;
        auto be=[&](size_t o){auto* p=flash.bytes.data()+o;return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];};
        while(pos+8<flash.bytes.size()) {
            if(flash.bytes[pos]=='X'||flash.bytes[pos]=='1'){container.version.assign((char*)flash.bytes.data()+pos,4);pos+=4;}
            auto n=be(pos);if(n<16||n>flash.bytes.size()-pos-8)break;
            md::fw::Section s;s.index=(int)container.sections.size();s.data=md::fw::aplibDepack(flash.bytes.data()+pos+8,n);
            container.sections.push_back(std::move(s));pos+=8+n;
        }
        if(pos+8<flash.bytes.size() && be(pos)==0 && be(pos+4)==0) {
            auto extension=md::fw::aplibDepack(flash.bytes.data()+pos+8,flash.bytes.size()-pos-8);
            std::cout<<"Extension "<<std::hex<<pos+8<<std::dec<<" size="<<extension.size()<<std::endl;
            container.sections[0].data.resize(0xc0000+extension.size());
            std::copy(extension.begin(),extension.end(),container.sections[0].data.begin()+0xc0000);
        }
        std::cout<<"Version "<<container.version<<" sections "<<container.sections.size()<<std::endl;
        std::filesystem::create_directories(argv[2]);
        for(const auto& s:container.sections) {
            std::cout<<s.index<<" size="<<s.data.size()<<" checksum="<<s.sumOk<<std::endl;
            std::ofstream f(std::filesystem::path(argv[2])/("section"+std::to_string(s.index)+".bin"),std::ios::binary);
            f.write((const char*)s.data.data(),s.data.size());
        }
        md::engine::MachineRunner runner(container.sections.at(0).data);
        for(const auto& m:runner.machines()) {
            if(m.id>=80)continue;
            std::cout<<(int)m.id<<" "<<m.name<<" fn="<<std::hex<<m.function<<std::dec;
            uint16_t p[24]{};uint32_t out[32]{};
            for(int i=0;i<8;++i)p[i]=m.defaults[i]<<7;
            auto n=runner.compute(m.id,p,out,32,true);
            std::cout<<" words="<<n<<" fault="<<runner.faultReason()<<" params=";
            for(auto& s:m.params)std::cout<<s<<",";
            std::cout<<std::endl;
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
}
