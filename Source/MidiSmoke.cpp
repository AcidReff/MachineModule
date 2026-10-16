#include <juce_audio_utils/juce_audio_utils.h>
#include <iostream>
#include <stdexcept>
#include <cmath>
#include "FirmwareLoader.h"
extern juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();
using namespace juce;
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(int argc,char** argv) {
    ScopedJuceInitialiser_GUI init;
    try {
        if(argc<2)return 2;
        const bool supportsTransposition=pc::loadFirmware(argv[1]).firmware.x13;
        std::unique_ptr<AudioProcessor> p(createPluginFilter());
        auto layout=p->getBusesLayout();for(int i=0;i<layout.outputBuses.size();++i)layout.outputBuses.set(i,i<3?AudioChannelSet::stereo():AudioChannelSet::mono());
        require(p->setBusesLayout(layout),"Could not enable all outputs");p->prepareToPlay(44100,512);
        MemoryBlock state;p->getStateInformation(state);
        auto xml=AudioProcessor::getXmlFromBinary(state.getData(),(int)state.getSize());xml->setAttribute("firmware",String(argv[1]));
        AudioProcessor::copyXmlToBinary(*xml,state);p->setStateInformation(state.getData(),(int)state.getSize());
        AudioBuffer<float> audio(22,512);MidiBuffer midi;
        bool ready=false;
        for(int i=0;i<1500;++i){p->getStateInformation(state);auto x=AudioProcessor::getXmlFromBinary(state.getData(),(int)state.getSize());if(x->getStringAttribute("firmware")==String(argv[1])){ready=true;break;}midi.clear();p->processBlock(audio,midi);Thread::sleep(10);}
        require(ready,"Firmware did not load");
        auto parameter=[&](const String& id)->RangedAudioParameter*{for(auto* a:p->getParameters())if(auto* r=dynamic_cast<RangedAudioParameter*>(a))if(r->paramID==id)return r;throw std::runtime_error("Parameter missing");};
        auto set=[&](const String& id,float v){auto* r=parameter(id);r->setValueNotifyingHost(r->convertTo0to1(v));};
        auto raw=[&](const String& id){auto* r=parameter(id);return r->convertFrom0to1(r->getValue());};
        set("voices",16);set("midi_mode",1);
        for(int t=1;t<=16;++t){const String prefix="t"+String(t);set(prefix+"_machine",1);set(prefix+"_tonal",0);for(int k=0;k<8;++k)set(prefix+"_p"+String(k),k==0?64.0f:k==1?127.0f:0.0f);}
        auto render=[&](int n){for(int i=0;i<n;++i){p->processBlock(audio,midi);midi.clear();for(int c=0;c<22;++c)for(int s=0;s<512;++s)require(std::isfinite(audio.getSample(c,s)),"Non-finite MIDI rendering");}};
        auto silence=[&]{midi.clear();for(int ch=1;ch<=16;++ch)midi.addEvent(MidiMessage::allSoundOff(ch),0);render(8);};
        for(int ch=1;ch<=16;++ch){silence();midi.addEvent(MidiMessage::noteOn(ch,60,(uint8)110),0);render(4);std::cout<<"Channel "<<ch<<" peak="<<audio.getMagnitude(ch+5,0,512)<<" mode="<<raw("midi_mode")<<std::endl;require(audio.getMagnitude(ch+5,0,512)>1e-5f,"MIDI channel did not play its track");for(int t=0;t<16;++t)if(t!=ch-1)require(audio.getMagnitude(t+6,0,512)<1e-4f,"MIDI channel leaked to another track");}
        std::cout<<"All 16 MIDI channels address their own tracks\n";
        auto frequency=[&](int note){silence();midi.addEvent(MidiMessage::noteOn(1,note,(uint8)110),0);render(3);int crossings=0;float previous=audio.getSample(6,511);for(int b=0;b<32;++b){render(1);for(int s=0;s<512;++s){float v=audio.getSample(6,s);if(previous<=0&&v>0)++crossings;previous=v;}}return crossings*44100.0/(32*512);};
        const double low=frequency(60),high=frequency(72);
        std::cout<<"Octave test: "<<low<<" Hz -> "<<high<<" Hz\n";
        require(low>10,"MIDI reference note was silent");
        if(supportsTransposition)
            require(std::abs(high/low-2.0)<0.08,"X.13 MIDI octave did not double pitch");
        else
            require(std::abs(high-low)<44100.0/(32*512)+0.01,"Stock firmware unexpectedly transposed pitch");
        std::cout<<(supportsTransposition?"X.13 octave transposition passed":"Stock firmware channel mapping without transposition passed")<<std::endl;
        require(raw("t1_p0")==64&&raw("t1_tonal")==0,"MIDI overwrote saved base pitch or tuning flag");
        silence();midi.addEvent(MidiMessage::noteOn(1,60,(uint8)110),0);midi.addEvent(MidiMessage::noteOn(2,60,(uint8)110),0);render(4);
        midi.addEvent(MidiMessage::allSoundOff(1),0);render(8);
        require(audio.getMagnitude(6,0,512)<1e-4f&&audio.getMagnitude(7,0,512)>1e-5f,"Channel all-sound-off affected another track");
        midi.addEvent(MidiMessage::noteOff(2,60),0);render(2);require(audio.getMagnitude(7,0,512)>1e-5f,"Note-off cut drum tail");
        set("midi_mode",0);render(8);midi.addEvent(MidiMessage::noteOn(16,36,(uint8)110),0);render(4);
        require(audio.getMagnitude(6,0,512)>1e-5f&&audio.getMagnitude(21,0,512)<1e-4f,"Drum mapping changed");
        set("midi_mode",1);render(8);for(int note:{0,127}){midi.addEvent(MidiMessage::noteOn(1,note,(uint8)110),0);render(2);}
        p->getStateInformation(state);std::unique_ptr<AudioProcessor> restored(createPluginFilter());restored->setStateInformation(state.getData(),(int)state.getSize());
        auto modeValue=[&]{for(auto* a:restored->getParameters())if(auto* r=dynamic_cast<RangedAudioParameter*>(a))if(r->paramID=="midi_mode")return r->getValue();return -1.0f;};
        require(modeValue()==1.0f,"Chromatic mode not restored");
        xml=AudioProcessor::getXmlFromBinary(state.getData(),(int)state.getSize());
        for(auto* child=xml->getFirstChildElement();child;){auto* next=child->getNextElement();if(child->getStringAttribute("id")=="midi_mode")xml->removeChildElement(child,true);child=next;}
        AudioProcessor::copyXmlToBinary(*xml,state);restored->setStateInformation(state.getData(),(int)state.getSize());require(modeValue()==0.0f,"Legacy project did not default to Drum");
        if(argc>2){std::unique_ptr<AudioProcessorEditor> editor(p->createEditor());auto img=editor->createComponentSnapshot(editor->getLocalBounds());FileOutputStream stream{File(String(argv[2]))};stream.setPosition(0);stream.truncate();PNGImageFormat png;require(png.writeImageToStream(img,stream),"Snapshot failed");}
        std::cout<<"Channel panic, note-off tails, Drum mode, extreme notes and state checks passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
}
