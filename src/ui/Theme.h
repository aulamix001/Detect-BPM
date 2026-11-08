#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace chi {
struct Theme {
    inline static const juce::Colour background{0xff18181b}, panel{0xff222226}, border{0xff2e2e33};
    inline static const juce::Colour accent{0xffc47f45}, accentHover{0xffd99559};
    inline static const juce::Colour primary{0xff7a2e22}, primaryHover{0xff9b4131};
    inline static const juce::Colour text{0xfff4f4f5}, muted{0xffb5b5bf};
    inline static const juce::Colour disabled{0xff303036}, disabledText{0xff91919c};
    inline static const juce::Colour well{0xff111114}, needle{0xffe4e4e7}, clip{0xffef5c4e};
    inline static constexpr float radius=12.0f;
};
class SmartLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    SmartLookAndFeel() {
        setColour(juce::Label::textColourId,Theme::text);
        setColour(juce::ToggleButton::textColourId,Theme::text);
        setColour(juce::ToggleButton::tickColourId,Theme::accent);
        setColour(juce::ToggleButton::tickDisabledColourId,Theme::disabledText);
        setColour(juce::Slider::rotarySliderFillColourId,Theme::accent);
        setColour(juce::Slider::rotarySliderOutlineColourId,Theme::border);
        setColour(juce::Slider::thumbColourId,Theme::text);
        setColour(juce::Slider::textBoxTextColourId,Theme::text);
        setColour(juce::Slider::textBoxBackgroundColourId,Theme::well);
        setColour(juce::Slider::textBoxOutlineColourId,Theme::border);
        setColour(juce::ComboBox::backgroundColourId,Theme::panel);
        setColour(juce::ComboBox::outlineColourId,Theme::border);
        setColour(juce::ComboBox::textColourId,Theme::text);
        setColour(juce::ComboBox::arrowColourId,Theme::accent);
        setColour(juce::PopupMenu::backgroundColourId,Theme::panel);
        setColour(juce::PopupMenu::textColourId,Theme::text);
        setColour(juce::PopupMenu::highlightedBackgroundColourId,Theme::primary);
    }
    void drawButtonBackground(juce::Graphics& g,juce::Button& b,const juce::Colour&,bool hover,bool down) override {
        const bool primary=b.getProperties().getWithDefault("primary",false);
        auto colour=primary?Theme::primary:Theme::panel;
        if(hover)colour=primary?Theme::primaryHover:Theme::border;
        if(down||b.getToggleState())colour=Theme::accent;
        if(!b.isEnabled())colour=Theme::disabled;
        auto bounds=b.getLocalBounds().toFloat().reduced(.5f);
        g.setColour(colour);g.fillRoundedRectangle(bounds,10);
        g.setColour(b.isEnabled()&&b.getToggleState()?Theme::accent:Theme::border);
        g.drawRoundedRectangle(bounds,10,1);
    }
    void drawButtonText(juce::Graphics& g,juce::TextButton& b,bool,bool) override {
        g.setColour(b.isEnabled()?Theme::text:Theme::disabledText);
        g.setFont(juce::FontOptions(14));
        g.drawFittedText(b.getButtonText(),b.getLocalBounds().reduced(6,2),juce::Justification::centred,2);
    }
};
}
