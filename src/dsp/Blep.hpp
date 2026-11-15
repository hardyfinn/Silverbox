// Two-point polyBLEP with one sample of latency. Each oscillator output owns one.
#pragma once

namespace sb {

struct Blep {
	float prevNaive = 0.f, prevCorr = 0.f, curCorr = 0.f;
	// Register a jump of height J that happened 'a' samples before the current sample (0 <= a < 1).
	void add(float J, float a) {
		prevCorr += 0.5f * J * a * a;
		curCorr += -0.5f * J * (1.f - a) * (1.f - a);
	}
	// Give the naive value of the current sample; returns the corrected previous sample.
	float process(float naive) {
		float out = prevNaive + prevCorr;
		prevNaive = naive;
		prevCorr = curCorr;
		curCorr = 0.f;
		return out;
	}
};

} // namespace sb
