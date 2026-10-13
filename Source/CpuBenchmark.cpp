#include "FirmwareLoader.h"
#include "MasterFx.h"
#include <chrono>
#include <iostream>
#include <iomanip>
#include <windows.h>
double processCpuMs(){FILETIME start{},end{},kernel{},user{};GetProcessTimes(GetCurrentProcess(),&start,&end,&kernel,&user);ULARGE_INTEGER k{},u{};k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;return double(k.QuadPart+u.QuadPart)/10000.0;}
struct Result {double ms,cpuMs;uint64_t hash;};
Result render(const pc::LoadedFirmware& fw,int budget,bool jit) {
 md::engine::ParallelEngine engine(fw.firmware,fw.os,3);
 md::engine::MasterFx fx(fw.firmware,fw.os,jit);
 if(!fx.configure(md::engine::MasterFx::defaults,120))throw std::runtime_error("Master configuration failed");
 auto& h=engine.host();h.setMaxActiveVoices(budget);h.setTempo(120);h.setSilenceRelease(128,138);
 for(int t=0;t<16;++t){h.setMachine(t,uint8_t(16+t));h.setParam(t,13,127);h.setParam(t,17,100);h.setLevel(t,100);h.setParam(t,19,40);h.setParam(t,20,40);}
 uint64_t hash=14695981039346656037ull;
 double cpuStart=processCpuMs();auto start=std::chrono::steady_clock::now();
 for(int b=0;b<3000;++b){
  if(b%128==0)for(int t=0;t<16;++t)h.trigger(t,110);
  md::engine::ParallelEngine::Output out{};
  if(!engine.render(out)||!fx.process(out.mix,false,false))throw std::runtime_error("Render failed");
  for(auto pair:out.mix.main)for(auto sample:pair){hash^=uint32_t(sample);hash*=1099511628211ull;}
 }
 return {std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count(),processCpuMs()-cpuStart,hash};
}
int main(int argc,char** argv){try{
 if(argc<2)return 2;auto fw=pc::loadFirmware(argv[1]);
 std::cout<<std::fixed<<std::setprecision(2)<<"3000 blocks / "<<3000.0*32/44.1<<" ms audio, three voice groups, original effects enabled\n";
 for(int budget:{6,16}){
  double slow=0,fast=0,slowCpu=0,fastCpu=0;
  for(int trial=0;trial<3;++trial){
   Result a{},b{};if(trial%2){b=render(fw,budget,true);a=render(fw,budget,false);}else{a=render(fw,budget,false);b=render(fw,budget,true);}
   if(a.hash!=b.hash)throw std::runtime_error("Optimized audio hash differs from interpreter");
   slow+=a.ms;fast+=b.ms;slowCpu+=a.cpuMs;fastCpu+=b.cpuMs;std::cout<<"Voices="<<budget<<" trial="<<trial+1<<" interpreter="<<a.ms<<" ms optimized="<<b.ms<<" ms CPU="<<a.cpuMs<<"/"<<b.cpuMs<<" ms audio hash matched\n"<<std::flush;
  }
  std::cout<<"Voices="<<budget<<" average interpreter="<<slow/3<<" ms optimized="<<fast/3<<" ms reduction="<<100*(1-fast/slow)<<"%\n";
  std::cout<<"Voices="<<budget<<" average process CPU interpreter="<<slowCpu/3<<" ms optimized="<<fastCpu/3<<" ms reduction="<<100*(1-fastCpu/slowCpu)<<"%\n";
 }
 return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
