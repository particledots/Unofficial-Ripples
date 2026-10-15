// Checks the plug-in as a whole (not just the filter maths): that the sidechain bus really reaches the FM,
// for all three FM sources.  Usage: ripples_proc_test   (exit code 0 = all passed)
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include <random>

static int failed = 0, total = 0;
#define CHECK(c, ...) do { ++total; if (!(c)) { ++failed; printf("FAIL line %d: %s  ", __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void setParam(UnofficialRipplesProcessor& p, const char* id, float real) {
    auto* prm = p.apvts.getParameter(id);
    prm->setValueNotifyingHost(prm->convertTo0to1(real));
}

// scMode: 0 = sidechain bus disabled, 1 = sidechain DC level scLevel, 2 = sidechain burst (1 for the first 2400 samples, then 0), 3 = sidechain silent
static double run(int fmSrc, float fmAmt, int scMode, float scLevel, double* energyAfterBurst = nullptr) {
    UnofficialRipplesProcessor p;
    juce::AudioProcessor::BusesLayout lay = p.getBusesLayout();
    if (scMode != 0) lay.inputBuses.getReference(1) = juce::AudioChannelSet::stereo();
    else lay.inputBuses.getReference(1) = juce::AudioChannelSet::disabled();
    const bool ok = p.setBusesLayout(lay);
    CHECK(ok, "could not set the bus layout (scMode %d)", scMode);
    if (scMode != 0) CHECK(p.getBus(true, 1)->isEnabled(), "sidechain bus did not switch on");
    const double fs = 48000.0; const int block = 512;
    p.setRateAndBufferSizeDetails(fs, block); p.prepareToPlay(fs, block);
    setParam(p, "freq", 800.0f); setParam(p, "reso", 0.0f); setParam(p, "output", 1.0f);
    setParam(p, "fm", fmAmt); setParam(p, "fmsrc", (float) fmSrc);
    setParam(p, "fmatk", 1.0f); setParam(p, "fmdec", 100.0f);
    std::mt19937 rng(7); std::uniform_real_distribution<float> u(-0.3f, 0.3f);
    const int chans = p.getTotalNumInputChannels() > 2 ? 4 : 2;
    juce::AudioBuffer<float> buf(std::max(chans, 4), block);
    juce::MidiBuffer midi; double e = 0, eAfter = 0; long cnt = 0, cntAfter = 0; long pos = 0;
    for (int b = 0; b < 40; ++b) {
        buf.clear();
        for (int i = 0; i < block; ++i) { float x = u(rng); buf.setSample(0, i, x); buf.setSample(1, i, x); }
        if (scMode != 0) for (int i = 0; i < block; ++i) {
            float v = scMode == 1 ? scLevel : scMode == 2 ? ((pos + i) < 2400 ? 1.0f : 0.0f) : 0.0f;
            buf.setSample(2, i, v); buf.setSample(3, i, v);
        }
        p.processBlock(buf, midi);
        if (b >= 10) for (int i = 0; i < block; ++i) { double y = buf.getSample(0, i); e += y * y; ++cnt; if (pos + i >= 2400 && pos + i < 2400 + 4800) { eAfter += y * y; ++cntAfter; } }
        pos += block;
    }
    if (energyAfterBurst) *energyAfterBurst = cntAfter ? eAfter / cntAfter : 0.0;
    return std::sqrt(e / std::max(1L, cnt));
}

int main() {
    juce::ScopedJuceInitialiser_GUI init;
    UnofficialRipplesProcessor probe;
    CHECK(probe.getBusCount(true) == 2, "expected a main input and a sidechain input, got %d input buses", probe.getBusCount(true));

    const double base = run(0, 0.0f, 0, 0.0f);                       // no FM at all
    CHECK(base > 0.01, "silent output %g", base);
    // 1. Sidechain, with a steady level on the sidechain: the filter opens, so a noise input comes out louder
    const double scUp = run(0, 0.6f, 1, 0.8f);
    CHECK(scUp > base * 1.5, "sidechain FM did nothing: %g vs %g", scUp, base);
    // 2. Same setting but the sidechain bus is off: must be the same as no FM
    const double scOff = run(0, 0.6f, 0, 0.0f);
    CHECK(std::fabs(scOff - base) < 1e-6, "FM moved with no sidechain: %g vs %g", scOff, base);
    // 3. A silent sidechain gives no FM either
    const double scSil = run(0, 0.6f, 3, 0.0f);
    CHECK(std::fabs(scSil - base) < 1e-6, "silent sidechain changed the output: %g vs %g", scSil, base);
    // 4. Negative amount closes the filter
    const double scDown = run(0, -0.6f, 1, 0.8f);
    CHECK(scDown < base * 0.7, "negative FM did not close the filter: %g vs %g", scDown, base);
    // 5. FM amount at 0 means no FM, whatever the sidechain
    const double scZero = run(0, 0.0f, 1, 0.9f);
    CHECK(std::fabs(scZero - base) < 1e-6, "amount 0 still moved the sound: %g vs %g", scZero, base);
    // 6. Own input as the source
    const double own = run(1, 0.6f, 0, 0.0f);
    CHECK(std::fabs(own - base) > base * 0.02, "own-input FM did nothing: %g vs %g", own, base);
    // 7. Envelope from the sidechain: a burst on the sidechain opens the filter for a while, then it falls back
    double eAfterEnv = 0, eAfterNone = 0;
    const double envBurst = run(2, 0.6f, 2, 0.0f, &eAfterEnv);
    run(2, 0.6f, 3, 0.0f, &eAfterNone);
    CHECK(eAfterEnv > eAfterNone * 2.0, "envelope FM did not open the filter after the burst: %g vs %g", eAfterEnv, eAfterNone);
    CHECK(envBurst > base * 1.02, "envelope run not louder overall: %g vs %g", envBurst, base);
    // 8. Envelope with no sidechain follows the plug-in's own input (the noise itself), so something happens
    const double envOwn = run(2, 0.6f, 0, 0.0f);
    CHECK(envOwn > base * 1.05, "envelope on own input did nothing: %g vs %g", envOwn, base);
    // 9. Envelope on a silent sidechain = no FM
    const double envSil = run(2, 0.6f, 3, 0.0f);
    CHECK(std::fabs(envSil - base) < 1e-6, "envelope with a silent sidechain moved the sound: %g vs %g", envSil, base);

    printf("%d checks, %d failed\n", total, failed);
    return failed ? 1 : 0;
}
