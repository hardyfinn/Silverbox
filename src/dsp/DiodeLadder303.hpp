// The 303 diode ladder (spec §5.2): four coupled stages with an unequal bottom
// capacitor, feedback through a high-pass, soft saturation at the input node.
// Trapezoidal (zero-delay-feedback) integration of the coupled linear system,
// solved as a tridiagonal system each sample; the saturating feedback node is
// solved with two Newton steps. Runs at the oversampled rate.
#pragma once
#include "Filters1.hpp"

namespace sb {

// Feedback needed for self-oscillation at resonant frequency fc (see Constants303.hpp).
inline float kcritAt(float fc) {
	float r = std::fmax(fc, KCOMP_MIN_FC) / FB_HP;
	return LADDER_KCRIT * (1.f + 1.412f / r + 0.576f / (r * r));
}
// Feedback for a RESONANCE knob value at cutoff fc.
inline float resonanceK(float res, float fc) {
	return std::pow(clampf(res, 0.f, 1.f), 0.8f) * RES_MAX * kcritAt(fc);
}

struct DiodeLadder303 {
	float y[4] = {0.f, 0.f, 0.f, 0.f};
	float uPrev = 0.f;
	float fbHpZ = 0.f;    // state of the feedback high-pass (its low-pass integrator)
	float fbHpG = 0.f;    // its G = g / (1 + g)
	float fbHpg = 0.f;    // its g
	float fs = 192000.f;

	void setRate(float fsOs) {
		fs = fsOs;
		fbHpg = tptG(FB_HP, fs);
		fbHpG = fbHpg / (1.f + fbHpg);
	}
	void reset() {
		for (int i = 0; i < 4; i++) y[i] = 0.f;
		uPrev = 0.f;
		fbHpZ = 0.f;
	}

	static inline float sat(float v) { return SAT_LEVEL * fastTanh(v / SAT_LEVEL); }

	// x: input (about ±1). fc: resonant frequency in Hz. k: feedback (0..K_MAX). Returns stage 4.
	float process(float x, float fc, float k) {
		// G = g*w*h/2 with the system scaled so its resonant peak lands on fc
		float fw = clampf(fc / LADDER_W180, 5.f, 0.45f * fs);
		float G = std::tan(PI_F * fw / fs);   // = w h / 2 after prewarping the matrix scale
		const float c1 = STAGE1_RATIO;

		// (I - G A) y1 = (I + G A) y0 + G B (u0 + u1)
		// Tridiagonal (I - G A): diag d, lower l (row i, col i-1), upper up (row i, col i+1)
		float d0 = 1.f + G * c1, d1 = 1.f + 2.f * G, d2 = d1, d3 = d1;
		float u0 = -G * c1, u1 = -G, u2 = -G;     // upper
		float l1 = -G, l2 = -G, l3 = -G;          // lower
		// right-hand side without the new input
		float r0 = y[0] + G * (c1 * (-y[0] + y[1])) + G * c1 * uPrev;
		float r1 = y[1] + G * (y[0] - 2.f * y[1] + y[2]);
		float r2 = y[2] + G * (y[1] - 2.f * y[2] + y[3]);
		float r3 = y[3] + G * (y[2] - 2.f * y[3]);
		// Thomas algorithm, solving for P (rhs r) and Q (rhs e0 * G c1) together
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

		// feedback high-pass of y3 (TPT): hp = a*y3 + b
		float a = 1.f / (1.f + fbHpg);
		float b = -fbHpZ / (1.f + fbHpg);
		// u = sat(x - k*(a*(P3 + Q3 u) + b))
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
		// advance the feedback high-pass state
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
