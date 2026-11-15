// SBO Silverbox Oscillator (round 2 spec §2).
#include "../plugin.hpp"
#include "../dsp/OscX.hpp"
#include "../generated/Layout.hpp"
#include "../widgets/ExpParam.hpp"

struct PulseWidthQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		float v = getValue();
		return string::f("%.1f", v * 100.f) + (std::fabs(v - sb::SBO_PW_STOCK) < 1e-6f ? " (stock)" : "");
	}
};

struct SBO : Module {
	enum ParamId { TUNING_PARAM, PW_PARAM, MIX_PARAM, SUB_PARAM, FOLD_PARAM, SYMMETRY_PARAM, PWM_PARAM, FOLDCV_PARAM,
	               FM_PARAM, SLIDETIME_PARAM, DRIFT_PARAM, TUNE_PARAM, WIDTH_PARAM, CURVE_PARAM, SHAPE_PARAM, PARAMS_LEN };
	enum InputId { VOCT_INPUT, SLIDE_INPUT, PWM_INPUT, MIX_INPUT, FOLD_INPUT, FM_INPUT, SYNC_INPUT, FOLDIN_INPUT, INPUTS_LEN };
	enum OutputId { SAW_OUTPUT, SQUARE_OUTPUT, SUB_OUTPUT, OUT_OUTPUT, OUTPUTS_LEN };
	enum LightId { LIGHTS_LEN };

	const ExpCurve slideCurve{0.002f, 0.5f, 0.060f};
	sb::OscX osc;
	GateIn slideIn;

	SBO() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(TUNING_PARAM, -12.f, 12.f, 0.f, "Tuning", " semitones");
		configParam<PulseWidthQuantity>(PW_PARAM, 0.05f, 0.95f, sb::SBO_PW_STOCK, "Pulse width", "%", 0.f, 100.f);
		configParam(MIX_PARAM, 0.f, 1.f, 0.f, "Wave mix (saw to square)", "%", 0.f, 100.f);
		configParam(SUB_PARAM, 0.f, 1.f, 0.f, "Sub-octave level into OUT", "%", 0.f, 100.f);
		configParam(FOLD_PARAM, 0.f, 1.f, 0.f, "Fold", "%", 0.f, 100.f);
		configParam(SYMMETRY_PARAM, -1.f, 1.f, 0.f, "Symmetry (bias into the folder)", "%", 0.f, 100.f);
		configParam(PWM_PARAM, -1.f, 1.f, 0.f, "PWM depth (±5 V × depth = ±45%)", "%", 0.f, 100.f);
		configParam(FOLDCV_PARAM, -1.f, 1.f, 0.f, "Fold CV depth", "%", 0.f, 100.f);
		configParam(FM_PARAM, 0.f, 1.f, 0.f, "Linear FM depth", "%", 0.f, 100.f);
		configExp(this, SLIDETIME_PARAM, slideCurve, "Slide time", " ms", 1000.f);
		configParam(DRIFT_PARAM, 0.f, 1.f, 0.f, "Drift", "%", 0.f, 100.f);
		configParam(TUNE_PARAM, -100.f, 100.f, 0.f, "Tune trimmer (TM4)", " cents");
		configParam(WIDTH_PARAM, 0.9f, 1.1f, 1.f, "Width trimmer (TM5, V/oct scale)", "×");
		configParam(CURVE_PARAM, 0.f, 3.f, sb::K_SAW, "Saw curve (0 = straight ramp)");
		configParam(SHAPE_PARAM, 1.5f, 20.f, sb::SQ_GAIN, "Square shape (shaper gain: low is rounded, high is hard)");
		configInput(VOCT_INPUT, "1V/oct pitch");
		configInput(SLIDE_INPUT, "Slide");
		configInput(PWM_INPUT, "PWM");
		configInput(MIX_INPUT, "Wave mix CV (±5 V = ±1)");
		configInput(FOLD_INPUT, "Fold CV");
		configInput(FM_INPUT, "Linear FM");
		configInput(SYNC_INPUT, "Hard sync (rising edge through 0 V)");
		configInput(FOLDIN_INPUT, "Fold in (replaces the oscillator mix into the folder)");
		configOutput(SAW_OUTPUT, "Saw");
		configOutput(SQUARE_OUTPUT, "Square");
		configOutput(SUB_OUTPUT, "Sub-octave square");
		configOutput(OUT_OUTPUT, "Mix through the folder");
		osc.seed(random::u32());
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override { osc.setRate(e.sampleRate); }

	void process(const ProcessArgs&) override {
		int want = (outputs[SAW_OUTPUT].isConnected() ? sb::OX_SAW : 0) | (outputs[SQUARE_OUTPUT].isConnected() ? sb::OX_SQUARE : 0) |
		           (outputs[SUB_OUTPUT].isConnected() ? sb::OX_SUB : 0) | (outputs[OUT_OUTPUT].isConnected() ? sb::OX_OUT : 0);
		sb::OscXParams p;
		p.tuning = params[TUNING_PARAM].getValue();
		float pw = params[PW_PARAM].getValue();
		if (std::fabs(pw - sb::SBO_PW_STOCK) < 0.008f) pw = sb::SBO_PW_STOCK;   // detent at stock
		p.pulseWidth = pw;
		p.pwm = params[PWM_PARAM].getValue();
		p.waveMix = params[MIX_PARAM].getValue();
		p.sub = params[SUB_PARAM].getValue();
		p.fold = params[FOLD_PARAM].getValue();
		p.foldCv = params[FOLDCV_PARAM].getValue();
		p.symmetry = params[SYMMETRY_PARAM].getValue();
		p.fm = params[FM_PARAM].getValue();
		p.slideTime = slideCurve.value(params[SLIDETIME_PARAM].getValue(), atDefault(this, SLIDETIME_PARAM));
		p.drift = params[DRIFT_PARAM].getValue();
		p.tune = params[TUNE_PARAM].getValue();
		p.width = params[WIDTH_PARAM].getValue();
		p.sawCurve = params[CURVE_PARAM].getValue();
		p.squareShape = params[SHAPE_PARAM].getValue();
		sb::OscXIn in;
		in.voct = inputs[VOCT_INPUT].getVoltage();
		in.slide = slideIn.process(inputs[SLIDE_INPUT].getVoltage());
		in.pwm = inputs[PWM_INPUT].getVoltage();
		in.mixCv = inputs[MIX_INPUT].getVoltage();
		in.fold = inputs[FOLD_INPUT].getVoltage();
		in.fm = inputs[FM_INPUT].getVoltage();
		in.syncPatched = inputs[SYNC_INPUT].isConnected();
		in.sync = inputs[SYNC_INPUT].getVoltage();
		in.foldInPatched = inputs[FOLDIN_INPUT].isConnected();
		in.foldIn = inputs[FOLDIN_INPUT].getVoltage();
		sb::OscXOut o = osc.process(in, p, want);
		outputs[SAW_OUTPUT].setVoltage(o.saw);
		outputs[SQUARE_OUTPUT].setVoltage(o.square);
		outputs[SUB_OUTPUT].setVoltage(o.sub);
		outputs[OUT_OUTPUT].setVoltage(o.out);
	}
};

struct SBOWidget : ModuleWidget {
	SBOWidget(SBO* module) {
		sbScaffold(this, module, "SBO");
		placeSBO(this, module);
	}
};

Model* modelSBO = createModel<SBO, SBOWidget>("SBO");
