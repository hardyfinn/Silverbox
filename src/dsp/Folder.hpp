// Wavefolder for SBO (round 2 spec §2.5). Reflecting triangle fold with rounded corners,
// optional bias (SYMMETRY) with DC removal while biased. Runs at the oversampled rate.
#pragma once
#include "Filters1.hpp"
#include "ConstantsX.hpp"

namespace sb {

// tri(u) = u on [-1, 1], reflected at +-1 beyond, corners rounded with a parabola over +-FOLD_ROUND.
inline float foldTri(float u) {
	// position in the 4-unit period, with the +1 corner at m = 0 and the -1 corner at m = +-2
	float q = (u - 1.f) * 0.25f;
	q -= std::floor(q + 0.5f);          // -0.5 .. 0.5
	float m = 4.f * q;                  // -2 .. 2, distance from the +1 corner is |m|
	float am = std::fabs(m);
	const float d = FOLD_ROUND;
	if (am < d) return 1.f - 0.5f * d - m * m / (2.f * d);
	float t = 2.f - am;                 // distance from the -1 corner
	if (t < d) return -1.f + 0.5f * d + t * t / (2.f * d);
	return 1.f - am;
}

struct Folder {
	OnePole dc;
	float fs = 192000.f;
	void setRate(float fsOs) { fs = fsOs; dc.set(FOLD_DC_HP, fs); }
	void reset() { dc.reset(); }

	// x: input (about +-1). fold: 0..1. symmetry: -1..1. Returns y; exactly x when fold is 0.
	float process(float x, float fold, float symmetry) {
		if (fold <= 0.f) { dc.reset(); return x; }
		float g = 1.f + FOLD_GAIN * fold * fold;
		float b = FOLD_BIAS * clampf(symmetry, -1.f, 1.f) * fold;
		float y = foldTri(g * x + b);
		// DC removal, crossfaded in with |b| so an unbiased fold is untouched
		float w = clampf(std::fabs(b) * 10.f, 0.f, 1.f);
		float lp = dc.lp(y);
		return y - w * lp;
	}
};

} // namespace sb
