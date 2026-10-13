#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Kits.h"

namespace pc {
inline void populateKitMenu(juce::ComboBox& picker,const std::vector<Kit>& kits) {
    picker.clear(juce::dontSendNotification);
    auto* root=picker.getRootMenu();
    root->addSectionHeader(juce::String((int)kits.size())+" imported kit slots");
    constexpr size_t pageSize=16;
    for(size_t start=0;start<kits.size();start+=pageSize) {
        juce::PopupMenu page;
        const auto end=std::min(start+pageSize,kits.size());
        for(size_t i=start;i<end;++i) {
            const auto name=juce::String(kits[i].name).trim();
            const auto label=juce::String(kits[i].slot+1).paddedLeft('0',2)+" - "
                +(name.isEmpty()?juce::String("UNNAMED"):name);
            page.addItem((int)i+1,label);
        }
        if(kits.size()<=pageSize)*root=std::move(page);
        else root->addSubMenu("Entries "+juce::String((int)start+1)+" - "+juce::String((int)end),page);
    }
    picker.setTooltip(juce::String((int)kits.size())+" imported kit slots. Numbers are original kit slots; UNNAMED means no readable name was stored.");
}
}
