// The 303 oscillator (spec §5.1): a falling, slightly curved capacitor-charge saw,
// and a square made from that saw by a one-transistor shaper. Runs at the oversampled rate.
#pragma once
#include "Blep.hpp"
#include "Filters1.hpp"

namespace sb {

struct Osc303 {
	float phase = 0.f;
	Blep blep;
	OnePole sqLp, sqHp;
	float fs = 192000.f;
	float lastShaped = 0.f;   // square before its filters (for tests)

	void setRate(float fsOs) {
		fs = fsOs;
		sqLp.set(SQ_LP, fs);
		sqHp.set(SQ_HP, fs);
	}
	void reset() { phase = 0.f; blep = Blep(); sqLp.reset(); sqHp.reset(); }

	static float sawShape(float p) {
		// rising capacitor charge 0..1, then mapped to +1..-1 (falling ramp)
		static const float norm = 1.f / (1.f - std::exp(-K_SAW));
		float c = (1.f - std::exp(-K_SAW * p)) * norm;
		return 1.f - 2.f * c;
	}

	// freq in Hz. square selects the waveform. Returns about ±1.
	float process(float freq, bool square) {
		float dt = clampf(freq / fs, 0.f, 0.45f);
		phase += dt;
		if (phase >= 1.f) {
			phase -= 1.f;
			// jump from the end of the ramp (-1) to its start (+1)
			float a = dt > 0.f ? phase / dt : 0.f;
			blep.add(2.f, clampf(a, 0.f, 0.999f));
		}
		float saw = blep.process(sawShape(phase));
		if (!square) return saw;
		lastShaped = fastTanh(SQ_GAIN * (saw - SQ_OFFSET));
		return sqHp.hp(sqLp.lp(lastShaped)) * SQ_LEVEL;
	}
};

} // namespace sb
