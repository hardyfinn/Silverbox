// The variable diode ladder for SBF (round 2 spec §3.2, §3.4). Same structure and integration
// as DiodeLadder303 (which stays untouched), with the bottom-capacitor ratio c1 (POLES) and the
// feedback high-pass corner (BASS) made variable, and low-pass, band-pass and high-pass taps.
// At c1 = 1.8 and BASS = 150 Hz it does exactly what DiodeLadder303 does.
#pragma once
#include "Filters1.hpp"
#include "ConstantsX.hpp"

namespace sb {

// Ladder coefficients for one POLES value, interpolated from the tables. Control rate.
struct LadderCoefs {
	float c1 = STAGE1_RATIO;
	float w180 = LADDER_W180;
	float kcrit0 = LADDER_KCRIT;
	float hpa = 1.412f, hpb = 0.576f;
	float bpg = 8.916697f;

	static LadderCoefs at(float poles) {
		LadderCoefs c;
		c.c1 = clampf(poles, POLES_MIN, POLES_MAX);
		float x = (c.c1 - POLES_MIN) * 10.f;
		int i = (int)x;
		if (i >= LX_N - 1) i = LX_N - 2;
		if (i < 0) i = 0;
		float t = x - i;
		// snap to the grid point when within float noise of it, so the stock row is used exactly
		if (t < 1e-4f) t = 0.f;
		if (t > 1.f - 1e-4f) { i++; t = 0.f; if (i >= LX_N - 1) { i = LX_N - 2; t = 1.f; } }
		auto lerp = [&](const float* a) { return t == 0.f ? a[i] : (t == 1.f ? a[i + 1] : a[i] + (a[i + 1] - a[i]) * t); };
		c.w180 = lerp(LX_W180);
		c.kcrit0 = lerp(LX_KCRIT0);
		c.hpa = lerp(LX_HPA);
		c.hpb = lerp(LX_HPB);
		c.bpg = lerp(LX_BPG);
		return c;
	}

	// Feedback needed to self-oscillate at resonant frequency fc with the feedback high-pass at bass Hz.
	// Compensation only down to fc = 4 * bass (600 Hz at stock), as round 1.
	float kcrit(float fc, float bass) const {
		float r = std::fmax(fc, 4.f * bass) / bass;
		return kcrit0 * (1.f + hpa / r + hpb / (r * r));
	}
};

enum FilterMode { MODE_LP = 0, MODE_BP = 1, MODE_HP = 2 };

// Output tap weights on (u, y1, y2, y3, y4).
inline void tapWeights(int mode, const LadderCoefs& c, float* w) {
	if (mode == MODE_BP) {
		w[0] = 0.f; w[1] = 0.f; w[2] = c.bpg; w[3] = -4.f * c.bpg; w[4] = 5.f * c.bpg;
	} else if (mode == MODE_HP) {
		// numerator s^4: (1, -(1 + 6/c1), 1 + 26/c1, -48/c1, 42/c1) (scripts/ladder_calibration.py)
		float r = 1.f / c.c1;
		w[0] = 1.f; w[1] = -(1.f + 6.f * r); w[2] = 1.f + 26.f * r; w[3] = -48.f * r; w[4] = 42.f * r;
	} else {
		w[0] = 0.f; w[1] = 0.f; w[2] = 0.f; w[3] = 0.f; w[4] = 1.f;
	}
}

struct DiodeLadderX {
	float y[4] = {0.f, 0.f, 0.f, 0.f};
	float uPrev = 0.f;
	float fbHpZ = 0.f;
	float fbHpG = 0.f, fbHpg = 0.f;
	float fs = 192000.f;
	float bassHz = -1.f;

	void setRate(float fsOs) { fs = fsOs; bassHz = -1.f; setBass(FB_HP); }
	// Feedback high-pass corner. Control rate; recomputed only when it changes.
	void setBass(float hz) {
		if (hz == bassHz) return;
		bassHz = hz;
		fbHpg = tptG(hz, fs);
		fbHpG = fbHpg / (1.f + fbHpg);
	}
	void reset() {
		for (int i = 0; i < 4; i++) y[i] = 0.f;
		uPrev = 0.f;
		fbHpZ = 0.f;
	}

	static inline float sat(float v) { return SAT_LEVEL * fastTanh(v / SAT_LEVEL); }

	// Weighted output tap after process(): w on (u, y1, y2, y3, y4).
	float tap(const float* w) const { return w[0] * uPrev + w[1] * y[0] + w[2] * y[1] + w[3] * y[2] + w[4] * y[3]; }

	// x: input. fc: resonant frequency in Hz. k: feedback. Returns stage 4 (low-pass).
	float process(float x, float fc, float k, const LadderCoefs& lc) {
		float fw = clampf(fc / lc.w180, 5.f, 0.45f * fs);
		float G = std::tan(PI_F * fw / fs);
		const float c1 = lc.c1;
		float d0 = 1.f + G * c1, d1 = 1.f + 2.f * G, d2 = d1, d3 = d1;
		float u0 = -G * c1, u1 = -G, u2 = -G;
		float l1 = -G, l2 = -G, l3 = -G;
		float r0 = y[0] + G * (c1 * (-y[0] + y[1])) + G * c1 * uPrev;
		float r1 = y[1] + G * (y[0] - 2.f * y[1] + y[2]);
		float r2 = y[2] + G * (y[1] - 2.f * y[2] + y[3]);
		float r3 = y[3] + G * (y[2] - 2.f * y[3]);
		float q0 = G * c1, q1 = 0.f, q2 = 0.f, q3 = 0.f;
		float cp0 = u0 / d0;
		float rp0 = r0 / d0, qp0 = q0 / d0;
		float m1 = d1 - l1 * cp0;
		float cp1 = u1 / m1;
		float rp1 = (r1 - l1 * rp0) / m1, qp1 = (q1 - l1 * qp0) / m1;
		float m2 = d2 - l2 * cp1;
		float cp2 = u2 / m2;
		float rp2 = (r2 - l2 * rp1) / m2, qp2 = (q2 - l2 * qp1) / m2;
		float m3 = d3 - l3 * cp2;
		float P3 = (r3 - l3 * rp2) / m3, Q3 = (q3 - l3 * qp2) / m3;
		float P2 = rp2 - cp2 * P3, Q2 = qp2 - cp2 * Q3;
		float P1 = rp1 - cp1 * P2, Q1 = qp1 - cp1 * Q2;
		float P0 = rp0 - cp0 * P1, Q0 = qp0 - cp0 * Q1;

		float a = 1.f / (1.f + fbHpg);
		float b = -fbHpZ / (1.f + fbHpg);
		float c = x - k * (a * P3 + b);
		float dd = k * a * Q3;
		float u = sat(c / (1.f + dd));
		for (int it = 0; it < 2; it++) {
			float arg = c - dd * u;
			float t = fastTanh(arg / SAT_LEVEL);
			float f = u - SAT_LEVEL * t;
			float fp = 1.f + dd * (1.f - t * t);
			u -= f / fp;
		}
		y[0] = P0 + Q0 * u;
		y[1] = P1 + Q1 * u;
		y[2] = P2 + Q2 * u;
		y[3] = P3 + Q3 * u;
		float v = (y[3] - fbHpZ) * fbHpG;
		float lp = v + fbHpZ;
		fbHpZ = lp + v;
		uPrev = u;
		for (int i = 0; i < 4; i++) {
			if (!std::isfinite(y[i])) { reset(); return 0.f; }
			y[i] = clampf(y[i], -20.f, 20.f);
		}
		return y[3];
	}
};

} // namespace sb
