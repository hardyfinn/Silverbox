// SBF, Silverbox Filter (round 2 spec §3): the back half of the voice. Input -> variable diode
// ladder (LP / BP / HP taps) -> VCA, with the MEG, the VEG and the accent circuit, and the board's
// fixed values made variable. No Rack includes. With every control at its stock position it
// processes a signal the way Voice303 does (test: equivalence within -60 dB).
#pragma once
#include <cstdint>
#include "Oversample.hpp"
#include "DiodeLadderX.hpp"
#include "Envelopes303.hpp"
#include "Voice303.hpp"
#include "ConstantsX.hpp"

namespace sb {

// VEG with a variable release (AmpEnv hard-codes VEG_RELEASE).
struct AmpEnvX {
	float v = 0.f;
	bool attacking = false;
	bool gateOn = false;
	void trigger() { attacking = true; gateOn = true; }
	void release() { gateOn = false; attacking = false; }
	void process(float dt, float attack, float decayT, float releaseT) {
		if (!gateOn) {
			v *= std::exp(-dt / releaseT);
		} else if (attacking) {
			v += (1.2f - v) * tauCoef(attack * 0.5581f, dt);
			if (v >= 1.f) { v = 1.f; attacking = false; }
		} else {
			v *= decayCoef(decayT, dt);
		}
		if (v < 1e-7f) v = 0.f;
	}
};

struct VbParams {
	float cutoff = 0.5f, resonance = 0.5f, envMod = 0.5f, decay = 0.5f, accent = 0.5f;
	int mode = MODE_LP;
	float driveDb = 0.f;          // 0..24
	float bassComp = 0.f;         // 0..1
	float keyTrack = 0.f;         // 0..1
	float fm = 0.f;               // 0..1
	float vcaDecayT = VEG_DECAY_T;      // s to -40 dB
	float accentDecayT = MEG_ACCENT_T;  // s to -40 dB
	// TRIM row
	float cutoffTrim = 0.f;       // octaves
	float resTrim = 1.f;          // 0.5..1.1
	float gateTrim = 0.f;         // s; 0 or less = OFF
	float bass = FB_HP;           // Hz, feedback high-pass
	float poles = STAGE1_RATIO;   // 1..3
	float envBias = ENV_SHIFT;    // octaves
	float envAttack = MEG_ATTACK; // s
	float release = VEG_RELEASE;  // s
	float accentVca = 1.f;        // 0..2
	bool sweepFree = false;
	float sweepShape = 0.5f;      // 0..1, used when sweepFree
	float click = 0.f;            // 0..1
};

struct VbIn {
	float in = 0.f;               // V
	bool gate = false, gatePatched = true;
	float trig = 0.f; bool trigPatched = false;
	float accent = 0.f;           // V (a level)
	float voct = 0.f;             // V, for KEY TRACK
	float cutoffCv = 0.f, resCv = 0.f, envModCv = 0.f, decayCv = 0.f, accentCv = 0.f;   // V
	float fm = 0.f;               // V
	float envIn = 0.f; bool envInPatched = false;
};

struct VbOut {
	float out = 0.f, filterOut = 0.f;   // V
	float filterEnv = 0.f, sweep = 0.f, vcaEnv = 0.f;   // 0..10 V
	bool gate = false, accent = false;  // lights
};

struct VoiceBackX {
	float fs = 48000.f, dt = 1.f / 48000.f;
	int factor = 4;
	OsKernel kernel;
	Upsampler up;
	Decimator dec, decFilt;
	DiodeLadderX ladder;
	OnePole inHp, outHp, filtHp, bassLp1, bassLp2, clickLp;
	DecayEnv meg;
	AmpEnvX veg;
	AccentSweep sweep;
	bool prevGate = false, prevTrig = false, trigHigh = false, accHigh = false;
	bool accentLatched = false;
	float strength = 1.f;
	float accGain = 1.f;
	float prevVca = 0.f, prevFcOct = 9.f, prevFm = 0.f;
	float sinceStart = 1e9f;      // s since the last envelope start
	float internalGate = 0.f;     // s left of a TRIG-made gate
	int curMode = MODE_LP, fromMode = MODE_LP;
	float xfade = 1.f;            // 0..1, progress of the tap crossfade
	float lastFcHz = 0.f, lastK = 0.f;   // for tests
	float prevVegClick = 0.f;
	uint32_t dither = 1u;

	VoiceBackX() { setRate(48000.f); }

	void setRate(float sampleRate) {
		fs = sampleRate;
		dt = 1.f / fs;
		factor = oversampleFactor(fs);
		kernel.design(factor);
		float fsOs = fs * factor;
		ladder.setRate(fsOs);
		inHp.set(IN_HP, fsOs);
		outHp.set(OUT_HP, fs);
		filtHp.set(OUT_HP, fs);
		clickLp.set(CLICK_LP, fs);
	}

	void reset() {
		up.reset(); dec.reset(); decFilt.reset(); ladder.reset();
		inHp.reset(); outHp.reset(); filtHp.reset(); bassLp1.reset(); bassLp2.reset(); clickLp.reset();
		meg = DecayEnv(); veg = AmpEnvX(); sweep = AccentSweep();
		prevGate = false; accGain = 1.f; prevVca = 0.f;
	}

	static float knobCv(float knob, float cv) { return clampf(knob + clampf(cv, -10.f, 10.f) / 5.f, 0.f, 1.f); }

	// osIn: optional input already at the oversampled rate (factor samples, about +-1), used by
	// tests to feed the oscillator exactly as Voice303 does. Otherwise IN is upsampled.
	VbOut process(const VbIn& in, const VbParams& p, bool wantFilterOut = true, const float* osIn = nullptr) {
		VbOut o;
		// ---- gate, trigger, accent ----
		bool trigRise = false;
		if (in.trigPatched) {
			float t = in.trig;
			if (trigHigh ? t <= 0.5f : t >= 1.f) trigHigh = !trigHigh;
			trigRise = trigHigh && !prevTrig;
			prevTrig = trigHigh;
		}
		bool gate = in.gate;
		if (!in.gatePatched) {
			if (trigRise) internalGate = p.gateTrim > 0.f ? p.gateTrim : TRIG_GATE_DEFAULT;
			gate = internalGate > 0.f;
			internalGate -= dt;
		}
		float accV = in.accent;
		if (accHigh ? accV <= 0.5f : accV >= 1.f) accHigh = !accHigh;
		if (accHigh) strength = clampf(accV / 5.f, ACC_STRENGTH_MIN, 1.f);
		bool rise = gate && !prevGate;
		bool fall = !gate && prevGate;
		prevGate = gate;
		bool start = rise || (trigRise && gate && in.gatePatched);
		if (start) {
			accentLatched = accHigh;
			meg.trigger();
			veg.trigger();
			sinceStart = 0.f;
		}
		if (fall) veg.release();
		if (p.gateTrim > 0.f && veg.gateOn && sinceStart > p.gateTrim) veg.release();
		sinceStart += dt;

		// ---- envelopes ----
		float decay = knobCv(p.decay, in.decayCv);
		float megT = accentLatched ? clampf(p.accentDecayT, 0.01f, 10.f) : Voice303::megDecayTime(decay, false);
		meg.process(dt, clampf(p.envAttack, 0.0005f, 2.f), megT);
		veg.process(dt, VEG_ATTACK, clampf(p.vcaDecayT, 0.005f, 20.f), clampf(p.release, 0.0005f, 5.f));
		float m = in.envInPatched ? clampf(in.envIn / 10.f, 0.f, 1.f) : meg.v;
		float res = knobCv(p.resonance, in.resCv);
		float acc = knobCv(p.accent, in.accentCv);
		float sweepRes = p.sweepFree ? clampf(p.sweepShape, 0.f, 1.f) : res;
		sweep.process(dt, accHigh ? m : 0.f, sweepRes, 1.f);
		float accTarget = 1.f + (accHigh ? acc * VCA_ACC_GAIN * clampf(p.accentVca, 0.f, 2.f) * strength : 0.f);
		accGain += (accTarget - accGain) * tauCoef(0.002f, dt);
		float vca = veg.v * accGain;

		// ---- cutoff ----
		float em = knobCv(p.envMod, in.envModCv);
		float fcOct = std::log2(CUT_LO_X) + knobCv(p.cutoff, 0.f) * std::log2(CUT_HI_X / CUT_LO_X) + p.cutoffTrim;
		fcOct += -em * p.envBias + em * ENV_OCT * m;
		fcOct += acc * ACC_OCT * sweep.s * strength;
		fcOct += clampf(in.cutoffCv, -10.f, 10.f) / 5.f * 3.f;
		fcOct += clampf(p.keyTrack, 0.f, 1.f) * (clampf(in.voct, -10.f, 10.f) - KEY_TRACK_REF);
		fcOct = clampf(fcOct, 4.f, 16.f);
		float fcHz = std::exp2(fcOct);
		LadderCoefs lc = LadderCoefs::at(p.poles);
		float bassHz = clampf(p.bass, BASS_MIN, BASS_MAX);
		ladder.setBass(bassHz);
		float k = std::pow(res, 0.8f) * RES_MAX * clampf(p.resTrim, 0.5f, 1.1f) * lc.kcrit(fcHz, bassHz);
		float fsOs = fs * factor;
		bool lp = p.mode == MODE_LP;
		float bc = std::fmin(BASS_CORNER_RATIO * fcHz, BASS_CORNER_MAX);
		float bass = lp ? clampf(p.bassComp, 0.f, 1.f) * std::pow(res, 0.8f) * BASS_GAIN : 0.f;
		if (bass > 0.f) { bassLp1.set(bc, fsOs); bassLp2.g = bassLp1.g; }
		float drive = DRIVE_IN * std::pow(10.f, clampf(p.driveDb, 0.f, 24.f) / 20.f);
		float fmNow = clampf(p.fm, 0.f, 1.f) * clampf(in.fm, -10.f, 10.f) / 5.f * 2.f;   // octaves
		bool selfOsc = p.resTrim > 1.f;

		// ---- output tap and its crossfade ----
		int mode = p.mode < 0 ? 0 : (p.mode > 2 ? 2 : p.mode);
		if (mode != curMode) { fromMode = curMode; curMode = mode; xfade = 0.f; }
		auto weights = [&](int md, float* ww) {
			tapWeights(md, lc, ww);
			float g = md == MODE_BP ? BP_LEVEL : (md == MODE_HP ? HP_LEVEL : 1.f);
			if (g != 1.f) for (int j = 0; j < 5; j++) ww[j] *= g;
		};
		float w[5];
		weights(curMode, w);
		if (xfade < 1.f) {
			float w0[5];
			weights(fromMode, w0);
			xfade = std::fmin(1.f, xfade + dt / TAP_XFADE);
			for (int j = 0; j < 5; j++) w[j] = w0[j] + (w[j] - w0[j]) * xfade;
		}

		// ---- audio rate, oversampled ----
		float inUp[OS_MAX];
		if (!osIn) up.process(kernel, clampf(in.in, -12.f, 12.f) / 5.f, inUp);
		float buf[OS_MAX], fbuf[OS_MAX];
		for (int i = 0; i < factor; i++) {
			float t = (i + 1.f) / factor;
			float vcaI = prevVca + (vca - prevVca) * t;
			float octI = prevFcOct + (fcOct - prevFcOct) * t;
			float fmI = prevFm + (fmNow - prevFm) * t;
			float src = osIn ? osIn[i] : inUp[i];
			float xin = inHp.hp(src) * drive;
			if (selfOsc) {   // a whisper of noise so an empty, self-oscillating filter starts
				dither ^= dither << 13; dither ^= dither >> 17; dither ^= dither << 5;
				xin += ((dither >> 9) * (1.f / 8388608.f) - 0.5f) * 1e-5f;
			}
			float hz = std::exp2(clampf(octI + fmI, 4.f, 16.f));
			ladder.process(xin, hz, k, lc);
			float y = ladder.tap(w);
			if (bass > 0.f) {
				y += bass * bassLp2.lp(bassLp1.lp(xin));
				y = 2.5f * fastTanh(y / 2.5f);
			}
			fbuf[i] = y;
			buf[i] = y * vcaI;
		}
		prevVca = vca;
		prevFcOct = fcOct;
		prevFm = fmNow;
		lastFcHz = fcHz;
		lastK = k;
		float y = dec.process(kernel, buf);
		y = outHp.hp(y) * OUT_LEVEL;
		if (p.click > 0.f) {
			// the VEG's attack edge leaking through an untrimmed VCA: band-limited rising edge
			// rate of rise, scaled so the stock 3 ms attack reads about 1 on average
			float d = std::fmax(0.f, veg.v - prevVegClick) * fs * VEG_ATTACK;
			y += clampf(p.click, 0.f, 1.f) * CLICK_LEVEL * clickLp.lp(d);
		}
		prevVegClick = veg.v;
		y = 10.f * fastTanh(y / 10.f);
		if (!std::isfinite(y)) { reset(); y = 0.f; }
		o.out = y;
		if (wantFilterOut) {
			float f = filtHp.hp(decFilt.process(kernel, fbuf)) * FILTER_OUT_LEVEL;
			o.filterOut = std::isfinite(f) ? 10.f * fastTanh(f / 10.f) : 0.f;
		}
		o.filterEnv = 10.f * m;
		o.sweep = 10.f * clampf(sweep.s, 0.f, 1.f);
		o.vcaEnv = 10.f * clampf(veg.v, 0.f, 1.f);
		o.gate = gate;
		o.accent = accHigh;
		return o;
	}
};

} // namespace sb
