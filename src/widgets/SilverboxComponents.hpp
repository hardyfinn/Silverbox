// Knobs, jack, toggles, buttons and keys. SVGs come from scripts/make_panels.py.
#pragma once
#include "../plugin.hpp"

inline std::shared_ptr<window::Svg> sbSvg(const std::string& name) {
	return window::Svg::load(asset::plugin(pluginInstance, "res/components/" + name + ".svg"));
}

struct SbKnob : app::SvgKnob {
	SbKnob() {
		minAngle = -0.75f * M_PI;
		maxAngle = 0.75f * M_PI;
		setSvg(sbSvg("SbKnob"));
		shadow->opacity = 0.2f;
	}
};
struct SbKnobSmall : app::SvgKnob {
	SbKnobSmall() {
		minAngle = -0.75f * M_PI;
		maxAngle = 0.75f * M_PI;
		setSvg(sbSvg("SbKnobSmall"));
		shadow->opacity = 0.2f;
	}
};

struct SbJack : app::SvgPort { SbJack() { setSvg(sbSvg("SbJack")); } };

struct SbToggle2 : app::SvgSwitch {
	SbToggle2() { addFrame(sbSvg("Toggle2_0")); addFrame(sbSvg("Toggle2_1")); shadow->opacity = 0.f; }
};
struct SbToggle3 : app::SvgSwitch {
	SbToggle3() { addFrame(sbSvg("Toggle3_0")); addFrame(sbSvg("Toggle3_1")); addFrame(sbSvg("Toggle3_2")); shadow->opacity = 0.f; }
};

struct SbButton : app::SvgSwitch {
	SbButton() { momentary = true; addFrame(sbSvg("SbButton_0")); addFrame(sbSvg("SbButton_1")); shadow->opacity = 0.f; }
};
struct SbLitButton : LightButton<SbButton, SmallSimpleLight<RedLight>> {};
struct SbKeyWhite : app::SvgSwitch {
	SbKeyWhite() { momentary = true; addFrame(sbSvg("SbKeyWhite_0")); addFrame(sbSvg("SbKeyWhite_1")); shadow->opacity = 0.f; }
};
struct SbKeyBlack : app::SvgSwitch {
	SbKeyBlack() { momentary = true; addFrame(sbSvg("SbKeyBlack_0")); addFrame(sbSvg("SbKeyBlack_1")); shadow->opacity = 0.f; }
};

// Panel and screws.
template <class W>
inline void sbScaffold(W* w, Module* m, const std::string& slug) {
	w->setModule(m);
	w->setPanel(createPanel(asset::plugin(pluginInstance, "res/" + slug + ".svg")));
	float width = w->box.size.x;
	w->addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
	w->addChild(createWidget<ScrewSilver>(Vec(width - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
	w->addChild(createWidget<ScrewSilver>(Vec(width - 2 * RACK_GRID_WIDTH, 0)));
	w->addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
}

// Gate input with the spec's 0.5 V / 1.0 V Schmitt thresholds.
struct GateIn {
	bool state = false;
	bool process(float v) {
		state = state ? (v > 0.5f) : (v >= 1.f);
		return state;
	}
};
