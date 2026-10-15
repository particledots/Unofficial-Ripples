#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

// A plain, original look: dark panel, round knobs with a value arc, four buttons for the output.
class RipplesLook : public juce::LookAndFeel_V4 {
public:
    RipplesLook();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return juce::FontOptions(14.0f, juce::Font::bold); }
    static juce::Colour bg()     { return juce::Colour(0xff1c1f24); }
    static juce::Colour panel()  { return juce::Colour(0xff262a31); }
    static juce::Colour accent() { return juce::Colour(0xffe8a33d); }
    static juce::Colour text()   { return juce::Colour(0xffe6e8eb); }
    static juce::Colour dim()    { return juce::Colour(0xff8b929c); }
};

class RipplesEditor : public juce::AudioProcessorEditor {
public:
    explicit RipplesEditor(UnofficialRipplesProcessor&);
    ~RipplesEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Knob {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };
    void addKnob(Knob&, const juce::String& id, const juce::String& name, bool small = false);

    UnofficialRipplesProcessor& proc;
    RipplesLook look;
    Knob freq, reso, fm, vca, inGain, outGain;
    juce::TextButton outButtons[4];
    juce::TextButton fmButtons[2];
    std::unique_ptr<juce::ParameterAttachment> fmAttach;
    juce::Label fmLabel;
    juce::TextButton taperButtons[2];
    std::unique_ptr<juce::ParameterAttachment> taperAttach;
    juce::Label taperLabel;
    std::unique_ptr<juce::ParameterAttachment> outAttach;
    juce::Label hint;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RipplesEditor)
};
