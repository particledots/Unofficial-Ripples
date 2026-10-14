#include "PluginProcessor.h"
#include "PluginEditor.h"

UnofficialRipplesProcessor::UnofficialRipplesProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                                      .withInput("Sidechain (FM)", juce::AudioChannelSet::stereo(), false)),
      apvts(*this, nullptr, "STATE", createLayout()) {}

juce::AudioProcessorValueTreeState::ParameterLayout UnofficialRipplesProcessor::createLayout() {
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    // Frequency: the pole frequency of each of the four stages, 5 Hz .. about 21.9 kHz (the top of the VCA range in the drawing), log scale
    NormalisableRange<float> fr(5.0f, 21900.0f, 0.0f, 0.25f);
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ "freq", 1 }, "Frequency", fr, 1000.0f,
        AudioParameterFloatAttributes().withLabel("Hz")));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ "reso", 1 }, "Resonance", NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    p.push_back(std::make_unique<AudioParameterChoice>(ParameterID{ "output", 1 }, "Output",
        StringArray{ "LP2 (12 dB low-pass)", "LP4 (24 dB low-pass)", "BP2 (band-pass)", "LP4 through VCA" }, 1));
    // FM: the sidechain signal moves the Frequency exponentially. 1.0 = one octave per volt (full scale = 5 V), so a full-scale signal at 1.0 swings 5 octaves each way.
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ "fm", 1 }, "FM amount", NormalisableRange<float>(0.0f, 1.0f, 0.0f, 0.5f), 0.0f));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ "vca", 1 }, "VCA level", NormalisableRange<float>(0.0f, 1.0f), 0.3f));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ "ingain", 1 }, "Input gain", NormalisableRange<float>(-24.0f, 24.0f), 0.0f,
        AudioParameterFloatAttributes().withLabel("dB")));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ "outgain", 1 }, "Output gain", NormalisableRange<float>(-24.0f, 24.0f), 0.0f,
        AudioParameterFloatAttributes().withLabel("dB")));
    return { p.begin(), p.end() };
}

void UnofficialRipplesProcessor::prepareToPlay(double sampleRate, int) {
    fs = sampleRate;
    for (auto& v : voices) v.reset();
    first = true;
    setLatencySamples(ripples::Oversampler2x::latencySamples());
}

bool UnofficialRipplesProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    if (in != out) return false;
    if (!(out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())) return false;
    const auto sc = l.getChannelSet(true, 1);   // the FM sidechain: off, mono or stereo
    return sc.isDisabled() || sc == juce::AudioChannelSet::mono() || sc == juce::AudioChannelSet::stereo();
}

void UnofficialRipplesProcessor::processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int n = buf.getNumSamples();
    auto mainBuf = getBusBuffer(buf, false, 0);
    const int chans = std::min(mainBuf.getNumChannels(), 2);
    // FM sidechain (if the host has connected one): read it before anything else
    const float* sc[2] = { nullptr, nullptr }; int scChans = 0;
    if (auto* scBus = getBus(true, 1)) if (scBus->isEnabled()) {
        auto scBuf = getBusBuffer(buf, true, 1);
        scChans = std::min(scBuf.getNumChannels(), 2);
        for (int c = 0; c < scChans; ++c) sc[c] = scBuf.getReadPointer(c);
    }
    const double targetHz = (double) *apvts.getRawParameterValue("freq");
    const double targetQ = (double) *apvts.getRawParameterValue("reso");
    const double targetVca = (double) *apvts.getRawParameterValue("vca");
    const double fmAmt = (double) *apvts.getRawParameterValue("fm");
    const auto out = (ripples::Output) juce::jlimit(0, 3, (int) std::lround(*apvts.getRawParameterValue("output")));
    const double inG = std::pow(10.0, *apvts.getRawParameterValue("ingain") / 20.0);
    const double outG = std::pow(10.0, *apvts.getRawParameterValue("outgain") / 20.0);
    if (first) { smHzLog = std::log(std::max(1.0, targetHz)); smQ = targetQ; smVca = targetVca; first = false; }
    // about 5 ms smoothing so automation and knob moves do not zipper
    const double a = 1.0 - std::exp(-1.0 / (0.005 * fs));
    const double tl = std::log(std::max(1.0, targetHz));
    float* d[2] = { mainBuf.getWritePointer(0), chans > 1 ? mainBuf.getWritePointer(1) : nullptr };
    const double fmScale = fmAmt * ripples::kVoltsPerFS;     // octaves per unit of sidechain level
    for (int i = 0; i < n; ++i) {
        smHzLog += a * (tl - smHzLog); smQ += a * (targetQ - smQ); smVca += a * (targetVca - smVca);
        const double hz = std::exp(smHzLog);
        double fm = 0.0;
        if (scChans == 1) fm = sc[0][i]; else if (scChans == 2) fm = 0.5 * (sc[0][i] + sc[1][i]);
        fm *= fmScale;
        for (int c = 0; c < chans; ++c) d[c][i] = voices[c].process(d[c][i], out, hz, smQ, smVca, inG, outG, fs, fm);
    }
}

juce::AudioProcessorEditor* UnofficialRipplesProcessor::createEditor() { return new RipplesEditor(*this); }

void UnofficialRipplesProcessor::getStateInformation(juce::MemoryBlock& dest) {
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary(*xml, dest);
}
void UnofficialRipplesProcessor::setStateInformation(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size)) if (xml->hasTagName(apvts.state.getType())) apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new UnofficialRipplesProcessor(); }
