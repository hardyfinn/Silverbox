// SBO, Silverbox Oscillator (round 2 spec §2): the 303 oscillator with variable pulse width,
// saw curvature and square shaper gain, a divider sub-octave, hard sync, linear FM, slide,
// drift and a wavefolder. No Rack includes. Osc303.hpp stays untouched; at the stock settings
// OscCoreX produces the same saw and square samples as Osc303.
#pragma once
#include <cstdint>
#include "Oversample.hpp"
#include "Blep.hpp"
#include "Filters1.hpp"
#include "Envelopes303.hpp"
#include "Folder.hpp"
#include "ConstantsX.hpp"

namespace sb {

// The saw's capacitor-charge shape with curvature K (Osc303::sawShape is K = K_SAW).
struct SawCurve {
	float K = K_SAW;
	float norm = 1.f / (1.f - std::exp(-K_SAW));
	bool linear = false;
	void set(float k) {
		if (k == K) return;
		K = k;
		linear = k < 1e-4f;
		norm = linear ? 1.f : 1.f / (1.f - std::exp(-K));
	}
	float shape(float p) const {
		float c = linear ? p : (1.f - std::exp(-K * p)) * norm;
		return 1.f - 2.f * c;
	}
};

struct OscCoreX {
	float phase = 0.f;
	Blep sawBlep, subBlep;
	OnePole sqLp, sqHp, subLp;
	float fs = 192000.f;
	bool subHigh = true;
	float lastShaped = 0.f;
	float saw = 0.f, square = 0.f, sub = 0.f;

	void setRate(float fsOs) {
		fs = fsOs;
		sqLp.set(SQ_LP, fs);
		sqHp.set(SQ_HP, fs);
		subLp.set(SQ_LP, fs);
	}
	void reset() {
		phase = 0.f; sawBlep = Blep(); subBlep = Blep();
		sqLp.reset(); sqHp.reset(); subLp.reset(); subHigh = true;
	}

	// freq: Hz. threshold: square shaper threshold. sqGain: SQUARE SHAPE.
	// syncAt: if >= 0, a hard-sync reset happens this sample, that fraction of the sample before its end.
	void process(float freq, const SawCurve& sc, float threshold, float sqGain, float syncAt = -1.f) {
		float dt = clampf(freq / fs, 0.f, 0.45f);
		phase += dt;
		if (phase >= 1.f) {
			phase -= 1.f;
			float a = dt > 0.f ? phase / dt : 0.f;
			a = clampf(a, 0.f, 0.999f);
			sawBlep.add(2.f, a);
			toggleSub(a);
		}
		if (syncAt >= 0.f) {
			// phase at the moment of the reset, then restart from 0
			float a = clampf(syncAt, 0.f, 0.999f);
			float before = phase - a * dt;
			if (before < 0.f) before += 1.f;
			float jump = sc.shape(0.f) - sc.shape(before);
			if (std::fabs(jump) > 1e-6f) {
				sawBlep.add(jump, a);
				if (before > 0.5f) toggleSub(a);   // a reset late in the cycle counts as a wrap for the divider
			}
			phase = a * dt;
		}
		saw = sawBlep.process(sc.shape(phase));
		lastShaped = fastTanh(sqGain * (saw - threshold));
		square = sqHp.hp(sqLp.lp(lastShaped)) * SQ_LEVEL;
		sub = subLp.lp(subBlep.process(subHigh ? 1.f : -1.f)) * SBO_SUB_LEVEL;
	}

private:
	void toggleSub(float a) {
		subBlep.add(subHigh ? -2.f : 2.f, a);
		subHigh = !subHigh;
	}
};

// Square threshold for a wanted duty d: sawShape(d) at the current SAW CURVE, plus the tiny
// constant that makes the stock duty at the stock curve give exactly SQ_OFFSET (SBV's square).
inline float pulseThreshold(const SawCurve& sc, float duty) {
	static const SawCurve stock;
	return SQ_OFFSET + (sc.shape(duty) - stock.shape(SBO_PW_STOCK));
}

struct OscXParams {
	float tuning = 0.f;         // semitones
	float pulseWidth = SBO_PW_STOCK;
	float pwm = 0.f;            // -1..1, depth of the PWM input
	float waveMix = 0.f;        // 0 saw .. 1 square
	float sub = 0.f;            // 0..1 into OUT
	float fold = 0.f;           // 0..1
	float foldCv = 0.f;         // -1..1, depth of the FOLD input
	float symmetry = 0.f;       // -1..1
	float fm = 0.f;             // 0..1
	float slideTime = 0.060f;   // s
	float drift = 0.f;          // 0..1
	float tune = 0.f;           // cents, -100..100
	float width = 1.f;          // V/oct scale
	float sawCurve = K_SAW;     // 0..3
	float squareShape = SQ_GAIN;   // 1.5..20
};

struct OscXIn {
	float voct = 0.f;
	bool slide = false;
	float pwm = 0.f;            // V
	float mixCv = 0.f;          // V
	float fold = 0.f;           // V
	float fm = 0.f;             // V
	float sync = 0.f;           // V
	bool syncPatched = false;
	float foldIn = 0.f;         // V
	bool foldInPatched = false;
};

struct OscXOut {
	float saw = 0.f, square = 0.f, sub = 0.f, out = 0.f;   // volts
};

// Which outputs to compute (decimating costs CPU).
enum { OX_SAW = 1, OX_SQUARE = 2, OX_SUB = 4, OX_OUT = 8, OX_ALL = 15 };

// White noise through two one-pole low-passes at DRIFT_LP Hz, normalised to about +-1.
struct DriftWalk {
	uint32_t state = 22222u;
	float a = 0.f, y1 = 0.f, y2 = 0.f, norm = 1.f;
	void seed(uint32_t s) { state = s ? s : 1u; y1 = y2 = 0.f; }
	void setRate(float fs) {
		a = 1.f - std::exp(-2.f * PI_F * DRIFT_LP / fs);
		// variance of two cascaded one-poles a / (1 - p z^-1) on unit-variance white noise:
		// a^4 (1 + p^2) / (1 - p^2)^3
		double p = 1.0 - a;
		double var = std::pow((double)a, 4) * (1 + p * p) / std::pow(1 - p * p, 3);
		norm = (float)(0.4 / std::sqrt(var));   // standard deviation 0.4
	}
	float process() {
		state ^= state << 13; state ^= state >> 17; state ^= state << 5;
		float w = ((state >> 8) * (1.f / 16777216.f) - 0.5f) * 3.4641016f;   // uniform, unit variance
		y1 += a * (w - y1);
		y2 += a * (y1 - y2);
		return fastTanh(y2 * norm);
	}
};

struct OscX {
	float fs = 48000.f, dt = 1.f / 48000.f;
	int factor = 4;
	OsKernel kernel;
	Upsampler foldInUp;
	Decimator decSaw, decSq, decSub, decOut;
	OscCoreX core;
	SawCurve curve;
	Folder folder;
	SlideLag slide;
	DriftWalk walk;
	float pitchV = 0.f;
	float prevSync = 0.f;
	float prevFm = 0.f, prevPwm = 0.f;
	float lastFreq = 0.f;       // for tests: pitch before FM
	float lastF = 0.f;          // for tests: frequency of the last sub-sample, after FM

	OscX() { setRate(48000.f); }

	void setRate(float sampleRate) {
		fs = sampleRate;
		dt = 1.f / fs;
		factor = oversampleFactor(fs);
		kernel.design(factor);
		core.setRate(fs * factor);
		folder.setRate(fs * factor);
		walk.setRate(fs);
	}
	void seed(uint32_t s) { walk.seed(s); }

	OscXOut process(const OscXIn& in, const OscXParams& p, int want = OX_ALL) {
		// ---- control rate ----
		float slideTau = clampf(p.slideTime, 0.001f, 1.f) / 1.715f;
		pitchV = slide.process(dt, in.voct, in.slide, false, slideTau);
		float drift = 0.f;
		if (p.drift > 0.f) drift = p.drift * p.drift * DRIFT_OCT * walk.process();
		float oct = clampf(p.width, 0.5f, 2.f) * pitchV + (p.tuning + p.tune / 100.f) / 12.f + drift;
		float freq = BASE_FREQ * std::exp2(clampf(oct, -6.f, 7.f));
		lastFreq = freq;
		curve.set(clampf(p.sawCurve, 0.f, 3.f));
		float sqGain = clampf(p.squareShape, 1.f, 30.f);
		float pwmNow = clampf(in.pwm, -10.f, 10.f);
		float fmNow = clampf(in.fm, -10.f, 10.f);
		float m = clampf(p.waveMix + clampf(in.mixCv, -10.f, 10.f) / 5.f, 0.f, 1.f);
		float fold = clampf(p.fold + p.foldCv * clampf(in.fold, -10.f, 10.f) / 5.f, 0.f, 1.f);
		float subMix = clampf(p.sub, 0.f, 1.f);
		// hard sync: rising crossing through 0 V between the previous and this host sample
		float syncPos = -1.f;   // 0..1 within the host sample interval
		if (in.syncPatched) {
			float s = clampf(in.sync, -12.f, 12.f);
			if (prevSync < 0.f && s >= 0.f) syncPos = prevSync / (prevSync - s);
			prevSync = s;
		}
		float foldUp[OS_MAX];
		if (in.foldInPatched) foldInUp.process(kernel, clampf(in.foldIn, -12.f, 12.f) / 5.f, foldUp);

		// ---- oversampled ----
		float bs[OS_MAX], bq[OS_MAX], bu[OS_MAX], bo[OS_MAX];
		for (int i = 0; i < factor; i++) {
			float t = (i + 1.f) / factor;
			float pwmI = prevPwm + (pwmNow - prevPwm) * t;
			float fmI = prevFm + (fmNow - prevFm) * t;
			float duty = clampf(p.pulseWidth + p.pwm * pwmI / 5.f * 0.45f, 0.05f, 0.95f);
			float thr = pulseThreshold(curve, duty);
			float f = freq;
			if (p.fm > 0.f) f = std::fmax(0.f, freq + p.fm * fmI / 5.f * freq);
			// sync position inside this sub-sample, measured back from its end
			float sa = -1.f;
			if (syncPos >= 0.f) {
				float lo = (float)i / factor, hi = (i + 1.f) / factor;
				if (syncPos > lo && syncPos <= hi) sa = (hi - syncPos) * factor;
			}
			lastF = f;
			core.process(f, curve, thr, sqGain, sa);
			float mix = (1.f - m) * core.saw + m * core.square + subMix * core.sub;
			float x = in.foldInPatched ? foldUp[i] : mix;
			bs[i] = core.saw; bq[i] = core.square; bu[i] = core.sub;
			bo[i] = folder.process(x, fold, p.symmetry);
		}
		prevPwm = pwmNow;
		prevFm = fmNow;
		OscXOut o;
		if (want & OX_SAW) o.saw = 5.f * decSaw.process(kernel, bs);
		if (want & OX_SQUARE) o.square = 5.f / SQ_LEVEL * decSq.process(kernel, bq);
		if (want & OX_SUB) o.sub = 5.f / SBO_SUB_LEVEL * decSub.process(kernel, bu);
		if (want & OX_OUT) o.out = clampf(5.f * decOut.process(kernel, bo), -12.f, 12.f);   // a biased fold can exceed +-1 after DC removal
		return o;
	}
};

} // namespace sb
