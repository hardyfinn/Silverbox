// The Tuning context menu shared by SBS (round 2 §1a) and SBT (§4.1). UI thread only: files are
// read and parsed here and the result is swapped into the ScaleSlot the audio thread reads.
#pragma once
#include "../plugin.hpp"
#include "../tuning/Tuner.hpp"
#include <osdialog.h>
#include <fstream>
#include <sstream>

inline bool sbReadTextFile(const std::string& path, std::string& out) {
	std::ifstream f(path, std::ios::binary);
	if (!f) return false;
	std::stringstream ss;
	ss << f.rdbuf();
	out = ss.str();
	return out.size() < (1 << 20);
}

inline std::string sbPickFile(const char* filterSpec) {
	osdialog_filters* filters = osdialog_filters_parse(filterSpec);
	char* path = osdialog_file(OSDIALOG_OPEN, NULL, NULL, filters);
	osdialog_filters_free(filters);
	if (!path) return "";
	std::string p = path;
	std::free(path);
	return p;
}

// Load a .scl into the slot, keeping the slot's mapping mode and .kbm.
inline void sbLoadScl(sb::ScaleSlot* slot, std::atomic<bool>* fileError) {
	std::string path = sbPickFile("Scala scale (.scl):scl;All files:*");
	if (path.empty()) return;
	std::string text, err;
	sb::Scale s = slot->get();
	if (!sbReadTextFile(path, text) || !sb::parseScl(text, s, err, system::getStem(path))) { *fileError = true; return; }
	slot->set(s);
}

inline void sbLoadKbm(sb::ScaleSlot* slot, std::atomic<bool>* fileError) {
	std::string path = sbPickFile("Scala keyboard mapping (.kbm):kbm;All files:*");
	if (path.empty()) return;
	std::string text, err;
	sb::Scale s = slot->get();
	if (!sbReadTextFile(path, text) || !sb::parseKbm(text, s.kbm, err)) { *fileError = true; return; }
	slot->set(s);
}

// root: SBS's root setting (null for SBT, whose ROOT is a knob). sbt adds SBT's extra items.
inline void appendTuningMenu(ui::Menu* menu, sb::ScaleSlot* slot, std::atomic<int>* root, std::atomic<bool>* fileError,
                             bool sbt = false, std::function<void()> resetAll = nullptr) {
	auto build = [=](ui::Menu* sub) {
		const sb::Scale& cur = slot->get();
		sub->addChild(createMenuLabel(cur.name + string::f(" (%d notes%s)", cur.n(), cur.kbm.loaded ? ", .kbm" : "")));
		sub->addChild(createMenuItem("Load Scala scale (.scl)…", "", [=]() { sbLoadScl(slot, fileError); }));
		sub->addChild(createMenuItem("Load keyboard mapping (.kbm)…", "", [=]() { sbLoadKbm(slot, fileError); }));
		if (sbt) {
			sub->addChild(createMenuItem("Clear mapping", "", [=]() { sb::Scale s = slot->get(); s.kbm = sb::Kbm(); slot->set(s); },
			                             !cur.kbm.loaded));
		}
		sub->addChild(createSubmenuItem("Built-in scales", "", [=](ui::Menu* m2) {
			const auto& list = sb::builtinScales();
			for (int i = 0; i < (int) list.size(); i++) {
				m2->addChild(createCheckMenuItem(list[i].name, "",
					[=]() { sb::Scale b; sb::builtinScale(i, b); const sb::Scale& c = slot->get(); return !c.kbm.loaded && c.cents == b.cents; },
					[=]() { sb::Scale b; sb::builtinScale(i, b); slot->set(b); }));
			}
		}));
		sub->addChild(createIndexSubmenuItem("Key mapping", {"12 keys per period", "One key per degree"},
			[=]() { return slot->get().oneKeyPerDegree ? 1 : 0; },
			[=](int i) { sb::Scale s = slot->get(); s.oneKeyPerDegree = i == 1; slot->set(s); }));
		if (root) {
			sub->addChild(createIndexSubmenuItem("Root", {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"},
				[=]() { return root->load(); }, [=](int i) { root->store(i); }));
		}
		sub->addChild(createMenuItem(sbt ? "Reset slot to 12-tone equal" : "Reset to 12-tone equal", "",
			[=]() { slot->set(sb::equal12()); }));
		if (sbt && resetAll) sub->addChild(createMenuItem("Reset all slots", "", [=]() { resetAll(); }));
	};
	if (sbt) build(menu);
	else menu->addChild(createSubmenuItem("Tuning", slot->get().isEqual12() ? "12-tone equal" : slot->get().name, build));
}
