#pragma once
#include <algorithm>
namespace pc {
inline bool supportsTonalPitch(int machine) {
    return machine==1||machine==4||machine==5
        ||machine==16||machine==17||machine==18||machine==20||machine==21
        ||machine==26||machine==27||machine==28||machine==29
        ||(machine>=32&&machine<=39);
}
inline int chromaticPitch(int basePitch,int note) {
    // X.13 PTCH uses quarter-tones. MIDI 60 is the untransposed kit pitch.
    return std::clamp(basePitch+2*(note-60),0,127);
}
}
