#include "PluginEditor.h"

RipplesLook::RipplesLook() {
    setColour(juce::Slider::textBoxTextColourId, text());
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Label::textColourId, text());
    setColour(juce::TextButton::textColourOffId, dim());
    setColour(juce::TextButton::textColourOnId, bg());
}

void RipplesLook::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float startAngle, float endAngle, juce::Slider& s) {
    const float d = (float) std::min(w, h) - 8.0f;
    const float cx = x + w * 0.5f, cy = y + h * 0.5f, r = d * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);
    // track
    juce::Path track; track.addCentredArc(cx, cy, r, r, 0.0f, startAngle, endAngle, true);
    g.setColour(panel().brighter(0.15f)); g.strokePath(track, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // value arc
    juce::Path val; val.addCentredArc(cx, cy, r, r, 0.0f, startAngle, angle, true);
    g.setColour(s.isEnabled() ? accent() : dim()); g.strokePath(val, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // knob body and pointer
    const float br = r - 8.0f;
    g.setColour(panel().brighter(0.05f)); g.fillEllipse(cx - br, cy - br, br * 2, br * 2);
    g.setColour(juce::Colours::black.withAlpha(0.35f)); g.drawEllipse(cx - br, cy - br, br * 2, br * 2, 1.0f);
    juce::Path p; p.addRoundedRectangle(-1.5f, -br + 3.0f, 3.0f, br * 0.55f, 1.5f);
    g.setColour(text()); g.fillPath(p, juce::AffineTransform::rotation(angle).translated(cx, cy));
}

void RipplesLook::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool) {
    auto r = b.getLocalBounds().toFloat().reduced(1.0f);
    const bool on = b.getToggleState();
    g.setColour(on ? accent() : (highlighted ? panel().brighter(0.2f) : panel()));
    g.fillRoundedRectangle(r, 5.0f);
    g.setColour(on ? accent() : panel().brighter(0.3f)); g.drawRoundedRectangle(r, 5.0f, 1.0f);
}

void RipplesEditor::addKnob(Knob& k, const juce::String& id, const juce::String& name, bool small) {
    k.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, small ? 70 : 90, 18);
    k.slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    k.slider.setDoubleClickReturnValue(true, proc.apvts.getParameter(id)->convertFrom0to1(proc.apvts.getParameter(id)->getDefaultValue()));
    k.label.setText(name, juce::dontSendNotification);
    k.label.setJustificationType(juce::Justification::centred);
    k.label.setFont(juce::FontOptions(small ? 11.0f : 12.5f, juce::Font::bold));
    k.label.setColour(juce::Label::textColourId, RipplesLook::dim());
    addAndMakeVisible(k.slider); addAndMakeVisible(k.label);
    k.attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, id, k.slider);
}

RipplesEditor::RipplesEditor(UnofficialRipplesProcessor& p) : AudioProcessorEditor(&p), proc(p) {
    setLookAndFeel(&look);
    addKnob(freq, "freq", "FREQUENCY");
    addKnob(reso, "reso", "RESONANCE");
    addKnob(fm, "fm", "FM AMOUNT");
    addKnob(vca, "vca", "VCA LEVEL");
    addKnob(inGain, "ingain", "INPUT", true);
    addKnob(outGain, "outgain", "OUTPUT", true);

    freq.slider.textFromValueFunction = [](double v) { return v < 1000.0 ? juce::String(v, v < 100.0 ? 1 : 0) + " Hz" : juce::String(v / 1000.0, 2) + " kHz"; };
    freq.slider.valueFromTextFunction = [](const juce::String& s) {
        double v = s.getDoubleValue(); return s.containsIgnoreCase("k") ? v * 1000.0 : v; };
    auto pct = [](double v) { return juce::String((int) std::lround(v * 100.0)) + " %"; };
    reso.slider.textFromValueFunction = pct; fm.slider.textFromValueFunction = pct; vca.slider.textFromValueFunction = pct;
    reso.slider.valueFromTextFunction = fm.slider.valueFromTextFunction = vca.slider.valueFromTextFunction = [](const juce::String& s) { return s.getDoubleValue() / 100.0; };
    auto dbf = [](double v) { return juce::String(v, 1) + " dB"; };
    inGain.slider.textFromValueFunction = outGain.slider.textFromValueFunction = dbf;
    freq.slider.updateText(); reso.slider.updateText(); fm.slider.updateText(); vca.slider.updateText(); inGain.slider.updateText(); outGain.slider.updateText();

    static const char* names[4] = { "LP2", "LP4", "BP2", "LP4 > VCA" };
    auto* outParam = dynamic_cast<juce::RangedAudioParameter*>(proc.apvts.getParameter("output"));
    outAttach = std::make_unique<juce::ParameterAttachment>(*outParam, [this](float v) {
        const int sel = (int) std::lround(v);
        for (int i = 0; i < 4; ++i) outButtons[i].setToggleState(i == sel, juce::dontSendNotification);
    });
    for (int i = 0; i < 4; ++i) {
        outButtons[i].setButtonText(names[i]);
        outButtons[i].setClickingTogglesState(false);
        outButtons[i].onClick = [this, i] { outAttach->setValueAsCompleteGesture((float) i); };
        addAndMakeVisible(outButtons[i]);
    }
    outAttach->sendInitialUpdate();

    {
        static const char* fmNames[2] = { "SIDECHAIN", "OWN INPUT" };
        auto* fmParam = dynamic_cast<juce::RangedAudioParameter*>(proc.apvts.getParameter("fmsrc"));
        fmAttach = std::make_unique<juce::ParameterAttachment>(*fmParam, [this](float v) {
            const int sel = (int) std::lround(v);
            for (int i = 0; i < 2; ++i) fmButtons[i].setToggleState(i == sel, juce::dontSendNotification);
        });
        for (int i = 0; i < 2; ++i) {
            fmButtons[i].setButtonText(fmNames[i]);
            fmButtons[i].onClick = [this, i] { fmAttach->setValueAsCompleteGesture((float) i); };
            addAndMakeVisible(fmButtons[i]);
        }
        fmAttach->sendInitialUpdate();
        fmLabel.setText("FM SOURCE", juce::dontSendNotification);
        fmLabel.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        fmLabel.setColour(juce::Label::textColourId, RipplesLook::dim());
        addAndMakeVisible(fmLabel);
    }
    hint.setText("SIDECHAIN: route another track into this plug-in's sidechain.  OWN INPUT: the sound going into the filter also modulates its frequency.  VCA LEVEL is used by the LP4 > VCA output only.",
                 juce::dontSendNotification);
    hint.setFont(juce::FontOptions(11.0f)); hint.setColour(juce::Label::textColourId, RipplesLook::dim());
    hint.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(hint);
    setSize(600, 392);
}

RipplesEditor::~RipplesEditor() { setLookAndFeel(nullptr); }

void RipplesEditor::paint(juce::Graphics& g) {
    g.fillAll(RipplesLook::bg());
    g.setColour(RipplesLook::panel()); g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(10.0f), 10.0f);
    g.setColour(RipplesLook::text()); g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    g.drawText("UNOFFICIAL RIPPLES", 28, 20, 360, 30, juce::Justification::centredLeft);
    g.setColour(RipplesLook::dim()); g.setFont(juce::FontOptions(12.0f));
    g.drawText("four-pole filter, modelled from a published circuit", 28, 48, 420, 18, juce::Justification::centredLeft);
    g.setColour(RipplesLook::dim()); g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText("OUTPUT", 28, 84, 80, 14, juce::Justification::centredLeft);
    g.setColour(RipplesLook::panel().brighter(0.2f));
    g.drawHorizontalLine(266, 28.0f, (float) getWidth() - 28.0f);
}

void RipplesEditor::resized() {
    auto r = getLocalBounds().reduced(28, 0);
    // output buttons
    {
        const int bw = 118, gap = 8, y = 102;
        for (int i = 0; i < 4; ++i) outButtons[i].setBounds(28 + i * (bw + gap), y, bw, 28);
    }
    // main knobs
    const int kw = (getWidth() - 56) / 4, y0 = 134;
    Knob* big[4] = { &freq, &reso, &fm, &vca };
    for (int i = 0; i < 4; ++i) {
        auto cell = juce::Rectangle<int>(28 + i * kw, y0, kw, 126);
        big[i]->label.setBounds(cell.removeFromTop(18));
        big[i]->slider.setBounds(cell);
    }
    // gains, smaller, on the lower row
    const int sw = 130;
    Knob* sm[2] = { &inGain, &outGain };
    for (int i = 0; i < 2; ++i) {
        auto cell = juce::Rectangle<int>(28 + i * sw, 274, sw, 100);
        sm[i]->label.setBounds(cell.removeFromTop(16));
        sm[i]->slider.setBounds(cell);
    }
    {
        const int x0 = 28 + 2 * sw + 24, w = getWidth() - x0 - 28;
        fmLabel.setBounds(x0, 276, w, 14);
        fmButtons[0].setBounds(x0, 294, 118, 26);
        fmButtons[1].setBounds(x0 + 126, 294, 118, 26);
        hint.setBounds(x0, 326, w, 44);
    }
    hint.setJustificationType(juce::Justification::topLeft);
    hint.setMinimumHorizontalScale(1.0f);
    (void) r;
}
