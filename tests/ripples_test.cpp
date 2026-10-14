// Tests of the Ripples model (no JUCE). Build:  c++ -std=c++17 -O2 -Isrc tests/ripples_test.cpp -o ripples_test && ./ripples_test
#include "RipplesModel.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
using namespace ripples;

static int checks = 0, fails = 0;
#define CHECK(c, ...) do { ++checks; if (!(c)) { ++fails; printf("FAIL line %d: %s  ", __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)

static double db(double x) { return 20.0 * std::log10(std::max(x, 1e-12)); }

// Run a sine through a Voice (raw model, not the oversampler) and measure the gain at the sine's frequency.
struct Run { double gain; double peak; };
static Run sineGain(Output out, double stageHz, double q, double vca, double f, double fs, double amp = 0.1, double secs = 0.6, double inGain = 1.0) {
    Voice v; v.reset();
    const int n = (int)(secs * fs);
    double sr = 0, si = 0, ir = 0, ii = 0, pk = 0; int cnt = 0;
    for (int i = 0; i < n; ++i) {
        const double ph = 2.0 * kPi * f * i / fs;
        const float x = (float)(amp * std::sin(ph));
        const float y = v.process(x, out, stageHz, q, vca, inGain, 1.0, fs);
        pk = std::max(pk, (double) std::fabs(y));
        if (i > n / 2) { sr += y * std::sin(ph); si += y * std::cos(ph); ir += x * std::sin(ph); ii += x * std::cos(ph); ++cnt; }
    }
    return { std::hypot(sr, si) / std::max(std::hypot(ir, ii), 1e-30), pk };
}
// signed gain at a low frequency: in phase (+) or opposite (-), allowing for the plug-in's latency
static double signedGain(Output out, double stageHz, double q, double vca, double f, double fs) {
    Voice v; v.reset(); const int n = (int)(0.6 * fs); const int lat = Oversampler2x::latencySamples();
    double num = 0, den = 0;
    for (int i = 0; i < n; ++i) {
        const double ph = 2.0 * kPi * f * i / fs; const float x = (float)(0.1 * std::sin(ph));
        const float y = v.process(x, out, stageHz, q, vca, 1.0, 1.0, fs);
        const float xd = (float)(0.1 * std::sin(2.0 * kPi * f * (i - lat) / fs));
        if (i > n / 2) { num += y * xd; den += xd * xd; }
    }
    return num / den;
}

static void testOversampler() {
    printf("-- oversampler\n");
    Oversampler2x os; const double fs = 48000.0;
    for (double f : {100.0, 1000.0, 8000.0}) {
        os.reset(); double s = 0, e = 0; const int n = 9600;
        std::vector<double> in(n), outv(n);
        for (int i = 0; i < n; ++i) { in[i] = std::sin(2 * kPi * f * i / fs); double a, b; os.up(in[i], a, b); outv[i] = os.down(a, b); }
        const int lat = Oversampler2x::latencySamples();
        for (int i = n / 2; i < n; ++i) { s += outv[i] * outv[i]; e += in[i - lat] * in[i - lat]; }
        CHECK(std::fabs(db(std::sqrt(s / e))) < 0.5, "gain at %g Hz = %g dB", f, db(std::sqrt(s / e)));
        double d = 0; for (int i = n / 2; i < n; ++i) d += (outv[i] - in[i - lat]) * (outv[i] - in[i - lat]);
        CHECK(std::sqrt(d / e) < 0.05, "shape/latency mismatch at %g Hz: %g", f, std::sqrt(d / e));
    }
}

static void testGains() {
    printf("-- gains and polarity (Q = 0, filter wide open)\n");
    const double fs = 48000.0;
    // DC-ish: 50 Hz, stages at 21.9 kHz => the filter is flat there
    double lp4 = signedGain(LP4, kStageMaxHz, 0, 0, 50.0, fs);
    double lp2 = signedGain(LP2, kStageMaxHz, 0, 0, 50.0, fs);
    CHECK(std::fabs(lp4 + 1.0) < 0.02, "LP4 gain %g (expected -1.00: (33/100) * (100/33), inverted)", lp4);
    CHECK(std::fabs(lp2 + 0.846) < 0.02, "LP2 gain %g (expected -0.846)", lp2);
    // BP2 has no DC: LP1 and LP2 cancel at low frequency
    double bp = sineGain(BP2, kStageMaxHz, 0, 0, 50.0, fs).gain;
    CHECK(bp < 0.02, "BP2 at 50 Hz = %g (should be nearly zero because LP1 and LP2 cancel)", bp);
}

static void testCutoffAndSlope() {
    printf("-- cutoff and slopes\n");
    const double fs = 96000.0;
    // one stage's pole at 1 kHz.  LP2 = two poles; LP4 = four poles. Measure slopes at 8 kHz -> 16 kHz (far above the pole)
    for (auto spec : { std::make_pair(LP2, 12.0), std::make_pair(LP4, 24.0) }) {
        double a = sineGain(spec.first, 1000.0, 0, 0, 8000.0, fs).gain;
        double b = sineGain(spec.first, 1000.0, 0, 0, 16000.0, fs).gain;
        double slope = db(a) - db(b);
        CHECK(std::fabs(slope - spec.second) < 1.0, "slope %g dB/oct (expected %g)", slope, spec.second);
    }
    // each stage is -3 dB at its own pole frequency: LP2 (two poles) at 1 kHz = 1/2 power per pole => -6 dB relative to its DC gain
    double ref = sineGain(LP2, 1000.0, 0, 0, 50.0, fs).gain;
    double at  = sineGain(LP2, 1000.0, 0, 0, 1000.0, fs).gain;
    CHECK(std::fabs(db(at / ref) + 6.02) < 0.5, "LP2 at the stage frequency is %g dB (expected -6.0)", db(at / ref));
    double ref4 = sineGain(LP4, 1000.0, 0, 0, 50.0, fs).gain;
    double at4  = sineGain(LP4, 1000.0, 0, 0, 1000.0, fs).gain;
    CHECK(std::fabs(db(at4 / ref4) + 12.04) < 0.7, "LP4 at the stage frequency is %g dB (expected -12.0)", db(at4 / ref4));
    // the frequency control moves it by octaves
    double c1 = sineGain(LP4, 2000.0, 0, 0, 2000.0, fs).gain / sineGain(LP4, 2000.0, 0, 0, 50.0, fs).gain;
    CHECK(std::fabs(db(c1) + 12.04) < 0.7, "doubling the control doubles the corner: %g dB", db(c1));
}

static void testHighCutoffAndOverload() {
    printf("-- high cutoff settings at 44.1 kHz, and recovery after an overload\n");
    // a stage frequency of 15 kHz at 44.1 kHz: each pole is still -3 dB at its own frequency (needs the frequency warping to be right)
    const double fs = 44100.0;
    double ref = sineGain(LP2, 15000.0, 0, 0, 100.0, fs).gain;
    double at  = sineGain(LP2, 15000.0, 0, 0, 15000.0, fs).gain;
    CHECK(std::fabs(db(at / ref) + 6.02) < 1.0, "LP2 at 15 kHz stage setting, 44.1 kHz: %g dB (expected -6.0)", db(at / ref));
    // the same frequency warping, checked on the filter itself at 48 kHz with a 10 kHz stage (no oversampler in the way): four poles = -12 dB there
    {
        auto gainAt = [](double f) { Channel c; c.reset(); double sr = 0, si = 0, ir = 0, ii = 0; const double fs = 48000.0; const int n = 48000;
            for (int i = 0; i < n; ++i) { double ph = 2 * kPi * f * i / fs, x = 0.5 * std::sin(ph); double y = c.process(x, LP4, 10000.0, 0, 0, fs);
                if (i > n / 2) { sr += y * std::sin(ph); si += y * std::cos(ph); ir += x * std::sin(ph); ii += x * std::cos(ph); } }
            return std::hypot(sr, si) / std::hypot(ir, ii); };
        double rel = db(gainAt(10000.0) / gainAt(50.0));
        CHECK(std::fabs(rel + 12.04) < 0.4, "LP4 at a 10 kHz stage setting, 48 kHz: %g dB (expected -12.0)", rel);
    }
    // a huge input for a while must not wind the filter up: the stages stop at the supply rails, so a slow filter recovers sooner
    Voice v; v.reset(); float y = 0;
    for (int i = 0; i < 48000; ++i) v.process(1.0f, LP4, 5.0, 0, 0, 40.0, 1.0, 48000.0);   // about 200 V in: far past the rails
    for (int i = 0; i < 14400; ++i) y = v.process(0.0f, LP4, 5.0, 0, 0, 40.0, 1.0, 48000.0);
    CHECK(std::fabs(y) < 0.25, "output %g, 0.3 s after a big overload ended (stage 5 Hz); stages are not stopping at the rails", y);
}

static void testSampleRates() {
    printf("-- same answer at 44.1 / 48 / 96 kHz\n");
    for (Output o : { LP2, LP4, BP2 }) for (double f : { 500.0, 2000.0, 6000.0 }) {
        double a = sineGain(o, 3000.0, 0.3, 0, f, 44100.0).gain;
        double b = sineGain(o, 3000.0, 0.3, 0, f, 48000.0).gain;
        double c = sineGain(o, 3000.0, 0.3, 0, f, 96000.0).gain;
        CHECK(std::fabs(db(a / c)) < 0.6 && std::fabs(db(b / c)) < 0.6, "out %d f %g: %g, %g, %g", (int) o, f, a, b, c);
    }
}

// frequency of a sustained oscillation: count positive zero crossings
static double oscFreq(const std::vector<float>& y, double fs, int from) {
    int cross = 0; int first = -1, last = -1;
    for (size_t i = from + 1; i < y.size(); ++i) if (y[i - 1] < 0 && y[i] >= 0) { if (first < 0) first = (int) i; last = (int) i; ++cross; }
    return cross < 3 ? 0.0 : (cross - 1) * fs / (double)(last - first);
}
static double tailRms(const std::vector<float>& y, int from) { double s = 0; for (size_t i = from; i < y.size(); ++i) s += y[i] * y[i]; return std::sqrt(s / (y.size() - from)); }

static std::vector<float> ring(double q, double stageHz, double fs, double secs, Output o = LP4) {
    Voice v; v.reset(); const int n = (int)(secs * fs); std::vector<float> y(n);
    for (int i = 0; i < n; ++i) y[i] = v.process(i < 20 ? 0.5f : 0.0f, o, stageHz, q, 0, 1.0, 1.0, fs);   // a short click, then silence
    return y;
}

static void testResonance() {
    printf("-- resonance and self-oscillation\n");
    const double fs = 48000.0;
    // below the threshold the ring dies away, above it keeps going
    auto lo = ring(0.60, 1000.0, fs, 3.0);
    auto hi = ring(0.85, 1000.0, fs, 3.0);
    CHECK(tailRms(lo, (int)(2.5 * fs)) < 1e-4, "Q 0.60 still ringing: %g", tailRms(lo, (int)(2.5 * fs)));
    CHECK(tailRms(hi, (int)(2.5 * fs)) > 0.02, "Q 0.85 does not self-oscillate: %g", tailRms(hi, (int)(2.5 * fs)));
    // threshold lands near the figure worked out from the schematic: loop gain 1 when gm * 33k / 23 / 4 = 1  => Iabc = 145 uA => Q = 145/193 = 0.75
    double thr = -1;
    for (double q = 0.60; q <= 0.90; q += 0.01) { auto y = ring(q, 1000.0, fs, 3.0); if (tailRms(y, (int)(2.5 * fs)) > 0.005) { thr = q; break; } }
    CHECK(thr > 0.72 && thr < 0.80, "threshold at Q = %g", thr);
    printf("   self-oscillation starts at Q = %.2f (a figure the Q scaling is built around, so this checks the maths, not the hardware)\n", thr);
    // pitch of the oscillation: four equal poles give 180 degrees at the stage frequency
    for (double f : { 200.0, 1000.0, 5000.0 }) {
        auto y = ring(0.95, f, fs, 3.0);
        double of = oscFreq(y, fs, (int)(2.0 * fs));
        CHECK(std::fabs(of / f - 1.0) < 0.05, "stage %g Hz oscillates at %g Hz", f, of);
    }
    // a resonant peak: the gain at the stage frequency rises with Q
    double g0 = sineGain(LP4, 2000.0, 0.0, 0, 2000.0, fs).gain, g5 = sineGain(LP4, 2000.0, 0.5, 0, 2000.0, fs).gain, g7 = sineGain(LP4, 2000.0, 0.7, 0, 2000.0, fs).gain;
    CHECK(g5 > g0 * 1.2 && g7 > g5 * 1.4, "peak does not grow with Q: %g %g %g", g0, g5, g7);
    // the oscillation level is limited (by the OTA and the rails)
    auto y = ring(1.0, 1000.0, fs, 2.0);
    double pk = 0; for (float s : y) pk = std::max(pk, (double) std::fabs(s));
    CHECK(pk < 2.2, "oscillation peak %g of full scale", pk);
    // the input is also sent through the OTA, so the level at DC changes less than the feedback alone would suggest
    double d0 = sineGain(LP4, 3000.0, 0.0, 0, 100.0, fs).gain, d6 = sineGain(LP4, 3000.0, 0.6, 0, 100.0, fs).gain;
    CHECK(d6 > 0.4 * d0 && d6 < 1.3 * d0, "bass level at Q 0.6: %g vs %g", d6, d0);
}

static void testVca() {
    printf("-- LP4 VCA output\n");
    const double fs = 48000.0;
    double g0 = sineGain(LP4_VCA, kStageMaxHz, 0, 0.0, 500.0, fs).gain;
    double g1 = sineGain(LP4_VCA, kStageMaxHz, 0, 0.2, 500.0, fs).gain;
    double g2 = sineGain(LP4_VCA, kStageMaxHz, 0, 0.4, 500.0, fs).gain;
    CHECK(g0 < 1e-3, "VCA at 0 is not silent: %g", g0);
    CHECK(g1 > 0.1 && std::fabs(g2 / g1 - 2.0) < 0.1, "VCA does not scale with its control: %g %g", g1, g2);
    // AC-coupled: a DC step produces no lasting output
    Voice v; v.reset(); float y = 0; for (int i = 0; i < 48000 * 2; ++i) y = v.process(0.2f, LP4_VCA, kStageMaxHz, 0, 0.5, 1.0, 1.0, fs);
    CHECK(std::fabs(y) < 0.02, "DC passes through the VCA output: %g", y);
}

static void testStability() {
    printf("-- stability with extreme and random settings\n");
    std::mt19937 rng(7); std::uniform_real_distribution<double> u(0.0, 1.0);
    for (double fs : { 44100.0, 96000.0, 192000.0 }) {
        Voice v; v.reset(); bool bad = false; double pk = 0;
        for (int i = 0; i < (int)(fs * 4); ++i) {
            if (i % 2000 == 0) { /* new random settings; also sudden jumps */ }
            static double hz = 1000, q = 0.5, va = 0.5; static Output o = LP4;
            if (i % 2000 == 0) { hz = std::exp(std::log(5.0) + u(rng) * std::log(30000.0 / 5.0)); q = u(rng) > 0.5 ? 1.0 : u(rng); va = u(rng); o = (Output)(rng() % 4); }
            float x = (float)((u(rng) * 2 - 1) * (u(rng) > 0.99 ? 4.0 : 1.0));
            float y = v.process(x, o, hz, q, va, 2.0, 1.0, fs);
            if (!std::isfinite(y)) { bad = true; break; }
            pk = std::max(pk, (double) std::fabs(y));
        }
        CHECK(!bad, "non-finite output at %g Hz", fs);
        CHECK(pk <= 1.5 * kRail / kVoltsPerFS, "output far beyond the rails: %g", pk);   // the anti-alias filter rings a little on hard-clipped edges
    }
}

static void testOutputSwitch() {
    printf("-- switching the output does not click\n");
    const double fs = 48000.0; Voice v; v.reset();
    // a steady 200 Hz tone; switch LP2 -> LP4 -> BP2 -> VCA; the biggest sample-to-sample jump must stay near the tone's own
    double maxStep = 0, last = 0, maxToneStep = 0; Output cur = LP2;
    for (int i = 0; i < 48000 * 2; ++i) {
        if (i == 24000) cur = LP4;
        if (i == 48000) cur = BP2;
        if (i == 72000) cur = LP4_VCA;
        float y = v.process((float)(0.2 * std::sin(2 * kPi * 200.0 * i / fs)), cur, 4000.0, 0.2, 0.4, 1.0, 1.0, fs);
        if (i > 4800) { double st = std::fabs(y - last); if (i < 23000) maxToneStep = std::max(maxToneStep, st); maxStep = std::max(maxStep, st); }
        last = y;
    }
    CHECK(maxStep < 4.0 * maxToneStep + 0.01, "a jump of %g when switching (the tone's own biggest step is %g)", maxStep, maxToneStep);
}

static void testSilenceAndReset() {
    printf("-- silence in, silence out; reset clears the state\n");
    Voice v; v.reset();
    float worst = 0; for (int i = 0; i < 48000; ++i) worst = std::max(worst, std::fabs(v.process(0.0f, LP4, 1000, 0.9, 0.5, 1, 1, 48000.0)));
    CHECK(worst < 1e-6, "silence in gave %g", worst);
    for (int i = 0; i < 4800; ++i) v.process(0.5f, LP4, 1000, 0.9, 0.5, 1, 1, 48000.0);
    v.reset();
    float after = 0; for (int i = 0; i < 100; ++i) after = std::max(after, std::fabs(v.process(0.0f, LP4, 1000, 0.9, 0.5, 1, 1, 48000.0)));
    CHECK(after < 1e-6, "state survived reset: %g", after);
}

int main() {
    testOversampler(); testGains(); testCutoffAndSlope(); testHighCutoffAndOverload(); testSampleRates(); testResonance(); testVca(); testStability(); testOutputSwitch(); testSilenceAndReset();
    printf("\n%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
