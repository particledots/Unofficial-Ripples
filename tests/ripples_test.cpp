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

static void testFm() {
    printf("-- FM (frequency modulation in octaves)\n");
    const double fs = 48000.0;
    // a constant +1 octave must give the same sound as doubling the frequency setting
    for (double f : { 700.0, 2000.0 }) {
        Voice a, b; a.reset(); b.reset(); double d = 0, e = 0;
        for (int i = 0; i < 48000; ++i) {
            float x = (float)(0.2 * std::sin(2 * kPi * f * i / fs));
            float ya = a.process(x, LP4, 1000.0, 0.4, 0, 1, 1, fs, 1.0);
            float yb = b.process(x, LP4, 2000.0, 0.4, 0, 1, 1, fs, 0.0);
            if (i > 24000) { d += (ya - yb) * (ya - yb); e += yb * yb; }
        }
        CHECK(std::sqrt(d / e) < 0.01, "+1 octave is not the same as doubling the frequency (difference %g)", std::sqrt(d / e));
    }
    // -1 octave: half
    {
        Voice a, b; a.reset(); b.reset(); double d = 0, e = 0;
        for (int i = 0; i < 48000; ++i) { float x = (float)(0.2 * std::sin(2 * kPi * 900.0 * i / fs));
            float ya = a.process(x, LP2, 4000.0, 0.2, 0, 1, 1, fs, -1.0), yb = b.process(x, LP2, 2000.0, 0.2, 0, 1, 1, fs, 0.0);
            if (i > 24000) { d += (ya - yb) * (ya - yb); e += yb * yb; } }
        CHECK(std::sqrt(d / e) < 0.01, "-1 octave is not the same as halving it (difference %g)", std::sqrt(d / e));
    }
    // timing: an FM step at input sample N must act on the audio that arrived at N (the audio is delayed by the oversampler, so the FM must be too)
    {
        const int N = 12000, lat = Oversampler2x::latencySamples();
        Voice a, b; a.reset(); b.reset(); double d = 0, e = 0;
        for (int i = 0; i < 24000; ++i) {
            float x = (float)(0.2 * std::sin(2 * kPi * 1500.0 * i / fs));
            float ya = a.process(x, LP4, 500.0, 0.3, 0, 1, 1, fs, i >= N ? 3.0 : 0.0);
            float yb = b.process(x, LP4, i >= N + lat ? 4000.0 : 500.0, 0.3, 0, 1, 1, fs, 0.0);
            if (i > N - 50 && i < N + lat + 300) { d += (ya - yb) * (ya - yb); e += yb * yb; }
        }
        CHECK(std::sqrt(d / e) < 0.02, "FM step is not lined up with the audio (difference %g)", std::sqrt(d / e));
    }
    // audio-rate FM: stays finite, bounded, and makes new frequencies (side-bands) that were not in the input
    {
        Voice v; v.reset(); double pk = 0; bool bad = false; std::vector<float> y(48000);
        for (int i = 0; i < 48000; ++i) {
            float x = (float)(0.3 * std::sin(2 * kPi * 440.0 * i / fs));
            double m = 2.0 * std::sin(2 * kPi * 330.0 * i / fs);
            y[i] = v.process(x, LP4, 3000.0, 0.5, 0, 1, 1, fs, m);
            if (!std::isfinite(y[i])) bad = true;
            pk = std::max(pk, (double) std::fabs(y[i]));
        }
        auto gz = [&](double f) { double sr = 0, si = 0; for (int i = 24000; i < 48000; ++i) { sr += y[i] * std::sin(2 * kPi * f * i / fs); si += y[i] * std::cos(2 * kPi * f * i / fs); } return std::hypot(sr, si) / 24000.0; };
        CHECK(!bad && pk <= 1.5 * kRail / kVoltsPerFS, "audio-rate FM gave bad output (peak %g)", pk);
        CHECK(gz(440.0) > 0.01, "audio-rate FM: the 440 Hz tone is gone");
    }
}

static void testRailKnee() {
    printf("-- the rails: linear up to 70%%, smooth bend, never past the rail\n");
    for (double v : { 0.0, 1.0, -3.0, 7.0, -7.3 }) CHECK(std::fabs(clipRail(v) - v) < 1e-12, "not linear at %g V: %g", v, clipRail(v));
    double prev = clipRail(7.35);
    for (double v = 7.4; v < 60.0; v += 0.05) {
        double c = clipRail(v);
        CHECK(c >= prev - 1e-12 && c <= kRail + 1e-9 && c - prev < 0.06, "knee misbehaves at %g V: %g (before %g)", v, c, prev);
        prev = c;
    }
    CHECK(std::fabs(clipRail(-5.0) + clipRail(5.0)) < 1e-12 && std::fabs(clipRail(-30.0) + clipRail(30.0)) < 1e-12, "not symmetric");
    CHECK(clipRail(1000.0) > 0.999 * kRail, "does not reach the rail: %g", clipRail(1000.0));
}

static void testSmoothTaper() {
    printf("-- smooth resonance taper\n");
    CHECK(std::fabs(smoothLoopGain(0.0)) < 1e-12, "k(0) = %g", smoothLoopGain(0.0));
    CHECK(std::fabs(smoothLoopGain(kSmoothOnset) - 1.0) < 1e-9, "k(onset) = %g", smoothLoopGain(kSmoothOnset));
    CHECK(std::fabs(smoothLoopGain(1.0) - 1.15) < 1e-9, "k(1) = %g", smoothLoopGain(1.0));
    double prev = -1; for (double q = 0; q <= 1.0001; q += 0.005) { double k = smoothLoopGain(q); CHECK(k > prev, "not rising at %g", q); prev = k; }
    CHECK(std::fabs(kIThreshold - 144e-6) < 3e-6, "threshold current %g", kIThreshold);
    const double fs = 48000.0;
    // self-oscillation starts where the taper says (near 0.88), and not before
    auto ringS = [&](double q) {
        Voice v; v.reset(); v.smoothTaper = true; const int n = (int)(4 * fs); std::vector<float> y(n);
        for (int i = 0; i < n; ++i) y[i] = v.process(i < 20 ? 0.5f : 0.0f, LP4, 1000.0, q, 0, 1, 1, fs);
        return tailRms(y, (int)(3.5 * fs)); };
    CHECK(ringS(0.84) < 1e-4, "smooth taper: still ringing at 0.84: %g", ringS(0.84));
    CHECK(ringS(0.93) > 0.005, "smooth taper: no self-oscillation at 0.93: %g", ringS(0.93));
    // the peak rises evenly: between 0.2 and 0.8 each step of 0.2 adds between 4 and 20 dB (the drawn linear taper rises 5 dB over the first half of the knob and then shoots up)
    auto pk = [&](double q, bool smooth) { Voice v; v.reset(); v.smoothTaper = smooth; const int n = (int)(1.5 * fs); double sr = 0, si = 0, cnt = 0;
        for (int i = 0; i < n; ++i) { double ph = 2 * kPi * 2000.0 * i / fs; float y = v.process((float)(0.0003 * std::sin(ph)), LP4, 2000.0, q, 0, 1, 1, fs); if (i > n / 2) { sr += y * std::sin(ph); si += y * std::cos(ph); ++cnt; } }
        return db(std::hypot(sr, si) / cnt * 2 / 0.0003); };
    double p0 = pk(0.0, true), p2 = pk(0.2, true), p4 = pk(0.4, true), p6 = pk(0.6, true), p8 = pk(0.8, true);
    CHECK(p2 - p0 > 4 && p2 - p0 < 20 && p4 - p2 > 4 && p4 - p2 < 20 && p6 - p4 > 4 && p6 - p4 < 20 && p8 - p6 > 4 && p8 - p6 < 20,
          "uneven rise: %g %g %g %g %g dB", p0, p2, p4, p6, p8);
    CHECK(p2 - p0 < 10.0 && p4 - p0 < 18.0 && p6 - p0 < 27.0, "smooth taper rises too fast early: %g %g %g", p2 - p0, p4 - p0, p6 - p0);
    // the 'as drawn' setting is unchanged
    Voice a, b; a.reset(); b.reset(); b.smoothTaper = false; double d = 0; for (int i = 0; i < 4800; ++i) { float x = (float)(0.1 * std::sin(i * 0.1)); d += std::fabs(a.process(x, LP4, 1000, 0.5, 0, 1, 1, 48000.0) - b.process(x, LP4, 1000, 0.5, 0, 1, 1, 48000.0)); }
    CHECK(d == 0.0, "default taper is not the drawn one");
}

static void testSpeedUps() {
    printf("-- the cheaper tan, tanh and the two-phase oversampler match the exact versions\n");
    double worstTan = 0, worstTanh = 0;
    for (double x = 0.0; x <= 1.0; x += 0.001) worstTan = std::max(worstTan, std::fabs(fastTan(x) / std::tan(x) - 1.0));
    CHECK(worstTan < 5e-5, "fastTan relative error %g", worstTan);
    for (double x = 0.001; x <= 1.26; x += 0.001) worstTan = std::max(worstTan, std::fabs(fastTan(x) / std::tan(x) - 1.0));
    CHECK(worstTan < 0.01, "fastTan relative error up to 1.26: %g", worstTan);
    for (double x = -8.0; x <= 8.0; x += 0.001) worstTanh = std::max(worstTanh, std::fabs(fastTanh(x) - std::tanh(x)));
    CHECK(worstTanh < 1.1e-4, "fastTanh error %g", worstTanh);
    // the oversampler, against a plain full-length convolution of the same filter
    Oversampler2x os; os.reset();
    const int m = 31; double h[63], sum = 0;
    for (int i = 0; i < 63; ++i) { double x = i - m; double s = (x == 0.0) ? 0.5 : std::sin(kPi * x * 0.5) / (kPi * x); double w = 0.42 - 0.5 * std::cos(2.0 * kPi * i / 62) + 0.08 * std::cos(4.0 * kPi * i / 62); h[i] = s * w; if (i != m && ((i - m) % 2 == 0)) h[i] = 0; sum += h[i]; }
    for (double& v : h) v *= 2.0 / sum;
    std::mt19937 rng(3); std::uniform_real_distribution<double> u(-1, 1);
    std::vector<double> in(2000), up(4000, 0.0), refUp(4000), gotUp(4000);
    for (double& v : in) v = u(rng);
    for (int n = 0; n < 2000; ++n) { up[2 * n] = in[n]; double a, b; os.up(in[n], a, b); gotUp[2 * n] = a; gotUp[2 * n + 1] = b; }
    double dU = 0;
    for (int k = 0; k < 4000; ++k) { double r = 0; for (int i = 0; i < 63; ++i) if (k - i >= 0) r += h[i] * up[k - i]; refUp[k] = r; dU = std::max(dU, std::fabs(r - gotUp[k])); }
    CHECK(dU < 1e-12, "up-sampler differs from a plain convolution by %g", dU);
    Oversampler2x os2; os2.reset(); double dD = 0;
    for (int n = 0; n < 2000; ++n) {
        double got = os2.down(gotUp[2 * n], gotUp[2 * n + 1]);
        double r = 0; for (int i = 0; i < 63; ++i) if (2 * n - i >= 0) r += h[i] * gotUp[2 * n - i];
        dD = std::max(dD, std::fabs(got - 0.5 * r));
    }
    CHECK(dD < 1e-12, "down-sampler differs from a plain convolution by %g", dD);
}

static void testEnvFollower() {
    printf("-- envelope follower\n");
    const double fs = 48000.0; EnvFollower e; e.setTimes(5.0, 100.0, fs);
    double v = 0; int tA = -1; for (int i = 0; i < 48000 && tA < 0; ++i) { v = e.run(1.0); if (v >= 1.0 - std::exp(-1.0)) tA = i; }
    CHECK(std::fabs(tA / fs * 1000.0 - 5.0) < 0.3, "attack time constant %g ms (expected 5)", tA / fs * 1000.0);
    for (int i = 0; i < 48000; ++i) e.run(1.0);
    CHECK(std::fabs(e.e - 1.0) < 1e-6, "does not settle at the input level: %g", e.e);
    int tD = -1; for (int i = 0; i < 48000 * 2 && tD < 0; ++i) { v = e.run(0.0); if (v <= std::exp(-1.0)) tD = i; }
    CHECK(std::fabs(tD / fs * 1000.0 - 100.0) < 2.0, "decay time constant %g ms (expected 100)", tD / fs * 1000.0);
    EnvFollower f; f.setTimes(1.0, 50.0, fs); double mn = 1e9, mx = -1e9;
    for (int i = 0; i < 48000; ++i) { double y = f.run(0.5 * std::sin(2 * kPi * 440.0 * i / fs)); if (i > 24000) { mn = std::min(mn, y); mx = std::max(mx, y); } }
    CHECK(mn > 0.25 && mx < 0.5 + 1e-9, "follower on a 0.5 sine runs between %g and %g", mn, mx);
    EnvFollower z; z.setTimes(1.0, 50.0, fs); bool neg = false; for (int i = 0; i < 1000; ++i) if (z.run(-0.7) < 0) neg = true;
    CHECK(!neg, "follower went negative");
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
    testOversampler(); testGains(); testCutoffAndSlope(); testHighCutoffAndOverload(); testSampleRates(); testResonance(); testVca(); testStability(); testOutputSwitch(); testFm(); testRailKnee(); testSmoothTaper(); testSpeedUps(); testEnvFollower(); testSilenceAndReset();
    printf("\n%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
