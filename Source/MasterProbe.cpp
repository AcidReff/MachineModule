#include "FirmwareLoader.h"
#include "MasterFx.h"
#include <iostream>
#include <chrono>
#include <stdexcept>
using namespace md::engine;
void require(bool v,const char* s){if(!v)throw std::runtime_error(s);}
void configure(md::mixref::MixerRef& dsp,MachineRunner& os,const std::array<uint8_t,32>& p,double bpm){
 require(os.computeMaster(p,bpm),"coefficient routine failed");
 for(auto map:{std::array<uint32_t,3>{0x1001af4,0x150,9},{0x1001b18,0x17a,11},{0x1001b44,0x170,10},{0x1001b74,0x185,8}})
  for(unsigned i=0;i<map[2];++i)dsp.pokeY(map[1]+i,os.peek32(map[0]+4*i));
 dsp.pokeY(0x15a,dsp.peekY(0x150));dsp.pokeY(0x15b,dsp.peekY(0x151));dsp.pokeY(0x15d,0);
}
int main(int argc,char**argv) {try{
 require(argc>=2,"Pass firmware path");auto fw=pc::loadFirmware(argv[1]);
 MasterFx fx(fw.firmware,fw.os);auto params=MasterFx::defaults;
 md::mixref::MixerRef ref(fw.firmware);MachineRunner os(fw.os);TrackFx::Tables tables(fw.firmware);Mixer mixer(tables);
 
 std::array<md::mixref::MixerRef::Block,16> tracks{};
 std::array<const int32_t*,16> pointers{};
 std::array<std::array<uint32_t,5>,16> coefficients{};uint32_t raw[16][5]{};
 for(int t=0;t<16;++t){pointers[t]=tracks[t].data();coefficients[t]={6,8000,8192,4000,4000};for(int j=0;j<5;++j)raw[t][j]=coefficients[t][j];}
 uint32_t rng=77;int peak=0;auto start=std::chrono::steady_clock::now();
 for(int variant=0;variant<3;++variant){ params=MasterFx::defaults;if(variant==1){params[8]=16;params[9]=30;params[11]=55;params[0]=32;params[6]=12;}if(variant==2){params[17]=90;params[19]=35;params[21]=80;params[26]=0;params[27]=100;params[31]=0;}fx.reset();ref.resetMaster();configure(ref,os,params,120);require(fx.configure(params,120),"config failed");
 for(int b=0;b<1200;++b){
  for(int t=0;t<16;++t)for(auto& v:tracks[t]){rng=rng*1664525+1013904223;v=b%100<2?(int(rng>>16)-32768)*(variant==2?100:1):0;}
  Mixer::Output out{};mixer.process(pointers.data(),coefficients.data(),out);
  require(ref.runMix(tracks.data(),raw),"reference mix failed");
  for(int i=0;i<32;++i)for(int c=0;c<2;++c)require((uint32_t(out.main[i][c])&0xffffff)==ref.peekX(0x180+2*i+c),"native mix mismatch");
  require(ref.runMaster(),"reference master failed");require(fx.process(out,false,false),"master failed");
  for(int i=0;i<32;++i)for(int c=0;c<2;++c){int expected=int32_t(ref.peekX(0x180+2*i+c)<<8)>>8;if(out.main[i][c]!=expected){std::cerr<<"Mismatch block="<<b<<" frame="<<i<<" channel="<<c<<" got="<<out.main[i][c]<<" expected="<<expected<<"\n";return 1;}peak=std::max(peak,std::abs(expected));}
 }
 } params=MasterFx::defaults;
 std::cout<<"Original DSP mixer+master comparison: 230400 matching samples, peak="<<peak<<"\n";
 // Exercise live control changes, extremes, clipping and tempo changes against
 // an independently running interpreter, including the delay's moving heads.
 {
  MasterFx interpreter(fw.firmware,fw.os,false);fx.reset();
  for(int b=0;b<2400;++b) {
   if(b%40==0){for(auto& p:params){rng=rng*1664525+1013904223;p=uint8_t((rng>>24)&127);}double bpm=30+(b%271);require(fx.configure(params,bpm)&&interpreter.configure(params,bpm),"Live parameter configuration failed");}
   Mixer::Output a{};
   for(int i=0;i<32;++i)for(int c=0;c<2;++c){rng=rng*1664525+1013904223;int v=int(rng>>9)-4194304;a.main[i][c]=v;a.rev[i][c]=v/4;a.del[i][c]=-v/4;}
   auto z=a;require(interpreter.process(a,false,false)&&fx.process(z,false,false),"Live control render failed");
   require(a.main==z.main,"JIT/interpreter mismatch during live effect changes");
  }
  std::cout<<"Live effect/tempo changes: 153600 matching samples\n";params=MasterFx::defaults;
 }
 auto renderImpulse=[&](int kind,double tempo){fx.reset();require(fx.configure(params,tempo),"config failed");std::vector<int32_t> result;result.reserve(88200*2);
  for(int b=0;b<2757;++b){Mixer::Output out{};if(b==0){auto& input=kind==0?out.main:kind==1?out.rev:out.del;input[0]={100000,100000};}require(fx.process(out,false,false),"impulse failed");for(auto s:out.main)for(int v:s)result.push_back(v);}return result;};
 for(int kind=0;kind<3;++kind){auto a=renderImpulse(kind,120),b=renderImpulse(kind,120);require(a==b,"Reset is not deterministic");double energy=0;int first=-1,pk=0;for(size_t i=0;i<a.size();++i){energy+=double(a[i])*a[i];pk=std::max(pk,std::abs(a[i]));if(std::abs(a[i])>10&&first<0)first=int(i/2);}require(energy>1000,"Missing impulse response");require(pk<8388607,"Impulse clipped");std::cout<<"Impulse "<<kind<<" first="<<first<<" peak="<<pk<<" energy="<<energy<<" reset matched\n";
  if(kind==2){auto fast=renderImpulse(kind,240);int f=-1;for(size_t i=0;i<fast.size();++i)if(std::abs(fast[i])>10){f=int(i/2);break;}std::cout<<"Fast delay first="<<f<<"\n";require(f>0&&f<first*0.6&&f>first*0.4,"Delay tempo did not halve");std::cout<<"Delay at 240 BPM first="<<f<<"\n";}}
 fx.reset();require(fx.configure(params,120),"config failed");for(int b=0;b<3000;++b){Mixer::Output out{};require(fx.process(out,false,false),"silence failed");for(auto s:out.main)for(int v:s)require(v==0,"Reset left a tail");}
 std::cout<<"Original master checks passed, elapsed "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<" seconds\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}


