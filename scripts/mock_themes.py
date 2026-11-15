#!/usr/bin/env python3
"""Render the three panels in several colour themes to one HTML page per theme (mock-ups only;
nothing in res/ is touched).   python3 scripts/mock_themes.py OUTDIR"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_panels as mp

THEMES = {
    "A-clean-silver": dict(GROUND="#e3e5e7", BAND="#1a1b1d", INK="#16171a", SILVER="#eceef0", OUTBOX="#1a1b1d", TITLE="#eceef0", TEXTURE=False, RULE=None,
                           knob=None, display="#0d0f0d"),
    "B-white-orange": dict(GROUND="#f5f4f0", BAND="#f5f4f0", INK="#1a1a1a", SILVER="#f5f4f0", OUTBOX="#1a1a1a", TITLE="#1a1a1a", TEXTURE=False, RULE="#e8641b",
                           knob=None, display="#111311"),
    "C-matte-black": dict(GROUND="#1b1c1f", BAND="#1b1c1f", INK="#e9e9e6", SILVER="#1b1c1f", OUTBOX="#e9e9e6", TITLE="#e9e9e6", TEXTURE=False, RULE="#e8641b",
                          knob=("#3a3c40", "#2a2b2e"), display="#050605"),
    "D-warm-grey": dict(GROUND="#d8d5cd", BAND="#3b3a37", INK="#222120", SILVER="#ecebe6", OUTBOX="#3b3a37", TITLE="#ecebe6", TEXTURE=False, RULE=None,
                        knob=None, display="#0d0f0d"),
}
S = 5
out_dir = sys.argv[1]
os.makedirs(out_dir, exist_ok=True)
for name, t in THEMES.items():
    for k in ("GROUND", "BAND", "INK", "SILVER", "OUTBOX", "TITLE", "TEXTURE", "RULE"):
        setattr(mp, k, t[k])
    comps = mp.components()
    if t["knob"]:
        for k in ("SbKnob", "SbKnobSmall"):
            comps[k] = comps[k].replace("#0d0d0d", t["knob"][0]).replace("#1c1c1c", t["knob"][1])
    d = os.path.join(out_dir, name)
    os.makedirs(d, exist_ok=True)
    for n, svg in comps.items():
        open(os.path.join(d, n + ".svg"), "w").write(svg)
    html = ['<html><body style="background:#8a8a8a;margin:0;display:flex;gap:6px;padding:10px;align-items:flex-start">']
    for p in mp.P:
        svg = p.svg().replace('fill="#10130f"', f'fill="{t["display"]}"')
        open(os.path.join(d, p.slug + ".svg"), "w").write(svg)
        html.append(f'<div style="position:relative;width:{p.w*S}px;height:{mp.H*S}px">')
        html.append(f'<img src="{p.slug}.svg" style="position:absolute;left:0;top:0;width:{p.w*S}px;height:{mp.H*S}px">')
        for it in p.items:
            k, x, y = it["kind"], it["x"], it["y"]
            if k == "led":
                html.append(f'<div style="position:absolute;left:{(x-1)*S}px;top:{(y-1)*S}px;width:{2*S}px;height:{2*S}px;border-radius:50%;background:#e22"></div>')
                continue
            nm, w, h = {
                "knob": (it.get("k", {}).get("svg"), it.get("k", {}).get("size"), it.get("k", {}).get("size")),
                "jack": ("SbJack", 8, 8),
                "switch": ("Toggle2_0" if len(it.get("positions", [])) == 2 else "Toggle3_1", mp.SW_W, mp.SW_H),
                "button": ("SbButton_0", mp.BTN_W, mp.BTN_H),
                "wkey": ("SbKeyWhite_0", mp.WKEY_W, mp.WKEY_H),
                "bkey": ("SbKeyBlack_0", mp.BKEY_W, mp.BKEY_H),
            }[k]
            html.append(f'<img src="{nm}.svg" style="position:absolute;left:{(x-w/2)*S}px;top:{(y-h/2)*S}px;width:{w*S}px;height:{h*S}px">')
        html.append('</div>')
    html.append('</body></html>')
    open(os.path.join(d, "index.html"), "w").write("\n".join(html))
print("themes:", ", ".join(THEMES))
