#pragma once
#include "Firmware.h"
#include "MdEngine.h"

namespace pc {
inline bool isRom(int id) {return (id>=128 && id<160)||(id>=176 && id<192);}
template<class Voices> bool loadSamples(Voices& voices,const std::filesystem::path& path) {
    if(!std::filesystem::exists(path))return false;
    const auto bytes=md::fw::readFile(path);
    if(bytes.size()<12 || std::string((const char*)bytes.data(),4)!="MDS1")throw std::runtime_error("Invalid ROM_SAMPLES.bin header");
    auto word=[&](size_t p){return uint32_t(bytes[p])|(uint32_t(bytes[p+1])<<8)|(uint32_t(bytes[p+2])<<16)|(uint32_t(bytes[p+3])<<24);};
    bool loaded=false;
    for(size_t p=4;p+8<=bytes.size();) {
        auto address=word(p),count=word(p+4);p+=8;
        if(count==0)return loaded;
        if(address<0x140000 || address>=0x200000 || count>0x200000-address || count>(bytes.size()-p)/4)
            throw std::runtime_error("Invalid ROM sample memory record");
        std::vector<uint32_t> data(count);
        for(auto& v:data){v=word(p)&0xffffff;p+=4;}
        voices.writeP(address,data.data(),data.size());loaded=true;
    }
    throw std::runtime_error("Truncated ROM sample file");
}
struct LoadedFirmware {
    md::fw::Firmware firmware;
    std::vector<uint8_t> os;
};
inline LoadedFirmware loadFirmware(const std::filesystem::path& path) {
    const auto bytes = md::fw::readFile(path);
    md::fw::FlashImage flash;
    if (bytes.size() == 0x800000) {
        flash.base = 0x4000;
        flash.bytes.assign(bytes.begin() + 0x4000, bytes.end());
    } else {
        flash = md::fw::parseSysex(bytes);
    }
    auto container = md::fw::parseContainer(flash);
    if (container.sections.size() < 3)
        throw md::fw::FirmwareError("No Machinedrum firmware sections found. Select OS 1.63 SysEx or an 8 MB flash image.");
    if (!container.x13 && container.version!="163 ")
        throw md::fw::FirmwareError("Supported firmware: OS 1.63 and the verified UW X.13 release; detected: " + container.version);
    for (int i = 1; i <= 2; ++i)
        if (!container.sections[i].sumOk && !container.x13) throw md::fw::FirmwareError("DSP firmware checksum failed or unsupported modified firmware.");
    LoadedFirmware result;
    result.firmware.dspA = md::fw::parseDspRecords(container.sections[1].data);
    result.firmware.dspB = md::fw::parseDspRecords(container.sections[2].data);
    result.firmware.version = container.version;
    result.firmware.source = path;
    result.os = std::move(container.sections[0].data);
    if(container.x13)md::fw::installX13VoiceProgram(result.firmware,result.os);
    return result;
}
}
