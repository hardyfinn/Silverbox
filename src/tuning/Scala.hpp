// Scala scale (.scl) and keyboard mapping (.kbm) parsing, the 1V/OCT mapping used by SBT and by
// SBS's Tuning menu, and the built-in scales (round 2 spec §4.2 to §4.4). No Rack includes.
// Formats: https://www.huygens-fokker.org/scala/scl_format.html and
// https://www.huygens-fokker.org/scala/help.htm#mappings
#pragma once
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>
#include <sstream>

namespace sb {

struct Kbm {
	bool loaded = false;
	int size = 0;               // 0 = linear
	int first = 0, last = 127;
	int middle = 60;            // key mapped to degree 0
	int refNote = 69;
	double refFreq = 440.0;
	int octaveDegree = 0;       // 0 = the scale's own period
	std::vector<int> map;       // degree per key, -1 = unmapped
};

struct Scale {
	std::string name = "12-tone equal";
	std::vector<double> cents;  // degrees 1..N in cents; the last is the period. Empty = only 1/1
	bool oneKeyPerDegree = false;
	Kbm kbm;

	int n() const { return (int)cents.size(); }
	double period() const { return cents.empty() ? 1200.0 : cents.back(); }
	// cents of degree d (any integer), repeating at the period
	double degreeCents(long d) const {
		int N = n();
		if (N == 0) return d * 1200.0;   // only 1/1: the period is taken as an octave
		long o = d >= 0 ? d / N : -((-d + N - 1) / N);
		long r = d - o * N;
		return o * period() + (r == 0 ? 0.0 : cents[r - 1]);
	}
	bool isEqual12() const {
		if (kbm.loaded || cents.size() != 12) return false;
		for (int i = 0; i < 12; i++) if (std::fabs(cents[i] - 100.0 * (i + 1)) > 1e-9) return false;
		return true;
	}
};

namespace scala_detail {
inline std::vector<std::string> lines(const std::string& text) {
	std::vector<std::string> out;
	std::string cur;
	for (char c : text) {
		if (c == '\n') { out.push_back(cur); cur.clear(); }
		else if (c != '\r') cur += c;
	}
	if (!cur.empty()) out.push_back(cur);
	return out;
}
inline std::string trim(const std::string& s) {
	size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
	return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
inline std::string firstToken(const std::string& s) {
	std::istringstream is(s);
	std::string t;
	is >> t;
	return t;
}
inline bool parseInt(const std::string& t, long& v) {
	if (t.empty()) return false;
	char* end = nullptr;
	v = std::strtol(t.c_str(), &end, 10);
	return end && *end == 0;
}
inline bool parseDouble(const std::string& t, double& v) {
	if (t.empty()) return false;
	char* end = nullptr;
	v = std::strtod(t.c_str(), &end);
	return end && *end == 0 && std::isfinite(v);
}
} // namespace scala_detail

// Parse a .scl text. On success fills cents and name (description, else fallbackName).
inline bool parseScl(const std::string& text, Scale& out, std::string& err, const std::string& fallbackName = "") {
	using namespace scala_detail;
	std::vector<std::string> ls = lines(text);
	std::vector<std::string> body;
	for (auto& l : ls) if (l.empty() || l[0] != '!') body.push_back(l);
	if (body.size() < 2) { err = "too short"; return false; }
	std::string desc = trim(body[0]);
	long N;
	if (!parseInt(firstToken(body[1]), N) || N < 0) { err = "bad note count"; return false; }
	if (N > 1024) { err = "more than 1024 notes"; return false; }
	std::vector<double> cents;
	size_t i = 2;
	for (long k = 0; k < N; k++, i++) {
		while (i < body.size() && trim(body[i]).empty()) i++;
		if (i >= body.size()) { err = "missing pitch lines"; return false; }
		std::string t = firstToken(body[i]);
		double c;
		if (t.find('.') != std::string::npos) {
			if (!parseDouble(t, c)) { err = "bad cents value: " + t; return false; }
		} else {
			size_t slash = t.find('/');
			long a, b = 1;
			if (slash == std::string::npos) {
				if (!parseInt(t, a)) { err = "bad pitch: " + t; return false; }
			} else if (!parseInt(t.substr(0, slash), a) || !parseInt(t.substr(slash + 1), b)) {
				err = "bad ratio: " + t; return false;
			}
			if (a <= 0 || b <= 0) { err = "ratio must be positive: " + t; return false; }
			c = 1200.0 * std::log2((double)a / (double)b);
		}
		cents.push_back(c);
	}
	if (!cents.empty() && cents.back() <= 0.0) { err = "period must be above 1/1"; return false; }
	out.cents = cents;
	out.name = desc.empty() ? fallbackName : desc;
	if (out.name.empty()) out.name = "Untitled scale";
	return true;
}

inline bool parseKbm(const std::string& text, Kbm& out, std::string& err) {
	using namespace scala_detail;
	std::vector<std::string> body;
	for (auto& l : lines(text)) {
		if (!l.empty() && l[0] == '!') continue;
		if (trim(l).empty()) continue;
		body.push_back(firstToken(l));
	}
	if (body.size() < 7) { err = "too short"; return false; }
	long v[5];
	for (int i = 0; i < 5; i++) if (!parseInt(body[i], v[i])) { err = "bad header"; return false; }
	double rf;
	if (!parseDouble(body[5], rf)) { err = "bad reference frequency"; return false; }
	long od;
	if (!parseInt(body[6], od) || od < 0) { err = "bad octave degree"; return false; }
	if (v[0] < 0 || v[0] > 2048 || rf <= 0.0) { err = "bad header"; return false; }
	Kbm k;
	k.loaded = true;
	k.size = (int)v[0];
	k.first = (int)v[1]; k.last = (int)v[2]; k.middle = (int)v[3]; k.refNote = (int)v[4];
	k.refFreq = rf;
	k.octaveDegree = (int)od;
	for (int i = 0; i < k.size; i++) {
		size_t j = 7 + i;
		if (j >= body.size() || body[j] == "x" || body[j] == "X") { k.map.push_back(-1); continue; }
		long d;
		if (!parseInt(body[j], d) || d < 0) { err = "bad map entry: " + body[j]; return false; }
		k.map.push_back((int)d);
	}
	out = k;
	return true;
}

// The mapping (spec §4.3). Volts in, volts out; 0 V = C.
struct TuningMap {
	// Pitch of a whole semitone key in octaves (relative to 0 V = C). Returns false if unmapped.
	static bool keyPitch(const Scale& s, int root, long key, double& oct) {
		int N = s.n();
		if (s.kbm.loaded) {
			const Kbm& k = s.kbm;
			long midi = 60 + key;
			if (midi < k.first || midi > k.last) return false;
			auto degreeOf = [&](long m, long& d) {
				long i = m - k.middle;
				if (k.size == 0) { d = i; return true; }
				long o = i >= 0 ? i / k.size : -((-i + k.size - 1) / k.size);
				long r = i - o * k.size;
				int e = k.map[r];
				if (e < 0) return false;
				int od = k.octaveDegree > 0 ? k.octaveDegree : (N > 0 ? N : 1);
				d = e + o * od;
				return true;
			};
			long d, dr;
			if (!degreeOf(midi, d)) return false;
			double c = s.degreeCents(d);
			double refC = degreeOf(k.refNote, dr) ? s.degreeCents(dr) : s.degreeCents(k.refNote - k.middle);
			double hz = k.refFreq * std::pow(2.0, (c - refC) / 1200.0);
			oct = std::log2(hz / 261.6256);
			return true;
		}
		long kk = key - root;
		double c;
		if (s.oneKeyPerDegree && N > 0) {
			c = s.degreeCents(kk);
		} else {
			long o = kk >= 0 ? kk / 12 : -((-kk + 11) / 12);
			long d = kk - o * 12;
			long dd = (N >= 12 || N == 0) ? d : (d * N) / 12;
			if (N == 0) dd = 0;
			c = o * s.period() + (dd == 0 ? 0.0 : s.cents[dd - 1]);
		}
		oct = c / 1200.0 + root / 12.0;
		return true;
	}

	// Map one voltage. hold: the previous output for this channel (used for unmapped keys).
	static float map(const Scale& s, int root, float fineCents, float vin, float hold) {
		double n = (double)vin * 12.0;
		double rn = std::round(n);
		if (std::fabs(n - rn) < 1e-4) n = rn;   // whole semitones from float volts land exactly on keys
		double kf = std::floor(n);
		long key = (long)kf;
		double frac = n - kf;
		double a, b;
		if (!keyPitch(s, root, key, a)) return hold;
		double out = a;
		if (frac > 0.0 && keyPitch(s, root, key + 1, b)) out = a + frac * (b - a);
		return (float)(out + fineCents / 1200.0);
	}
};

// Built-in scales (spec §4.4). Sources in CREDITS.md.
struct BuiltinScale { const char* name; const char* scl; };
inline const std::vector<BuiltinScale>& builtinScales() {
	static const std::vector<BuiltinScale> list = {
		{"12-tone equal", "12-tone equal\n12\n100.\n200.\n300.\n400.\n500.\n600.\n700.\n800.\n900.\n1000.\n1100.\n2/1\n"},
		{"Just intonation", "Just intonation (5-limit)\n12\n16/15\n9/8\n6/5\n5/4\n4/3\n45/32\n3/2\n8/5\n5/3\n9/5\n15/8\n2/1\n"},
		{"Pythagorean", "Pythagorean\n12\n256/243\n9/8\n32/27\n81/64\n4/3\n729/512\n3/2\n128/81\n27/16\n16/9\n243/128\n2/1\n"},
		{"Quarter-comma meantone", "Quarter-comma meantone\n12\n76.04900\n193.15686\n310.26371\n386.31371\n503.42157\n579.47057\n696.57843\n772.62743\n889.73529\n1006.84314\n1082.89214\n2/1\n"},
		{"Werckmeister III", "Werckmeister III\n12\n90.22500\n192.18000\n294.13500\n390.22500\n498.04500\n588.27000\n696.09000\n792.18000\n888.27000\n996.09000\n1092.18000\n2/1\n"},
		{"Harmonic series 8 to 16", "Harmonic series 8 to 16\n8\n9/8\n10/8\n11/8\n12/8\n13/8\n14/8\n15/8\n16/8\n"},
		{"7-tone equal", "7-tone equal\n7\n171.42857\n342.85714\n514.28571\n685.71429\n857.14286\n1028.57143\n2/1\n"},
		{"19-tone equal", ""},
		{"24-tone equal", ""},
		{"Bohlen-Pierce", ""},
		{"Wendy Carlos alpha", ""},
		{"Slendro (5-tone equal)", "Slendro (5-tone equal)\n5\n240.\n480.\n720.\n960.\n2/1\n"},
		{"Pelog (Alves, just)", "Pelog (Bill Alves, just intonation)\n7\n8/7\n6/5\n21/16\n3/2\n8/5\n7/4\n2/1\n"},
	};
	return list;
}

// Builds built-in scale i into s (the generated equal divisions are made here).
inline bool builtinScale(int i, Scale& s) {
	const auto& list = builtinScales();
	if (i < 0 || i >= (int)list.size()) return false;
	Scale t;
	std::string name = list[i].name;
	auto equal = [&](int n, double periodCents) {
		t.cents.clear();
		for (int k = 1; k <= n; k++) t.cents.push_back(periodCents * k / n);
		t.name = name;
	};
	if (name == "19-tone equal") equal(19, 1200.0);
	else if (name == "24-tone equal") equal(24, 1200.0);
	else if (name == "Bohlen-Pierce") { equal(13, 1200.0 * std::log2(3.0)); t.oneKeyPerDegree = true; }
	else if (name == "Wendy Carlos alpha") { equal(15, 78.0 * 15); t.oneKeyPerDegree = true; }
	else {
		std::string err;
		if (!parseScl(list[i].scl, t, err)) return false;
	}
	s = t;
	return true;
}

inline Scale equal12() { Scale s; builtinScale(0, s); return s; }

} // namespace sb
