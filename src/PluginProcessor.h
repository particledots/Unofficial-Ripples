#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "RipplesModel.h"

class UnofficialRipplesProcessor : public juce::AudioProcessor {
public:
    UnofficialRipplesProcessor();
    ~UnofficialRipplesProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Unofficial Ripples"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }   // it can ring (self-oscillate) when the input stops

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    ripples::Voice voices[2];
    ripples::EnvFollower env;
    double smQ = 0.0, smVca = 0.3;   // smoothed values (frequency is smoothed on a log scale)
    double smHzLog = std::log(1000.0);
    double fs = 44100.0;
    bool first = true;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UnofficialRipplesProcessor)
};
