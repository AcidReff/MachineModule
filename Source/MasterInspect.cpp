#include "FirmwareLoader.h"
#include "dsp56kEmu/disasm.h"
#include "dsp56kEmu/opcodes.h"
#include "Musashi/m68k.h"
#include <fstream>
#include <iostream>
#include <map>
int main(int argc,char** argv){
    if(argc<3)return 2;
    auto fw=pc::loadFirmware(argv[1]);
    std::ofstream dspFile(std::string(argv[2])+"-dsp.txt"),cpuFile(std::string(argv[2])+"-cpu.txt");
    std::map<uint32_t,uint32_t> program;
    for(auto& r:fw.firmware.dspB.records)if(r.space==md::fw::Space::P||r.addr>=0x20000)for(size_t i=0;i<r.words.size();++i)program[r.addr+(uint32_t)i]=r.words[i];
    dsp56k::Opcodes op;dsp56k::Disassembler dis(op);
    for(auto range:{std::pair<uint32_t,uint32_t>{0x24,0xa4},{0x342,0x9de},{0x100000,0x100400}})
        for(auto address=range.first;address<range.second;){std::string s;auto n=dis.disassemble(s,program[address],program[address+1],0,0,address);dspFile<<std::hex<<address<<" "<<s<<"\n";address+=n?n:1;}
    for(auto range:{std::pair<uint32_t,uint32_t>{0x200000,0x249000},{0x1000000,0x100029e}})
        for(auto address=range.first;address<range.second;){char s[512];size_t offset=address<0x1000000?address-0x200000:address-0x1000000+0x622f4;
            auto n=m68k_disassemble_raw(s,address,fw.os.data()+offset,fw.os.data()+offset,M68K_CPU_TYPE_68020)&0xffff;cpuFile<<std::hex<<address<<" "<<s<<"\n";address+=n?n:2;}
    std::cout<<"Private disassembly written\n";
}


