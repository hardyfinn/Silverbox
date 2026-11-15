// The whole 303 voice (spec §5): oscillator, diode ladder, MEG, VEG, accent sweep,
// slide, BASS COMP, KEY TRACK, and the SBX extensions. No Rack includes.
#pragma once
#include "Oversample.hpp"
#include "Osc303.hpp"
#include "DiodeLadder303.hpp"
#include "Envelopes303.hpp"

namespace sb {

struct VoiceParams {
	float tuning = 0.f;     // semitones, -12..12
	float cutoff = 0.5f;    // 0..1
	float resonance = 0.5f; // 0..1
	float envMod = 0.5f;    // 0..1
	float decay = 0.5f;     // 0..1
	float accent = 0.5f;    // 0..1
	bool square = false;
	float bassComp = 0.f;   // 0..1
	float keyTrack = 0.f;   // 0..1
};

// SBX, Silverbox Extended. Defaults are the stock behaviour.
struct VoiceExt {
	float vegDecayT = VEG_DECAY_T;      // s to -40 dB
	float accentDecayT = MEG_ACCENT_T;  // s to -40 dB
	bool decayWide = false;
	float slideTime = 0.060f;           // s at which a glide is 82% done
	float softAttack = VEG_ATTACK;      // s, VEG attack on non-accented notes
	float sweepSpeed = 1.f;             // multiplies the sweep time constants
	float filterFm = 0.f;               // 0..1
	float overdriveDb = 0.f;            // 0..24
	int muffler = 0;                    // 0 off, 1 soft, 2 hard
};

struct VoiceIn {
	float voct = 0.f;
	bool gate = false;
	bool accent = false;
	bool slide = false;
	float cutoffCv = 0.f;
	float audioIn = 0.f;
	bool audioPatched = false;
};

struct VoiceOut {
	float out = 0.f;     // volts
	float env = 0.f;     // 0..10 V
	float sweep = 0.f;   // 0..10 V
};

struct Voice303 {
	float fs = 48000.f, dt = 1.f / 48000.f;
	int factor = 4;
	OsKernel kernel;
	Upsampler up;
	Decimator dec;
	Osc303 osc;
	DiodeLadder303 ladder;
	OnePole inHp, outHp, bassLp1, bassLp2;
	float bassGain = BASS_GAIN, bassRatio = BASS_CORNER_RATIO;   // tunable for tests
	DecayEnv meg;
	AmpEnv veg;
	AccentSweep sweep;
	SlideLag slide;
	bool prevGate = false;
	bool accentLatched = false;
	float vegAttack = VEG_ATTACK;
	float accGain = 1.f;
	float prevVca = 0.f, prevFcOct = 9.f;
	float lastVcaOut = 0.f;
	float pitchV = 0.f;          // gliding pitch (for tests and KEY TRACK)
	float lastFcHz = 0.f;        // for tests

	Voice303() { setRate(48000.f); }

	void setRate(float sampleRate) {
		fs = sampleRate;
		dt = 1.f / fs;
		factor = oversampleFactor(fs);
		kernel.design(factor);
		float fsOs = fs * factor;
		osc.setRate(fsOs);
		ladder.setRate(fsOs);
		inHp.set(IN_HP, fsOs);
		outHp.set(OUT_HP, fs);
	}

	void reset() {
		up.reset(); dec.reset(); osc.reset(); ladder.reset();
		inHp.reset(); outHp.reset(); bassLp1.reset(); bassLp2.reset();
		meg = DecayEnv(); veg = AmpEnv(); sweep = AccentSweep(); slide = SlideLag();
		prevGate = false; accGain = 1.f; prevVca = 0.f; lastVcaOut = 0.f;
	}

	static float megDecayTime(float decay, bool wide) {
		decay = clampf(decay, 0.f, 1.f);
		if (wide) return 0.03f * std::pow(100.f, decay);               // 30 ms .. 3 s
		return MEG_DECAY_MIN * std::pow(MEG_DECAY_MAX / MEG_DECAY_MIN, decay);  // 0.2 .. 2 s
	}

	VoiceOut process(const VoiceIn& in, const VoiceParams& p, const VoiceExt& x) {
		VoiceOut o;
		// ---- control rate ----
		bool rise = in.gate && !prevGate;
		bool fall = !in.gate && prevGate;
		prevGate = in.gate;
		float slideTau = clampf(x.slideTime, 0.001f, 1.f) / 1.715f;   // e^-1.715 = 18% left
		pitchV = slide.process(dt, in.voct, in.slide, rise, slideTau);
		if (rise) {
			accentLatched = in.accent;
			meg.trigger();
			veg.trigger();
			vegAttack = in.accent ? VEG_ATTACK : x.softAttack;
		}
		if (fall) veg.release();
		float megT = accentLatched ? x.accentDecayT : megDecayTime(p.decay, x.decayWide);
		meg.process(dt, MEG_ATTACK, megT);
		veg.process(dt, vegAttack, x.vegDecayT);
		float res = clampf(p.resonance, 0.f, 1.f);
		sweep.process(dt, in.accent ? meg.v : 0.f, res, x.sweepSpeed);
		float accTarget = 1.f + (in.accent ? clampf(p.accent, 0.f, 1.f) * VCA_ACC_GAIN : 0.f);
		accGain += (accTarget - accGain) * tauCoef(0.002f, dt);
		float vca = veg.v * accGain;

		float fcOct = std::log2(CUT_LO) + clampf(p.cutoff, 0.f, 1.f) * std::log2(CUT_HI / CUT_LO);
		float em = clampf(p.envMod, 0.f, 1.f);
		fcOct += -em * ENV_SHIFT + em * ENV_OCT * meg.v;
		fcOct += clampf(p.accent, 0.f, 1.f) * ACC_OCT * sweep.s;
		fcOct += clampf(in.cutoffCv, -10.f, 10.f) / 5.f * 3.f;
		fcOct += clampf(p.keyTrack, 0.f, 1.f) * (pitchV - KEY_TRACK_REF);
		fcOct = clampf(fcOct, 4.f, 16.f);
		float freq = BASE_FREQ * std::exp2(clampf(pitchV + p.tuning / 12.f, -6.f, 7.f));
		float drive = DRIVE_IN * std::pow(10.f, clampf(x.overdriveDb, 0.f, 24.f) / 20.f);
		float fsOs = fs * factor;
		float fcHz = std::exp2(fcOct);
		float k = resonanceK(res, fcHz);
		float bc = std::fmin(bassRatio * fcHz, BASS_CORNER_MAX);
		bassLp1.set(bc, fsOs); bassLp2.g = bassLp1.g;
		float bass = clampf(p.bassComp, 0.f, 1.f) * std::pow(res, 0.8f) * bassGain;
		float fm = clampf(x.filterFm, 0.f, 1.f) * 2.f;

		// ---- audio rate, oversampled ----
		float inUp[OS_MAX];
		if (in.audioPatched) up.process(kernel, clampf(in.audioIn, -12.f, 12.f) / 5.f, inUp);
		float buf[OS_MAX];
		for (int i = 0; i < factor; i++) {
			float t = (i + 1.f) / factor;
			float vcaI = prevVca + (vca - prevVca) * t;
			float octI = prevFcOct + (fcOct - prevFcOct) * t;
			float src = in.audioPatched ? inUp[i] : osc.process(freq, p.square);
			float xin = inHp.hp(src) * drive;
			float hz = std::exp2(clampf(octI + fm * lastVcaOut, 4.f, 16.f));
			float y = ladder.process(xin, hz, k);
			if (bass > 0.f) {
				y += bass * bassLp2.lp(bassLp1.lp(xin));
				y = 2.5f * fastTanh(y / 2.5f);
			}
			float v = y * vcaI;
			lastVcaOut = v;
			if (x.muffler == 1) v = fastTanh(1.5f * v) / 1.5f;
			else if (x.muffler == 2) v = 0.6f * fastTanh(v / 0.6f);
			buf[i] = v;
		}
		prevVca = vca;
		prevFcOct = fcOct;
		lastFcHz = fcHz;
		float y = dec.process(kernel, buf);
		y = outHp.hp(y) * OUT_LEVEL;
		y = 10.f * fastTanh(y / 10.f);
		if (!std::isfinite(y)) { reset(); y = 0.f; }
		o.out = y;
		o.env = 10.f * meg.v;
		o.sweep = 10.f * clampf(sweep.s, 0.f, 1.f);
		return o;
	}
};

} // namespace sb
