#pragma once
#include <juce_audio_utils/juce_audio_utils.h>

// Original 5x7 glyphs drawn as rectangles: no external font or firmware artwork.
namespace lcd {
inline juce::Colour red(){return juce::Colour(0xffff4838);}
inline juce::Colour ink(){return juce::Colour(0xff580d0b);}
inline std::array<unsigned char,7> glyph(juce::juce_wchar c) {
    switch(c) {
    case 'A':return {14,17,17,31,17,17,17};case 'B':return {30,17,17,30,17,17,30};
    case 'C':return {15,16,16,16,16,16,15};case 'D':return {30,17,17,17,17,17,30};
    case 'E':return {31,16,16,30,16,16,31};case 'F':return {31,16,16,30,16,16,16};
    case 'G':return {15,16,16,23,17,17,15};case 'H':return {17,17,17,31,17,17,17};
    case 'I':return {31,4,4,4,4,4,31};case 'J':return {7,2,2,2,18,18,12};
    case 'K':return {17,18,20,24,20,18,17};case 'L':return {16,16,16,16,16,16,31};
    case 'M':return {17,27,21,21,17,17,17};case 'N':return {17,25,21,19,17,17,17};
    case 'O':return {14,17,17,17,17,17,14};case 'P':return {30,17,17,30,16,16,16};
    case 'Q':return {14,17,17,17,21,18,13};case 'R':return {30,17,17,30,20,18,17};
    case 'S':return {15,16,16,14,1,1,30};case 'T':return {31,4,4,4,4,4,4};
    case 'U':return {17,17,17,17,17,17,14};case 'V':return {17,17,17,17,17,10,4};
    case 'W':return {17,17,17,21,21,21,10};case 'X':return {17,17,10,4,10,17,17};
    case 'Y':return {17,17,10,4,4,4,4};case 'Z':return {31,1,2,4,8,16,31};
    case '0':return {14,17,19,21,25,17,14};case '1':return {4,12,4,4,4,4,14};
    case '2':return {14,17,1,2,4,8,31};case '3':return {30,1,1,14,1,1,30};
    case '4':return {2,6,10,18,31,2,2};case '5':return {31,16,16,30,1,1,30};
    case '6':return {14,16,16,30,17,17,14};case '7':return {31,1,2,4,8,8,8};
    case '8':return {14,17,17,14,17,17,14};case '9':return {14,17,17,15,1,1,14};
    case '-':return {0,0,0,31,0,0,0};case '+':return {0,4,4,31,4,4,0};
    case '/':return {1,1,2,4,8,16,16};case '.':return {0,0,0,0,0,12,12};
    case ':':return {0,12,12,0,12,12,0};case '>':return {16,8,4,2,4,8,16};
    case '%':return {25,25,2,4,8,19,19};case '_':return {0,0,0,0,0,0,31};
    default:return {};
    }
}
inline void text(juce::Graphics& g,juce::String str,juce::Rectangle<int> bounds,int scale=2,bool left=false) {
    str=str.toUpperCase();
    if(str.isEmpty())return;
    scale=juce::jmax(1,juce::jmin(scale,bounds.getWidth()/juce::jmax(1,str.length()*6-1)));
    int width=(str.length()*6-1)*scale;
    int x=left?bounds.getX():bounds.getCentreX()-width/2,y=bounds.getCentreY()-7*scale/2;
    juce::Graphics::ScopedSaveState save(g);g.reduceClipRegion(bounds);
    for(auto c:str) {auto rows=glyph(c);for(int r=0;r<7;++r)for(int col=0;col<5;++col)
        if(rows[r]&(1<<(4-col)))g.fillRect(x+col*scale,y+r*scale,scale+(scale>1?1:0),scale+(scale>1?1:0));
        x+=6*scale;
    }
}
inline void dotted(juce::Graphics& g,int x1,int y1,int x2,int y2) {
    if(y1==y2)for(int x=x1;x<=x2;x+=6)g.fillRect(x,y1,2,2);
    else for(int y=y1;y<=y2;y+=6)g.fillRect(x1,y,2,2);
}
class Style final:public juce::LookAndFeel_V4 {
public:
    Style() {
        setColour(juce::PopupMenu::backgroundColourId,ink());setColour(juce::PopupMenu::textColourId,red());
        setColour(juce::PopupMenu::highlightedBackgroundColourId,red());setColour(juce::PopupMenu::highlightedTextColourId,ink());
        setColour(juce::TextEditor::backgroundColourId,red());setColour(juce::TextEditor::textColourId,ink());
        setColour(juce::TextEditor::outlineColourId,ink());setColour(juce::TextEditor::highlightColourId,ink().withAlpha(0.3f));
        setColour(juce::Slider::textBoxTextColourId,ink());setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);
        setColour(juce::Label::textColourId,ink());
    }
    void drawLabel(juce::Graphics& g,juce::Label& l) override {
        if(l.isBeingEdited())return;
        g.setColour(l.findColour(juce::Label::textColourId));
        const bool combo=dynamic_cast<juce::ComboBox*>(l.getParentComponent())!=nullptr;
        text(g,l.getText(),l.getLocalBounds().reduced(3,0),combo?3:2,combo);
    }
    void drawComboBox(juce::Graphics& g,int w,int h,bool,int,int,int,int,juce::ComboBox&) override {
        g.setColour(ink());g.fillRect(0,0,w,h);
        g.setColour(red());int cx=w-19,cy=h/2;
        for(int i=0;i<4;++i)g.fillRect(cx-6+i*2,cy-3+i*2,14-i*4,2);
    }
    void positionComboBoxText(juce::ComboBox& box,juce::Label& l) override {
        l.setBounds(12,0,box.getWidth()-44,box.getHeight());l.setColour(juce::Label::textColourId,red());
    }
    void drawButtonBackground(juce::Graphics& g,juce::Button& b,const juce::Colour&,bool over,bool down) override {
        auto r=b.getLocalBounds().toFloat().reduced(1);
        bool selected=b.getToggleState();
        if((bool)b.getProperties()["lcd"]) {
            g.setColour(selected||down?ink():red().darker(over?0.12f:0.0f));g.fillRect(r);
            g.setColour(ink());g.drawRect(r,2);
        } else {
            g.setColour(juce::Colour(down?0xff202020:over?0xff484848:0xff323232));g.fillRoundedRectangle(r,3);
            g.setColour(juce::Colour(0xff777777));g.drawRoundedRectangle(r,3,1);
        }
    }
    void drawButtonText(juce::Graphics& g,juce::TextButton& b,bool,bool down) override {
        bool on=b.getToggleState()||down;
        g.setColour((bool)b.getProperties()["lcd"]?(on?red():ink()):juce::Colour(0xffece8df));
        text(g,b.getButtonText(),b.getLocalBounds().reduced(5),2);
    }
    void drawRotarySlider(juce::Graphics& g,int x,int y,int w,int h,float pos,float,float,juce::Slider& s) override {
        const auto r=juce::Rectangle<int>(x,y,w,h);
        if(s.isMouseOverOrDragging()){g.setColour(ink().withAlpha(0.07f));g.fillRect(r);}
        g.setColour(ink());
        int kind=(int)s.getProperties()["lcdKind"];
        if(kind==1)return; // A destination or mode: large text in the value field.
        if(kind==2) {
            int shape=(int)s.getValue();int ox=r.getCentreX()-32,oy=r.getCentreY()-17;
            int lastY=0;
            for(int i=0;i<32;++i) {
                double t=i/31.0, v=0;
                // Firmware shape order: triangle, saw, square, ramp, exponential, random.
                switch(shape){case 0:v=1-std::abs(2*t-1);break;case 1:v=std::fmod(t*2,1.0);break;
                    case 2:v=t>=0.2&&t<0.75?1:0;break;case 3:v=juce::jlimit(0.0,1.0,1.3-1.8*t);break;
                    case 4:v=std::exp(-5*t);break;default:{constexpr double steps[]={0.2,0.8,0.45,0.95,0.1,0.6,0.3,0.7};v=steps[i/4];break;}}
                const int sy=oy+32-(int)std::round(v*16)*2;
                g.fillRect(ox+i*2,i?std::min(lastY,sy):sy,2,i?std::abs(lastY-sy)+2:2);lastY=sy;
            }
            return;
        }
        constexpr int pixel=2;
        int cx=r.getCentreX(),cy=r.getCentreY();
        for(int py=-9;py<=9;++py)for(int px=-9;px<=9;++px) {
            double radius=std::sqrt(double(px*px+py*py));
            if(radius>=7.2&&radius<8.4)g.fillRect(cx+px*pixel,cy+py*pixel,pixel,pixel);
        }
        double angle=(225.0-270.0*pos)*juce::MathConstants<double>::pi/180.0;
        for(int radius=3;radius<=7;++radius)g.fillRect(cx+(int)std::round(std::cos(angle)*radius)*pixel,cy-(int)std::round(std::sin(angle)*radius)*pixel,pixel*2,pixel*2);
        g.fillRect(cx-1,cy-23,2,4);
    }
    void drawLinearSlider(juce::Graphics& g,int x,int y,int w,int h,float sliderPos,float,float,const juce::Slider::SliderStyle,juce::Slider&) override {
        g.setColour(juce::Colour(0xff454545));g.fillRect(x,y+h/2-2,w,4);
        g.setColour(red());g.fillRect((int)sliderPos-3,y+h/2-7,6,14);
    }
};
}
