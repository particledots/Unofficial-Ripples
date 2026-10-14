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
* The op-amps stop at the supply rails (modelled as ±10.5 V).
* Runs at 2× oversampling (a 63-tap filter; the plug-in reports about 31 samples of latency to the host).

## Things that are guesses, not read from the drawing

1. **Level:** 0 dBFS = 5 V. (The module has no such number; I chose it.) Input gain and Output gain let you move it.
2. **Rails:** ±10.5 V clipping.
3. **Resonance knob scaling:** the drawing's Q control chain (op-amp, PNP transistor, offset) was not worked through in detail. The knob is scaled so that **self-oscillation starts at 0.75**,
   from a loop-gain estimate (four equal poles give loop gain 1 when the OTA current is about 145 µA, and I set 100% = 193 µA). So "starts at 0.75" in the tests confirms the maths, not the hardware.
4. **VCA level scaling:** 100% = 185 µA OTA bias (roughly 5 V into the 27k input resistor). The hardware's own VCA CV chain was not modelled.
5. **OTA input:** the LM13700's linearising diodes are not modelled (it is a plain tanh).
6. **Frequency control:** the hardware's V/oct input, CV attenuator and temperature-compensating network are not modelled; Frequency is a direct pole frequency in Hz.
7. Resistor and capacitor values were read off the PDF picture. They have not been checked against the parts list. Real parts have tolerances and the VCA chips have their own quirks, which are not modelled.

## Controls

| Control | What it does |
|---|---|
| Frequency | Pole frequency of each of the four stages (5 Hz – 21.9 kHz, log) |
| Resonance | 0–1; self-oscillation begins at 0.75 and the pitch is the Frequency setting |
| Output | LP2, LP4, BP2, LP4 through VCA (a switch fades over 10 ms) |
| VCA level | 0–1, only for the "LP4 through VCA" output |
| Input gain, Output gain | ±24 dB |

The interface is JUCE's plain automatic one (sliders and a menu), so every control can be automated in Logic.

## Tests

`tests/ripples_test.cpp` (47 checks, no JUCE): oversampler gain and delay; gain and polarity of each output; BP2 has no DC; slopes of 12 and 24 dB per octave; −6 dB / −12 dB at the stage
frequency for LP2 / LP4; the same answers at 44.1, 48 and 96 kHz; resonance (dies away below the threshold, keeps going above it, pitch follows Frequency within 5%, peak grows with Q, the output stays bounded);
the VCA output scales with its control and blocks DC; no crash, NaN or runaway with random extreme settings at 44.1 / 96 / 192 kHz; rail clipping; output switching without a click; silence and reset.

**Deliberate breaks** (a change made to a copy, to check the tests notice): wrong feedback sign, wrong stage resistor, wrong LP2 ratio, BP2 missing LP2, no input path through the OTA, no frequency warping, VCA output not AC-coupled,
no rail clipping, wrong Q scale, wrong state update, no fade on output change. Each is caught by at least one check.

CPU on the Linux test machine: about 6% of one core for a stereo instance at 48 kHz (not measured on a Mac).

## Build

GitHub Actions, macOS runner, universal (arm64 + x86_64), minimum macOS 11. Download the **Unofficial-Ripples-macOS** artifact from the latest passing run, then follow `INSTALL.txt` inside.
Run the tests yourself: `c++ -std=c++17 -O2 -Isrc tests/ripples_test.cpp -o ripples_test && ./ripples_test`.

## Licence and credit

The model is written from the published hardware design files of the original Ripples (design by Émilie Gillet), which are released under **CC BY-SA 3.0**. A plugin built from the schematic is, in my reading,
an adaptation, so credit is given here and any redistribution should keep the same licence (CC BY-SA 3.0) for the modelling code. That is my reading, not legal advice; it should be settled before the plug-in is shared with anyone.
No code from the module's firmware is used (there is none). JUCE is used under its own terms (AGPLv3 / commercial).
