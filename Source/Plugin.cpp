#include <juce_audio_utils/juce_audio_utils.h>
#include "FirmwareLoader.h"
#include "Kits.h"
#include "KitMenu.h"
#include "LcdStyle.h"
#include "MasterFx.h"
#include "MidiMapping.h"
#include <condition_variable>
#include <cstdio>
#include <thread>

using namespace juce;
namespace {
constexpr const char* names[] = {"SYN 1", "SYN 2", "SYN 3", "SYN 4", "SYN 5", "SYN 6", "SYN 7", "SYN 8",
    "AMD", "AMF", "EQF", "EQG", "FLTF", "FLTW", "FLTQ", "SRR", "DIST", "VOL", "PAN", "DEL", "REV", "LFO SPEED", "LFO DEPTH", "LFO MIX",
    "LEVEL", "LFO TRACK", "LFO PARAM", "LFO SHAPE 1", "LFO SHAPE 2", "LFO MODE"};
String key(int t, int p) { return "t" + String(t + 1) + "_p" + String(p); }
String machineKey(int t) { return "t" + String(t + 1) + "_machine"; }
String tonalKey(int t) {return "t"+String(t+1)+"_tonal";}
String masterKey(int p) {return "master_"+String(p);}
int maximum(int p) { return p == 25 ? 15 : p == 26 ? 23 : p == 27 || p == 28 ? 5 : p == 29 ? 2 : 127; }
int initial(int t, int p) { return p == 13 ? 127 : p == 10 || p == 11 || p == 18 ? 64 : p == 17 || p == 24 ? 100 : p == 25 ? t : 0; }
AudioProcessorValueTreeState::ParameterLayout layout() {
    AudioProcessorValueTreeState::ParameterLayout l;
    for (int t = 0; t < 16; ++t) {
        l.add(std::make_unique<AudioParameterInt>(ParameterID(machineKey(t), 1), "Track " + String(t+1) + " Machine", 0, 191, t<14?16+t:32+t-14));
        for (int p = 0; p < 30; ++p)
            l.add(std::make_unique<AudioParameterInt>(ParameterID(key(t,p), 1), "Track " + String(t+1) + " " + names[p], 0, maximum(p), initial(t,p)));
    }
    l.add(std::make_unique<AudioParameterFloat>(ParameterID("tempo",1), "Tempo", NormalisableRange<float>(30,300,0.1f),120.0f));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID("gain",1), "Output", NormalisableRange<float>(0,1,0.001f),0.7f));
    l.add(std::make_unique<AudioParameterInt>(ParameterID("voices",1),"Voice budget",1,32,6));
    for(int p=0;p<32;++p)l.add(std::make_unique<AudioParameterInt>(ParameterID(masterKey(p),1),
        String(p<8?"Reverb ":p<16?"Delay ":p<24?"Master EQ ":"Dynamix ")+md::engine::MasterFx::names[p],0,127,md::engine::MasterFx::defaults[p]));
    // Append parameters to preserve existing host parameter indices.
    for(int t=0;t<16;++t)l.add(std::make_unique<AudioParameterBool>(ParameterID(tonalKey(t),1),"Track "+String(t+1)+" Tonal",false));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID("midi_mode",1),"MIDI mode",StringArray{"Drum","Chromatic"},0));
    return l;
}
}

class Processor final : public AudioProcessor {
public:
    static BusesProperties buses() {
        auto b=BusesProperties().withOutput("Main",AudioChannelSet::stereo(),true)
            .withOutput("Reverb send",AudioChannelSet::stereo(),false).withOutput("Delay send",AudioChannelSet::stereo(),false);
        for(int t=0;t<16;++t)b=b.withOutput("Track "+String(t+1),AudioChannelSet::mono(),false);
        return b;
    }
    AudioProcessorValueTreeState state{*this,nullptr,"MachinedrumPC",layout()};
    std::array<std::atomic<int>,16> pads{};
    std::atomic<bool> samplesLoaded{false};
    std::atomic<bool> extendedFirmware{false};
    Processor() : AudioProcessor(buses()) {
        for (int t=0;t<16;++t) {
            machineValues[t]=state.getRawParameterValue(machineKey(t));
            tonalValues[t]=state.getRawParameterValue(tonalKey(t));
            for(int p=0;p<30;++p) values[t][p]=state.getRawParameterValue(key(t,p));
        }
        for(int p=0;p<32;++p)masterValues[p]=state.getRawParameterValue(masterKey(p));
        resetCache();
        loader=std::thread([this]{loadLoop();});
        const auto saved = settingsFile().loadFileAsString().trim();
        if (File(saved).existsAsFile()) requestFirmware(saved);
    }
    ~Processor() override {
        {std::lock_guard<std::mutex> lock(requestMutex); quitting=true;}
        requestCv.notify_one();
        loader.join();
    }
    const String getName() const override {return "MachineModule";}
    bool acceptsMidi() const override {return true;}
    bool producesMidi() const override {return false;}
    double getTailLengthSeconds() const override {return 30;}
    int getNumPrograms() override {return 1;}
    int getCurrentProgram() override {return 0;}
    void setCurrentProgram(int) override {}
    const String getProgramName(int) override {return "Kit";}
    void changeProgramName(int,const String&) override {}
    bool hasEditor() const override {return true;}
    AudioProcessorEditor* createEditor() override;
    bool isBusesLayoutSupported(const BusesLayout& l) const override {
        if (!l.inputBuses.isEmpty() || l.getMainOutputChannelSet()!=AudioChannelSet::stereo()) return false;
        for (int i=0;i<l.outputBuses.size();++i) if (!l.outputBuses[i].isDisabled() && l.outputBuses[i]!=(i<3?AudioChannelSet::stereo():AudioChannelSet::mono())) return false;
        return true;
    }
    void prepareToPlay(double sr,int) override {
        std::lock_guard<std::mutex> lock(engineMutex);
        rate=sr; phase=0; cursor=32; previous.fill(0); next.fill(0);
        if(effects)effects->reset();
        setLatencySamples((int)std::ceil(51.0*sr/44100.0));
    }
    void releaseResources() override {}
    void setValue(const String& id,float value) {
        auto* p=state.getParameter(id); p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(value)); p->endChangeGesture();
    }
    void selectMachine(int track,int id) {
        setValue(machineKey(track),(float)id);
        const auto list=machineList();
        for(const auto& m:list) if(m.id==id)
            for(int p=0;p<8;++p) setValue(key(track,p),(float)m.defaults[p]);
    }
    void requestFirmware(const String& path) {
        {std::lock_guard<std::mutex> lock(requestMutex);
            if(requested==path&&(pending||loading))return;
            requested=path; pending=true; status="Loading firmware...";}
        requestCv.notify_one();
    }
    void applyKit(const pc::Kit& kit) {
        preserveParameters=true;
        for(int t=0;t<16;++t) {
            setValue(machineKey(t),(float)kit.machine[t]);
            setValue(tonalKey(t),kit.tonal[t]?1.0f:0.0f);
            for(int p=0;p<24;++p)setValue(key(t,p),(float)kit.params[t][p]);
            setValue(key(t,24),(float)kit.level[t]);
            for(int p=0;p<5;++p)setValue(key(t,25+p),(float)std::clamp(kit.lfo[t][p],0,maximum(25+p)));
        }
    }
    String getStatus() { std::lock_guard<std::mutex> lock(requestMutex); return status; }
    std::vector<md::engine::MachineInfo> machineList() {std::lock_guard<std::mutex> lock(requestMutex);return machines;}
    void getStateInformation(MemoryBlock& dest) override {
        auto tree=state.copyState();
        {std::lock_guard<std::mutex> lock(requestMutex);tree.setProperty("firmware",firmwarePath,nullptr);}
        if(auto xml=tree.createXml()) copyXmlToBinary(*xml,dest);
    }
    void setStateInformation(const void* data,int size) override {
        if(auto xml=getXmlFromBinary(data,size)) if(xml->hasTagName("MachinedrumPC")) {
            auto tree=ValueTree::fromXml(*xml);
            if(!tree.getChildWithProperty("id","midi_mode").isValid()) {
                ValueTree mode("PARAM");mode.setProperty("id","midi_mode",nullptr);mode.setProperty("value",0.0f,nullptr);tree.addChild(mode,-1,nullptr);
            }
            preserveParameters=true; state.replaceState(tree);
            const auto path=tree.getProperty("firmware").toString();
            if(path.isNotEmpty()) requestFirmware(path);
        }
    }
    void processBlock(AudioBuffer<float>& audio,MidiBuffer& midi) override {
        ScopedNoDenormals noDenormals;
        audio.clear();
        std::unique_lock<std::mutex> lock(engineMutex,std::try_to_lock);
        if(!lock.owns_lock() || !engine || faulted) {midi.clear();return;}
        auto& host=engine->host();
        const bool chromatic=state.getRawParameterValue("midi_mode")->load()>0.5f;
        if(lastMidiMode!=(int)chromatic) {
            host.allSoundOff();midiNotes.fill(-1);for(auto& v:lastValues)v[0]=-1;
            lastMidiMode=(int)chromatic;
        }
        host.setMaxActiveVoices((int)state.getRawParameterValue("voices")->load());
        engine->dryMute=0;
        for(int t=0;t<16;++t)if(getBus(false,t+3)->isEnabled())engine->dryMute|=1u<<t;
        double bpm=state.getRawParameterValue("tempo")->load();
        if(auto* ph=getPlayHead()) if(auto pos=ph->getPosition()) if(auto tempo=pos->getBpm()) bpm=*tempo;
        host.setTempo(bpm);
        const bool externalReverb=getBus(false,1)->isEnabled(),externalDelay=getBus(false,2)->isEnabled();
        std::array<uint8_t,32> masterParams{};
        for(int p=0;p<32;++p)masterParams[p]=(uint8_t)masterValues[p]->load();
        if(externalReverb)masterParams[7]=0;
        if(externalDelay)masterParams[15]=0;
        if(!effects->configure(masterParams,bpm)){faulted=true;midi.clear();return;}
        for(int t=0;t<16;++t) {
            int m=(int)machineValues[t]->load();
            const bool pitched=chromatic&&extendedFirmware.load()&&pc::supportsTonalPitch(m);
            host.setTonal(t,pitched||tonalValues[t]->load()>0.5f);
            if((pc::isRom(m)&&!samplesLoaded.load())||(m>=80&&!pc::isRom(m)))m=0;
            if(m!=lastMachine[t]) {host.setMachine(t,(uint8_t)m);lastMachine[t]=m;lastValues[t].fill(-1);midiNotes[t]=-1;}
            bool lfo=false;
            for(int p=0;p<30;++p) {
                int v=(int)values[t][p]->load();
                if(p==0&&pitched&&midiNotes[t]>=0)v=pc::chromaticPitch(v,midiNotes[t]);
                if(v==lastValues[t][p]) continue;
                lastValues[t][p]=v;
                if(p<24) host.setParam(t,p,v);
                else if(p==24) host.setLevel(t,v);
                else lfo=true;
            }
            if(lfo) host.setLfo(t,lastValues[t][25],lastValues[t][26],lastValues[t][27],lastValues[t][28],lastValues[t][29]);
            if(int v=pads[t].exchange(0)) host.trigger(t,v);
        }
        auto it=midi.cbegin();
        const float gain=state.getRawParameterValue("gain")->load();
        // Bus layouts are fixed for this callback. Resolve their buffers once,
        // rather than constructing 19 temporary bus views for every sample.
        std::array<float*,22> outputPointers{};
        std::array<int,22> outputIndices{};
        int outputCount=0;
        for(int bus=0;bus<getBusCount(false);++bus) {
            auto b=getBusBuffer(audio,false,bus);
            for(int c=0;c<b.getNumChannels();++c) {
                outputPointers[outputCount]=b.getWritePointer(c);
                outputIndices[outputCount++]=bus<3?2*bus+c:bus+3;
            }
        }
        for(int s=0;s<audio.getNumSamples();++s) {
            while(it!=midi.cend() && (*it).samplePosition<=s) {
                const auto msg=(*it).getMessage();
                if(msg.isNoteOn()) {
                    int note=msg.getNoteNumber();
                    int t=chromatic?msg.getChannel()-1:note>=36&&note<52?note-36:note<16?note:-1;
                    if(t>=0) {
                        if(chromatic&&extendedFirmware.load()&&pc::supportsTonalPitch(lastMachine[t])) {
                            midiNotes[t]=note;
                            const int pitch=pc::chromaticPitch((int)values[t][0]->load(),note);
                            host.setParamImmediately(t,0,pitch);lastValues[t][0]=pitch;
                        }
                        host.trigger(t,msg.getVelocity());
                    }
                }
                if(msg.isAllSoundOff() || msg.isAllNotesOff()) {
                    if(chromatic) {
                        const int t=msg.getChannel()-1;host.stopTrack(t);midiNotes[t]=-1;lastValues[t][0]=-1;
                    } else {
                    host.allSoundOff();
                    effects->reset();
                    if(!effects->configure(masterParams,bpm)){faulted=true;midi.clear();return;}
                    cursor=32; previous.fill(0);next.fill(0);
                    }
                }
                ++it;
            }
            phase+=44100.0/rate;
            while(phase>=1.0) {
                previous=next;
                if(cursor>=32) {
                    if(!engine->render(output)) {faulted=true; midi.clear(); return;}
                    if(!effects->process(output.mix,externalReverb,externalDelay)){faulted=true;midi.clear();return;}
                    cursor=0;
                }
                for(int c=0;c<2;++c) {
                    next[c]=(float)output.mix.main[cursor][c]/8388608.0f;
                    next[2+c]=(float)output.mix.rev[cursor][c]/8388608.0f;
                    next[4+c]=(float)output.mix.del[cursor][c]/8388608.0f;
                }
                for(int t=0;t<16;++t)next[6+t]=(float)md::engine::Mixer::solo(output.tracks[t][cursor],host.mixerInput(t).mix[1])/8388608.0f;
                ++cursor;phase-=1.0;
            }
            for(int c=0;c<outputCount;++c) {
                const int index=outputIndices[c];
                outputPointers[c][s]=jlimit(-1.0f,1.0f,gain*(previous[index]+(float)phase*(next[index]-previous[index])));
            }
        }
        midi.clear();
    }
private:
    static File settingsFile() {return File::getSpecialLocation(File::userApplicationDataDirectory).getChildFile("MachinedrumModulePC/firmware-path.txt");}
    void resetCache() {lastMachine.fill(-1);for(auto& v:lastValues)v.fill(-1);midiNotes.fill(-1);lastMidiMode=-1;}
    void loadLoop() {
        for(;;) {
            String path;
            {std::unique_lock<std::mutex> lock(requestMutex);requestCv.wait(lock,[this]{return quitting||pending;});
                if(quitting)return;path=requested;pending=false;loading=true;}
            try {
                auto fw=pc::loadFirmware(std::filesystem::path(path.toWideCharPointer()));
                auto freshEffects=std::make_unique<md::engine::MasterFx>(fw.firmware,fw.os);
                auto fresh=std::make_unique<md::engine::ParallelEngine>(fw.firmware,fw.os,fw.firmware.x13?1:3);
                fresh->voices().warmUp(fw.os);
                std::fprintf(stderr,"MachineModule DSP: voices=%s master=%s groups=%d\n",
                    fresh->voices().usingJit()?"JIT":"interpreter",
                    freshEffects->usingJit()?"JIT":"interpreter",fresh->voices().groupCount());
                const bool rom=pc::loadSamples(fresh->voices(),std::filesystem::path(path.toWideCharPointer()).parent_path()/"ROM_SAMPLES.bin");
                auto list=fresh->os().machines();
                if(!preserveParameters.exchange(true)) {
                    for(int t=0;t<16;++t) for(const auto& m:list)
                        if(m.id==(int)machineValues[t]->load())
                            for(int p=0;p<8;++p) setValue(key(t,p),(float)m.defaults[p]);
                }
                // NFX must keep listening when a neighbouring source starts later.
                fresh->host().setSilenceRelease(fw.firmware.x13?-1:128,138);
                {std::lock_guard<std::mutex> lock(engineMutex);engine.swap(fresh);effects.swap(freshEffects);samplesLoaded=rom;resetCache();cursor=32;phase=0;previous.fill(0);next.fill(0);faulted=false;}
                {std::lock_guard<std::mutex> lock(requestMutex);machines=std::move(list);firmwarePath=path;extendedFirmware=fw.firmware.x13;loading=false;status=fw.firmware.x13?"OS X.13 ready":"OS 1.63 ready";}
                auto file=settingsFile(); file.getParentDirectory().createDirectory();file.replaceWithText(path);
            } catch(const std::exception& e) {std::lock_guard<std::mutex> lock(requestMutex);loading=false;status="Load failed: "+String(e.what());}
        }
    }
    std::mutex engineMutex,requestMutex;
    std::condition_variable requestCv;
    std::thread loader;
    bool quitting=false,pending=false,loading=false,faulted=false;
    std::atomic<bool> preserveParameters{false};
    String requested,firmwarePath,status="Select OS 1.63 or X.13 firmware (.syx / flash .bin)";
    std::vector<md::engine::MachineInfo> machines;
    std::unique_ptr<md::engine::ParallelEngine> engine;
    md::engine::ParallelEngine::Output output{};
    std::array<std::atomic<float>*,16> machineValues{};
    std::array<std::atomic<float>*,16> tonalValues{};
    std::array<std::atomic<float>*,32> masterValues{};
    std::array<std::array<std::atomic<float>*,30>,16> values{};
    std::array<int,16> lastMachine{};
    std::array<int,16> midiNotes{};
    int lastMidiMode=-1;
    std::array<std::array<int,30>,16> lastValues{};
    double rate=44100,phase=0;
    int cursor=32;
    std::array<float,22> previous{},next{};
    std::unique_ptr<md::engine::MasterFx> effects;
};

class Editor final : public AudioProcessorEditor,private Timer {
public:
    explicit Editor(Processor& p):AudioProcessorEditor(p),proc(p) {
        setLookAndFeel(&style);
        setSize(1120,860);
        addAndMakeVisible(load);load.setButtonText("FIRMWARE");load.onClick=[this]{
            chooser=std::make_unique<FileChooser>("Machinedrum OS 1.63 / X.13",File{},"*.syx;*.bin");
            chooser->launchAsync(FileBrowserComponent::openMode|FileBrowserComponent::canSelectFiles,
                [safe=Component::SafePointer<Editor>(this)](const FileChooser& f){if(safe && f.getResult().existsAsFile())safe->proc.requestFirmware(f.getResult().getFullPathName());});
        };
        addAndMakeVisible(status);addAndMakeVisible(machine);
        addAndMakeVisible(importKit);importKit.setButtonText("IMPORT KIT");
        importKit.onClick=[this]{
            chooser=std::make_unique<FileChooser>("Machinedrum kit SysEx",File{},"*.syx");
            chooser->launchAsync(FileBrowserComponent::openMode|FileBrowserComponent::canSelectFiles,
                [safe=Component::SafePointer<Editor>(this)](const FileChooser& f){
                    if(!safe||!f.getResult().existsAsFile())return;
                    try {
                        safe->kits=pc::readKits(std::filesystem::path(f.getResult().getFullPathName().toWideCharPointer()));
                        pc::populateKitMenu(safe->kitPicker,safe->kits);
                        safe->kitPicker.setSelectedId(1);
                    }catch(const std::exception& e){AlertWindow::showMessageBoxAsync(MessageBoxIconType::WarningIcon,"Kit import",e.what());}
                });
        };
        addAndMakeVisible(kitPicker);kitPicker.setTextWhenNothingSelected("Imported kits");
        kitPicker.onChange=[this]{int i=kitPicker.getSelectedId()-1;if(i>=0&&i<(int)kits.size())proc.applyKit(kits[(size_t)i]);};
        machine.onChange=[this]{if(machine.getSelectedId()>0)proc.selectMachine(track,machine.getSelectedId()-1);};
        addAndMakeVisible(masterPage);masterPage.setButtonText("MASTER FX");
        addAndMakeVisible(tonal);tonal.setButtonText("TONAL");tonal.getProperties().set("lcd",true);
        tonal.setTooltip("X.13 tonal tuning for the selected track");
        tonal.onClick=[this]{proc.setValue(tonalKey(track),proc.state.getRawParameterValue(tonalKey(track))->load()>0.5f?0.0f:1.0f);};
        masterPage.getProperties().set("lcd",true);
        masterPage.onClick=[this]{showMaster=!showMaster;masterPage.setButtonText(showMaster?"TRACK EDIT":"MASTER FX");bind();resized();};
        addAndMakeVisible(midiMode);midiMode.addItem("DRUM",1);midiMode.addItem("CHROMATIC",2);
        midiMode.setTooltip("Drum: notes 36-51 trigger tracks. Chromatic: channels 1-16 select tracks; X.13 tonal machines transpose around MIDI note 60. Note-offs preserve drum tails.");
        midiModeAttach=std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment>(proc.state,"midi_mode",midiMode);
        for(int t=0;t<16;++t) {
            addAndMakeVisible(pads[t]);pads[t].setButtonText(String(t+1).paddedLeft('0',2));
            pads[t].getProperties().set("lcd",true);
            pads[t].setTooltip("Select and play track "+String(t+1)+" / MIDI note "+String(36+t));
            pads[t].onClick=[this,t]{track=t;proc.pads[t]=110;bind();};
        }
        for(int i=0;i<32;++i) {
            addAndMakeVisible(sliders[i]);addAndMakeVisible(labels[i]);
            sliders[i].setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
            sliders[i].setTextBoxStyle(Slider::TextBoxBelow,false,104,24);
            sliders[i].setMouseDragSensitivity(160);
            const int p=parameterMap[i];
            sliders[i].getProperties().set("lcdKind",p==25||p==26||p==29?1:p==27||p==28?2:0);
            sliders[i].setTooltip(String(names[p])+" - drag to change; double-click the value to type.");
            if(p==19)sliders[i].setTooltip("Send to the original Rhythm Echo. Edit its settings on MASTER FX.");
            if(p==20)sliders[i].setTooltip("Send to the original Gate Box. Edit its settings on MASTER FX.");
            labels[i].setJustificationType(Justification::centred);
        }
        addAndMakeVisible(tempo);tempo.setSliderStyle(Slider::LinearHorizontal);tempo.setTextBoxStyle(Slider::TextBoxRight,false,60,20);
        tempo.setTooltip("Standalone tempo. A DAW's tempo takes precedence.");
        tempoAttach=std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(proc.state,"tempo",tempo);
        addAndMakeVisible(gain);gain.setSliderStyle(Slider::LinearHorizontal);gain.setTextBoxStyle(Slider::TextBoxRight,false,60,20);
        gainAttach=std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(proc.state,"gain",gain);
        addAndMakeVisible(voices);voices.setSliderStyle(Slider::LinearHorizontal);voices.setTextBoxStyle(Slider::TextBoxRight,false,45,20);
        voices.setTooltip("CPU budget: synth voices cost 1, ROM voices cost 2. Oldest voices are cut when the budget is exceeded.");
        voicesAttach=std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(proc.state,"voices",voices);
        addAndMakeVisible(level);level.setSliderStyle(Slider::LinearHorizontal);level.setTextBoxStyle(Slider::TextBoxRight,false,48,24);
        level.setTooltip("Selected track's kit level (LEV)");
        bind();startTimerHz(5);
    }
    ~Editor() override {stopTimer();setLookAndFeel(nullptr);}
    void paint(Graphics& g) override {
        g.setGradientFill(ColourGradient(Colour(0xffd5d5d2),0,0,Colour(0xff939590),0,(float)getHeight(),false));g.fillAll();
        for(int y=0;y<getHeight();++y) {
            const auto shade=uint8_t(80+(y*37)%80);
            g.setColour(Colour(shade,shade,shade).withAlpha(y%3==0?0.12f:0.04f));g.fillRect(0,y,getWidth(),1);
        }
        for(auto point:{Point<int>(15,15),Point<int>(1105,15),Point<int>(15,844),Point<int>(1105,844)}) {
            g.setColour(Colour(0xff656762));g.fillEllipse((float)point.x-5,(float)point.y-5,10,10);
            g.setColour(Colour(0xff353630));g.drawLine((float)point.x-3,(float)point.y+2,(float)point.x+3,(float)point.y-2,1.5f);
        }
        g.setColour(Colour(0xff555653));g.fillRoundedRectangle(27,21,1066,712,15);
        g.setColour(Colour(0xff0c0c0c));g.fillRoundedRectangle(29,23,1062,708,13);
        const Rectangle<int> screen(40,34,1040,686);
        g.setColour(lcd::red());g.fillRect(screen);
        for(int n=0;n<12;++n){g.setColour(lcd::ink().withAlpha(0.035f+(11-n)*0.008f));g.drawRect(screen.reduced(n),1);}
        g.setColour(lcd::ink());
        lcd::text(g,"MachineModule",{443,60,613,40},4);
        lcd::text(g,"SPS-1 UW / PC",{760,109,296,20},2);
        lcd::text(g,"TRACK "+String(track+1).paddedLeft('0',2),{310,77,160,22},2);
        panel(g,64,152,showMaster?"RHYTHM ECHO":"SYN");panel(g,600,152,showMaster?"GATE BOX":"AMP/EFX");
        panel(g,64,396,showMaster?"MASTER EQ":"ROUTE");panel(g,600,396,showMaster?"DYNAMIX":"LFO");
        g.setColour(lcd::ink());lcd::text(g,"TRACK SELECT / TRIG",{64,638,500,16},1,true);
        lcd::text(g,"MIDI MODE",{736,638,115,16},1,true);
        g.setColour(Colour(0xff292b28));
        lcd::text(g,"TRACK LEVEL",{650,749,160,28},2,true);
        lcd::text(g,"TEMPO",{64,792,80,26},2,true);
        lcd::text(g,"VOICES",{396,792,90,26},2,true);
        lcd::text(g,"OUTPUT",{736,792,90,26},2,true);
        g.setColour(lcd::ink());
        lcd::text(g,"By AcidReff",{getWidth()-154,828,130,20},2);
    }
    void resized() override {
        load.setBounds(64,746,150,34);importKit.setBounds(226,746,156,34);kitPicker.setBounds(398,746,224,34);
        status.setBounds(64,828,780,20);
        for(int t=0;t<16;++t)pads[t].setBounds(64+t*62,663,55,34);
        machine.setBounds(64,62,234,58);
        masterPage.setBounds(443,112,190,29);
        tonal.setBounds(646,112,106,29);
        midiMode.setBounds(861,634,195,24);
        for(int i=0;i<32;++i) {
            const int group=i/8,cell=i%8;
            int x=(group%2==0?64:600)+(cell%4)*114,y=(group<2?152:396)+28+(cell/4)*100;
            // SYN and AMP/EFX occupy the top row, ROUTE and LFO the bottom row.
            labels[i].setBounds(x+3,y+5,108,23);
            bool textOnly=(int)sliders[i].getProperties()["lcdKind"]==1;
            sliders[i].setBounds(x+3,y+(textOnly?30:28),108,textOnly?45:68);
        }
        level.setBounds(811,750,245,25);
        tempo.setBounds(145,792,210,26);voices.setBounds(487,792,210,26);gain.setBounds(825,792,231,26);
    }
private:
    static constexpr std::array<int,32> parameterMap{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,25,26,27,28,29,21,22,23};
    void panel(Graphics& g,int x,int y,const String& title) {
        g.setColour(lcd::ink());g.fillRect(x,y,456,26);
        g.setColour(lcd::red());lcd::text(g,title,{x+6,y,444,26},3,true);
        g.setColour(lcd::ink());
        for(int col=0;col<=4;++col)lcd::dotted(g,x+col*114,y+28,x+col*114,y+228);
        for(int row=0;row<=2;++row)lcd::dotted(g,x,y+28+row*100,x+456,y+28+row*100);
    }
    void bind() {
        for(auto& a:attachments)a.reset();
        for(int i=0;i<32;++i) {
            const int p=parameterMap[i];
            sliders[i].textFromValueFunction=nullptr;sliders[i].valueFromTextFunction=nullptr;
            sliders[i].getProperties().set("lcdKind",showMaster?0:p==25||p==26||p==29?1:p==27||p==28?2:0);
            if(showMaster) {
                int m=i<8?i+8:i<16?i-8:i;
                attachments[i]=std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(proc.state,masterKey(m),sliders[i]);
                sliders[i].setTooltip(String(md::engine::MasterFx::names[m])+" - original firmware master effect");
                if(m==11)sliders[i].setTooltip("Original delay feedback. Values above 63 can build into self-oscillation.");
                if(m==31)sliders[i].setTooltip("Dynamix blend: higher values favour the uncompressed signal.");
                sliders[i].updateText();continue;
            }
            sliders[i].setTooltip(String(names[p])+" - drag to change");
            attachments[i]=std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(proc.state,key(track,p),sliders[i]);
            if(p==18 || p==11) {
                sliders[i].textFromValueFunction=[](double v){int n=(int)v-64;return (n>0?String("+"):String())+String(n);};
                sliders[i].valueFromTextFunction=[](const String& s){return (double)s.getIntValue()+64;};
            } else if(p==25) {
                sliders[i].textFromValueFunction=[this](double v){
                    auto result=String((int)v+1).paddedLeft('0',2);
                    int id=(int)proc.state.getRawParameterValue(machineKey((int)v))->load();
                    for(const auto& m:proc.machineList())if(m.id==id)result+="-"+String(m.name).getLastCharacters(2);
                    return result;
                };
                sliders[i].valueFromTextFunction=[](const String& s){return (double)s.getIntValue()-1;};
            } else if(p==26) {
                sliders[i].textFromValueFunction=[this](double v){return destinationName((int)v);};
                sliders[i].valueFromTextFunction=[this](const String& s){for(int n=0;n<24;++n)if(s.equalsIgnoreCase(destinationName(n)))return (double)n;return (double)s.getIntValue();};
            } else if(p==27||p==28) {
                sliders[i].textFromValueFunction=[](double v){return String(shapeNames[std::clamp((int)v,0,5)]);};
                sliders[i].valueFromTextFunction=[](const String& s){for(int n=0;n<6;++n)if(s.equalsIgnoreCase(shapeNames[n]))return (double)n;return (double)s.getIntValue();};
            } else if(p==29) {
                sliders[i].textFromValueFunction=[](double v){return String((int)v==0?"FREE":(int)v==1?"TRIG":"HOLD");};
                sliders[i].valueFromTextFunction=[](const String& s){return s.equalsIgnoreCase("TRIG")?1.0:s.equalsIgnoreCase("HOLD")?2.0:0.0;};
            }
            sliders[i].updateText();
        }
        levelAttach.reset();levelAttach=std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(proc.state,key(track,24),level);
        for(int t=0;t<16;++t)pads[t].setToggleState(t==track,dontSendNotification);
        timerCallback();repaint();
    }
    static constexpr const char* shapeNames[]={"TRI","SAW","SQR","RAMP","EXP","RND"};
    String destinationName(int p) {
        p=std::clamp(p,0,23);
        int target=(int)proc.state.getRawParameterValue(key(track,25))->load();
        int id=(int)proc.state.getRawParameterValue(machineKey(target))->load();
        if(p<8)for(const auto& m:proc.machineList())if(m.id==id)return String(m.params[p]);
        return p==21?"LFOS":p==22?"LFOD":p==23?"LFOM":String(names[p]);
    }
    void timerCallback() override {
        status.setText(proc.getStatus(),dontSendNotification);
        auto list=proc.machineList();
        const bool x13=proc.extendedFirmware.load();
        const bool chromatic=proc.state.getRawParameterValue("midi_mode")->load()>0.5f;
        const bool autoTonal=x13&&chromatic&&pc::supportsTonalPitch((int)proc.state.getRawParameterValue(machineKey(track))->load());
        tonal.setVisible(x13);tonal.setEnabled(!autoTonal);
        tonal.setToggleState(autoTonal||proc.state.getRawParameterValue(tonalKey(track))->load()>0.5f,dontSendNotification);
        tonal.setTooltip(autoTonal?"Tonal tuning is automatic in Chromatic MIDI mode":"X.13 tonal tuning for the selected track");
        for(int t=0;t<16;++t)pads[t].setTooltip("Select and play track "+String(t+1)+(chromatic?" / MIDI channel "+String(t+1):" / MIDI note "+String(36+t)));
        if((machine.getNumItems()==0 || hadSamples!=proc.samplesLoaded.load() || hadX13!=x13) && !list.empty()) {
            hadX13=x13;
            hadSamples=proc.samplesLoaded.load();machine.clear(dontSendNotification);
            for(const auto& m:list) if(m.id<80||(hadSamples&&pc::isRom(m.id))) {
                auto n=String(m.name);if(n.length()>3)n=n.substring(0,3)+" "+n.substring(3);
                machine.addItem(n,m.id+1);
            }
        }
        int id=(int)proc.state.getRawParameterValue(machineKey(track))->load();machine.setSelectedId(id+1,dontSendNotification);
        if(showMaster){for(int i=0;i<32;++i)labels[i].setText(md::engine::MasterFx::names[i<8?i+8:i<16?i-8:i],dontSendNotification);return;}
        static const char* captions[]={"PTCH","DEC","RAMP","HOLD","TICK","NOIS","DIRT","DIST","AMD","AMF","EQF","EQG","FLTF","FLTW","FLTQ","SRR","DIST","VOL","PAN","DEL","REV","LFOS","LFOD","LFOM","TRACK","PARAM","SHP1","SHP2","UPDTE","SPEED","DEPTH","SHMIX"};
        for(int i=0;i<32;++i)labels[i].setText(captions[i],dontSendNotification);
        for(const auto& m:list)if(m.id==id)for(int p=0;p<8;++p)labels[p].setText(String(m.params[p]),dontSendNotification);
        sliders[24].updateText();sliders[25].updateText();
    }
    lcd::Style style;
    Processor& proc;
    int track=0;
    bool hadSamples=false,hadX13=false,showMaster=false;
    TextButton load,importKit,masterPage,tonal;
    Label status;
    ComboBox machine,kitPicker,midiMode;
    std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment> midiModeAttach;
    std::vector<pc::Kit> kits;
    std::array<TextButton,16> pads;
    std::array<Slider,32> sliders;
    std::array<Label,32> labels;
    Slider tempo,gain,voices,level;
    std::array<std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment>,32> attachments;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> tempoAttach,gainAttach,voicesAttach,levelAttach;
    std::unique_ptr<FileChooser> chooser;
};
AudioProcessorEditor* Processor::createEditor(){return new Editor(*this);}
AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new Processor;}
