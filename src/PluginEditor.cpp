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
    addKnob(fmAtk, "fmatk", "FM ATTACK", true);
    addKnob(fmDec, "fmdec", "FM DECAY", true);
    for (Knob* k : { &fmAtk, &fmDec }) k->slider.textFromValueFunction = [](double v) { return v < 10.0 ? juce::String(v, 1) + " ms" : v < 1000.0 ? juce::String((int) std::lround(v)) + " ms" : juce::String(v / 1000.0, 2) + " s"; };
    fmAtk.slider.updateText(); fmDec.slider.updateText();

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
        static const char* fmNames[3] = { "SIDECHAIN", "OWN INPUT", "ENVELOPE" };
        auto* fmParam = dynamic_cast<juce::RangedAudioParameter*>(proc.apvts.getParameter("fmsrc"));
        fmAttach = std::make_unique<juce::ParameterAttachment>(*fmParam, [this](float v) {
            const int sel = (int) std::lround(v);
            for (int i = 0; i < 3; ++i) fmButtons[i].setToggleState(i == sel, juce::dontSendNotification);
        });
        for (int i = 0; i < 3; ++i) {
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
    {
        static const char* tNames[2] = { "SMOOTH", "AS DRAWN" };
        auto* tParam = dynamic_cast<juce::RangedAudioParameter*>(proc.apvts.getParameter("taper"));
        taperAttach = std::make_unique<juce::ParameterAttachment>(*tParam, [this](float v) {
            const int sel = (int) std::lround(v);
            for (int i = 0; i < 2; ++i) taperButtons[i].setToggleState(i == sel, juce::dontSendNotification);
        });
        for (int i = 0; i < 2; ++i) {
            taperButtons[i].setButtonText(tNames[i]);
            taperButtons[i].onClick = [this, i] { taperAttach->setValueAsCompleteGesture((float) i); };
            addAndMakeVisible(taperButtons[i]);
        }
        taperAttach->sendInitialUpdate();
        taperLabel.setText("RESONANCE TAPER", juce::dontSendNotification);
        taperLabel.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        taperLabel.setColour(juce::Label::textColourId, RipplesLook::dim());
        addAndMakeVisible(taperLabel);
    }
    hint.setText("FM: SIDECHAIN = the sidechain signal moves the frequency; OWN INPUT = the input does; ENVELOPE = the level of the sidechain (or of the input, if no sidechain) moves it, shaped by FM ATTACK and DECAY.  A negative FM AMOUNT pushes the frequency down instead of up.  TAPER: SMOOTH raises the resonance evenly and self-oscillates near 88%; AS DRAWN follows the schematic (self-oscillates at 75%).  VCA LEVEL only affects the LP4 > VCA output.",
                 juce::dontSendNotification);
    hint.setFont(juce::FontOptions(11.0f)); hint.setColour(juce::Label::textColourId, RipplesLook::dim());
    hint.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(hint);
    setSize(780, 580);
    startTimerHz(20);
}

RipplesEditor::~RipplesEditor() { stopTimer(); setLookAndFeel(nullptr); }

void RipplesEditor::timerCallback() {
    const double sig = param("freq") + 3.1 * param("reso") + 7.7 * param("taper") + 13.0 * param("output") + 23.0 * param("vca");
    if (std::fabs(sig - lastSig) > 1e-9) { lastSig = sig; repaint(curveArea); }
}

// The shape of the filter at the current settings (small-signal maths: see ripples::response), not a measurement of the real module.
void RipplesEditor::drawCurve(juce::Graphics& g, juce::Rectangle<int> r) const {
    g.setColour(RipplesLook::bg()); g.fillRoundedRectangle(r.toFloat(), 6.0f);
    const double fLo = 20.0, fHi = 20000.0, dbLo = -48.0, dbHi = 24.0;
    auto X = [&](double f) { return r.getX() + (float) (std::log(f / fLo) / std::log(fHi / fLo)) * r.getWidth(); };
    auto Y = [&](double d) { return r.getBottom() - (float) ((juce::jlimit(dbLo, dbHi, d) - dbLo) / (dbHi - dbLo)) * r.getHeight(); };
    g.setFont(juce::FontOptions(10.0f));
    for (double f : { 100.0, 1000.0, 10000.0 }) {
        g.setColour(RipplesLook::panel().brighter(0.1f)); g.drawVerticalLine((int) X(f), (float) r.getY(), (float) r.getBottom());
        g.setColour(RipplesLook::dim()); g.drawText(f >= 1000.0 ? juce::String((int) (f / 1000.0)) + " kHz" : juce::String((int) f) + " Hz", (int) X(f) + 3, r.getBottom() - 14, 50, 12, juce::Justification::left);
    }
    for (double d : { -36.0, -24.0, -12.0, 0.0, 12.0 }) {
        g.setColour(d == 0.0 ? RipplesLook::panel().brighter(0.35f) : RipplesLook::panel().brighter(0.1f)); g.drawHorizontalLine((int) Y(d), (float) r.getX(), (float) r.getRight());
        g.setColour(RipplesLook::dim()); g.drawText(juce::String((int) d) + " dB", r.getX() + 3, (int) Y(d) - 12, 44, 12, juce::Justification::left);
    }
    const double hz = param("freq"), q = param("reso");
    const bool smooth = param("taper") < 0.5;
    const auto out = (ripples::Output) juce::jlimit(0, 3, (int) std::lround(param("output")));
    const double iReso = ripples::resoCurrent(q, smooth), iVca = param("vca") * ripples::kIVcaMax;
    { const float x = X(hz); const float dash[] = { 4.0f, 4.0f }; g.setColour(RipplesLook::dim().withAlpha(0.7f));
      g.drawDashedLine(juce::Line<float>(x, (float) r.getY(), x, (float) r.getBottom()), dash, 2, 1.0f); }
    juce::Path path; const int W = r.getWidth();
    for (int i = 0; i <= W; i += 2) {
        const double f = fLo * std::pow(fHi / fLo, (double) i / W);
        const double mag = std::abs(ripples::response(out, f, hz, iReso, iVca));
        const float y = Y(20.0 * std::log10(std::max(mag, 1e-6))), x = (float) (r.getX() + i);
        if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
    }
    g.saveState(); g.reduceClipRegion(r);
    g.setColour(RipplesLook::accent()); g.strokePath(path, juce::PathStrokeType(2.0f));
    g.restoreState();
    g.setColour(RipplesLook::dim()); g.setFont(juce::FontOptions(10.0f));
    g.drawText("Shape of the filter at the current settings (small signals; not a measurement of the real module)", r.getX() + 50, r.getY() + 3, r.getWidth() - 60, 12, juce::Justification::right);
}

void RipplesEditor::paint(juce::Graphics& g) {
    g.fillAll(RipplesLook::bg());
    g.setColour(RipplesLook::panel()); g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(10.0f), 10.0f);
    g.setColour(RipplesLook::text()); g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    g.drawText("UNOFFICIAL RIPPLES", 28, 20, 360, 30, juce::Justification::centredLeft);
    g.setColour(RipplesLook::dim()); g.setFont(juce::FontOptions(12.0f));
    g.drawText("four-pole filter, modelled from a published circuit", 28, 48, 420, 18, juce::Justification::centredLeft);
    g.setColour(RipplesLook::dim()); g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText("OUTPUT", 28, 84, 80, 14, juce::Justification::centredLeft);
    drawCurve(g, curveArea);
    g.setColour(RipplesLook::panel().brighter(0.2f));
    g.drawHorizontalLine(266 + 150, 28.0f, (float) getWidth() - 28.0f);
}

void RipplesEditor::resized() {
    auto r = getLocalBounds().reduced(28, 0);
    // output buttons
    {
        const int bw = 118, gap = 8, y = 102;
        for (int i = 0; i < 4; ++i) outButtons[i].setBounds(28 + i * (bw + gap), y, bw, 28);
    }
    curveArea = { 28, 136, getWidth() - 56, 136 };
    // main knobs
    const int kw = (getWidth() - 56) / 4, y0 = 134 + 150;
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
        auto cell = juce::Rectangle<int>(28 + i * sw, 274 + 150, sw, 100);
        sm[i]->label.setBounds(cell.removeFromTop(16));
        sm[i]->slider.setBounds(cell);
    }
    {
        const int x0 = 28 + 2 * sw + 24, w = getWidth() - x0 - 28;
        fmLabel.setBounds(x0, 276 + 150, w, 14);
        for (int i = 0; i < 3; ++i) fmButtons[i].setBounds(x0 + i * 88, 292 + 150, 82, 26);
        fmLabel.setBounds(x0, 276 + 150, 260, 14);
        for (int i = 0; i < 2; ++i) {
            Knob* k = i == 0 ? &fmAtk : &fmDec;
            auto cell = juce::Rectangle<int>(x0 + 272 + i * 84, 274 + 150, 84, 100);
            k->label.setBounds(cell.removeFromTop(16));
            k->slider.setBounds(cell);
        }
        taperLabel.setBounds(x0, 326 + 150, w, 14);
        taperButtons[0].setBounds(x0, 342 + 150, 125, 26);
        taperButtons[1].setBounds(x0 + 135, 342 + 150, 125, 26);
        hint.setBounds(28, 380 + 150, getWidth() - 56, 40);
    }
    hint.setJustificationType(juce::Justification::topLeft);
    hint.setMinimumHorizontalScale(1.0f);
    hint.setMinimumHorizontalScale(1.0f);
    (void) r;
}
