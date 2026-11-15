// Polyphase FIR up/down sampling for the nonlinear models. Factor 4 up to 50 kHz,
// 2 up to 100 kHz, otherwise 1. No user-facing controls.
#pragma once
#include <cmath>
#include "Constants303.hpp"

namespace sb {

constexpr int OS_MAX = 4;
constexpr int OS_TAPS_PER_PHASE = 32;
constexpr int OS_MAX_TAPS = OS_MAX * OS_TAPS_PER_PHASE;

inline int oversampleFactor(float sampleRate) {
	if (sampleRate <= 50000.f) return 4;
	if (sampleRate <= 100000.f) return 2;
	return 1;
}

struct OsKernel {
	int factor = 1;
	int taps = 1;
	float h[OS_MAX_TAPS] = {1.f};

	void design(int f) {
		factor = f;
		if (f == 1) { taps = 1; h[0] = 1.f; return; }
		taps = f * OS_TAPS_PER_PHASE;
		float fc = 0.42f / f;   // cutoff as a fraction of the oversampled rate (about 20 kHz at 48 kHz)
		float sum = 0.f;
		for (int i = 0; i < taps; i++) {
			float n = i - (taps - 1) * 0.5f;
			float x = 2.f * PI_F * fc * n;
			float sinc = (std::fabs(n) < 1e-6f) ? 2.f * fc : std::sin(x) / (PI_F * n);
			// Blackman window
			float w = 0.42f - 0.5f * std::cos(2.f * PI_F * i / (taps - 1)) + 0.08f * std::cos(4.f * PI_F * i / (taps - 1));
			h[i] = sinc * w;
			sum += h[i];
		}
		for (int i = 0; i < taps; i++) h[i] /= sum;
	}
};

// Histories are stored twice (at i and i + N) so the filter loops read one
// contiguous block with no wrap-around arithmetic.
struct Upsampler {
	float hist[2 * OS_TAPS_PER_PHASE] = {};
	int pos = 0;
	// out[0..factor-1]
	void process(const OsKernel& k, float x, float* out) {
		if (k.factor == 1) { out[0] = x; return; }
		const int N = OS_TAPS_PER_PHASE;
		pos = (pos == 0) ? N - 1 : pos - 1;
		hist[pos] = hist[pos + N] = x;
		const float* h = hist + pos;
		for (int p = 0; p < k.factor; p++) {
			float acc = 0.f;
			for (int j = 0; j < N; j++) acc += k.h[j * k.factor + p] * h[j];
			out[p] = acc * k.factor;
		}
	}
	void reset() { for (float& v : hist) v = 0.f; pos = 0; }
};

struct Decimator {
	float hist[2 * OS_MAX_TAPS] = {};
	int pos = 0;
	float process(const OsKernel& k, const float* in) {
		if (k.factor == 1) return in[0];
		const int N = k.taps;
		for (int p = 0; p < k.factor; p++) {
			pos = (pos == 0) ? N - 1 : pos - 1;
			hist[pos] = hist[pos + N] = in[p];
		}
		const float* h = hist + pos;
		float acc = 0.f;
		for (int j = 0; j < N; j++) acc += k.h[j] * h[j];
		return acc;
	}
	void reset() { for (float& v : hist) v = 0.f; pos = 0; }
};

} // namespace sb
