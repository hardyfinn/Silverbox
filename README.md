# Silverbox

A TB-303-style voice, sequencer and expander for VCV Rack 2. This is a personal plugin, not affiliated with Roland.

| Module | |
|---|---|
| **SBV Silverbox Voice** | 303 voice: curved saw and shaped square, diode ladder, accent sweep, slide, plus BASS COMP and KEY TRACK |
| **SBS Silverbox Sequencer** | 303 sequencer: separate pitch and time programming, 64 patterns, 7 tracks, scramble, mutate, undo, direct edit |
| **SBX Silverbox Extended** | Expander for SBV with extended controls, each with CV |

- Manual: [docs/MANUAL.md](docs/MANUAL.md), also as a PDF: [docs/Silverbox-Manual.pdf](docs/Silverbox-Manual.pdf)
- Build spec: [docs/BUILD-INSTRUCTIONS.md](docs/BUILD-INSTRUCTIONS.md). Where the build differs from it: [docs/DECISIONS.md](docs/DECISIONS.md)

## Install (Mac, Apple silicon)

1. Open the latest run under **Actions → Build** on GitHub, and download the **Silverbox-mac-arm64** artifact. Unzip it to get `Silverbox-2.0.0-mac-arm64.vcvplugin`.
2. In Rack, choose **Help → Open user folder**. Put the `.vcvplugin` file in the `plugins-mac-arm64` folder there.
3. Restart Rack. Search the module browser for "Silverbox", "SBV", "SBS" or "303".

## Build from source

```
export RACK_DIR=/path/to/Rack-SDK
make dist          # plugin
make test          # model tests, no SDK needed
make render        # writes tests/out/demo.wav (the demo pattern through the voice)
make panels        # regenerate panels and layout (needs Python 3 + fonttools)
scripts/make_manual_pdf.sh   # PDF manual (needs pandoc and Node with Playwright)
```
