#pragma once
#include <array>
#include <cstring>
#include "Firmware.h"
namespace pc {
struct Kit {
    std::string name;
    int slot=0;
    std::array<bool,16> tonal{};
    std::array<int,16> machine{},level{};
    std::array<std::array<int,24>,16> params{};
    std::array<std::array<int,5>,16> lfo{};
};
inline std::vector<uint8_t> decode7(const uint8_t* p,size_t n) {
    std::vector<uint8_t> out;
    for(size_t i=0;i<n;i+=8)for(size_t j=1;j<8&&i+j<n;++j)out.push_back((uint8_t)(p[i+j]|((p[i]<<j)&0x80)));
    return out;
}
// Adapted from sd88me/mpc-vst-machinedrum/vst/engine.cpp (AGPL-3.0-only).
inline std::vector<Kit> readKits(const std::filesystem::path& path) {
    auto bytes=md::fw::readFile(path);std::vector<Kit> kits;
    constexpr uint8_t header[]={0xf0,0,0x20,0x3c,2,0,0x52};
    for(size_t i=0;i<bytes.size();++i) {
        if(bytes[i]!=0xf0)continue;
        size_t end=i+1;while(end<bytes.size()&&bytes[end]!=0xf7)++end;
        const auto* m=bytes.data()+i;
        if(end<bytes.size()&&end-i+1==0x4d1&&!std::memcmp(m,header,sizeof(header))) {
            Kit kit;kit.slot=m[9]&127;
            for(int c=0;c<16&&m[10+c]>=32&&m[10+c]<127;++c)kit.name+=(char)m[10+c];
            auto machines=decode7(m+0x1aa,74),lfo=decode7(m+0x1f4,664);
            for(int t=0;t<16;++t) {
                kit.machine[t]=machines[t*4+3];kit.level[t]=m[0x19a+t]&127;
                kit.tonal[t]=(machines[t*4+1]&2)!=0;
                for(int p=0;p<24;++p)kit.params[t][p]=m[0x1a+t*24+p]&127;
                for(int p=0;p<5;++p)kit.lfo[t][p]=lfo[t*36+p];
            }
            kits.push_back(std::move(kit));
        }
        i=end;
    }
    if(kits.empty())throw std::runtime_error("No Machinedrum kits found in this SysEx file.");
    return kits;
}
}
