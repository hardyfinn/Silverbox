// One-pole TPT low-pass and high-pass, plus small helpers.
#pragma once
#include <cmath>
#include "Constants303.hpp"

namespace sb {

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

// Rational tanh, accurate to about 2e-4 within |x| < 4.5, exact limit beyond.
inline float fastTanh(float x) {
	if (x > 4.5f) return 1.f;
	if (x < -4.5f) return -1.f;
	float x2 = x * x;
	float n = x * (135135.f + x2 * (17325.f + x2 * (378.f + x2)));
	float d = 135135.f + x2 * (62370.f + x2 * (3150.f + 28.f * x2));
	float y = n / d;
	return y > 1.f ? 1.f : (y < -1.f ? -1.f : y);
}

// g = tan(pi f / fs), prewarped integrator gain
inline float tptG(float f, float fs) {
	float w = PI_F * clampf(f, 1.f, 0.49f * fs) / fs;
	return std::tan(w);
}

struct OnePole {
	float z = 0.f;
	float g = 0.f;      // G = g / (1 + g)
	void set(float f, float fs) { float t = tptG(f, fs); g = t / (1.f + t); }
	float lp(float x) { float v = (x - z) * g; float y = v + z; z = y + v; return y; }
	float hp(float x) { return x - lp(x); }
	void reset(float v = 0.f) { z = v; }
};

inline float decayCoef(float timeTo40dB, float dt) {
	// exp(-dt / tau), tau = T / ln(100)
	return std::exp(-dt * 4.6051702f / timeTo40dB);
}
inline float tauCoef(float tau, float dt) { return 1.f - std::exp(-dt / tau); }

} // namespace sb
