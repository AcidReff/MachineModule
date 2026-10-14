#include <juce_audio_utils/juce_audio_utils.h>
#include <iostream>
#include <iomanip>
#include <windows.h>
extern juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();
using namespace juce;
static void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
static double cpuMs(){FILETIME a{},b{},k{},u{};GetProcessTimes(GetCurrentProcess(),&a,&b,&k,&u);ULARGE_INTEGER x{},y{};x.LowPart=k.dwLowDateTime;x.HighPart=k.dwHighDateTime;y.LowPart=u.dwLowDateTime;y.HighPart=u.dwHighDateTime;return double(x.QuadPart+y.QuadPart)/10000.;}
int main(int argc,char** argv){ScopedJuceInitialiser_GUI init;try {
 require(argc>1,"Pass firmware path");
 for(int layout:{0,1,2}) {
  std::unique_ptr<AudioProcessor> p(createPluginFilter());
  auto buses=p->getBusesLayout();
  for(int i=1;i<buses.outputBuses.size();++i)buses.outputBuses.set(i,(layout==2||(layout==1&&(i==2||i==7||i==18)))?(i<3?AudioChannelSet::stereo():AudioChannelSet::mono()):AudioChannelSet::disabled());
  require(p->setBusesLayout(buses),"Output layout rejected");
  MemoryBlock state;p->getStateInformation(state);auto xml=AudioProcessor::getXmlFromBinary(state.getData(),(int)state.getSize());
  xml->setAttribute("firmware",String(argv[1]));AudioProcessor::copyXmlToBinary(*xml,state);p->setStateInformation(state.getData(),(int)state.getSize());
  bool loaded=false;
  for(int i=0;i<3000;++i){p->getStateInformation(state);auto current=AudioProcessor::getXmlFromBinary(state.getData(),(int)state.getSize());if(current->getStringAttribute("firmware")==String(argv[1])){loaded=true;break;}Thread::sleep(10);}
  require(loaded,"Firmware load timeout");
  auto set=[&](const String& id,float value){for(auto* a:p->getParameters())if(auto* r=dynamic_cast<RangedAudioParameter*>(a))if(r->paramID==id){r->setValueNotifyingHost(r->convertTo0to1(value));return;}throw std::runtime_error("Missing parameter");};
  set("voices",6);set("midi_mode",0);
  for(int t=1;t<=16;++t){const String key="t"+String(t);set(key+"_machine",17);set(key+"_p0",64);set(key+"_p1",96);set(key+"_p13",127);set(key+"_p17",100);set(key+"_p19",50);set(key+"_p20",50);}
  set("t1_p27",5);set("t1_p28",5);set("t1_p21",80);set("t1_p22",35);
  const double rate=layout==0?44100:layout==1?48000:96000;
  p->prepareToPlay(rate,511);AudioBuffer<float> audio(p->getTotalNumOutputChannels(),511);MidiBuffer midi;
  uint64_t hash=14695981039346656037ull;float peak=0;
  const double start=Time::getMillisecondCounterHiRes(),cpuStart=cpuMs();
  for(int block=0;block<400;++block){
   const int n=block%13==0?17:511;audio.setSize(p->getTotalNumOutputChannels(),n,false,false,true);midi.clear();
   if(block%24==0)for(int t=0;t<16;++t)midi.addEvent(MidiMessage::noteOn(1,36+t,(uint8)(80+t)),t%n);
   if(block%31==0)set("t1_p18",float((block*7)%128));
   p->processBlock(audio,midi);
   for(int c=0;c<audio.getNumChannels();++c)for(int s=0;s<n;++s){float value=audio.getSample(c,s);require(std::isfinite(value),"Nonfinite output");peak=std::max(peak,std::abs(value));uint32_t bits;std::memcpy(&bits,&value,4);hash^=bits;hash*=1099511628211ull;}
  }
  require(peak>1e-5f,"Silent processor output");
  std::cout<<std::fixed<<std::setprecision(2)<<"layout="<<layout<<" rate="<<rate<<" wall="<<Time::getMillisecondCounterHiRes()-start<<" cpu="<<cpuMs()-cpuStart<<" hash="<<std::hex<<hash<<std::dec<<std::endl;
 }
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
