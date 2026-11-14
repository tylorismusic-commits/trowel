# Changelog

## 0.1.0

- Initial release of **Trowel** and **Trowel Master**, two stereo LV2/CLAP EQ
  suites sharing one DSP and UI codebase.
- Pultec EQP-1A-style Colour section with the low boost/cut interaction, high
  boost with bandwidth, high cut, and optional Iron transformer colour.
- SSL 4000E-style Console section: four stepped bands, fixed Q, LF/HF
  shelf/bell switching, 18 dB/oct HPF and 12 dB/oct LPF.
- Zero-latency Trowel Tame: 16-band adaptive dynamic resonance suppressor.
- Trowel Master Tame: 4096-point spectral suppressor with M/S, auto-gain,
  whole-strip delta and fixed 4095-sample latency.
- Six signal routings with click-free reordering, per-module bypass, master
  bypass, and a shared response scope.
- Eight in-UI presets, four LV2 preset resources per plugin, and a wide
  resizable X11/Cairo console interface.
- LV2 inline-display gain reduction and CLAP gain-adjustment metering.
