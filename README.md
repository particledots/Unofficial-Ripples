# Unofficial Ripples — AU / VST3 (experiment)

A software model of the **original (2019) Ripples filter circuit**, built from its published schematic (`ripples_v40`). There is no firmware in the
hardware, so this is not a port of code: it is a model of the circuit, written from the drawing. Not a Mutable Instruments product, and not made or
endorsed by the module's makers.

**Status, plainly:**
* Built and tested on Linux with the tests listed below. See the bottom of this file for the macOS build result.
* **Not yet loaded in Logic. Not yet listened to. Nobody has compared it with the hardware.** The tests check that the model does what the *drawing* says
  it should (gain, slopes, where it self-oscillates). They cannot say whether it sounds like the real module. That is for Bryn to judge by ear.

## What it models

* **Four identical stages.** Each is a V2164 VCA feeding an op-amp integrator with a 220 pF capacitor, which makes an *inverting one-pole low-pass*.
  All four share the control, so the **Frequency** knob is the pole frequency of each stage (about 5 Hz up to 21.9 kHz, which is the top of the range set by the 33k and 220p parts).
* **Resonance.** An LM13700 turns (input/301 minus LP4/23) into a current added at the first stage's input; the feedback is negative at low frequency.
  Its output is a tanh (it saturates), which limits the size of the self-oscillation. The input is also sent through the OTA, as in the drawing.
  The feedback loop has no delay: it is solved exactly for every sample.
* **Outputs:** LP2 (12 dB low-pass, inverted), LP4 (24 dB low-pass, inverted), BP2 (LP1 + LP2, inverted; it has no bass), and **LP4 through the VCA** (AC-coupled, controlled by **VCA level**).
  The polarity is the hardware's (each output stage inverts); it is not audible on its own.
* The op-amps stop at the supply rails (modelled as ±10.5 V): linear up to 70% of the rail, then a smooth bend, not a brick wall.
* Runs at 2× oversampling (a 63-tap filter; the plug-in reports about 31 samples of latency to the host).

## Things that are guesses, not read from the drawing

1. **Level:** 0 dBFS = 10 V, so the rails (10.5 V) sit just above full scale and the plug-in cannot go far over 0 dBFS. (The module has no such number; I chose it. The first two builds used 5 V, which let the output run up to +6 dBFS.) Input gain and Output gain let you move it.
2. **Rails:** ±10.5 V, with the soft bend above 70%.
3. **Resonance knob scaling (setting "As drawn"):** the drawing's Q control chain (op-amp, PNP transistor, offset) was not worked through in detail. The knob is scaled so that **self-oscillation starts at 0.75**,
   from a loop-gain estimate (four equal poles give loop gain 1 when the OTA current is about 145 µA, and I set 100% = 193 µA). So "starts at 0.75" in the tests confirms the maths, not the hardware.
4. **VCA level scaling:** 100% = 185 µA OTA bias (roughly 5 V into the 27k input resistor). The hardware's own VCA CV chain was not modelled.
5. **OTA input:** the LM13700's linearising diodes are not modelled (it is a plain tanh).
6. **Frequency control:** the hardware's V/oct input, CV attenuator and temperature-compensating network are not modelled; Frequency is a direct pole frequency in Hz.
7. Resistor and capacitor values were read off the PDF picture. They have not been checked against the parts list. Real parts have tolerances and the VCA chips have their own quirks, which are not modelled.

## Controls

| Control | What it does |
|---|---|
| Frequency | Pole frequency of each of the four stages (5 Hz – 21.9 kHz, log) |
| Resonance | 0–1; the pitch of the self-oscillation is the Frequency setting. Where it starts depends on **Resonance taper** |
| Resonance taper | **Smooth** (default): the peak rises evenly across the knob and self-oscillation starts near 88%. **As drawn**: linear, follows the schematic, self-oscillation at 75% with most of the resonance packed into the last few percent |
| Output | LP2, LP4, BP2, LP4 through VCA (a switch fades over 10 ms) |
| FM amount | 0–1, how far the **sidechain** signal moves the frequency (see below) |
| VCA level | 0–1, only for the "LP4 through VCA" output |
| Input gain, Output gain | ±24 dB |

The panel is a simple custom one (knobs, four output buttons). Every control is an ordinary parameter, so all of them can be automated in Logic.

## Resonance taper: Smooth vs As drawn

Build 2 had only the drawn (linear) taper. Bryn found it harsh and droney just below 3 o'clock, "like a fade-in rather than real resonance", while the Blades plug-in's Resonance (which is exponential, with self-oscillation at about 86%) sounded good. The drawing's Q control is linear in current, so **As drawn** is the faithful setting.
**Smooth** is a deliberate departure from the drawing, borrowed from how the Blades model behaves: the distance from self-oscillation shrinks exponentially with the knob, so the resonant peak rises evenly (measured in the model: about +6, +12, +19, +32 dB at 20, 40, 60, 80% above the knob-zero level) and self-oscillation only starts at 88%, rising to 15% above threshold at 100%.
Whether either is closer to the hardware is for Bryn to judge by ear.

## FM input

The original has no FM jack. It has a CV input with a level knob that moves the frequency, which is what FM does, so this plug-in has one. **FM AMOUNT** sets how much; **FM SOURCE** picks what does the modulating:

* **Sidechain:** another track goes into the plug-in's sidechain input (in Logic: the **Side Chain** menu in the plug-in window's header bar, next to the View menu).
* **Own input:** the sound going into the filter also modulates its frequency (audio-rate self-FM). This needs no routing, so use it first to hear what FM does.

The signal moves the frequency of all four stages exponentially, in octaves (like the hardware's 1 V/oct summing). FM AMOUNT 100% = 5 octaves for a full-scale signal. Positive = higher. A stereo sidechain is averaged to mono.
The FM signal is delayed to line up with the audio it belongs to. **Guesses:** the scale and the plain linear sum in octaves; the hardware's CV attenuator and exponential converter were not modelled in detail.
**Logic caches what a plug-in offers, by version number.** Each build that changes the plug-in's inputs raises the version (this one is 0.4.0). If the Side Chain menu is missing: Logic Pro > Settings > Plug-in Manager, select Unofficial Ripples, click **Reset & Rescan Selection**.
Not yet tried in Logic: the sidechain and Own input.

## Tests

`tests/ripples_test.cpp` (1323 checks, no JUCE): oversampler gain and delay; gain and polarity of each output; BP2 has no DC; slopes of 12 and 24 dB per octave; −6 dB / −12 dB at the stage
frequency for LP2 / LP4; the same answers at 44.1, 48 and 96 kHz; resonance (dies away below the threshold, keeps going above it, pitch follows Frequency within 5%, peak grows with Q, the output stays bounded);
the VCA output scales with its control and blocks DC; no crash, NaN or runaway with random extreme settings at 44.1 / 96 / 192 kHz; rail clipping; output switching without a click; FM (+1 octave equals doubling the frequency, −1 halves it, an FM step lines up with the audio, audio-rate FM stays bounded); the rail bend (linear to 70%, continuous, symmetric, never past the rail); the smooth taper (zero at 0, threshold at 88%, rising, an even rise, no oscillation before 88%, oscillation after, the drawn setting unchanged); silence and reset.

**Deliberate breaks** (a change made to a copy, to check the tests notice): wrong feedback sign, wrong stage resistor, wrong LP2 ratio, BP2 missing LP2, no input path through the OTA, no frequency warping, VCA output not AC-coupled,
no rail clipping, wrong Q scale, wrong state update, no fade on output change, FM too weak, FM upside down, FM not delayed to match the audio, hard rails, rails bending too early, wrong taper onset, taper not used, taper too steep. Each is caught by at least one check.

CPU on the Linux test machine: about 6% of one core for a stereo instance at 48 kHz (not measured on a Mac).

## Build

GitHub Actions, macOS runner, universal (arm64 + x86_64), minimum macOS 11. Download the **Unofficial-Ripples-macOS** artifact from the latest passing run, then follow `INSTALL.txt` inside.
Run the tests yourself: `c++ -std=c++17 -O2 -Isrc tests/ripples_test.cpp -o ripples_test && ./ripples_test`.

## Licence and credit

The model is written from the published hardware design files of the original Ripples (design by Émilie Gillet), which are released under **CC BY-SA 3.0**. A plugin built from the schematic is, in my reading,
an adaptation, so credit is given here and any redistribution should keep the same licence (CC BY-SA 3.0) for the modelling code. That is my reading, not legal advice; it should be settled before the plug-in is shared with anyone.
No code from the module's firmware is used (there is none). JUCE is used under its own terms (AGPLv3 / commercial).
