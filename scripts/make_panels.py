#!/usr/bin/env python3
"""Generate the Silverbox panels (res/*.svg), the component SVGs (res/components/*.svg)
and the widget placement header (src/generated/Layout.hpp) from one table.

Every control is placed from the same coordinates in both outputs, every control
gets its printed label, and the script fails if anything overlaps, sits too close,
or leaves the panel.

    python3 scripts/make_panels.py [--font /path/to/DejaVuSans-Bold.ttf]

Needs fontTools (pip install fonttools). Text is converted to paths, because Rack
does not render SVG text.
"""
import math
import os
import random
import sys

from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
if "--font" in sys.argv:
    FONT = sys.argv[sys.argv.index("--font") + 1]

# ---------------------------------------------------------------- look ------
GROUND = "#e3e5e7"
BAND = "#1a1b1d"
INK = "#16171a"
SILVER = "#eceef0"
OUTBOX = "#1a1b1d"
TITLE = SILVER      # title and brand lettering on the bands
TEXTURE = False     # brushed hairlines
RULE = None         # colour of a thin rule under the title band, or None
H = 128.5
HP = 5.08
TOP_BAND = 11.5
BOTTOM_BAND = 121.8
LABEL_CAP = 2.0
SMALL_CAP = 1.6
TITLE_CAP = 3.0
CONDENSE = 0.86

KNOB = dict(r=5.5, scale=6.6, box=7.0, svg="SbKnob", size=11.0)
SKNOB = dict(r=4.0, scale=5.0, box=5.4, svg="SbKnobSmall", size=8.0)
JACK_R = 4.0
JACK_BOX = 4.2
LED_R = 1.0
SW_W, SW_H = 5.6, 9.6
BTN_W, BTN_H = 7.0, 5.0
WKEY_W, WKEY_H = 7.6, 10.0
BKEY_W, BKEY_H = 6.0, 7.0

# ---------------------------------------------------------------- text ------
_font = TTFont(FONT)
_glyphs = _font.getGlyphSet()
_cmap = _font.getBestCmap()
_cap = getattr(_font["OS/2"], "sCapHeight", 0) or 0.729 * _font["head"].unitsPerEm
_hmtx = _font["hmtx"]


def text_width(s, cap):
    scale = cap / _cap
    w = 0
    for ch in s:
        g = _cmap.get(ord(ch))
        if g is None:
            raise SystemExit(f"font has no glyph for {ch!r}")
        w += _hmtx[g][0]
    return w * scale * CONDENSE


def text_path(s, x, y, cap, anchor="middle"):
    scale = cap / _cap
    w = text_width(s, cap)
    if anchor == "middle":
        x -= w / 2
    elif anchor == "end":
        x -= w
    d = []
    pen_x = x
    for ch in s:
        g = _cmap[ord(ch)]
        sp = SVGPathPen(_glyphs)
        tp = TransformPen(sp, (scale * CONDENSE, 0, 0, -scale, pen_x, y))
        _glyphs[g].draw(tp)
        d.append(sp.getCommands())
        pen_x += _hmtx[g][0] * scale * CONDENSE
    return " ".join(p for p in d if p)


# ---------------------------------------------------------------- model -----
class Box:
    def __init__(self, x0, y0, x1, y1, what):
        self.x0, self.y0, self.x1, self.y1, self.what = x0, y0, x1, y1, what

    def overlaps(self, o, pad=0.25):
        return not (self.x1 + pad <= o.x0 or o.x1 + pad <= self.x0 or self.y1 + pad <= o.y0 or o.y1 + pad <= self.y0)

    def inside(self, x0, y0, x1, y1):
        return self.x0 >= x0 and self.x1 <= x1 and self.y0 >= y0 and self.y1 <= y1


class Panel:
    def __init__(self, slug, title, hp, label_cap=LABEL_CAP):
        self.slug, self.title, self.hp = slug, title, hp
        self.w = hp * HP
        self.items = []      # dicts
        self.outboxes = []   # (x0, y0, x1, y1)
        self.links = []
        self.display = None  # (x0, y0, x1, y1)
        self.groups = []     # (x0, y0, x1, y1, title): thin outline, title breaking the top edge
        self.label_cap = label_cap

    def add(self, kind, pid, label, x, y, **ex):
        self.items.append(dict(kind=kind, pid=pid, label=label, x=x, y=y, **ex))

    def knob(self, pid, label, x, y, small=False, cap=None):
        self.add("knob", pid, label, x, y, k=SKNOB if small else KNOB, cap=cap or self.label_cap)

    def jack(self, pid, label, x, y, cap=None):
        assert pid.endswith("_INPUT") or pid.endswith("_OUTPUT"), pid
        self.add("jack", pid, label, x, y, cap=cap or self.label_cap)

    def switch(self, pid, label, positions, x, y):
        self.add("switch", pid, label, x, y, positions=positions, cap=self.label_cap)

    def button(self, pid, label, x, y, light=None, cap=None):
        self.add("button", pid, label, x, y, light=light, cap=cap or self.label_cap)

    def wkey(self, pid, label, x, y):
        self.add("wkey", pid, label, x, y, cap=self.label_cap)

    def bkey(self, pid, label, x, y):
        self.add("bkey", pid, label, x, y, cap=self.label_cap)

    def led(self, pid, label, x, y):
        self.add("led", pid, label, x, y, cap=SMALL_CAP)

    def group(self, title, x0, y0, x1, y1):
        self.groups.append((x0, y0, x1, y1, title))

    def group_title_box(self, g):
        x0, y0, x1, y1, t = g
        w = text_width(t, SMALL_CAP)
        cx = (x0 + x1) / 2
        return Box(cx - w / 2 - 0.8, y0 - SMALL_CAP / 2 - 0.3, cx + w / 2 + 0.8, y0 + SMALL_CAP / 2 + 0.3, f"group title {t}")

    def link(self, knob_id, jack_id):
        k = next(i for i in self.items if i["pid"] == knob_id)
        j = next(i for i in self.items if i["pid"] == jack_id)
        assert abs(k["x"] - j["x"]) < 0.01 and j["y"] > k["y"]
        self.links.append((k["x"], k["y"] + k["k"]["scale"] + 0.6, j["x"], j["y"] - JACK_R - 0.6))

    # ------------------------------------------------ geometry --
    def comp_box(self, it):
        x, y = it["x"], it["y"]
        kind = it["kind"]
        if kind == "knob":
            r = it["k"]["box"]
            return Box(x - r, y - r, x + r, y + r, it["pid"])
        if kind == "jack":
            return Box(x - JACK_BOX, y - JACK_BOX, x + JACK_BOX, y + JACK_BOX, it["pid"])
        if kind == "led":
            return Box(x - LED_R - 0.2, y - LED_R - 0.2, x + LED_R + 0.2, y + LED_R + 0.2, it["pid"])
        if kind == "switch":
            return Box(x - SW_W / 2, y - SW_H / 2, x + SW_W / 2, y + SW_H / 2, it["pid"])
        if kind == "button":
            return Box(x - BTN_W / 2, y - BTN_H / 2, x + BTN_W / 2, y + BTN_H / 2, it["pid"])
        if kind == "wkey":
            return Box(x - WKEY_W / 2, y - WKEY_H / 2, x + WKEY_W / 2, y + WKEY_H / 2, it["pid"])
        if kind == "bkey":
            return Box(x - BKEY_W / 2, y - BKEY_H / 2, x + BKEY_W / 2, y + BKEY_H / 2, it["pid"])
        raise ValueError(kind)

    def label_lines(self, it):
        out = []
        if not it["label"]:
            return out
        lines = it["label"].split("\n")
        cap = it["cap"]
        x, y = it["x"], it["y"]
        b = self.comp_box(it)
        kind = it["kind"]
        if kind == "knob":
            base = y - it["k"]["scale"] - 1.4
            for i, ln in enumerate(reversed(lines)):
                out.append((ln, x, base - i * (cap + 0.8), cap, "middle"))
        elif kind in ("jack", "button", "wkey", "led"):
            base = b.y1 + 1.0 + cap
            for i, ln in enumerate(lines):
                out.append((ln, x, base + i * (cap + 0.8), cap, "middle"))
        elif kind in ("switch", "bkey"):
            base = b.y0 - 1.0
            for i, ln in enumerate(reversed(lines)):
                out.append((ln, x, base - i * (cap + 0.8), cap, "middle"))
        return out

    def switch_labels(self, it):
        pos = it["positions"]
        x, y = it["x"], it["y"]
        ys = [y - 3.4, y + 3.4 + SMALL_CAP] if len(pos) == 2 else [y - 3.4, y + SMALL_CAP / 2, y + 3.4 + SMALL_CAP]
        return [(p, x + SW_W / 2 + 0.8, yy, SMALL_CAP, "start") for p, yy in zip(pos, ys)]

    def all_labels(self):
        out = []
        for it in self.items:
            out += [(t, it) for t in self.label_lines(it)]
            if it["kind"] == "switch":
                out += [(t, it) for t in self.switch_labels(it)]
        return out

    def boxes(self):
        b = [self.comp_box(it) for it in self.items]
        for (t, x, base, cap, anchor), it in self.all_labels():
            w = text_width(t, cap)
            x0 = x - w / 2 if anchor == "middle" else x if anchor == "start" else x - w
            b.append(Box(x0, base - cap, x0 + w, base, f"label '{t}' of {it['pid']}"))
        return b

    def check(self):
        errs = []
        bx = self.boxes()
        if self.display:
            bx.append(Box(*self.display, "display"))
        for g in self.groups:
            bx.append(self.group_title_box(g))
        for i, a in enumerate(bx):
            if a.x0 < 1.6 or a.x1 > self.w - 1.6 or a.y0 < TOP_BAND + 1.0 or a.y1 > BOTTOM_BAND - 1.0:
                errs.append(f"{a.what} leaves the panel area")
            for c in bx[i + 1:]:
                if a.overlaps(c):
                    errs.append(f"{a.what} overlaps {c.what}")
        for ob in self.outboxes:
            for a in bx:
                inside = a.inside(*ob)
                outside = a.x1 <= ob[0] or a.x0 >= ob[2] or a.y1 <= ob[1] or a.y0 >= ob[3]
                if not (inside or outside):
                    errs.append(f"{a.what} crosses an output box")
        for g in self.groups:
            for a in self.boxes():
                inside = a.inside(g[0], g[1], g[2], g[3])
                outside = a.x1 <= g[0] or a.x0 >= g[2] or a.y1 <= g[1] or a.y0 >= g[3]
                if not (inside or outside):
                    errs.append(f"{a.what} crosses the {g[4]} outline")
        for it in self.items:
            if it["kind"] == "jack" and it["pid"].endswith("_OUTPUT"):
                if not any(self.comp_box(it).inside(*ob) for ob in self.outboxes):
                    errs.append(f"{it['pid']} is not on an output box")
        comps = [it for it in self.items if it["kind"] in ("knob", "jack")]
        for i in range(len(comps)):
            for j in range(i + 1, len(comps)):
                a, c = comps[i], comps[j]
                d = math.hypot(a["x"] - c["x"], a["y"] - c["y"])
                need = {("knob", "knob"): 14.0, ("jack", "jack"): 10.0}.get((a["kind"], c["kind"]), 12.0)
                if d < need - 1e-6:
                    errs.append(f"{a['pid']} and {c['pid']} are {d:.1f} mm apart (need {need})")
        for it in self.items:
            if not it["label"]:
                errs.append(f"{it['pid']} has no label")
        return errs

    # ------------------------------------------------------------ output --
    def svg(self):
        w = self.w
        o = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{w:.2f}mm" height="{H}mm" viewBox="0 0 {w:.2f} {H}">']
        o.append(f'<rect x="0" y="0" width="{w:.2f}" height="{H}" fill="{GROUND}"/>')
        rnd = random.Random(self.slug)
        hair = []
        for _ in range(int(w * 4)):
            y = rnd.uniform(TOP_BAND, BOTTOM_BAND)
            x0 = rnd.uniform(-5, w)
            hair.append(f'<line x1="{x0:.2f}" y1="{y:.2f}" x2="{x0 + rnd.uniform(5, 30):.2f}" y2="{y:.2f}"/>')
        if TEXTURE:
            o.append(f'<g stroke="#ffffff" stroke-opacity="0.18" stroke-width="0.08">{"".join(hair[::2])}</g>')
            o.append(f'<g stroke="#000000" stroke-opacity="0.05" stroke-width="0.08">{"".join(hair[1::2])}</g>')
        o.append(f'<rect x="0" y="0" width="{w:.2f}" height="{TOP_BAND}" fill="{BAND}"/>')
        o.append(f'<rect x="0" y="{BOTTOM_BAND}" width="{w:.2f}" height="{H - BOTTOM_BAND:.2f}" fill="{BAND}"/>')
        if RULE:
            o.append(f'<rect x="0" y="{TOP_BAND - 0.5}" width="{w:.2f}" height="0.5" fill="{RULE}"/>')
            o.append(f'<rect x="0" y="{BOTTOM_BAND}" width="{w:.2f}" height="0.5" fill="{RULE}"/>')
        ink, silver, title = [], [], []
        cap = TITLE_CAP
        while text_width(self.title, cap) > w - 22:
            cap -= 0.1
        title.append(text_path(self.title, w / 2, TOP_BAND / 2 + cap / 2, cap))
        title.append(text_path("SILVERBOX", w / 2, BOTTOM_BAND + (H - BOTTOM_BAND) / 2 + SMALL_CAP / 2, SMALL_CAP))
        g = []
        for (x0, y0, x1, y1) in self.outboxes:
            g.append(f'<rect x="{x0:.2f}" y="{y0:.2f}" width="{x1 - x0:.2f}" height="{y1 - y0:.2f}" rx="1.4" fill="{OUTBOX}"/>')
        if self.display:
            x0, y0, x1, y1 = self.display
            g.append(f'<rect x="{x0 - 0.8:.2f}" y="{y0 - 0.8:.2f}" width="{x1 - x0 + 1.6:.2f}" height="{y1 - y0 + 1.6:.2f}" rx="1.2" fill="#2a2c2e"/>')
            g.append(f'<rect x="{x0:.2f}" y="{y0:.2f}" width="{x1 - x0:.2f}" height="{y1 - y0:.2f}" rx="0.6" fill="#0d0f0d"/>')
        for grp in self.groups:
            x0, y0, x1, y1, t = grp
            tb = self.group_title_box(grp)
            g.append(f'<path d="M{tb.x0:.2f},{y0:.2f} H{x0 + 1:.2f} Q{x0:.2f},{y0:.2f} {x0:.2f},{y0 + 1:.2f} V{y1 - 1:.2f} '
                     f'Q{x0:.2f},{y1:.2f} {x0 + 1:.2f},{y1:.2f} H{x1 - 1:.2f} Q{x1:.2f},{y1:.2f} {x1:.2f},{y1 - 1:.2f} '
                     f'V{y0 + 1:.2f} Q{x1:.2f},{y0:.2f} {x1 - 1:.2f},{y0:.2f} H{tb.x1:.2f}" fill="none" stroke="{INK}" stroke-width="0.3"/>')
        for (lx0, ly0, lx1, ly1) in self.links:
            g.append(f'<line x1="{lx0:.2f}" y1="{ly0:.2f}" x2="{lx1:.2f}" y2="{ly1:.2f}" stroke="{INK}" stroke-width="0.3"/>')
        for it in self.items:
            x, y = it["x"], it["y"]
            if it["kind"] == "knob":
                r = it["k"]["scale"]
                a0, a1 = math.radians(-135 - 90), math.radians(135 - 90)
                sx, sy = x + r * math.cos(a0), y + r * math.sin(a0)
                ex, ey = x + r * math.cos(a1), y + r * math.sin(a1)
                g.append(f'<path d="M{sx:.2f},{sy:.2f} A{r},{r} 0 1 1 {ex:.2f},{ey:.2f}" fill="none" stroke="{INK}" stroke-width="0.3"/>')
                for t in range(11):
                    a = math.radians(-135 + 27 * t - 90)
                    g.append(f'<line x1="{x + r * math.cos(a):.2f}" y1="{y + r * math.sin(a):.2f}" x2="{x + (r + 0.7) * math.cos(a):.2f}" y2="{y + (r + 0.7) * math.sin(a):.2f}" stroke="{INK}" stroke-width="0.25"/>')
        def on_outbox(x, y):
            return any(ob[0] <= x <= ob[2] and ob[1] <= y <= ob[3] for ob in self.outboxes)
        for (t, x, base, cap_, anchor), it in self.all_labels():
            (silver if on_outbox(x, base - cap_ / 2) else ink).append(text_path(t, x, base, cap_, anchor))
        for grp in self.groups:
            ink.append(text_path(grp[4], (grp[0] + grp[2]) / 2, grp[1] + SMALL_CAP / 2, SMALL_CAP))
        o.append("".join(g))
        o.append(f'<path fill="{INK}" d="{" ".join(ink)}"/>')
        o.append(f'<path fill="{SILVER}" d="{" ".join(silver)}"/>')
        o.append(f'<path fill="{TITLE}" d="{" ".join(title)}"/>')
        o.append("</svg>")
        return "\n".join(o)

    def cpp(self):
        L = [f"template <class M, class W>\ninline void place{self.slug}(W* w, M* m) {{"]
        for it in self.items:
            v = f"mm2px(Vec({it['x']:.3f}f, {it['y']:.3f}f))"
            pid, kind = it["pid"], it["kind"]
            if kind == "knob":
                L.append(f"\tw->addParam(createParamCentered<{it['k']['svg']}>({v}, m, M::{pid}));")
            elif kind == "switch":
                cls = "SbToggle2" if len(it["positions"]) == 2 else "SbToggle3"
                L.append(f"\tw->addParam(createParamCentered<{cls}>({v}, m, M::{pid}));")
            elif kind == "jack":
                fn, add = ("createInputCentered", "addInput") if pid.endswith("_INPUT") else ("createOutputCentered", "addOutput")
                L.append(f"\tw->{add}({fn}<SbJack>({v}, m, M::{pid}));")
            elif kind == "led":
                L.append(f"\tw->addChild(createLightCentered<SmallLight<RedLight>>({v}, m, M::{pid}));")
            elif kind == "button":
                if it["light"]:
                    L.append(f"\tw->addParam(createLightParamCentered<SbLitButton>({v}, m, M::{pid}, M::{it['light']}));")
                else:
                    L.append(f"\tw->addParam(createParamCentered<SbButton>({v}, m, M::{pid}));")
            elif kind == "wkey":
                L.append(f"\tw->addParam(createParamCentered<SbKeyWhite>({v}, m, M::{pid}));")
            elif kind == "bkey":
                L.append(f"\tw->addParam(createParamCentered<SbKeyBlack>({v}, m, M::{pid}));")
        L.append("}\n")
        if self.display:
            x0, y0, x1, y1 = self.display
            L.append(f"constexpr float {self.slug}_DISPLAY_X = {x0:.3f}f, {self.slug}_DISPLAY_Y = {y0:.3f}f, "
                     f"{self.slug}_DISPLAY_W = {x1 - x0:.3f}f, {self.slug}_DISPLAY_H = {y1 - y0:.3f}f;\n")
        return "\n".join(L)

    def labels(self):
        """(pid, label) for every control, for the label check in the module tables."""
        return [(it["pid"], it["label"].replace("\n", " ")) for it in self.items]


P = []

# ---------------------------------------------------------------- SBV -------
p = Panel("SBV", "SBV · VOICE", 16)
c3 = [13.5, 40.64, 67.78]
for x, (pid, lab) in zip(c3, [("TUNING_PARAM", "TUNING"), ("CUTOFF_PARAM", "CUT OFF FREQ"), ("RESONANCE_PARAM", "RESONANCE")]):
    p.knob(pid, lab, x, 27)
for x, (pid, lab) in zip(c3, [("ENVMOD_PARAM", "ENV MOD"), ("DECAY_PARAM", "DECAY"), ("ACCENT_PARAM", "ACCENT")]):
    p.knob(pid, lab, x, 49)
p.switch("WAVE_PARAM", "WAVEFORM", ["SAW", "SQUARE"], 12.0, 68.5)
p.knob("BASSCOMP_PARAM", "BASS COMP", c3[1], 68, small=True)
p.knob("KEYTRACK_PARAM", "KEY TRACK", c3[2], 68, small=True)
c5 = [10.6, 25.6, 40.64, 55.6, 70.6]
for x, (pid, lab) in zip(c5, [("VOCT_INPUT", "1V/OCT"), ("GATE_INPUT", "GATE"), ("ACCENT_INPUT", "ACCENT"),
                              ("SLIDE_INPUT", "SLIDE"), ("CUTOFF_INPUT", "CUTOFF CV")]):
    p.jack(pid, lab, x, 91)
for x, (pid, lab) in zip(c5[1:4], [("GATE_LIGHT", "GATE"), ("ACCENT_LIGHT", "ACCENT"), ("SLIDE_LIGHT", "SLIDE")]):
    p.led(pid, lab, x, 79.5)
p.jack("AUDIO_INPUT", "AUDIO IN", c5[0], 107)
p.outboxes.append((32.6, 100.6, 78.6, 119.6))
for x, (pid, lab) in zip(c5[2:], [("FENV_OUTPUT", "FILTER\nENV"), ("SWEEP_OUTPUT", "ACCENT\nSWEEP"), ("OUT_OUTPUT", "OUT")]):
    p.jack(pid, lab, x, 107)
P.append(p)

# ---------------------------------------------------------------- SBX -------
p = Panel("SBX", "SBX · EXTENDED", 12, label_cap=1.8)
c3 = [10.5, 30.48, 50.46]
rows = [(26, [("VEGDECAY", "VEG DECAY"), ("ACCDECAY", "ACCENT DECAY"), ("SLIDETIME", "SLIDE TIME")]),
        (56, [("SOFTATTACK", "SOFT ATTACK"), ("SWEEPSPEED", "SWEEP SPEED"), ("FILTERFM", "FILTER FM")]),
        (86, [("OVERDRIVE", "OVERDRIVE")])]
for y, row in rows:
    for x, (pid, lab) in zip(c3, row):
        p.knob(pid + "_PARAM", lab, x, y, small=True)
        p.jack(pid + "_INPUT", "CV", x, y + 13)
        p.link(pid + "_PARAM", pid + "_INPUT")
p.switch("DECAYRANGE_PARAM", "DECAY RANGE", ["STOCK", "WIDE"], c3[1] - 2.0, 92)
p.switch("MUFFLER_PARAM", "MUFFLER", ["OFF", "SOFT", "HARD"], c3[2] - 2.5, 92)
p.led("LINKED_LIGHT", "LINKED", c3[1], 112)
P.append(p)

# ---------------------------------------------------------------- SBS -------
p = Panel("SBS", "SBS · SEQUENCER", 32)
W = p.w
p.display = (6.0, 14.0, W - 6.0, 44.0)
for i, (lab) in enumerate(["PITCH\nWRITE", "TIME\nWRITE", "PATTERN\nPLAY", "TRACK\nWRITE", "TRACK\nPLAY"]):
    p.button(f"MODE{i}_PARAM", lab, 12 + 16 * i, 51, light=f"MODE{i}_LIGHT")
p.button("RUN_PARAM", "RUN", 96, 51, light="RUN_LIGHT")
p.button("CLEAR_PARAM", "CLEAR", 112, 51, light="CLEAR_LIGHT")
p.button("SCRAMBLE_PARAM", "SCRAMBLE", 128, 51, light="SCRAMBLE_LIGHT")
p.button("UNDO_PARAM", "UNDO", 144, 51)
for x, (pid, lab) in zip([14, 32, 50, 68, 86, 104], [("PATTERN_PARAM", "PATTERN"), ("TRACK_PARAM", "TRACK"), ("TEMPO_PARAM", "TEMPO"),
                                                    ("GATELEN_PARAM", "GATE LENGTH"), ("LENGTH_PARAM", "LENGTH"), ("MUTATE_PARAM", "MUTATE")]):
    p.knob(pid, lab, x, 72)
p.switch("TRIPLET_PARAM", "TRIPLET", ["OFF", "ON"], 119, 73)
p.button("SHIFTP_PARAM", "SHIFT\nPITCH", 137, 72, cap=1.8)
p.button("SHIFTT_PARAM", "SHIFT\nTIME", 151, 72, cap=1.8)
# keyboard: white keys C D E F G A B C', black keys above
whites = [0, 2, 4, 5, 7, 9, 11, 12]
names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B", "HI C"]
wx = [10 + 9.5 * i for i in range(8)]
for x, n in zip(wx, whites):
    p.wkey(f"KEY{n}_PARAM", names[n], x, 96)
for n, (a, b) in [(1, (0, 1)), (3, (1, 2)), (6, (3, 4)), (8, (4, 5)), (10, (5, 6))]:
    p.bkey(f"KEY{n}_PARAM", names[n], (wx[a] + wx[b]) / 2, 86)
fb = [("DOWN_PARAM", "DOWN", "DOWN_LIGHT"), ("UP_PARAM", "UP", "UP_LIGHT"), ("ACC_PARAM", "ACCENT", "ACC_LIGHT"),
      ("SLIDEB_PARAM", "SLIDE", "SLIDEB_LIGHT"), ("NOTE_PARAM", "NOTE", None), ("TIE_PARAM", "TIE", None),
      ("REST_PARAM", "REST", None), ("BACK_PARAM", "BACK", None), ("NEXT_PARAM", "NEXT", None)]
for i, (pid, lab, light) in enumerate(fb):
    p.button(pid, lab, 8.0 + 9.1 * i, 112, light=light, cap=1.8)
jx = [91 + 12.8 * i for i in range(6)]
for x, (pid, lab) in zip(jx, [("CLOCK_INPUT", "CLOCK"), ("RESET_INPUT", "RESET"), ("TRANSPOSE_INPUT", "TRANSPOSE"),
                             ("RUN_INPUT", "RUN"), ("PATTERNCV_INPUT", "PATTERN\nCV"), ("MUTATECV_INPUT", "MUTATE\nCV")]):
    p.jack(pid, lab, x, 88, cap=1.8)
p.outboxes.append((85.0, 100.4, W - 2.0, 119.8))
for x, (pid, lab) in zip(jx, [("VOCT_OUTPUT", "1V/OCT"), ("GATE_OUTPUT", "GATE"), ("ACCENT_OUTPUT", "ACCENT"),
                             ("SLIDE_OUTPUT", "SLIDE"), ("CLOCK_OUTPUT", "CLOCK"), ("END_OUTPUT", "END")]):
    p.jack(pid, lab, x, 107, cap=1.8)
P.append(p)


# ---------------------------------------------------------------- SBO (round 2) --
p = Panel("SBO", "SBO · OSCILLATOR", 20)
c6 = [9.5, 26.0, 42.5, 59.0, 75.5, 92.0]
for x, (pid, lab) in zip([12.0, 31.4, 50.8, 70.2, 89.6], [("TUNING_PARAM", "TUNING"), ("PW_PARAM", "PULSE WIDTH"), ("MIX_PARAM", "WAVE MIX"),
                                                         ("SUB_PARAM", "SUB"), ("FOLD_PARAM", "FOLD")]):
    p.knob(pid, lab, x, 26)
p.knob("SYMMETRY_PARAM", "SYMMETRY", c6[0], 47)
for x, (pid, lab) in zip(c6[1:], [("PWM_PARAM", "PWM"), ("FOLDCV_PARAM", "FOLD CV"), ("FM_PARAM", "FM"),
                                  ("SLIDETIME_PARAM", "SLIDE TIME"), ("DRIFT_PARAM", "DRIFT")]):
    p.knob(pid, lab, x, 47, small=True)
p.group("TRIM", 4.0, 58.5, p.w - 4.0, 75.5)
for x, (pid, lab) in zip([16.0, 38.0, 60.0, 82.0], [("TUNE_PARAM", "TUNE"), ("WIDTH_PARAM", "WIDTH"), ("CURVE_PARAM", "SAW CURVE"),
                                                   ("SHAPE_PARAM", "SQUARE SHAPE")]):
    p.knob(pid, lab, x, 68.5, small=True, cap=1.8)
jx = [7.5 + 12.3 * i for i in range(8)]
for x, (pid, lab) in zip(jx, [("VOCT_INPUT", "1V/OCT"), ("SLIDE_INPUT", "SLIDE"), ("PWM_INPUT", "PWM"), ("MIX_INPUT", "MIX CV"),
                             ("FOLD_INPUT", "FOLD"), ("FM_INPUT", "FM"), ("SYNC_INPUT", "SYNC"), ("FOLDIN_INPUT", "FOLD IN")]):
    p.jack(pid, lab, x, 88, cap=1.8)
p.outboxes.append((12.0, 100.6, p.w - 12.0, 119.6))
for x, (pid, lab) in zip([22.0, 41.0, 60.0, 79.0], [("SAW_OUTPUT", "SAW"), ("SQUARE_OUTPUT", "SQUARE"), ("SUB_OUTPUT", "SUB"), ("OUT_OUTPUT", "OUT")]):
    p.jack(pid, lab, x, 107)
P.append(p)

# ---------------------------------------------------------------- SBF (round 2) --
p = Panel("SBF", "SBF · FILTER", 28)
c5 = [16.0, 43.0, 70.0, 97.0, 124.0]
for x, (pid, lab, cv, cvl) in zip(c5, [("CUTOFF", "CUT OFF FREQ", "CUTOFF", "CUTOFF CV"), ("RESONANCE", "RESONANCE", "RES", "RES CV"),
                                       ("ENVMOD", "ENV MOD", "ENVMOD", "ENV MOD CV"), ("DECAY", "DECAY", "DECAY", "DECAY CV"),
                                       ("ACCENT", "ACCENT", "ACCENTCV", "ACCENT CV")]):
    p.knob(pid + "_PARAM", lab, x, 25)
    p.jack(cv + "_INPUT", cvl, x, 39, cap=1.8)
    p.link(pid + "_PARAM", cv + "_INPUT")
p.switch("MODE_PARAM", "MODE", ["LP", "BP", "HP"], 9.0, 57.5)
for x, (pid, lab) in zip([28.0, 47.0, 66.0, 85.0, 104.0, 123.0], [("DRIVE_PARAM", "DRIVE"), ("BASSCOMP_PARAM", "BASS COMP"),
                                                               ("KEYTRACK_PARAM", "KEY TRACK"), ("FM_PARAM", "FM"),
                                                               ("VCADECAY_PARAM", "VCA DECAY"), ("ACCDECAY_PARAM", "ACCENT DECAY")]):
    p.knob(pid, lab, x, 57.0, small=True, cap=1.8)
p.group("TRIM", 4.0, 65.2, p.w - 4.0, 97.0)
tx = [14.0 + 22.8 * i for i in range(6)]
for x, (pid, lab) in zip(tx, [("CUTTRIM_PARAM", "CUTOFF TRIM"), ("RESTRIM_PARAM", "RES TRIM"), ("GATETRIM_PARAM", "GATE TRIM"),
                             ("BASS_PARAM", "BASS"), ("POLES_PARAM", "POLES"), ("ENVBIAS_PARAM", "ENV BIAS")]):
    p.knob(pid, lab, x, 75.0, small=True, cap=1.8)
for x, (pid, lab) in zip(tx[:3] + tx[4:], [("ENVATTACK_PARAM", "ENV ATTACK"), ("RELEASE_PARAM", "RELEASE"), ("ACCVCA_PARAM", "ACCENT VCA"),
                                          ("SWEEPSHAPE_PARAM", "SWEEP SHAPE"), ("CLICK_PARAM", "CLICK")]):
    p.knob(pid, lab, x, 89.5, small=True, cap=1.8)
p.switch("SWEEP_PARAM", "SWEEP", ["GANGED", "FREE"], tx[3] - 3.0, 90.0)
jx = [8.0 + 10.6 * i for i in range(7)]
for x, (pid, lab) in zip(jx, [("IN_INPUT", "IN"), ("GATE_INPUT", "GATE"), ("TRIG_INPUT", "TRIG"), ("ACCENT_INPUT", "ACCENT"),
                             ("VOCT_INPUT", "1V/OCT"), ("FM_INPUT", "FM"), ("ENVIN_INPUT", "ENV IN")]):
    p.jack(pid, lab, x, 110, cap=1.8)
p.led("GATE_LIGHT", "GATE", jx[1], 100.5)
p.led("ACCENT_LIGHT", "ACCENT", jx[3], 100.5)
p.outboxes.append((79.4, 102.6, p.w - 2.0, 120.2))
for x, (pid, lab) in zip([86.0, 98.0, 110.0, 122.0, 134.0], [("OUT_OUTPUT", "OUT"), ("FILTER_OUTPUT", "FILTER\nOUT"),
                                                           ("FENV_OUTPUT", "FILTER\nENV"), ("SWEEP_OUTPUT", "ACCENT\nSWEEP"),
                                                           ("VCAENV_OUTPUT", "VCA\nENV")]):
    p.jack(pid, lab, x, 108, cap=1.8)
P.append(p)

# ---------------------------------------------------------------- SBT (round 2) --
p = Panel("SBT", "SBT · TUNER", 8, label_cap=1.8)
p.display = (4.0, 14.5, p.w - 4.0, 33.0)
p.knob("SCALE_PARAM", "SCALE", 11.0, 47)
p.knob("ROOT_PARAM", "ROOT", 29.64, 47)
p.knob("FINE_PARAM", "FINE", 11.0, 67, small=True)
p.button("LOAD_PARAM", "LOAD", 29.64, 64.5)
p.led("ACTIVE_LIGHT", "ACTIVE", 29.64, 74.0)
p.jack("VOCT_INPUT", "1V/OCT IN", 11.0, 90)
p.jack("SCALE_INPUT", "SCALE CV", 29.64, 90)
p.outboxes.append((10.3, 100.6, 30.3, 119.6))
p.jack("VOCT_OUTPUT", "1V/OCT OUT", 20.32, 107)
P.append(p)

# ---------------------------------------------------------------- components --
def components():
    c = {}
    c["SbJack"] = '''<svg xmlns="http://www.w3.org/2000/svg" width="8mm" height="8mm" viewBox="0 0 8 8">
<circle cx="4" cy="4" r="4" fill="#9a9ea2"/>
<polygon points="4,0.35 7.16,2.17 7.16,5.83 4,7.65 0.84,5.83 0.84,2.17" fill="#3b3e41" stroke="#17191a" stroke-width="0.25"/>
<circle cx="4" cy="4" r="2.6" fill="#c2c5c8"/>
<circle cx="4" cy="4" r="1.85" fill="#0b0b0b"/>
</svg>'''

    def knob(size, name):
        r = size / 2
        c[name] = f'''<svg xmlns="http://www.w3.org/2000/svg" width="{size}mm" height="{size}mm" viewBox="0 0 {size} {size}">
<circle cx="{r}" cy="{r}" r="{r}" fill="#0d0d0d"/>
<circle cx="{r}" cy="{r}" r="{r * 0.78:.3f}" fill="#1c1c1c" stroke="#333" stroke-width="0.25"/>
<circle cx="{r * 0.9:.3f}" cy="{r * 0.82:.3f}" r="{r * 0.5:.3f}" fill="#ffffff" fill-opacity="0.05"/>
<line x1="{r}" y1="{r * 0.12:.3f}" x2="{r}" y2="{r * 0.72:.3f}" stroke="#f2f2f2" stroke-width="{size * 0.065:.3f}" stroke-linecap="round"/>
</svg>'''
    knob(KNOB["size"], "SbKnob")
    knob(SKNOB["size"], "SbKnobSmall")

    def rect_btn(name, w, h, fill, edge, pressed):
        inset = 0.35 if pressed else 0.0
        c[name] = f'''<svg xmlns="http://www.w3.org/2000/svg" width="{w}mm" height="{h}mm" viewBox="0 0 {w} {h}">
<rect x="0" y="0" width="{w}" height="{h}" rx="0.7" fill="{edge}"/>
<rect x="{0.4 + inset:.2f}" y="{0.4 + inset:.2f}" width="{w - 0.8 - 2 * inset:.2f}" height="{h - 0.8 - 2 * inset:.2f}" rx="0.5" fill="{fill}"/>
</svg>'''
    rect_btn("SbButton_0", BTN_W, BTN_H, "#8d9195", "#3a3d40", False)
    rect_btn("SbButton_1", BTN_W, BTN_H, "#73777b", "#3a3d40", True)
    rect_btn("SbKeyWhite_0", WKEY_W, WKEY_H, "#f1f1ee", "#5b5e61", False)
    rect_btn("SbKeyWhite_1", WKEY_W, WKEY_H, "#d6d6d2", "#5b5e61", True)
    rect_btn("SbKeyBlack_0", BKEY_W, BKEY_H, "#1d1d1d", "#000000", False)
    rect_btn("SbKeyBlack_1", BKEY_W, BKEY_H, "#3a3a3a", "#000000", True)

    def toggle(frame):
        d = {0: -1, 1: 0, 2: 1}[frame]
        cx, cy = 2.8, 4.8
        tip = cy + d * 3.6
        handle = (f'<line x1="{cx}" y1="{cy}" x2="{cx}" y2="{tip}" stroke="#e4e4e4" stroke-width="1.3" stroke-linecap="round"/>'
                  f'<circle cx="{cx}" cy="{tip}" r="0.9" fill="#f6f6f6" stroke="#888" stroke-width="0.15"/>') if d else \
            f'<circle cx="{cx}" cy="{cy}" r="1.3" fill="#f6f6f6" stroke="#888" stroke-width="0.2"/>'
        return f'''<svg xmlns="http://www.w3.org/2000/svg" width="5.6mm" height="9.6mm" viewBox="0 0 5.6 9.6">
<rect x="0.3" y="0.3" width="5.0" height="9.0" rx="0.6" fill="#2b2b2b"/>
<circle cx="{cx}" cy="{cy}" r="2.1" fill="#9a9a9a" stroke="#6a6a6a" stroke-width="0.25"/>
<circle cx="{cx}" cy="{cy}" r="1.4" fill="#c8c8c8"/>
{handle}
</svg>'''
    c["Toggle2_0"] = toggle(0)
    c["Toggle2_1"] = toggle(2)
    c["Toggle3_0"] = toggle(0)
    c["Toggle3_1"] = toggle(1)
    c["Toggle3_2"] = toggle(2)
    return c


def main():
    errs = []
    for p in P:
        for e in p.check():
            errs.append(f"{p.slug}: {e}")
    if errs:
        print("\n".join(errs))
        raise SystemExit(f"{len(errs)} layout problems")
    os.makedirs(os.path.join(ROOT, "res", "components"), exist_ok=True)
    os.makedirs(os.path.join(ROOT, "src", "generated"), exist_ok=True)
    for p in P:
        with open(os.path.join(ROOT, "res", f"{p.slug}.svg"), "w") as f:
            f.write(p.svg())
    for name, svg in components().items():
        with open(os.path.join(ROOT, "res", "components", f"{name}.svg"), "w") as f:
            f.write(svg)
    with open(os.path.join(ROOT, "src", "generated", "Layout.hpp"), "w") as f:
        f.write("// Generated by scripts/make_panels.py. Do not edit.\n#pragma once\n#include \"../widgets/SilverboxComponents.hpp\"\n\n")
        for p in P:
            f.write(p.cpp())
    with open(os.path.join(ROOT, "src", "generated", "labels.txt"), "w") as f:
        for p in P:
            for pid, lab in p.labels():
                f.write(f"{p.slug} {pid} {lab}\n")
    print(f"wrote {len(P)} panels")


if __name__ == "__main__":
    main()
