// Every tunable constant of the voice model, in one place (spec §0.3 and §12).
// "est." = this build's estimate, not a measured value. Change by ear here.
#pragma once

namespace sb {

constexpr float PI_F = 3.14159265358979f;

// ---- pitch ------------------------------------------------------------
constexpr float BASE_FREQ = 65.406f;      // 0 V = C2 (spec §2)
constexpr float SLIDE_TAU = 0.035f;       // s; ~82% of the way after 60 ms (est.)
constexpr float PITCH_CHANGE_V = 0.0005f; // a target move bigger than this is a "pitch change" (spec §5.6)

// ---- oscillator (spec §5.1) --------------------------------------------
constexpr float K_SAW = 0.6f;             // curvature of the capacitor-charge ramp (est.)
constexpr float SQ_GAIN = 6.f;            // transistor shaper gain (est.)
constexpr float SQ_OFFSET = -0.08f;       // shaper threshold: duty about 46% (est.)
constexpr float SQ_LP = 7000.f;           // Hz, slanted edges (est.)
constexpr float SQ_HP = 20.f;             // Hz, top droop (est.)
constexpr float SQ_LEVEL = 0.55f;         // square level relative to saw (est.)

// ---- diode ladder (spec §5.2) -------------------------------------------
// Linear model: dy/dt = w * (A y + B u) with
//   A = [[-c1, c1, 0, 0], [1, -2, 1, 0], [0, 1, -2, 1], [0, 0, 1, -2]],  B = [c1, 0, 0, 0]
// c1 is the bottom stage's capacitor ratio. For c1 = 1.8 the loop phase reaches -180 deg
// at w * 1.3775 and needs k = 17.03 to self-oscillate (scripts/ladder_calibration.py).
constexpr float STAGE1_RATIO = 1.8f;      // (est.)
constexpr float LADDER_W180 = 1.3775f;    // resonant frequency in units of w, for STAGE1_RATIO
constexpr float LADDER_KCRIT = 17.03f;    // feedback for self-oscillation (high cutoff, no HP)
// With the feedback high-pass the feedback needed to oscillate rises at low cutoffs:
// kcrit(fc) = LADDER_KCRIT * (1 + 1.412/r + 0.576/r^2), r = fc / FB_HP (fit, scripts/ladder_calibration.py).
// The resonance knob is scaled to that, but only down to KCOMP_MIN_FC, so resonance still
// thins out at low cutoffs as on the original.
constexpr float KCOMP_MIN_FC = 600.f;     // Hz
constexpr float RES_MAX = 0.995f;         // full RESONANCE = this fraction of kcrit: rings long, does not sustain
constexpr float K_MAX = RES_MAX * LADDER_KCRIT;   // the largest feedback ever used (high cutoffs)
constexpr float FB_HP = 150.f;            // Hz, high-pass inside the feedback path (est.)
constexpr float IN_HP = 30.f;             // Hz, input coupling (est.)
constexpr float OUT_HP = 40.f;            // Hz, output coupling after the VCA (est.)
constexpr float DRIVE_IN = 1.0f;          // level into the ladder (est.)
constexpr float SAT_LEVEL = 1.6f;         // soft saturation of the ladder input node (est.)

// ---- cutoff law (spec §5.3). Frequencies are the resonant peak. --------------
constexpr float CUT_LO = 125.f;           // Hz at CUT OFF FREQ = 0. Range centred on 500 Hz at half way (service-note TM3 calibration, 2.1.0)
constexpr float CUT_HI = 2000.f;          // Hz at CUT OFF FREQ = 1. Was 300 Hz to 5 kHz before 2.1.0
constexpr float ENV_OCT = 4.0f;           // octaves of MEG sweep at full ENV MOD (est.)
constexpr float ENV_SHIFT = 1.2f;         // octaves the resting cutoff drops at full ENV MOD (est.)
constexpr float ACC_OCT = 2.5f;           // octaves of accent sweep at full ACCENT (est.)
constexpr float KEY_TRACK_REF = 1.f;      // V; KEY TRACK pivots here (C3)

// ---- BASS COMP (spec §5.2a) ----------------------------------------------
// The lost low end is added back from the ladder's input, through a 2-pole low-pass at
// min(BASS_CORNER_RATIO * fc, BASS_CORNER_MAX), scaled by how much feedback is in use.
constexpr float BASS_CORNER_RATIO = 0.1f;
constexpr float BASS_CORNER_MAX = 250.f;  // Hz (est.)
constexpr float BASS_GAIN = 1.1f;         // tuned to the ±2 dB target in tests/test_voice.cpp

// ---- envelopes (spec §5.4) -----------------------------------------------
constexpr float MEG_ATTACK = 0.003f;      // s
constexpr float MEG_DECAY_MIN = 0.2f;     // s to -40 dB at DECAY = 0
constexpr float MEG_DECAY_MAX = 2.0f;     // s to -40 dB at DECAY = 1
constexpr float MEG_ACCENT_T = 0.2f;      // s, forced decay on accented notes
constexpr float VEG_ATTACK = 0.003f;      // s
constexpr float VEG_DECAY_T = 3.5f;       // s to -40 dB (est.)
constexpr float VEG_RELEASE = 0.004f;     // s, VCA close on gate off (est.)

// ---- accent (spec §5.5) --------------------------------------------------
constexpr float SWEEP_TC_MIN = 0.0008f;    // s, sweep cap charge at RESONANCE 0 (est.)
constexpr float SWEEP_TC_MAX = 0.080f;    // s, at RESONANCE 1 (est.)
constexpr float SWEEP_TD_MIN = 0.030f;    // s, discharge at RESONANCE 0 (est.)
constexpr float SWEEP_TD_MAX = 0.450f;    // s, at RESONANCE 1 (est.)
constexpr float VCA_ACC_GAIN = 0.6f;      // VCA boost at full ACCENT (about +4 dB) (est.)

// ---- output ----------------------------------------------------------------
constexpr float OUT_LEVEL = 32.0f;        // volts per unit ladder output (tuned so a default saw peaks near 5 V)

} // namespace sb
