#include "FirmwareLoader.h"
#include "MasterFx.h"
#include <chrono>
#include <iostream>
#include <iomanip>
#include <windows.h>
static double cpuMs(){FILETIME a{},b{},k{},u{};GetProcessTimes(GetCurrentProcess(),&a,&b,&k,&u);ULARGE_INTEGER x{},y{};x.LowPart=k.dwLowDateTime;x.HighPart=k.dwHighDateTime;y.LowPart=u.dwLowDateTime;y.HighPart=u.dwHighDateTime;return double(x.QuadPart+y.QuadPart)/10000.;}
int main(int argc,char** argv){try {
 if(argc<2)return 2;auto fw=pc::loadFirmware(argv[1]);
 for(int budget:{6,16})for(int trial=0;trial<3;++trial){
  md::engine::ParallelEngine e(fw.firmware,fw.os,fw.firmware.x13?1:3);
  md::engine::MasterFx fx(fw.firmware,fw.os);fx.configure(md::engine::MasterFx::defaults,120);
  auto& h=e.host();h.setMaxActiveVoices(budget);h.setTempo(120);h.setSilenceRelease(fw.firmware.x13?-1:128,138);e.timingOn=true;
  for(int t=0;t<16;++t){h.setMachine(t,uint8_t(16+t));h.setParam(t,13,127);h.setParam(t,17,100);h.setLevel(t,100);h.setParam(t,19,40);h.setParam(t,20,40);}
  uint64_t hash=14695981039346656037ull;double master=0;
  const double cpuStart=cpuMs();auto start=std::chrono::steady_clock::now();
  for(int b=0;b<3000;++b){
   if(b%128==0)for(int t=0;t<16;++t)h.trigger(t,110);
   md::engine::ParallelEngine::Output out{};
   if(!e.render(out))throw std::runtime_error("Voice render failed");
   auto m=std::chrono::steady_clock::now();if(!fx.process(out.mix,false,false))throw std::runtime_error("Master render failed");
   master+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-m).count();
   for(auto pair:out.mix.main)for(auto sample:pair){hash^=uint32_t(sample);hash*=1099511628211ull;}
  }
  const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  std::cout<<std::fixed<<std::setprecision(2)<<"voices="<<budget<<" trial="<<trial+1<<" wall="<<elapsed<<" cpu="<<cpuMs()-cpuStart<<" control="<<h.tickUs/1000<<" dsp="<<h.dspUs/1000<<" mix="<<e.mixUs/1000<<" master="<<master<<" hash="<<std::hex<<hash<<std::dec<<std::endl;
 }
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
