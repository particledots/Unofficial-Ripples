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

// 2x oversampler: linear-phase half-band-style FIR (windowed sinc, cutoff fs/4 of the 2x rate)
class Oversampler2x {
public:
    static constexpr int kTaps = 63;                 // odd, symmetric
    Oversampler2x() {
        const int m = (kTaps - 1) / 2;
        double sum = 0.0;
        for (int i = 0; i < kTaps; ++i) {
            double x = i - m;
            double s = (x == 0.0) ? 0.5 : std::sin(kPi * x * 0.5) / (kPi * x);
            double w = 0.42 - 0.5 * std::cos(2.0 * kPi * i / (kTaps - 1)) + 0.08 * std::cos(4.0 * kPi * i / (kTaps - 1));
            h[i] = s * w; sum += h[i];
        }
        for (int i = 0; i < kTaps; ++i) h[i] *= 2.0 / sum;   // DC gain 2 on the zero-stuffed signal => unity after interpolation
        reset();
    }
    void reset() { upBuf.assign(kTaps, 0.0); dnBuf.assign(kTaps, 0.0); upPos = dnPos = 0; }
    static constexpr int latencySamples() { return kTaps - 1 == 62 ? 31 : (kTaps - 1) / 2; }   // host-rate samples: up (15.5) + down (15.5)
    // one host sample in -> two oversampled samples out
    void up(double x, double& o0, double& o1) {
        push(upBuf, upPos, x);  o0 = conv(upBuf, upPos);
        push(upBuf, upPos, 0.0); o1 = conv(upBuf, upPos);
    }
    // two oversampled samples in -> one host sample out
    double down(double a, double b) {
        push(dnBuf, dnPos, a);
        const double r = 0.5 * conv(dnBuf, dnPos);   // decimate: keep the output that lines up with the first of each pair
        push(dnBuf, dnPos, b);
        return r;
    }
private:
    double h[kTaps];
    std::vector<double> upBuf, dnBuf;
    int upPos = 0, dnPos = 0;
    void push(std::vector<double>& b, int& p, double x) { p = (p + 1) % kTaps; b[p] = x; }
    double conv(const std::vector<double>& b, int p) const {
        double a = 0.0;
        for (int i = 0; i < kTaps; ++i) a += h[i] * b[(p - i + kTaps) % kTaps];
        return a;
    }
};

// One channel of the filter, running at the (oversampled) rate it is given.
class Channel {
public:
    void reset() { s[0] = s[1] = s[2] = s[3] = 0.0; hpIn = hpVca = 0.0; iOta = 0.0; }
    // volts in; returns the selected output in volts. stageHz = pole frequency of each stage (<= kStageMaxHz),
    // iReso / iVca = OTA bias currents (amps), fs = rate this runs at.
    double process(double inV, Output out, double stageHz, double iReso, double iVca, double fs) {
        double o[4]; processAll(inV, stageHz, iReso, iVca, fs, o); return o[out];
    }
    // the same, but returns all four outputs (volts) so a caller can switch or fade between them
    void processAll(double inV, double stageHz, double iReso, double iVca, double fs, double* o) {
        const double wcT = std::min(stageHz, 0.45 * fs);
        const double g = std::tan(kPi * wcT / fs);
        const double G = g / (1.0 + g);
        // 2.4 Hz input high-pass (C11 220n with R15 300k, ahead of the OTA's + input) and 0.34 Hz (C16 4.7u, R20 100k)
        const double aIn = 1.0 - std::exp(-2.0 * kPi * 2.4 / fs);
        hpIn += aIn * (inV - hpIn);
        const double vplus = (inV - hpIn) * kInDiv;
        // linear part of each stage:  y_k = c_k - G * w_k  with  c_k = s_k * (1 - G)
        const double a1 = (kR_stage / kR_in) * inV;
        double c[4], p[4], q[4];
        for (int k = 0; k < 4; ++k) c[k] = s[k] * (1.0 - G);
        p[0] = c[0] - G * a1;           q[0] = -G * kR_stage;
        for (int k = 1; k < 4; ++k) { p[k] = c[k] - G * p[k - 1]; q[k] = -G * q[k - 1]; }
        // solve  I = Iabc * tanh((vplus - (p3 + q3*I) * kFbDiv) / (2Vt))  for I (monotone: Newton)
        double I = iOta;
        for (int it = 0; it < 8; ++it) {
            const double vd = vplus - (p[3] + q[3] * I) * kFbDiv;
            const double th = std::tanh(vd / kVt2);
            const double F  = I - iReso * th;
            const double dF = 1.0 + iReso * (1.0 - th * th) * (q[3] * kFbDiv / kVt2);
            const double step = F / dF;
            I -= step;
            if (std::fabs(step) < 1e-12) break;
        }
        iOta = I;
        double y[4];
        for (int k = 0; k < 4; ++k) {
            y[k] = p[k] + q[k] * I;
            y[k] = clipRail(y[k]);               // the op-amps stop at the supply rails
            s[k] = 2.0 * y[k] - s[k];            // trapezoidal state update
        }
        o[LP2] = clipRail(-(100e3 / 39e3) * y[1]);
        o[LP4] = clipRail(-(100e3 / 33e3) * y[3]);
        o[BP2] = clipRail(-(100e3 / 39e3) * (y[0] + y[1]));
        const double aV = 1.0 - std::exp(-2.0 * kPi * 0.34 / fs);
        hpVca += aV * (y[3] - hpVca);
        const double v = (y[3] - hpVca) * kVcaDiv;
        o[LP4_VCA] = clipRail(-100e3 * iVca * std::tanh(v / kVt2));
    }
private:
    double s[4] = {0, 0, 0, 0};
    double hpIn = 0, hpVca = 0, iOta = 0;
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
        const double hzA = std::max(1.0, std::min(stageHz * std::exp2(fmA), kStageMaxHz));
        const double hzB = std::max(1.0, std::min(stageHz * std::exp2(fmB), kStageMaxHz));
        double oa[4], ob[4];
        ch0.processAll(a, hzA, iq, iv, fs2, oa);
        ch0.processAll(b, hzB, iq, iv, fs2, ob);
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

} // namespace ripples
