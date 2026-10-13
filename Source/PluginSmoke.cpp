#include <juce_audio_utils/juce_audio_utils.h>
#include <iostream>
#include <stdexcept>
#include "KitMenu.h"

extern juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();
using namespace juce;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
int main(int argc,char** argv) {
    ScopedJuceInitialiser_GUI init;
    try {
        require(argc>=2,"Pass the firmware path and optional editor PNG path.");
        if(argc>3) {
            const auto kits=pc::readKits(std::filesystem::path(File(String(argv[3])).getFullPathName().toWideCharPointer()));
            ComboBox picker;pc::populateKitMenu(picker,kits);
            require(picker.getNumItems()==(int)kits.size(),"Imported kit slots missing from menu");
            int unnamed=0;
            for(size_t i=0;i<kits.size();++i) {
                picker.setSelectedId((int)i+1,dontSendNotification);
                require(picker.getSelectedId()==(int)i+1&&!picker.getText().isEmpty(),"Kit slot cannot be selected");
                if(kits[i].name.empty()) {
                    ++unnamed;require(picker.getText().contains("UNNAMED"),"Unnamed slot needs a visible fallback label");
                }
            }
            std::cout<<"Kit browser: all "<<kits.size()<<" slots selectable, "<<unnamed<<" unnamed slots visible"<<std::endl;
        }
        std::unique_ptr<AudioProcessor> p(createPluginFilter());
        MemoryBlock state;p->getStateInformation(state);
        auto xml=AudioProcessor::getXmlFromBinary(state.getData(),(int)state.getSize());
        xml->setAttribute("firmware",String(argv[1]));
        AudioProcessor::copyXmlToBinary(*xml,state);p->setStateInformation(state.getData(),(int)state.getSize());
        p->prepareToPlay(44100,512);
        AudioBuffer<float> block(2,512);MidiBuffer midi;
        auto waitForFirmware=[&](const String& path) {
            const auto end=Time::getMillisecondCounterHiRes()+15000.0;
            while(Time::getMillisecondCounterHiRes()<end) {
                MemoryBlock current;p->getStateInformation(current);
                auto currentXml=AudioProcessor::getXmlFromBinary(current.getData(),(int)current.getSize());
                if(currentXml->getStringAttribute("firmware")==path)return;
                midi.clear();p->processBlock(block,midi);Thread::sleep(10);
            }
            throw std::runtime_error("Firmware load did not complete");
        };
        waitForFirmware(String(argv[1]));
        float peak=0;
        // Restore set all SYN values to initial zeros; explicitly choose audible synthesis values.
        for(auto* parameter:p->getParameters()) {
            auto* named=dynamic_cast<AudioProcessorParameterWithID*>(parameter);
            if(named&&named->paramID=="t1_machine")parameter->setValueNotifyingHost(17.0f/191.0f);
            if(named&&named->paramID=="t1_p1")parameter->setValueNotifyingHost(0.5f);
            if(named&&named->paramID=="t1_p2")parameter->setValueNotifyingHost(0.5f);
        }
        for(int n=0;n<500 && peak<1e-6f;++n) {
            midi.addEvent(MidiMessage::noteOn(1,36,(uint8)110),0);
            p->processBlock(block,midi);peak=std::max(peak,block.getMagnitude(0,512));Thread::sleep(10);
        }
        require(peak>1e-6f,"Plugin produced no audio after firmware load and MIDI trigger.");
        std::cout<<"Firmware/MIDI peak: "<<peak<<std::endl;
        for(double sr:{44100.0,48000.0,96000.0}) {
            p->releaseResources();p->prepareToPlay(sr,511);
            for(int size:{1,17,127,511}) {
                block.setSize(2,size);midi.clear();midi.addEvent(MidiMessage::noteOn(1,36,(uint8)100),size-1);
                for(int n=0;n<20;++n) {
                    p->processBlock(block,midi);
                    for(int c=0;c<2;++c)for(int i=0;i<size;++i)require(std::isfinite(block.getSample(c,i)),"Non-finite audio");
                }
            }
            std::cout<<"Rate/block-size checks: "<<sr<<std::endl;
        }
        auto buses=p->getBusesLayout();for(int i=1;i<buses.outputBuses.size();++i)buses.outputBuses.set(i,i<3?AudioChannelSet::stereo():AudioChannelSet::mono());
        require(p->setBusesLayout(buses),"Could not enable individual outputs");
        p->prepareToPlay(48000,512);block.setSize(22,512);
        float trackPeak=0,mainPeak=0;
        for(int n=0;n<100;++n){midi.clear();if(n%10==0)midi.addEvent(MidiMessage::noteOn(1,36,(uint8)110),0);p->processBlock(block,midi);if(n>2){trackPeak=std::max(trackPeak,block.getMagnitude(6,0,512));mainPeak=std::max(mainPeak,block.getMagnitude(0,0,512));}}
        require(trackPeak>0,"Individual track output is silent");require(mainPeak==0,"Individually routed track leaked into main mix");
        std::cout<<"Individual output routing passed\n";
        // With dry track 1 routed out, only the built-in effects can reach Main.
        buses.outputBuses.set(1,AudioChannelSet::disabled());buses.outputBuses.set(2,AudioChannelSet::disabled());
        require(p->setBusesLayout(buses),"Could not select internal effects");
        auto setRaw=[&](const String& id,float v){for(auto* parameter:p->getParameters())if(auto* named=dynamic_cast<RangedAudioParameter*>(parameter))if(named->paramID==id)named->setValueNotifyingHost(named->convertTo0to1(v));};
        for(double sr:{44100.0,48000.0,96000.0})for(int effect:{19,20}) {
            setRaw("t1_p19",effect==19?127.0f:0.0f);setRaw("t1_p20",effect==20?127.0f:0.0f);
            p->prepareToPlay(sr,512);block.setSize(p->getTotalNumOutputChannels(),512);
            float wetPeak=0;
            for(int n=0;n<(int)(sr/512)+10;++n){midi.clear();if(n==0)midi.addEvent(MidiMessage::noteOn(1,36,(uint8)110),0);p->processBlock(block,midi);wetPeak=std::max(wetPeak,block.getMagnitude(0,0,512));}
            require(wetPeak>1e-5f,"Send knob produced no built-in effect on Main");
            std::cout<<(effect==19?"Delay":"Reverb")<<" main return at "<<sr<<": "<<wetPeak<<std::endl;
        }
        setRaw("t1_p19",0);setRaw("t1_p20",0);p->prepareToPlay(48000,512);
        for(int budget:{6,16}) {
        setRaw("voices",(float)budget);
        auto started=Time::getMillisecondCounterHiRes();
        for(int n=0;n<200;++n){midi.clear();if(n%20==0)for(int t=0;t<16;++t)midi.addEvent(MidiMessage::noteOn(1,36+t,(uint8)110),0);p->processBlock(block,midi);}
        const double elapsed=Time::getMillisecondCounterHiRes()-started;
        std::cout<<"16-track stress, voice budget "<<budget<<": "<<elapsed<<" ms for "<<200.0*512/48.0<<" ms of audio\n";
        }
        setRaw("voices",6);
        setRaw("t1_tonal",1);
        p->getStateInformation(state);
        std::unique_ptr<AudioProcessor> restored(createPluginFilter());restored->setStateInformation(state.getData(),(int)state.getSize());
        require(restored->getParameters().size()==p->getParameters().size(),"Parameter count changed");
        for(int i=0;i<p->getParameters().size();++i)require(std::abs(restored->getParameters()[i]->getValue()-p->getParameters()[i]->getValue())<1e-6,"State restore mismatch");
        std::cout<<"All "<<p->getParameters().size()<<" parameter values restored\n";
        if(argc>2) {
            std::unique_ptr<AudioProcessorEditor> editor(p->createEditor());
            auto img=editor->createComponentSnapshot(editor->getLocalBounds());
            FileOutputStream stream{File(String(argv[2]))};stream.setPosition(0);stream.truncate();PNGImageFormat png;require(png.writeImageToStream(img,stream),"Could not save editor snapshot");
            std::cout<<"Editor snapshot saved\n";
            for(int i=0;i<editor->getNumChildComponents();++i)if(auto* button=dynamic_cast<TextButton*>(editor->getChildComponent(i)))
                if(button->getButtonText()=="MASTER FX"){
                    button->onClick();
                    auto masterImage=editor->createComponentSnapshot(editor->getLocalBounds());
                    FileOutputStream masterStream{File(String(argv[2])).getSiblingFile("machinemodule-master-preview.png")};
                    masterStream.setPosition(0);masterStream.truncate();require(png.writeImageToStream(masterImage,masterStream),"Could not save master snapshot");
                    button->onClick();
                }
        }
        if(argc>4) {
            std::unique_ptr<AudioProcessorEditor> switchEditor(p->createEditor());
            auto switchFirmware=[&](const String& path,bool expectExpanded) {
                p->getStateInformation(state);
                auto switchXml=AudioProcessor::getXmlFromBinary(state.getData(),(int)state.getSize());
                switchXml->setAttribute("firmware",path);
                AudioProcessor::copyXmlToBinary(*switchXml,state);p->setStateInformation(state.getData(),(int)state.getSize());
                waitForFirmware(path);
                Thread::sleep(100);Timer::callPendingTimersSynchronously();
                bool sawExpanded=false;
                for(auto* child:switchEditor->getChildren())if(auto* combo=dynamic_cast<ComboBox*>(child))
                    for(int i=0;i<combo->getNumItems();++i)if(combo->getItemText(i)=="GND SW")sawExpanded=true;
                require(sawExpanded==expectExpanded,"Machine menu did not match switched firmware");
            };
            switchFirmware(String(argv[4]),false);
            switchFirmware(String(argv[1]),true);
            std::cout<<"X.13 / stock / X.13 switching and machine menus passed\n";
        }
        p->releaseResources();std::cout<<"Plugin checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
}

