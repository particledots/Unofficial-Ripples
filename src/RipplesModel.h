// RipplesModel.h - a circuit model of the ORIGINAL Ripples filter (schematic "ripples_v40", 2019), written from the published
// drawing. No JUCE in here, so it can be tested on its own. Header only.
//
// What is modelled (all values are read off the drawing; see README for the list of what was NOT checked):
//   * Four identical stages. Each is a V2164 VCA feeding a TL072 integrator with a 220 pF capacitor, so each stage is an INVERTING
//     one-pole low-pass:  dy/dt = -wc * (y + w)   with   wc = g / (33k * 220p),  g = VCA gain (1 = top of the range, 21.9 kHz).
//     Stage 1 sees   w1 = (33k/100k) * IN + 33k * I_ota ;  stages 2..4 see w = the previous stage's output.
//   * Resonance: an LM13700 OTA turns  (IN/301 [after a 2.4 Hz high-pass] - LP4/23)  into a current that is added at stage 1's input.
//     Its output current is  Iabc * tanh(vd / (2*Vt)).  The feedback is negative at DC (four inversions plus the OTA's inversion).
//   * Outputs (each an inverting op-amp):  LP2 = -(100k/39k)*y2,  LP4 = -(100k/33k)*y4,  BP2 = -(100k/39k)*(y1+y2),
//     LP4 VCA = -100k * Iabc_vca * tanh((HP(y4)/101) / (2*Vt)),  with an AC-coupling capacitor (4.7 uF) in front of the VCA.
//   * The op-amps clip at +-kRail volts. Audio level: 1.0 in the plug-in = kVoltsPerFS volts.
// The delay-free feedback loop (OTA <-> stages) is solved exactly each sample (Newton on one number), not with a one-sample delay.
#pragma once
#include <cmath>
#include <vector>
#include <algorithm>
#include <complex>

namespace ripples {

constexpr double kPi          = 3.14159265358979323846;
constexpr double kVoltsPerFS  = 10.0;     // ASSUMPTION: 0 dBFS = 10 V peak (not from the drawing); chosen so the supply rails sit at about full scale
constexpr double kRail        = 10.5;     // ASSUMPTION: TL072 output swing on +-12 V
constexpr double kR_stage     = 33e3;     // R8/R2/R6/R13 (feedback) and R1/R5/R12 (inter-stage)
constexpr double kR_in        = 100e3;    // R9
constexpr double kC_int       = 220e-12;  // C6/C1/C4/C9
constexpr double kStageMaxHz  = 1.0 / (2.0 * kPi * kR_stage * kC_int);   // about 21.9 kHz (VCA gain 1)
constexpr double kVt2         = 2.0 * 0.02585;                            // 2 * thermal voltage
constexpr double kFbDiv       = 1.0 / 23.0;     // R16 22k / R17 1k on LP4
constexpr double kInDiv       = 1.0 / 301.0;    // R15 300k / R14 1k on the input
constexpr double kVcaDiv      = 1.0 / 101.0;    // R20 100k / R21 1k
constexpr double kIQmax       = 193e-6;         // ASSUMPTION: Q = 1.0 gives this OTA current (puts self-oscillation near 75%)
constexpr double kIVcaMax     = 185e-6;         // ASSUMPTION: VCA level 1.0 (about 5 V into 27k)

enum Output { LP2 = 0, LP4 = 1, BP2 = 2, LP4_VCA = 3 };

// OTA bias current at which the feedback loop gain is exactly 1 (self-oscillation): gm * R8 / 23 / 4 = 1 with gm = Iabc / (2 Vt).
constexpr double kIThreshold  = (4.0 * 23.0 / kR_stage) * kVt2;      // about 144 uA

// "Smooth" Resonance taper (a choice, NOT what the drawing says: the drawing's Q control is linear in current).
// k = loop gain relative to self-oscillation (1 = starts to self-oscillate). Below the onset knob position the distance from
// self-oscillation (1 - k) falls exponentially with the knob, so the peak rises evenly across the knob instead of all at the end;
// above it k rises linearly to 1.15 at the top.
constexpr double kSmoothOnset = 0.88;
inline double smoothLoopGain(double q) {
    q = std::max(0.0, std::min(1.0, q));
    if (q >= kSmoothOnset) return 1.0 + (q - kSmoothOnset) * (0.15 / (1.0 - kSmoothOnset));
    const double c = 1.5;
    const double e0 = std::exp(-c * kSmoothOnset);
    return 1.0 - (std::exp(-c * q) - e0) / (1.0 - e0);
}

// The op-amps stop at the rails, but not like a brick wall: linear up to 70% of the rail, then a smooth bend (tanh) that reaches the rail.
inline double clipRail(double v) {
    const double a = 0.7 * kRail, b = kRail - a, m = std::fabs(v);
    if (m <= a) return v;
    const double r = a + b * std::tanh((m - a) / b);
    return v < 0 ? -r : r;
}

// Cheap stand-ins for tan and tanh (checked against the real ones in the tests).
// fastTan: Pade [5/4], error below 3e-5 (relative) up to x = 1.0 and below 1% up to x = 1.26 (the filter never goes beyond that: its pole frequency is limited to 0.4 of the sample rate).
inline double fastTan(double x) { const double x2 = x * x; return x * (945.0 - 105.0 * x2 + x2 * x2) / (945.0 - 420.0 * x2 + 15.0 * x2 * x2); }
// fastTanh: Pade [7/6], absolute error below 1e-4 everywhere (worst at the cut-off where it switches to +-1).
inline double fastTanh(double x) {
    const double ax = std::fabs(x);
    if (ax >= 4.97) return x < 0 ? -1.0 : 1.0;
    const double x2 = x * x;
    return x * (135135.0 + x2 * (17325.0 + x2 * (378.0 + x2))) / (135135.0 + x2 * (62370.0 + x2 * (3150.0 + 28.0 * x2)));
}

// 2x oversampler: linear-phase half-band FIR (windowed sinc, cutoff fs/4 of the 2x rate), written as two phases.
// For a half-band filter every second tap is zero, so one phase is a single tap (a pure delay) and the other has 32 taps.
class Oversampler2x {
public:
    static constexpr int kTaps = 63;                 // odd, symmetric
    static constexpr int kN = 32;                    // taps in the long phase
    Oversampler2x() {
        const int m = (kTaps - 1) / 2;
        double h[kTaps], sum = 0.0;
        for (int i = 0; i < kTaps; ++i) {
            double x = i - m;
            double s = (x == 0.0) ? 0.5 : std::sin(kPi * x * 0.5) / (kPi * x);
            double w = 0.42 - 0.5 * std::cos(2.0 * kPi * i / (kTaps - 1)) + 0.08 * std::cos(4.0 * kPi * i / (kTaps - 1));
            h[i] = s * w;
            if (i != m && ((i - m) % 2 == 0)) h[i] = 0.0;     // exactly zero in a half-band filter (rounding noise otherwise)
            sum += h[i];
        }
        for (int i = 0; i < kTaps; ++i) h[i] *= 2.0 / sum;   // DC gain 2 on the zero-stuffed signal => unity after interpolation
        for (int j = 0; j < kN; ++j) h0[j] = h[2 * j];        // even taps: all non-zero
        hc = h[m]; cj = (m - 1) / 2;                            // odd taps: only the centre one is non-zero (index m = 2*cj+1)
        reset();
    }
    void reset() { for (double& v : xa) v = 0.0; for (double& v : xb) v = 0.0; for (double& v : xd) v = 0.0; pa = pb = pd = 0; }
    static constexpr int latencySamples() { return 31; }   // host-rate samples: up (15.5) + down (15.5)
    // one host sample in -> two oversampled samples out
    void up(double x, double& o0, double& o1) {
        pa = (pa + 1) & (kN - 1); xa[pa] = xa[pa + kN] = x;
        const double* r = &xa[pa + kN];      // r[-j] = x[n-j]
        double a = 0.0;
        for (int j = 0; j < kN; ++j) a += h0[j] * r[-j];
        o0 = a;
        o1 = hc * r[-cj];
    }
    // two oversampled samples in -> one host sample out (the output that lines up with the first of each pair)
    double down(double a, double b) {
        pb = (pb + 1) & (kN - 1); xb[pb] = xb[pb + kN] = a;
        const double* ra = &xb[pb + kN];
        double s = 0.0;
        for (int j = 0; j < kN; ++j) s += h0[j] * ra[-j];
        s += hc * xd[(pd - cj + kN) & (kN - 1)];     // b[n-1-cj]: the history of b before the current one
        pd = (pd + 1) & (kN - 1); xd[pd] = b;
        return 0.5 * s;
    }
private:
    double h0[kN], hc = 0.0; int cj = 0;
    double xa[2 * kN], xb[2 * kN], xd[kN];
    int pa = 0, pb = 0, pd = 0;
};

// One channel of the filter, running at the (oversampled) rate it is given.
class Channel {
public:
    void reset() { s[0] = s[1] = s[2] = s[3] = 0.0; hpIn = hpVca = 0.0; iOta = 0.0; }
    // volts in; returns the selected output in volts. stageHz = pole frequency of each stage (<= kStageMaxHz),
    // iReso / iVca = OTA bias currents (amps), fs = rate this runs at.
    double process(double inV, Output out, double stageHz, double iReso, double iVca, double fs) {
        double o[4]; processAll(inV, stageHz, iReso, iVca, fs, o, 1u << out); return o[out];
    }
    // the same, but returns the outputs asked for in 'mask' (bit n = output n; the others are left alone), so a caller can switch or fade between them
    void processAll(double inV, double stageHz, double iReso, double iVca, double fs, double* o, unsigned mask = 0xF) {
        if (fs != lastFs) { lastFs = fs; aIn = 1.0 - std::exp(-2.0 * kPi * 2.4 / fs); aV = 1.0 - std::exp(-2.0 * kPi * 0.34 / fs); }
        const double wcT = std::min(stageHz, 0.4 * fs);
        const double g = fastTan(kPi * wcT / fs);
        const double G = g / (1.0 + g);
        // 2.4 Hz input high-pass (C11 220n with R15 300k, ahead of the OTA's + input) and 0.34 Hz (C16 4.7u, R20 100k)
        hpIn += aIn * (inV - hpIn);
        const double vplus = (inV - hpIn) * kInDiv;
        // linear part of each stage:  y_k = c_k - G * w_k  with  c_k = s_k * (1 - G)
        const double a1 = (kR_stage / kR_in) * inV;
        const double G1 = 1.0 - G;
        double p[4], q[4];
        p[0] = s[0] * G1 - G * a1;      q[0] = -G * kR_stage;
        for (int k = 1; k < 4; ++k) { p[k] = s[k] * G1 - G * p[k - 1]; q[k] = -G * q[k - 1]; }
        // solve  I = Iabc * tanh((vplus - (p3 + q3*I) * kFbDiv) / (2Vt))  for I (monotone: Newton, started from the last answer)
        double I = iOta;
        const double dvdI = q[3] * kFbDiv / kVt2;
        const double v0 = (vplus - p[3] * kFbDiv) / kVt2;
        for (int it = 0; it < 6; ++it) {
            const double th = fastTanh(v0 - dvdI * I);
            const double F  = I - iReso * th;
            const double dF = 1.0 + iReso * (1.0 - th * th) * dvdI;
            const double step = F / dF;
            I -= step;
            if (std::fabs(step) < 1e-10) break;
        }
        iOta = I;
        double y[4];
        for (int k = 0; k < 4; ++k) {
            y[k] = clipRail(p[k] + q[k] * I);        // the op-amps stop at the supply rails
            s[k] = 2.0 * y[k] - s[k];                // trapezoidal state update
        }
        if (mask & (1u << LP2)) o[LP2] = clipRail(-(100e3 / 39e3) * y[1]);
        if (mask & (1u << LP4)) o[LP4] = clipRail(-(100e3 / 33e3) * y[3]);
        if (mask & (1u << BP2)) o[BP2] = clipRail(-(100e3 / 39e3) * (y[0] + y[1]));
        hpVca += aV * (y[3] - hpVca);
        if (mask & (1u << LP4_VCA)) o[LP4_VCA] = clipRail(-100e3 * iVca * fastTanh((y[3] - hpVca) * kVcaDiv / kVt2));
    }
private:
    double s[4] = {0, 0, 0, 0};
    double hpIn = 0, hpVca = 0, iOta = 0;
    double lastFs = 0, aIn = 0, aV = 0;
};

// Envelope follower for the FM "Envelope" source: rises towards the rectified input with the attack time, falls with the decay time.
// Output is in the same units as the input (1.0 = full scale), never negative.
struct EnvFollower {
    double e = 0.0, aA = 0.0, aD = 0.0;
    void reset() { e = 0.0; }
    void setTimes(double attackMs, double decayMs, double fs) {
        aA = 1.0 - std::exp(-1.0 / (std::max(0.01, attackMs) * 0.001 * fs));
        aD = 1.0 - std::exp(-1.0 / (std::max(0.1, decayMs) * 0.001 * fs));
    }
    double run(double x) { x = std::fabs(x); e += (x > e ? aA : aD) * (x - e); return e; }
};

// What the plug-in uses: one channel at 2x oversampling, with the plug-in's units (Hz, 0..1 knobs, dB) and 1.0 = kVoltsPerFS volts.
// Changing the output fades between the old and the new one over about 10 ms (so the switch does not click).
// FM: fmOct is a frequency offset in octaves (positive = higher), applied to all four stages (exponential, like the hardware's V/oct summing).
// It is delayed by the oversampler's latency so that it lines up with the audio it belongs to.
class Voice {
public:
    static constexpr int kFmDelay = 31;     // = Oversampler2x::latencySamples()
    static_assert(kFmDelay == Oversampler2x::latencySamples(), "FM delay must equal the oversampler latency");
    bool smoothTaper = false;     // false: Resonance as the drawing has it (linear current); true: the smoother taper above
    void reset() { os.reset(); ch0.reset(); curOut = prevOut = LP4; xf = 1.0; for (double& d : fmBuf) d = 0.0; fmPos = 0; fmPrev = 0.0; }
    // stageHz: pole frequency of each stage. q, vca: 0..1. inGainLin / outGainLin: linear gains.
    float process(float in, Output out, double stageHz, double q, double vca, double inGainLin, double outGainLin, double hostFs, double fmOct = 0.0) {
        if (out != curOut) { prevOut = curOut; curOut = out; xf = 0.0; }
        // FM, delayed to match the audio path
        fmBuf[fmPos] = std::max(-8.0, std::min(8.0, fmOct));
        const int readPos = (fmPos - kFmDelay + kFmBufLen) % kFmBufLen;
        const double fmCur = fmBuf[readPos];
        fmPos = (fmPos + 1) % kFmBufLen;
        const double fmA = 0.5 * (fmPrev + fmCur), fmB = fmCur;
        fmPrev = fmCur;
        double a, b;
        os.up(in * inGainLin * kVoltsPerFS, a, b);
        const double fs2 = 2.0 * hostFs;
        const double qq = std::max(0.0, std::min(1.0, q));
        const double iq = smoothTaper ? smoothLoopGain(qq) * kIThreshold : qq * kIQmax;
        const double iv = std::max(0.0, std::min(1.0, vca)) * kIVcaMax;
        const double hzA = std::max(1.0, std::min(fmA == 0.0 ? stageHz : stageHz * std::exp2(fmA), kStageMaxHz));
        const double hzB = std::max(1.0, std::min(fmB == 0.0 ? stageHz : stageHz * std::exp2(fmB), kStageMaxHz));
        double oa[4], ob[4];
        const unsigned mask = (1u << curOut) | (xf < 1.0 ? (1u << prevOut) : 0u);
        ch0.processAll(a, hzA, iq, iv, fs2, oa, mask);
        ch0.processAll(b, hzB, iq, iv, fs2, ob, mask);
        double ya, yb;
        if (xf >= 1.0) { ya = oa[curOut]; yb = ob[curOut]; }
        else { ya = xf * oa[curOut] + (1.0 - xf) * oa[prevOut]; yb = xf * ob[curOut] + (1.0 - xf) * ob[prevOut]; xf = std::min(1.0, xf + 1.0 / (0.010 * hostFs)); }
        return (float) (os.down(ya, yb) * outGainLin / kVoltsPerFS);
    }
private:
    static constexpr int kFmBufLen = 64;
    Oversampler2x os;
    Channel ch0;
    Output curOut = LP4, prevOut = LP4;
    double xf = 1.0;
    double fmBuf[kFmBufLen] = {};
    int fmPos = 0;
    double fmPrev = 0.0;
};

// OTA current for a Resonance knob position (0..1) and taper (true = Smooth), as the Voice uses it.
inline double resoCurrent(double q, bool smooth) { q = std::max(0.0, std::min(1.0, q)); return smooth ? smoothLoopGain(q) * kIThreshold : q * kIQmax; }

// Small-signal response (linear theory), for the panel's curve and for the tests, which compare it with the real model.
// Gain from the input (volts) to the chosen output (volts) at frequency f. Excludes the plug-in's Input and Output gain.
// Each stage is H = 1/(1 + j f/f0). With K = gm * R * kFbDiv (gm = iReso / 2Vt) and Kin = gm * R * kInDiv:
//   w1 = (R/Rin * x + Kin * HP(x)) / (1 + K H^4);  LP1..LP4 = -H w1, +H^2 w1, -H^3 w1, +H^4 w1.
inline std::complex<double> response(Output out, double f, double stageHz, double iReso, double iVca) {
    using C = std::complex<double>;
    const C j(0.0, 1.0);
    const C H = 1.0 / (1.0 + j * (f / stageHz));
    const double gm = iReso / kVt2;
    const double K = gm * kR_stage * kFbDiv, Kin = gm * kR_stage * kInDiv;
    const C hpIn = (j * f) / (2.4 + j * f);
    const C w1 = ((kR_stage / kR_in) + Kin * hpIn) / (1.0 + K * H * H * H * H);
    const C y1 = -H * w1, y2 = H * H * w1, y4 = H * H * H * H * w1;
    switch (out) {
        case LP2: return -(100e3 / 39e3) * y2;
        case LP4: return -(100e3 / 33e3) * y4;
        case BP2: return -(100e3 / 39e3) * (y1 + y2);
        default: { const C hpV = (j * f) / (0.34 + j * f); return -100e3 * iVca * (y4 * hpV) * kVcaDiv / kVt2; }
    }
}

} // namespace ripples
