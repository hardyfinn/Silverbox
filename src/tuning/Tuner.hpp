// SBT, Silverbox Tuner (round 2 spec §4) and the tuning step SBS uses (§1a). No Rack includes.
#pragma once
#include <atomic>
#include <cstdio>
#include <vector>
#include "Scala.hpp"

namespace sb {

constexpr int TUNER_SLOTS = 8;
constexpr int TUNER_CHANNELS = 16;

// Patch storage as text, so a patch does not depend on the files still being there.
// Exact round trip: cents are written with 17 significant digits.
inline std::string serializeScale(const Scale& s) {
	std::string out;
	char buf[64];
	out += s.name + "\n";
	out += s.oneKeyPerDegree ? "degree\n" : "twelve\n";
	std::snprintf(buf, sizeof(buf), "%d\n", s.n()); out += buf;
	for (double c : s.cents) { std::snprintf(buf, sizeof(buf), "%.17g\n", c); out += buf; }
	if (s.kbm.loaded) {
		const Kbm& k = s.kbm;
		std::snprintf(buf, sizeof(buf), "kbm %d %d %d %d %d ", k.size, k.first, k.last, k.middle, k.refNote); out += buf;
		std::snprintf(buf, sizeof(buf), "%.17g %d", k.refFreq, k.octaveDegree); out += buf;
		for (int e : k.map) { std::snprintf(buf, sizeof(buf), " %d", e); out += buf; }
		out += "\n";
	}
	return out;
}

inline bool deserializeScale(const std::string& text, Scale& s) {
	std::vector<std::string> ls = scala_detail::lines(text);
	if (ls.size() < 3) return false;
	Scale t;
	t.name = ls[0];
	t.oneKeyPerDegree = ls[1] == "degree";
	long n;
	if (!scala_detail::parseInt(ls[2], n) || n < 0 || n > 1024 || (long)ls.size() < 3 + n) return false;
	for (long i = 0; i < n; i++) {
		double c;
		if (!scala_detail::parseDouble(ls[3 + i], c)) return false;
		t.cents.push_back(c);
	}
	if ((long)ls.size() > 3 + n && ls[3 + n].compare(0, 4, "kbm ") == 0) {
		std::istringstream is(ls[3 + n].substr(4));
		Kbm& k = t.kbm;
		if (!(is >> k.size >> k.first >> k.last >> k.middle >> k.refNote >> k.refFreq >> k.octaveDegree)) return false;
		if (k.size < 0 || k.size > 2048) return false;
		for (int i = 0; i < k.size; i++) { int e; if (!(is >> e)) return false; k.map.push_back(e); }
		k.loaded = true;
	}
	s = t;
	return true;
}

// A scale handed from the UI thread to the audio thread by pointer swap. The audio thread only
// reads; replaced scales are kept until destruction so no reader ever sees freed memory.
struct ScaleSlot {
	std::atomic<Scale*> live{nullptr};
	std::vector<Scale*> retired;   // UI thread only
	ScaleSlot() { live.store(new Scale(equal12())); }
	~ScaleSlot() { delete live.load(); for (Scale* s : retired) delete s; }
	ScaleSlot(const ScaleSlot&) = delete;
	ScaleSlot& operator=(const ScaleSlot&) = delete;
	void set(const Scale& s) { Scale* old = live.exchange(new Scale(s)); if (old) retired.push_back(old); }
	const Scale& get() const { return *live.load(std::memory_order_acquire); }
};

// SBT's slot choice: SCALE knob (0..7) plus SCALE CV (0..10 V = 0..7 slots), wrapping.
inline int tunerSlot(float knob, float cv) {
	int s = (int)std::lround(knob) + (int)std::lround(cv * 0.7f);
	s %= TUNER_SLOTS;
	if (s < 0) s += TUNER_SLOTS;
	return s;
}

struct TunerCore {
	float hold[TUNER_CHANNELS] = {};
	// Maps n channels. A 12-tone equal slot with no mapping is exact: out = in + fine.
	void process(const Scale& s, int root, float fineCents, const float* in, float* out, int n) {
		bool eq = s.isEqual12();
		for (int c = 0; c < n; c++) {
			float v = eq ? in[c] + fineCents / 1200.f : TuningMap::map(s, root, fineCents, in[c], hold[c]);
			hold[c] = v;
			out[c] = v;
		}
	}
};

// SBS's Tuning menu (§1a): the engine's note (pattern note + UP/DOWN + key and track transposes)
// is mapped through the scale; the TRANSPOSE jack is added afterwards as an exact pitch shift.
// The engine adds the jack's semitones when a NOTE step starts, so the jack value is latched at
// that moment here and taken off before mapping.
struct SbsTuning {
	int latchedJack = 0;
	float hold = 0.f;
	// voct: engine output. jack: the engine's current TRANSPOSE semitones. noteStart: a NOTE step
	// began this sample (or the engine changed pitch while stopped: pass jackNow = 0 then).
	float process(const Scale* s, int root, float voct, int jackNow, bool noteStart) {
		if (noteStart) latchedJack = jackNow;
		if (!s || s->isEqual12()) return voct;   // 12-tone equal is the identity at any root
		float base = voct - latchedJack / 12.f;
		float m = TuningMap::map(*s, root, 0.f, base, hold);
		hold = m;
		return m + latchedJack / 12.f;
	}
};

} // namespace sb
