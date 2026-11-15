// Exponential knobs for the round 2 modules: a 0..1 knob mapped to lo * (hi / lo)^v, with the
// exact stock value at the default position (as SBX does), so stock settings are bit-exact.
#pragma once
#include "../plugin.hpp"

struct ExpCurve {
	float lo, hi, stock;
	float defaultV() const { return std::log(stock / lo) / std::log(hi / lo); }
	// v: knob plus CV, 0..1. atDefault: the knob sits on its default and no CV is patched.
	float value(float v, bool atDefault) const {
		if (atDefault) return stock;
		return lo * std::pow(hi / lo, clamp(v, 0.f, 1.f));
	}
};

// Configure a knob with an ExpCurve. displayScale converts to the shown unit (1000 for ms).
template <class M>
inline void configExp(M* m, int id, const ExpCurve& c, const std::string& name, const std::string& unit, float displayScale = 1.f) {
	m->configParam(id, 0.f, 1.f, c.defaultV(), name, unit, c.hi / c.lo, c.lo * displayScale);
}

inline bool atDefault(Module* m, int id) {
	ParamQuantity* q = m->paramQuantities[id];
	return std::fabs(m->params[id].getValue() - q->getDefaultValue()) < 1e-5f;
}
