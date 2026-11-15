// MEG, VEG, accent sweep and slide (spec §5.4 to §5.6). All run at the host rate.
#pragma once
#include "Filters1.hpp"

namespace sb {

// Fast one-pole attack to 1, then exponential decay. Restarts from its current value.
struct DecayEnv {
	float v = 0.f;
	bool attacking = false;
	void trigger() { attacking = true; }
	// attack: seconds (one-pole time constant), decayT: seconds to -40 dB
	void process(float dt, float attack, float decayT) {
		if (attacking) {
			// one-pole toward 1.2; time constant attack/ln(6) so the rise reaches 1 in "attack" seconds
			v += (1.2f - v) * tauCoef(attack * 0.5581f, dt);
			if (v >= 1.f) { v = 1.f; attacking = false; }
		} else {
			v *= decayCoef(decayT, dt);
			if (v < 1e-7f) v = 0.f;
		}
	}
};

// VEG: same shape, plus a fast close when the gate falls.
struct AmpEnv {
	float v = 0.f;
	bool attacking = false;
	bool gateOn = false;
	void trigger() { attacking = true; gateOn = true; }
	void release() { gateOn = false; attacking = false; }
	void process(float dt, float attack, float decayT) {
		if (!gateOn) {
			v *= std::exp(-dt / VEG_RELEASE);
		} else if (attacking) {
			v += (1.2f - v) * tauCoef(attack * 0.5581f, dt);
			if (v >= 1.f) { v = 1.f; attacking = false; }
		} else {
			v *= decayCoef(decayT, dt);
		}
		if (v < 1e-7f) v = 0.f;
	}
};

// The accent sweep capacitor (spec §5.5).
struct AccentSweep {
	float s = 0.f;
	// a: drive (MEG while accented, else 0). res: RESONANCE knob 0..1. speed: SBX multiplier.
	void process(float dt, float a, float res, float speed) {
		float tc = (SWEEP_TC_MIN + res * res * (SWEEP_TC_MAX - SWEEP_TC_MIN)) * speed;
		float td = (SWEEP_TD_MIN + res * (SWEEP_TD_MAX - SWEEP_TD_MIN)) * speed;
		if (a > s) s += (a - s) * tauCoef(tc, dt);
		else s *= std::exp(-dt / td);
		if (s < 1e-7f) s = 0.f;
	}
};

// One-pole pitch lag whose mode is chosen at each pitch change (spec §5.6).
struct SlideLag {
	float y = 0.f;
	float target = 0.f;
	bool gliding = false;
	bool first = true;
	// t: target volts. slide: SLIDE input high. gateRise: a gate rising edge this sample.
	float process(float dt, float t, bool slide, bool gateRise, float tau) {
		if (first) { y = target = t; first = false; }
		if (std::fabs(t - target) > PITCH_CHANGE_V) {
			gliding = slide;
			target = t;
			if (!gliding) y = t;
		} else {
			target = t;
		}
		if (gateRise && !slide) { gliding = false; y = target; }
		if (gliding) y += (target - y) * tauCoef(tau, dt);
		else y = target;
		return y;
	}
};

} // namespace sb
