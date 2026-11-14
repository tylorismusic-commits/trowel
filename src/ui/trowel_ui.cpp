// Trowel — X11/Cairo console-strip UI
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <cairo/cairo-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <lv2/atom/atom.h>
#include <lv2/options/options.h>
#include <lv2/ui/ui.h>
#include <lv2/urid/urid.h>

#include "param_info.hpp"
#include "hearth_theme.hpp"
#include "pultec.hpp"
#include "ssl_eq.hpp"

namespace trowel {
namespace ui {

// Logical layout.  The UI is rendered at this size and scaled to whatever the
// host gives us, so it stays sharp and fits small and large displays.
constexpr int UI_W = 1240;
constexpr int UI_H = 920;
constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------------------
// Palette — the Hearth family slate/cream/red, with a verdigris accent for the
// Tame section. The values are loaded from the selected suite skin at runtime.
// ---------------------------------------------------------------------------
using Color = hearth::theme::Color;

static Color kBg;
static Color kBgTop;
static Color kBgBottom;
static Color kHeaderTop;
static Color kHeaderBottom;
static Color kPanelTop;
static Color kPanelBottom;
static Color kPanelEdge;
static Color kPanelInner;
static Color kPanelHeaderTop;
static Color kPanelHeaderBottom;
static Color kPanelHeaderText;
static Color kText;
static Color kCream;
static Color kMuted;
static Color kRed;
static Color kRedDark;
static Color kAmber;
static Color kKnobTop;
static Color kKnobBottom;
static Color kKnobRing;
static Color kSilver;
static Color kTrack;
static Color kMeterFace;
static Color kMeterInk;
static Color kGreen;
static Color kVerdigris;
static Color kScopeBgTop;
static Color kScopeBgBottom;
static Color kScopeGrid;
static Color kScopeGreen;
static Color kHighlight;
static Color kShadow;
static Color kChassisLight;
static Color kChassisMid;
static Color kChassisDark;
static double gTextureScale = 1.0;

static void applyTheme(const hearth::theme::Theme& t) {
  kBg = t.background;
  kBgTop = t.background_top;
  kBgBottom = t.background_bottom;
  kHeaderTop = t.header_top;
  kHeaderBottom = t.header_bottom;
  kPanelTop = t.panel_top;
  kPanelBottom = t.panel_bottom;
  kPanelEdge = t.panel_edge;
  kPanelInner = t.panel_inner;
  kPanelHeaderTop = t.panel_header_top;
  kPanelHeaderBottom = t.panel_header_bottom;
  kPanelHeaderText = t.panel_header_text;
  kText = t.text;
  kCream = t.cream;
  kMuted = t.muted;
  kRed = t.accent;
  kRedDark = t.accent_dark;
  kAmber = t.amber;
  kKnobTop = t.knob_top;
  kKnobBottom = t.knob_bottom;
  kKnobRing = t.knob_ring;
  kSilver = t.silver;
  kTrack = t.track;
  kMeterFace = t.meter_face;
  kMeterInk = t.meter_ink;
  kGreen = t.green;
  kVerdigris = t.verdigris;
  kScopeBgTop = t.scope_bg_top;
  kScopeBgBottom = t.scope_bg_bottom;
  kScopeGrid = t.scope_grid;
  kScopeGreen = t.scope_trace;
  kHighlight = t.highlight;
  kShadow = t.shadow;
  kChassisLight = t.chassis_light;
  kChassisMid = t.chassis_mid;
  kChassisDark = t.chassis_dark;
  const double base = 0.06;
  gTextureScale = base > 0.0 ? std::min(std::max(t.texture_opacity / base, 0.0), 2.0)
                            : 0.0;
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
enum class Kind { Knob, Switch, Toggle, Fader, Vu, Meter };

struct Widget {
  Kind kind;
  uint32_t port;
  bool bipolar;
  float cx, cy, r;
  float x, y, w, h;
};

constexpr Widget K(uint32_t port, float cx, float cy, float r,
                   bool bipolar = false) {
  return Widget{Kind::Knob, port, bipolar, cx, cy, r, 0, 0, 0, 0};
}
constexpr Widget S(uint32_t port, float cx, float cy, float r) {
  return Widget{Kind::Switch, port, false, cx, cy, r, 0, 0, 0, 0};
}
constexpr Widget T(uint32_t port, float x, float y, float w, float h) {
  return Widget{Kind::Toggle, port, false, 0, 0, 0, x, y, w, h};
}
constexpr Widget F(uint32_t port, float x, float y, float w, float h) {
  return Widget{Kind::Fader, port, false, 0, 0, 0, x, y, w, h};
}
constexpr Widget V(uint32_t port, float x, float y, float w, float h) {
  return Widget{Kind::Vu, port, false, 0, 0, 0, x, y, w, h};
}
constexpr Widget M(uint32_t port, float x, float y, float w, float h) {
  return Widget{Kind::Meter, port, false, 0, 0, 0, x, y, w, h};
}

// Panel geometry:
//   Colour  14,360,370,520
//   Console 396,360,370,520
//   Tame    778,360,350,520
//   I/O     1140,360,86,520
constexpr Widget kWidgets[] = {
    // Colour / Pultec
    T(PORT_PULTEC_ON, 302.0f, 368.0f, 58.0f, 24.0f),
    S(PORT_PUL_LOW_FREQ, 70.0f, 455.0f, 26.0f),
    K(PORT_PUL_LOW_BOOST, 175.0f, 455.0f, 27.0f),
    K(PORT_PUL_LOW_CUT, 280.0f, 455.0f, 27.0f),
    S(PORT_PUL_HIGH_FREQ, 70.0f, 575.0f, 26.0f),
    K(PORT_PUL_HIGH_BOOST, 175.0f, 575.0f, 27.0f),
    K(PORT_PUL_BANDWIDTH, 280.0f, 575.0f, 27.0f),
    S(PORT_PUL_HIGH_CUT_FREQ, 70.0f, 695.0f, 26.0f),
    K(PORT_PUL_HIGH_CUT, 175.0f, 695.0f, 27.0f),
    T(PORT_PUL_IRON, 240.0f, 700.0f, 90.0f, 26.0f),

    // Console / SSL
    T(PORT_SSL_ON, 684.0f, 368.0f, 58.0f, 24.0f),
    S(PORT_SSL_LF_FREQ, 452.0f, 440.0f, 25.0f),
    K(PORT_SSL_LF_GAIN, 552.0f, 440.0f, 26.0f, true),
    T(PORT_SSL_LF_BELL, 622.0f, 429.0f, 88.0f, 24.0f),
    S(PORT_SSL_LMF_FREQ, 452.0f, 555.0f, 25.0f),
    K(PORT_SSL_LMF_GAIN, 552.0f, 555.0f, 26.0f, true),
    S(PORT_SSL_HMF_FREQ, 652.0f, 555.0f, 25.0f),
    K(PORT_SSL_HMF_GAIN, 452.0f, 670.0f, 26.0f, true),
    S(PORT_SSL_HF_FREQ, 552.0f, 670.0f, 25.0f),
    K(PORT_SSL_HF_GAIN, 652.0f, 670.0f, 26.0f, true),
    T(PORT_SSL_HF_BELL, 420.0f, 773.0f, 88.0f, 24.0f),
    S(PORT_SSL_HPF, 560.0f, 785.0f, 25.0f),
    S(PORT_SSL_LPF, 660.0f, 785.0f, 25.0f),

    // Tame
    T(PORT_TAME_ON, 1046.0f, 368.0f, 58.0f, 24.0f),
    K(PORT_TAME_AMOUNT, 838.0f, 455.0f, 27.0f),
    K(PORT_TAME_FOCUS, 928.0f, 455.0f, 27.0f),
    K(PORT_TAME_SELECTIVITY, 1018.0f, 455.0f, 27.0f),
    K(PORT_TAME_SENSITIVITY, 838.0f, 570.0f, 27.0f),
    K(PORT_TAME_ATTACK, 928.0f, 570.0f, 27.0f),
    K(PORT_TAME_RELEASE, 1018.0f, 570.0f, 27.0f),
    K(PORT_TAME_MIX, 838.0f, 685.0f, 27.0f),
    T(PORT_TAME_LISTEN, 900.0f, 673.0f, 100.0f, 26.0f),
#ifdef TROWEL_MASTER
    T(PORT_STEREO_MODE, 1010.0f, 673.0f, 100.0f, 26.0f),
    T(PORT_AUTO_GAIN, 800.0f, 800.0f, 100.0f, 26.0f),
    T(PORT_DELTA, 915.0f, 800.0f, 90.0f, 26.0f),
#endif
    M(PORT_METER_GR, 1090.0f, 770.0f, 14.0f, 82.0f),

    // I/O column
    K(PORT_INPUT_GAIN, 1183.0f, 438.0f, 23.0f, true),
    K(PORT_OUTPUT_GAIN, 1183.0f, 548.0f, 23.0f, true),
    T(PORT_ENABLE, 1150.0f, 620.0f, 66.0f, 30.0f),
};

constexpr int kNumWidgets = static_cast<int>(sizeof(kWidgets) / sizeof(kWidgets[0]));

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
struct State {
  float values[PORT_COUNT] = {};
  float meterGr = 0.0f;
  float meterOut = -60.0f;
  float meterGrSmooth = 0.0f;
  float vuSmooth = -60.0f;
  float spectrum[kTelemetryBins] = {};
  float spectrumSmooth[kTelemetryBins] = {};
  float tameGain[kTelemetryBins] = {};
  float tameGainSmooth[kTelemetryBins] = {};
  float latency = 0.0f;
  int hover = -1;
  int active = -1;
  double dragY = 0.0;
  float dragValue = 0.0f;
  int lastClickWidget = -1;
  unsigned long lastClickTime = 0;
  int presetIndex = -1;
  bool presetDirty = false;
  int presetHover = 0;
  bool skinHover = false;
  char skinToast[64] = {};
  double skinToastUntil = 0.0;
  bool dirty = true;
};

static const ParamInfo& infoFor(uint32_t port) {
  for (int i = 0; i < kParamInfoCount; ++i) {
    if (kParams[i].index == port) {
      return kParams[i];
    }
  }
  static ParamInfo fallback;
  return fallback;
}

static void initState(State& s) {
  for (int i = 0; i < kParamInfoCount; ++i) {
    s.values[kParams[i].index] = kParams[i].def;
  }
  s.meterGr = 0.0f;
  s.meterOut = -60.0f;
  s.meterGrSmooth = 0.0f;
  s.vuSmooth = -60.0f;
  s.latency = 0.0f;
  for (int i = 0; i < static_cast<int>(kTelemetryBins); ++i) {
    s.spectrum[i] = -100.0f;
    s.spectrumSmooth[i] = -100.0f;
    s.tameGain[i] = 0.0f;
    s.tameGainSmooth[i] = 0.0f;
  }
}

static double nowSeconds() {
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}

static void showSkinToast(State& s, const char* name) {
  std::snprintf(s.skinToast, sizeof(s.skinToast), "%s",
                name && *name ? name : "Classic");
  s.skinToastUntil = nowSeconds() + 1.6;
}

static float normOf(const ParamInfo& p, float v) {
  if (p.toggle || p.enumeration) {
    return v >= 0.5f ? 1.0f : 0.0f;
  }
  if (p.logarithmic && p.min > 0.0f && p.max > p.min) {
    return std::log(std::max(v, p.min) / p.min) / std::log(p.max / p.min);
  }
  if (p.max <= p.min) {
    return 0.0f;
  }
  return (v - p.min) / (p.max - p.min);
}

static float valueOf(const ParamInfo& p, float n) {
  n = std::min(std::max(n, 0.0f), 1.0f);
  if (p.toggle || p.enumeration) {
    return n >= 0.5f ? 1.0f : 0.0f;
  }
  if (p.logarithmic && p.min > 0.0f && p.max > p.min) {
    return p.min * std::pow(p.max / p.min, n);
  }
  return p.min + n * (p.max - p.min);
}

static int switchIndex(const ParamInfo& p, float value) {
  if (!p.points || p.pointCount <= 0) {
    return 0;
  }
  int best = 0;
  float bestDistance = 1.0e30f;
  for (int i = 0; i < p.pointCount; ++i) {
    const float d = std::fabs(value - p.points[i].value);
    if (d < bestDistance) {
      bestDistance = d;
      best = i;
    }
  }
  return best;
}

static const char* shortLabel(uint32_t port) {
  switch (port) {
    case PORT_INPUT_GAIN: return "INPUT";
    case PORT_OUTPUT_GAIN: return "OUTPUT";
    case PORT_PUL_LOW_BOOST: return "LOW BOOST";
    case PORT_PUL_LOW_CUT: return "LOW CUT";
    case PORT_PUL_HIGH_BOOST: return "HIGH BOOST";
    case PORT_PUL_BANDWIDTH: return "BANDWIDTH";
    case PORT_PUL_HIGH_CUT: return "HIGH CUT";
    case PORT_SSL_LF_GAIN: return "LF GAIN";
    case PORT_SSL_LMF_GAIN: return "LMF GAIN";
    case PORT_SSL_HMF_GAIN: return "HMF GAIN";
    case PORT_SSL_HF_GAIN: return "HF GAIN";
    case PORT_TAME_AMOUNT: return "AMOUNT";
    case PORT_TAME_FOCUS: return "FOCUS";
    case PORT_TAME_SELECTIVITY: return "SELECTIVITY";
    case PORT_TAME_SENSITIVITY: return "SENSITIVITY";
    case PORT_TAME_ATTACK: return "ATTACK";
    case PORT_TAME_RELEASE: return "RELEASE";
    case PORT_TAME_MIX: return "MIX";
    default: return "";
  }
}

static const char* switchCaption(uint32_t port) {
  switch (port) {
    case PORT_PUL_LOW_FREQ: return "LOW FREQ";
    case PORT_PUL_HIGH_FREQ: return "HIGH FREQ";
    case PORT_PUL_HIGH_CUT_FREQ: return "CUT FREQ";
    case PORT_SSL_LF_FREQ: return "LF FREQ";
    case PORT_SSL_LMF_FREQ: return "LMF FREQ";
    case PORT_SSL_HMF_FREQ: return "HMF FREQ";
    case PORT_SSL_HF_FREQ: return "HF FREQ";
    case PORT_SSL_HPF: return "HIGH PASS";
    case PORT_SSL_LPF: return "LOW PASS";
    default: return "";
  }
}

static std::string formatValue(uint32_t port, float v) {
  const ParamInfo& p = infoFor(port);
  char buf[64];
  if (port == PORT_SSL_LF_BELL || port == PORT_SSL_HF_BELL) {
    return v >= 0.5f ? "BELL" : "SHELF";
  }
  if (port == PORT_TAME_LISTEN || port == PORT_PUL_IRON ||
      port == PORT_AUTO_GAIN || port == PORT_DELTA ||
      port == PORT_ENABLE || port == PORT_PULTEC_ON ||
      port == PORT_SSL_ON || port == PORT_TAME_ON) {
    return v >= 0.5f ? "ON" : "OFF";
  }
  if (p.enumeration) {
    if (port == PORT_STEREO_MODE) {
      return v >= 0.5f ? "M/S" : "LINKED";
    }
    if (p.points && p.pointCount > 0) {
      return p.points[switchIndex(p, v)].label;
    }
    return v >= 0.5f ? "1" : "0";
  }
  if (p.points && p.pointCount > 0) {
    return p.points[switchIndex(p, v)].label;
  }
  switch (p.unit) {
    case Unit::Db:
      std::snprintf(buf, sizeof(buf), "%+.1f dB", v);
      return buf;
    case Unit::Hz:
      if (v >= 1000.0f) {
        std::snprintf(buf, sizeof(buf), "%.2f kHz", v / 1000.0f);
      } else {
        std::snprintf(buf, sizeof(buf), "%.0f Hz", v);
      }
      return buf;
    case Unit::Ms:
      if (v >= 100.0f) {
        std::snprintf(buf, sizeof(buf), "%.0f ms", v);
      } else {
        std::snprintf(buf, sizeof(buf), "%.1f ms", v);
      }
      return buf;
    case Unit::Percent:
      std::snprintf(buf, sizeof(buf), "%.0f%%", v);
      return buf;
    case Unit::Unitless:
    default:
      std::snprintf(buf, sizeof(buf), "%.1f", v);
      return buf;
  }
}

// ---------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------
struct PresetValue {
  uint32_t port;
  float value;
};

struct Preset {
  const char* name;
  const char* shortName;
  const PresetValue* values;
  int count;
};

#define PV(port_, value_) PresetValue{port_, value_}

constexpr PresetValue kPresetVocal[] = {
    PV(PORT_PUL_LOW_FREQ, 100.0f), PV(PORT_PUL_LOW_BOOST, 1.5f),
    PV(PORT_PUL_HIGH_FREQ, 12000.0f), PV(PORT_PUL_HIGH_BOOST, 2.0f),
    PV(PORT_PUL_BANDWIDTH, 6.0f),
    PV(PORT_SSL_LF_FREQ, 90.0f), PV(PORT_SSL_LF_GAIN, -1.0f),
    PV(PORT_SSL_LMF_FREQ, 300.0f), PV(PORT_SSL_LMF_GAIN, -2.0f),
    PV(PORT_SSL_HMF_FREQ, 3000.0f), PV(PORT_SSL_HMF_GAIN, 1.5f),
    PV(PORT_SSL_HF_FREQ, 10000.0f), PV(PORT_SSL_HF_GAIN, 1.0f),
    PV(PORT_SSL_HPF, 80.0f),
    PV(PORT_TAME_AMOUNT, 35.0f), PV(PORT_TAME_FOCUS, 60.0f),
    PV(PORT_TAME_SELECTIVITY, 55.0f), PV(PORT_TAME_SENSITIVITY, 55.0f),
    PV(PORT_OUTPUT_GAIN, 0.0f)};

constexpr PresetValue kPresetDrumBus[] = {
    PV(PORT_PUL_LOW_FREQ, 60.0f), PV(PORT_PUL_LOW_BOOST, 2.5f),
    PV(PORT_PUL_LOW_CUT, -1.5f), PV(PORT_PUL_HIGH_FREQ, 8000.0f),
    PV(PORT_PUL_HIGH_BOOST, 1.0f), PV(PORT_PUL_BANDWIDTH, 7.0f),
    PV(PORT_SSL_LF_FREQ, 60.0f), PV(PORT_SSL_LF_GAIN, 2.0f),
    PV(PORT_SSL_LMF_FREQ, 400.0f), PV(PORT_SSL_LMF_GAIN, -2.0f),
    PV(PORT_SSL_HMF_FREQ, 3000.0f), PV(PORT_SSL_HMF_GAIN, 1.0f),
    PV(PORT_SSL_HF_FREQ, 12000.0f), PV(PORT_SSL_HF_GAIN, 1.0f),
    PV(PORT_TAME_AMOUNT, 20.0f), PV(PORT_TAME_FOCUS, 70.0f),
    PV(PORT_TAME_SENSITIVITY, 50.0f), PV(PORT_OUTPUT_GAIN, -1.0f)};

constexpr PresetValue kPresetBass[] = {
    PV(PORT_PUL_LOW_FREQ, 60.0f), PV(PORT_PUL_LOW_BOOST, 3.0f),
    PV(PORT_PUL_LOW_CUT, -1.0f), PV(PORT_PUL_HIGH_BOOST, 0.0f),
    PV(PORT_SSL_LF_FREQ, 60.0f), PV(PORT_SSL_LF_GAIN, 1.0f),
    PV(PORT_SSL_LMF_FREQ, 200.0f), PV(PORT_SSL_LMF_GAIN, 1.0f),
    PV(PORT_SSL_HMF_FREQ, 800.0f), PV(PORT_SSL_HMF_GAIN, -1.0f),
    PV(PORT_SSL_HPF, 30.0f),
    PV(PORT_TAME_AMOUNT, 30.0f), PV(PORT_TAME_FOCUS, 20.0f),
    PV(PORT_TAME_SELECTIVITY, 60.0f), PV(PORT_OUTPUT_GAIN, -1.0f)};

constexpr PresetValue kPresetAcoustic[] = {
    PV(PORT_PUL_LOW_FREQ, 100.0f), PV(PORT_PUL_LOW_BOOST, 0.5f),
    PV(PORT_PUL_HIGH_FREQ, 10000.0f), PV(PORT_PUL_HIGH_BOOST, 1.5f),
    PV(PORT_PUL_BANDWIDTH, 4.0f),
    PV(PORT_SSL_LF_FREQ, 100.0f), PV(PORT_SSL_LF_GAIN, -1.0f),
    PV(PORT_SSL_LMF_FREQ, 300.0f), PV(PORT_SSL_LMF_GAIN, 1.0f),
    PV(PORT_SSL_HMF_FREQ, 4000.0f), PV(PORT_SSL_HMF_GAIN, 1.5f),
    PV(PORT_SSL_HF_FREQ, 12000.0f), PV(PORT_SSL_HF_GAIN, 1.0f),
    PV(PORT_SSL_HPF, 50.0f),
    PV(PORT_TAME_AMOUNT, 25.0f), PV(PORT_TAME_FOCUS, 55.0f),
    PV(PORT_OUTPUT_GAIN, 0.0f)};

constexpr PresetValue kPresetMixBus[] = {
    PV(PORT_PUL_LOW_FREQ, 30.0f), PV(PORT_PUL_LOW_BOOST, 0.5f),
    PV(PORT_PUL_HIGH_FREQ, 12000.0f), PV(PORT_PUL_HIGH_BOOST, 0.5f),
    PV(PORT_PUL_IRON, 1.0f),
    PV(PORT_SSL_LF_FREQ, 60.0f), PV(PORT_SSL_LF_GAIN, 0.5f),
    PV(PORT_SSL_LMF_FREQ, 300.0f), PV(PORT_SSL_LMF_GAIN, -0.5f),
    PV(PORT_SSL_HMF_FREQ, 3000.0f), PV(PORT_SSL_HMF_GAIN, 0.5f),
    PV(PORT_SSL_HF_FREQ, 12000.0f), PV(PORT_SSL_HF_GAIN, 0.5f),
    PV(PORT_SSL_HPF, 30.0f),
    PV(PORT_TAME_AMOUNT, 15.0f), PV(PORT_TAME_FOCUS, 70.0f),
    PV(PORT_TAME_SENSITIVITY, 45.0f), PV(PORT_OUTPUT_GAIN, -0.5f)};

constexpr PresetValue kPresetGuitar[] = {
    PV(PORT_PUL_LOW_FREQ, 100.0f), PV(PORT_PUL_LOW_CUT, -2.0f),
    PV(PORT_PUL_HIGH_FREQ, 3000.0f), PV(PORT_PUL_HIGH_BOOST, 2.0f),
    PV(PORT_PUL_BANDWIDTH, 3.0f),
    PV(PORT_SSL_LMF_FREQ, 300.0f), PV(PORT_SSL_LMF_GAIN, -2.0f),
    PV(PORT_SSL_HMF_FREQ, 3000.0f), PV(PORT_SSL_HMF_GAIN, 2.0f),
    PV(PORT_SSL_HF_FREQ, 8000.0f), PV(PORT_SSL_HF_GAIN, 1.0f),
    PV(PORT_SSL_HPF, 80.0f),
    PV(PORT_TAME_AMOUNT, 35.0f), PV(PORT_TAME_FOCUS, 65.0f),
    PV(PORT_OUTPUT_GAIN, -1.5f)};

constexpr PresetValue kPresetDeHarsh[] = {
    PV(PORT_PUL_HIGH_FREQ, 10000.0f), PV(PORT_PUL_HIGH_CUT, -2.0f),
    PV(PORT_SSL_HMF_FREQ, 5000.0f), PV(PORT_SSL_HMF_GAIN, -2.0f),
    PV(PORT_SSL_HF_FREQ, 12000.0f), PV(PORT_SSL_HF_GAIN, -1.0f),
    PV(PORT_TAME_AMOUNT, 70.0f), PV(PORT_TAME_FOCUS, 80.0f),
    PV(PORT_TAME_SELECTIVITY, 65.0f), PV(PORT_TAME_SENSITIVITY, 65.0f),
    PV(PORT_TAME_RELEASE, 300.0f), PV(PORT_OUTPUT_GAIN, 0.0f)};

constexpr PresetValue kPresetBroken[] = {
    PV(PORT_PUL_LOW_FREQ, 60.0f), PV(PORT_PUL_LOW_BOOST, 10.0f),
    PV(PORT_PUL_LOW_CUT, -10.0f), PV(PORT_PUL_HIGH_FREQ, 5000.0f),
    PV(PORT_PUL_HIGH_BOOST, 12.0f), PV(PORT_PUL_BANDWIDTH, 1.0f),
    PV(PORT_PUL_HIGH_CUT_FREQ, 10000.0f), PV(PORT_PUL_HIGH_CUT, -10.0f),
    PV(PORT_PUL_IRON, 1.0f),
    PV(PORT_SSL_LF_FREQ, 100.0f), PV(PORT_SSL_LF_GAIN, 12.0f),
    PV(PORT_SSL_LMF_FREQ, 300.0f), PV(PORT_SSL_LMF_GAIN, -12.0f),
    PV(PORT_SSL_HMF_FREQ, 5000.0f), PV(PORT_SSL_HMF_GAIN, 12.0f),
    PV(PORT_SSL_HF_FREQ, 10000.0f), PV(PORT_SSL_HF_GAIN, -12.0f),
    PV(PORT_SSL_HPF, 160.0f), PV(PORT_SSL_LPF, 8000.0f),
    PV(PORT_TAME_AMOUNT, 100.0f), PV(PORT_TAME_FOCUS, 50.0f),
    PV(PORT_TAME_SELECTIVITY, 80.0f), PV(PORT_TAME_SENSITIVITY, 80.0f),
    PV(PORT_OUTPUT_GAIN, -6.0f)};

#undef PV

constexpr Preset kPresets[] = {
    {"Vocal - Air & Tame", "VOCAL", kPresetVocal,
     static_cast<int>(sizeof(kPresetVocal) / sizeof(kPresetVocal[0]))},
    {"Drum Bus - Punch", "DRUM BUS", kPresetDrumBus,
     static_cast<int>(sizeof(kPresetDrumBus) / sizeof(kPresetDrumBus[0]))},
    {"Bass - Weight", "BASS", kPresetBass,
     static_cast<int>(sizeof(kPresetBass) / sizeof(kPresetBass[0]))},
    {"Acoustic - Open", "ACOUSTIC", kPresetAcoustic,
     static_cast<int>(sizeof(kPresetAcoustic) / sizeof(kPresetAcoustic[0]))},
    {"Mix Bus - Polish", "MIX BUS", kPresetMixBus,
     static_cast<int>(sizeof(kPresetMixBus) / sizeof(kPresetMixBus[0]))},
    {"Guitar - Bite", "GUITAR", kPresetGuitar,
     static_cast<int>(sizeof(kPresetGuitar) / sizeof(kPresetGuitar[0]))},
    {"De-Harsh", "DE-HARSH", kPresetDeHarsh,
     static_cast<int>(sizeof(kPresetDeHarsh) / sizeof(kPresetDeHarsh[0]))},
    {"Broken Trowel", "BROKEN", kPresetBroken,
     static_cast<int>(sizeof(kPresetBroken) / sizeof(kPresetBroken[0]))},
};

constexpr int kPresetCount = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));

struct Rect {
  double x, y, w, h;
};

constexpr Rect kPresetLcd{300.0, 24.0, 220.0, 36.0};
constexpr Rect kPresetPrev{530.0, 24.0, 28.0, 36.0};
constexpr Rect kPresetNext{562.0, 24.0, 28.0, 36.0};
constexpr Rect kSkinChip{1036.0, 22.0, 178.0, 40.0};

#ifdef TROWEL_MASTER
constexpr const char* kSkinPluginId = "trowel-master";
#else
constexpr const char* kSkinPluginId = "trowel";
#endif
constexpr Rect kChainPrev{114.0, 316.0, 34.0, 32.0};
constexpr Rect kChainNext{1150.0, 316.0, 34.0, 32.0};

static bool inRect(double x, double y, const Rect& r) {
  return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------
static void setColor(cairo_t* cr, const Color& c) {
  cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a);
}

static void setColorA(cairo_t* cr, const Color& c, double alpha) {
  cairo_set_source_rgba(cr, c.r, c.g, c.b, alpha);
}

static void roundedRect(cairo_t* cr, double x, double y, double w, double h,
                        double r) {
  r = std::min(r, std::min(w, h) * 0.5);
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - r, y + r, r, -kPi * 0.5, 0.0);
  cairo_arc(cr, x + w - r, y + h - r, r, 0.0, kPi * 0.5);
  cairo_arc(cr, x + r, y + h - r, r, kPi * 0.5, kPi);
  cairo_arc(cr, x + r, y + r, r, kPi, kPi * 1.5);
  cairo_close_path(cr);
}

static void textAt(cairo_t* cr, double x, double y, const char* text,
                   double size, bool bold, const Color& c,
                   const char* family = "DejaVu Sans") {
  cairo_select_font_face(cr, family, CAIRO_FONT_SLANT_NORMAL,
                         bold ? CAIRO_FONT_WEIGHT_BOLD
                              : CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, size);
  setColor(cr, c);
  cairo_move_to(cr, x, y);
  cairo_show_text(cr, text);
}

static void textCentered(cairo_t* cr, double cx, double y, const char* text,
                         double size, bool bold, const Color& c,
                         const char* family = "DejaVu Sans") {
  cairo_select_font_face(cr, family, CAIRO_FONT_SLANT_NORMAL,
                         bold ? CAIRO_FONT_WEIGHT_BOLD
                              : CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, size);
  cairo_text_extents_t ext;
  cairo_text_extents(cr, text, &ext);
  setColor(cr, c);
  cairo_move_to(cr, cx - (ext.width * 0.5 + ext.x_bearing), y);
  cairo_show_text(cr, text);
}

static void paintTexture(cairo_t* cr, double x, double y, double w, double h,
                         double alpha);

static void drawScrew(cairo_t* cr, double x, double y) {
  cairo_arc(cr, x, y + 0.8, 5.2, 0.0, kPi * 2.0);
  setColorA(cr, kBg, 0.55);
  cairo_fill(cr);

  cairo_pattern_t* head = cairo_pattern_create_radial(
      x - 1.8, y - 1.8, 0.5, x, y, 4.8);
  cairo_pattern_add_color_stop_rgb(head, 0.0, 0.74, 0.75, 0.72);
  cairo_pattern_add_color_stop_rgb(head, 0.55, 0.40, 0.42, 0.42);
  cairo_pattern_add_color_stop_rgb(head, 1.0, 0.13, 0.15, 0.17);
  cairo_arc(cr, x, y, 4.2, 0.0, kPi * 2.0);
  cairo_set_source(cr, head);
  cairo_fill(cr);
  cairo_pattern_destroy(head);

  cairo_arc(cr, x, y, 4.2, 0.0, kPi * 2.0);
  setColorA(cr, kBg, 0.85);
  cairo_set_line_width(cr, 0.8);
  cairo_stroke(cr);

  setColorA(cr, kBg, 0.8);
  cairo_set_line_width(cr, 1.2);
  cairo_move_to(cr, x - 2.2, y - 2.2);
  cairo_line_to(cr, x + 2.2, y + 2.2);
  cairo_stroke(cr);

  setColorA(cr, kHighlight, 0.30);
  cairo_set_line_width(cr, 0.7);
  cairo_move_to(cr, x - 2.0, y - 2.6);
  cairo_line_to(cr, x + 1.6, y - 1.0);
  cairo_stroke(cr);
}

static void drawPanel(cairo_t* cr, double x, double y, double w, double h,
                      const char* title) {
  // raised chassis shadow
  roundedRect(cr, x + 1.0, y + 4.0, w, h, 8.0);
  setColorA(cr, kBg, 0.38);
  cairo_fill(cr);
  roundedRect(cr, x + 0.5, y + 2.0, w, h, 8.0);
  setColorA(cr, kBg, 0.24);
  cairo_fill(cr);

  cairo_pattern_t* pat = cairo_pattern_create_linear(x, y, x, y + h);
  cairo_pattern_add_color_stop_rgb(pat, 0.0, kPanelTop.r, kPanelTop.g,
                                   kPanelTop.b);
  cairo_pattern_add_color_stop_rgb(pat, 1.0, kPanelBottom.r, kPanelBottom.g,
                                   kPanelBottom.b);
  roundedRect(cr, x, y, w, h, 7.0);
  cairo_set_source(cr, pat);
  cairo_fill_preserve(cr);
  cairo_pattern_destroy(pat);
  setColor(cr, kPanelEdge);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  // brushed texture and recessed top shadow inside the panel
  cairo_save(cr);
  roundedRect(cr, x, y, w, h, 7.0);
  cairo_clip(cr);
  paintTexture(cr, x, y, w, h, 0.065);
  cairo_pattern_t* inner = cairo_pattern_create_linear(x, y, x, y + 18.0);
  cairo_pattern_add_color_stop_rgba(inner, 0.0, 0.0, 0.0, 0.0, 0.28);
  cairo_pattern_add_color_stop_rgba(inner, 1.0, 0.0, 0.0, 0.0, 0.0);
  cairo_rectangle(cr, x, y, w, 18.0);
  cairo_set_source(cr, inner);
  cairo_fill(cr);
  cairo_pattern_destroy(inner);
  cairo_restore(cr);

  // top bevel and bottom shadow
  setColorA(cr, kPanelInner, 0.75);
  cairo_set_line_width(cr, 1.0);
  cairo_move_to(cr, x + 9.0, y + 1.0);
  cairo_line_to(cr, x + w - 9.0, y + 1.0);
  cairo_stroke(cr);
  setColorA(cr, kBg, 0.55);
  cairo_move_to(cr, x + 9.0, y + h - 1.0);
  cairo_line_to(cr, x + w - 9.0, y + h - 1.0);
  cairo_stroke(cr);

  // recessed title plate
  roundedRect(cr, x + 8.0, y + 8.0, w - 16.0, 27.0, 3.5);
  cairo_pattern_t* plate = cairo_pattern_create_linear(
      x, y + 8.0, x, y + 35.0);
  cairo_pattern_add_color_stop_rgb(plate, 0.0, kPanelHeaderTop.r,
                                   kPanelHeaderTop.g, kPanelHeaderTop.b);
  cairo_pattern_add_color_stop_rgb(plate, 1.0, kPanelHeaderBottom.r,
                                   kPanelHeaderBottom.g,
                                   kPanelHeaderBottom.b);
  cairo_set_source(cr, plate);
  cairo_fill_preserve(cr);
  cairo_pattern_destroy(plate);
  setColorA(cr, kPanelEdge, 0.8);
  cairo_set_line_width(cr, 0.8);
  cairo_stroke(cr);

  textAt(cr, x + 16.0, y + 26.0, title, 11.0, true, kPanelHeaderText);
  setColor(cr, kRed);
  cairo_rectangle(cr, x + 16.0, y + 33.0, 24.0, 2.0);
  cairo_fill(cr);

  cairo_arc(cr, x + w - 18.0, y + 21.0, 2.2, 0.0, kPi * 2.0);
  setColorA(cr, kRed, 0.85);
  cairo_fill(cr);
  cairo_arc(cr, x + w - 18.0, y + 21.0, 4.5, 0.0, kPi * 2.0);
  setColorA(cr, kRed, 0.15);
  cairo_fill(cr);

  drawScrew(cr, x + 12.0, y + h - 12.0);
  drawScrew(cr, x + w - 12.0, y + h - 12.0);
}

static void debugLog(const char* fmt, ...);

static cairo_surface_t* gKnobImage = nullptr;
static cairo_surface_t* gMetalTexture = nullptr;
static FT_Library gFtLibrary = nullptr;
static FT_Face gLogoFace = nullptr;
static cairo_font_face_t* gLogoFont = nullptr;
static bool gLogoTried = false;

static void destroyThemeAssets() {
  if (gKnobImage) {
    cairo_surface_destroy(gKnobImage);
    gKnobImage = nullptr;
  }
  if (gMetalTexture) {
    cairo_surface_destroy(gMetalTexture);
    gMetalTexture = nullptr;
  }
}

static bool loadPng(const std::string& path, cairo_surface_t** out) {
  if (path.empty()) return false;
  cairo_surface_t* surface =
      cairo_image_surface_create_from_png(path.c_str());
  if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(surface);
    return false;
  }
  *out = surface;
  return true;
}

static std::string bundleDirOf(const char* bundlePath) {
  std::string dir(bundlePath ? bundlePath : "");
  if (!dir.empty() && dir.back() != '/') dir += '/';
  return dir;
}

static void loadThemeAssets(const char* bundlePath) {
  destroyThemeAssets();
  const auto& manager = hearth::theme::global();
  const auto& t = manager.theme();
  const std::string dir = bundleDirOf(bundlePath);
  const std::string skinDir = manager.currentSkinDir();

  if (!skinDir.empty() && !t.knob.empty()) {
    loadPng(skinDir + "/" + t.knob, &gKnobImage);
  }
  if (!gKnobImage && !t.knob.empty()) {
    loadPng(dir + t.knob, &gKnobImage);
  }

  if (t.texture_opacity > 0.0 && !t.texture.empty()) {
    if (!skinDir.empty()) {
      loadPng(skinDir + "/" + t.texture, &gMetalTexture);
    }
    if (!gMetalTexture) {
      loadPng(dir + t.texture, &gMetalTexture);
    }
  }
  debugLog("theme %s: skinDir=%s knob=%s texture=%s", t.id.c_str(),
           skinDir.c_str(), gKnobImage ? "yes" : "no",
           gMetalTexture ? "yes" : "no");
}

static void paintTexture(cairo_t* cr, double x, double y, double w, double h,
                         double alpha) {
  if (!gMetalTexture || gTextureScale <= 0.0) {
    return;
  }
  cairo_pattern_t* pattern = cairo_pattern_create_for_surface(gMetalTexture);
  cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);
  cairo_rectangle(cr, x, y, w, h);
  cairo_set_source(cr, pattern);
  cairo_paint_with_alpha(cr, alpha * gTextureScale);
  cairo_pattern_destroy(pattern);
}

static void loadLogoFont(const char* bundlePath) {
  if (gLogoTried || !bundlePath || !*bundlePath) {
    return;
  }
  gLogoTried = true;
  std::string dir(bundlePath);
  if (!dir.empty() && dir.back() != '/') {
    dir += '/';
  }
  const std::string path = dir + "Jost-ExtraBold.ttf";
  if (FT_Init_FreeType(&gFtLibrary) != 0) {
    gFtLibrary = nullptr;
    return;
  }
  if (FT_New_Face(gFtLibrary, path.c_str(), 0, &gLogoFace) != 0) {
    FT_Done_FreeType(gFtLibrary);
    gFtLibrary = nullptr;
    gLogoFace = nullptr;
    return;
  }
  gLogoFont = cairo_ft_font_face_create_for_ft_face(gLogoFace, 0);
  if (cairo_font_face_status(gLogoFont) != CAIRO_STATUS_SUCCESS) {
    cairo_font_face_destroy(gLogoFont);
    gLogoFont = nullptr;
  }
  debugLog("loaded logo font %s", path.c_str());
}

static void drawWordmark(cairo_t* cr, double x, double baseline, double size) {
  if (!gLogoFont) {
    textAt(cr, x, baseline, "Trowel", size, true, kCream);
    return;
  }
  cairo_save(cr);
  cairo_set_font_face(cr, gLogoFont);
  cairo_set_font_size(cr, size);
  setColor(cr, kCream);
  const char* text = "Trowel";
  const double tracking = size * 0.025;
  double px = x;
  for (const char* p = text; *p; ++p) {
    char glyph[2] = {*p, 0};
    cairo_text_extents_t ext;
    cairo_text_extents(cr, glyph, &ext);
    cairo_move_to(cr, px - ext.x_bearing, baseline);
    cairo_show_text(cr, glyph);
    px += ext.x_advance - tracking;
  }
  cairo_restore(cr);
}

static void drawKnobBody(cairo_t* cr, double cx, double cy, double r,
                         bool hovered) {
  // soft contact shadow
  cairo_pattern_t* shadow = cairo_pattern_create_radial(
      cx, cy + 3.0, r * 0.15, cx, cy + 3.0, r * 1.55);
  cairo_pattern_add_color_stop_rgba(shadow, 0.0, 0.0, 0.0, 0.0, 0.55);
  cairo_pattern_add_color_stop_rgba(shadow, 1.0, 0.0, 0.0, 0.0, 0.0);
  cairo_arc(cr, cx, cy + 3.0, r * 1.55, 0.0, kPi * 2.0);
  cairo_set_source(cr, shadow);
  cairo_fill(cr);
  cairo_pattern_destroy(shadow);

  if (gKnobImage) {
    cairo_save(cr);
    cairo_translate(cr, cx - r, cy - r);
    const double scale = (2.0 * r) / 512.0;
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, gKnobImage, 0.0, 0.0);
    cairo_paint(cr);
    cairo_restore(cr);
  } else {
    // procedural fallback
    cairo_pattern_t* pat = cairo_pattern_create_radial(
        cx - r * 0.35, cy - r * 0.40, r * 0.10, cx, cy, r * 1.15);
    cairo_pattern_add_color_stop_rgb(pat, 0.0, kKnobTop.r, kKnobTop.g,
                                     kKnobTop.b);
    cairo_pattern_add_color_stop_rgb(pat, 1.0, kKnobBottom.r, kKnobBottom.g,
                                     kKnobBottom.b);
    cairo_arc(cr, cx, cy, r, 0.0, kPi * 2.0);
    cairo_set_source(cr, pat);
    cairo_fill(cr);
    cairo_pattern_destroy(pat);

    cairo_arc(cr, cx, cy, r, 0.0, kPi * 2.0);
    setColor(cr, kKnobRing);
    cairo_set_line_width(cr, 1.1);
    cairo_stroke(cr);
  }

  // top rim light
  setColorA(cr, kHighlight, 0.10);
  cairo_set_line_width(cr, 1.2);
  cairo_arc(cr, cx, cy, r * 0.95, kPi * 1.12, kPi * 1.72);
  cairo_stroke(cr);

  if (hovered) {
    cairo_arc(cr, cx, cy, r, 0.0, kPi * 2.0);
    setColor(cr, kAmber);
    cairo_set_line_width(cr, 1.8);
    cairo_stroke(cr);
  }
}

static void drawKnob(cairo_t* cr, const Widget& w, float value, bool hovered,
                     bool enabled) {
  const ParamInfo& p = infoFor(w.port);
  const float n = normOf(p, value);
  const double start = kPi * 0.75;
  const double sweep = kPi * 1.5;
  const double cx = w.cx;
  const double cy = w.cy;
  const double r = w.r;

  for (int i = 0; i <= 4; ++i) {
    const double a = start + sweep * (i / 4.0);
    setColor(cr, (i == 0 || i == 4) ? kMuted : kPanelEdge);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, cx + std::cos(a) * (r + 3.0),
                  cy + std::sin(a) * (r + 3.0));
    cairo_line_to(cr, cx + std::cos(a) * (r + 7.0),
                  cy + std::sin(a) * (r + 7.0));
    cairo_stroke(cr);
  }

  drawKnobBody(cr, cx, cy, r, hovered);

  // track and value arcs
  cairo_set_line_width(cr, 2.6);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  setColorA(cr, kBg, 0.9);
  cairo_arc(cr, cx, cy, r - 5.0, start, start + sweep);
  cairo_stroke(cr);

  const double pos = start + sweep * n;
  setColorA(cr, enabled ? kAmber : kMuted, 0.38);
  cairo_set_line_width(cr, 1.8);
  if (w.bipolar) {
    const double mid = start + sweep * 0.5;
    cairo_arc(cr, cx, cy, r - 5.0, std::min(mid, pos), std::max(mid, pos));
  } else {
    cairo_arc(cr, cx, cy, r - 5.0, start, pos);
  }
  cairo_stroke(cr);

  const double a = pos;
  setColor(cr, kCream);
  cairo_set_line_width(cr, 2.3);
  cairo_move_to(cr, cx + std::cos(a) * (r * 0.22),
                cy + std::sin(a) * (r * 0.22));
  cairo_line_to(cr, cx + std::cos(a) * (r * 0.84),
                cy + std::sin(a) * (r * 0.84));
  cairo_stroke(cr);

  if (!gKnobImage) {
    cairo_arc(cr, cx, cy, r * 0.20, 0.0, kPi * 2.0);
    setColor(cr, kSilver);
    cairo_fill(cr);
    cairo_arc(cr, cx, cy, r * 0.20, 0.0, kPi * 2.0);
    setColorA(cr, kBg, 0.7);
    cairo_set_line_width(cr, 0.8);
    cairo_stroke(cr);
  }

  textCentered(cr, cx, cy + r + 19.0, shortLabel(w.port), 8.0, true, kMuted);
  const std::string valueText = formatValue(w.port, value);
  textCentered(cr, cx, cy + r + 32.0, valueText.c_str(), 8.5, false, kText);
}

static void drawSwitch(cairo_t* cr, const Widget& w, float value, bool hovered,
                       bool enabled) {
  (void)enabled;
  const ParamInfo& p = infoFor(w.port);
  const int count = (p.points && p.pointCount > 0) ? p.pointCount : 1;
  const int selected = switchIndex(p, value);
  const double start = kPi * 0.75;
  const double sweep = kPi * 1.5;
  const double cx = w.cx;
  const double cy = w.cy;
  const double r = w.r;

  textCentered(cr, cx, cy - r - 13.0, switchCaption(w.port), 7.5, true, kMuted);

  // ticks only; the numbers live in a compact strip below the knob
  for (int i = 0; i < count; ++i) {
    const double t = (count > 1) ? (i / static_cast<double>(count - 1)) : 0.5;
    const double a = start + sweep * t;
    setColor(cr, i == selected ? kAmber : kPanelEdge);
    cairo_set_line_width(cr, i == selected ? 1.8 : 1.0);
    cairo_move_to(cr, cx + std::cos(a) * (r + 3.0),
                  cy + std::sin(a) * (r + 3.0));
    cairo_line_to(cr, cx + std::cos(a) * (r + 7.0),
                  cy + std::sin(a) * (r + 7.0));
    cairo_stroke(cr);
  }

  drawKnobBody(cr, cx, cy, r, hovered);

  const double t = (count > 1) ? (selected / static_cast<double>(count - 1)) : 0.5;
  const double a = start + sweep * t;
  setColor(cr, kCream);
  cairo_set_line_width(cr, 2.4);
  cairo_move_to(cr, cx + std::cos(a) * (r * 0.22),
                cy + std::sin(a) * (r * 0.22));
  cairo_line_to(cr, cx + std::cos(a) * (r * 0.84),
                cy + std::sin(a) * (r * 0.84));
  cairo_stroke(cr);

  if (!gKnobImage) {
    cairo_arc(cr, cx, cy, r * 0.18, 0.0, kPi * 2.0);
    setColor(cr, kSilver);
    cairo_fill(cr);
  }

  // Compact legend: show every point for short switches, otherwise show only
  // the selected value so the wide console panels stay readable.
  if (count <= 6) {
    const double fontSize = 7.0;
    double total = 0.0;
    for (int i = 0; i < count; ++i) {
      cairo_text_extents_t ext;
      cairo_select_font_face(cr, "DejaVu Sans", CAIRO_FONT_SLANT_NORMAL,
                             i == selected ? CAIRO_FONT_WEIGHT_BOLD
                                          : CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, fontSize);
      cairo_text_extents(cr, p.points[i].label, &ext);
      total += ext.width;
      if (i + 1 < count) total += 4.0;
    }
    double lx = cx - total * 0.5;
    const double ly = cy + r + 14.0;
    for (int i = 0; i < count; ++i) {
      cairo_text_extents_t ext;
      cairo_select_font_face(cr, "DejaVu Sans", CAIRO_FONT_SLANT_NORMAL,
                             i == selected ? CAIRO_FONT_WEIGHT_BOLD
                                          : CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, fontSize);
      cairo_text_extents(cr, p.points[i].label, &ext);
      setColor(cr, i == selected ? kAmber : kMuted);
      cairo_move_to(cr, lx, ly);
      cairo_show_text(cr, p.points[i].label);
      lx += ext.width + 4.0;
    }
  } else {
    textCentered(cr, cx, cy + r + 15.0, p.points[selected].label, 8.5, true,
                 kAmber);
  }
}

static void drawPowerButton(cairo_t* cr, const Widget& w, bool on,
                            const char* onLabel, const char* offLabel) {
  const double r = 6.0;
  roundedRect(cr, w.x + 1.0, w.y + 3.0, w.w, w.h, r);
  setColorA(cr, kBg, 0.45);
  cairo_fill(cr);

  roundedRect(cr, w.x, w.y, w.w, w.h, r);
  cairo_pattern_t* pat = cairo_pattern_create_linear(w.x, w.y, w.x,
                                                     w.y + w.h);
  if (on) {
    cairo_pattern_add_color_stop_rgb(pat, 0.0, 0.86, 0.35, 0.30);
    cairo_pattern_add_color_stop_rgb(pat, 0.55, 0.62, 0.18, 0.15);
    cairo_pattern_add_color_stop_rgb(pat, 1.0, 0.34, 0.08, 0.07);
  } else {
    cairo_pattern_add_color_stop_rgb(pat, 0.0, 0.21, 0.18, 0.18);
    cairo_pattern_add_color_stop_rgb(pat, 1.0, 0.06, 0.05, 0.05);
  }
  cairo_set_source(cr, pat);
  cairo_fill_preserve(cr);
  cairo_pattern_destroy(pat);
  setColorA(cr, kBg, 0.9);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  cairo_save(cr);
  roundedRect(cr, w.x, w.y, w.w, w.h, r);
  cairo_clip(cr);
  cairo_pattern_t* gloss = cairo_pattern_create_linear(
      w.x, w.y, w.x, w.y + w.h * 0.62);
  cairo_pattern_add_color_stop_rgba(gloss, 0.0, 1.0, 1.0, 1.0, 0.22);
  cairo_pattern_add_color_stop_rgba(gloss, 1.0, 1.0, 1.0, 1.0, 0.0);
  cairo_rectangle(cr, w.x, w.y, w.w, w.h * 0.62);
  cairo_set_source(cr, gloss);
  cairo_fill(cr);
  cairo_pattern_destroy(gloss);
  cairo_restore(cr);

  setColorA(cr, kHighlight, 0.18);
  cairo_set_line_width(cr, 0.8);
  cairo_move_to(cr, w.x + 9.0, w.y + 1.0);
  cairo_line_to(cr, w.x + w.w - 9.0, w.y + 1.0);
  cairo_stroke(cr);

  const double ledX = w.x + 16.0;
  const double ledY = w.y + w.h * 0.5;
  if (on) {
    cairo_pattern_t* glow = cairo_pattern_create_radial(
        ledX, ledY, 1.0, ledX, ledY, 10.0);
    cairo_pattern_add_color_stop_rgba(glow, 0.0, 1.0, 0.35, 0.20, 0.45);
    cairo_pattern_add_color_stop_rgba(glow, 1.0, 1.0, 0.20, 0.10, 0.0);
    cairo_arc(cr, ledX, ledY, 10.0, 0.0, kPi * 2.0);
    cairo_set_source(cr, glow);
    cairo_fill(cr);
    cairo_pattern_destroy(glow);
  }
  cairo_arc(cr, ledX, ledY, 3.4, 0.0, kPi * 2.0);
  setColor(cr, on ? kAmber : kRedDark);
  cairo_fill(cr);
  cairo_arc(cr, ledX - 1.0, ledY - 1.0, 1.1, 0.0, kPi * 2.0);
  setColorA(cr, kHighlight, on ? 0.7 : 0.15);
  cairo_fill(cr);

  textCentered(cr, w.x + w.w * 0.5 + 7.0, w.y + w.h * 0.5 + 3.5,
               on ? onLabel : offLabel, 9.0, true, on ? kCream : kMuted);
}

static void drawPillToggle(cairo_t* cr, const Widget& w, bool on,
                           const char* label) {
  const double r = w.h * 0.5;
  roundedRect(cr, w.x + 1.0, w.y + 2.0, w.w, w.h, r);
  setColorA(cr, kBg, 0.45);
  cairo_fill(cr);

  roundedRect(cr, w.x, w.y, w.w, w.h, r);
  setColor(cr, on ? kAmber : kTrack);
  cairo_fill_preserve(cr);
  setColor(cr, on ? kAmber : kPanelEdge);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  cairo_save(cr);
  roundedRect(cr, w.x, w.y, w.w, w.h, r);
  cairo_clip(cr);
  cairo_pattern_t* gloss = cairo_pattern_create_linear(
      w.x, w.y, w.x, w.y + w.h * 0.7);
  cairo_pattern_add_color_stop_rgba(gloss, 0.0, 1.0, 1.0, 1.0, 0.20);
  cairo_pattern_add_color_stop_rgba(gloss, 1.0, 1.0, 1.0, 1.0, 0.0);
  cairo_rectangle(cr, w.x, w.y, w.w, w.h * 0.7);
  cairo_set_source(cr, gloss);
  cairo_fill(cr);
  cairo_pattern_destroy(gloss);
  cairo_restore(cr);

  const double knobX = on ? (w.x + w.w - r) : (w.x + r);
  cairo_arc(cr, knobX + 1.0, w.y + r + 2.0, std::max(2.0, r - 4.0), 0.0,
            kPi * 2.0);
  setColorA(cr, kBg, 0.5);
  cairo_fill(cr);
  cairo_arc(cr, knobX, w.y + r, std::max(2.0, r - 4.0), 0.0, kPi * 2.0);
  setColor(cr, on ? kBg : kMuted);
  cairo_fill(cr);
  cairo_arc(cr, knobX - 1.0, w.y + r - 1.0, std::max(1.0, r - 6.0), 0.0,
            kPi * 2.0);
  setColorA(cr, kHighlight, 0.15);
  cairo_fill(cr);

  textCentered(cr, w.x + w.w * 0.5 + (on ? -r * 0.45 : r * 0.45),
               w.y + w.h * 0.5 + 3.0, label, 8.0, true,
               on ? kBg : kText);
}

static void drawFader(cairo_t* cr, const Widget& w, float value, bool hovered,
                      bool enabled) {
  const ParamInfo& p = infoFor(w.port);
  const float n = normOf(p, value);
  const double cx = w.x + w.w * 0.5;
  const double top = w.y;
  const double bottom = w.y + w.h;
  const double capY = bottom - n * w.h;

  // dB scale (range is -60 .. +12 dB)
  const double dbMarks[] = {12.0, 0.0, -12.0, -24.0, -36.0, -48.0, -60.0};
  for (double db : dbMarks) {
    const double t = (db + 60.0) / 72.0;
    const double y = bottom - t * w.h;
    setColorA(cr, kPanelEdge, 0.85);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, cx - 27.0, y);
    cairo_line_to(cr, cx - 21.0, y);
    cairo_stroke(cr);

    char label[16];
    if (db > 0.0) {
      std::snprintf(label, sizeof(label), "+%.0f", db);
    } else {
      std::snprintf(label, sizeof(label), "%.0f", db);
    }
    cairo_select_font_face(cr, "DejaVu Sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 6.8);
    cairo_text_extents_t ext;
    cairo_text_extents(cr, label, &ext);
    setColor(cr, kMuted);
    cairo_move_to(cr, cx - 30.0 - ext.width, y + 2.2);
    cairo_show_text(cr, label);
  }

  // recessed slot with a metal edge
  roundedRect(cr, cx - 10.0, top - 2.0, 20.0, w.h + 4.0, 6.0);
  setColorA(cr, kBg, 0.55);
  cairo_fill(cr);
  roundedRect(cr, cx - 6.0, top, 12.0, w.h, 5.0);
  cairo_pattern_t* slot = cairo_pattern_create_linear(cx - 6.0, top,
                                                      cx + 6.0, top);
  cairo_pattern_add_color_stop_rgb(slot, 0.0, 0.020, 0.025, 0.030);
  cairo_pattern_add_color_stop_rgb(slot, 0.5, 0.120, 0.145, 0.170);
  cairo_pattern_add_color_stop_rgb(slot, 1.0, 0.020, 0.025, 0.030);
  cairo_set_source(cr, slot);
  cairo_fill(cr);
  cairo_pattern_destroy(slot);
  setColorA(cr, kBg, 0.9);
  cairo_set_line_width(cr, 0.8);
  roundedRect(cr, cx - 6.0, top, 12.0, w.h, 5.0);
  cairo_stroke(cr);

  // filled part
  cairo_rectangle(cr, cx - 1.5, capY, 3.0, bottom - capY);
  setColor(cr, enabled ? kAmber : kMuted);
  cairo_fill(cr);

  // cap shadow, then a brushed-metal cap with a glass highlight
  roundedRect(cr, cx - 26.0, capY - 8.0, 54.0, 20.0, 3.0);
  setColorA(cr, kBg, 0.45);
  cairo_fill(cr);

  roundedRect(cr, cx - 27.0, capY - 10.0, 54.0, 20.0, 3.0);
  cairo_pattern_t* pat = cairo_pattern_create_linear(cx - 27.0, capY,
                                                     cx + 27.0, capY);
  cairo_pattern_add_color_stop_rgb(pat, 0.0, 0.38, 0.41, 0.43);
  cairo_pattern_add_color_stop_rgb(pat, 0.42, 0.84, 0.86, 0.86);
  cairo_pattern_add_color_stop_rgb(pat, 0.58, 0.72, 0.74, 0.75);
  cairo_pattern_add_color_stop_rgb(pat, 1.0, 0.31, 0.34, 0.36);
  cairo_set_source(cr, pat);
  cairo_fill_preserve(cr);
  cairo_pattern_destroy(pat);
  setColor(cr, hovered ? kAmber : kBg);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  cairo_save(cr);
  roundedRect(cr, cx - 27.0, capY - 10.0, 54.0, 20.0, 3.0);
  cairo_clip(cr);
  cairo_pattern_t* gloss = cairo_pattern_create_linear(
      cx, capY - 10.0, cx, capY + 2.0);
  cairo_pattern_add_color_stop_rgba(gloss, 0.0, 1.0, 1.0, 1.0, 0.35);
  cairo_pattern_add_color_stop_rgba(gloss, 1.0, 1.0, 1.0, 1.0, 0.0);
  cairo_rectangle(cr, cx - 27.0, capY - 10.0, 54.0, 12.0);
  cairo_set_source(cr, gloss);
  cairo_fill(cr);
  cairo_pattern_destroy(gloss);
  cairo_restore(cr);

  setColorA(cr, kBg, 0.95);
  cairo_rectangle(cr, cx - 18.0, capY - 1.5, 36.0, 3.0);
  cairo_fill(cr);

  const std::string valueText = formatValue(w.port, value);
  textCentered(cr, cx, bottom + 26.0, valueText.c_str(), 9.0, false, kText);
  textCentered(cr, cx, bottom + 42.0, "OUTPUT", 8.0, true, kMuted);
}

static void drawVu(cairo_t* cr, const Widget& w, float levelDb) {
  const double x = w.x;
  const double y = w.y;
  const double width = w.w;
  const double height = w.h;
  const double cx = x + width * 0.5;
  const double pivotY = y + height - 14.0;
  const double radius = std::min(width * 0.42, height * 0.74);

  // metal bezel
  roundedRect(cr, x - 4.0, y - 4.0, width + 8.0, height + 8.0, 7.0);
  cairo_pattern_t* bezel = cairo_pattern_create_linear(
      x - 4.0, y - 4.0, x - 4.0, y + height + 4.0);
  cairo_pattern_add_color_stop_rgb(bezel, 0.0, 0.68, 0.71, 0.73);
  cairo_pattern_add_color_stop_rgb(bezel, 0.45, 0.30, 0.34, 0.38);
  cairo_pattern_add_color_stop_rgb(bezel, 1.0, 0.09, 0.11, 0.13);
  cairo_set_source(cr, bezel);
  cairo_fill_preserve(cr);
  cairo_pattern_destroy(bezel);
  setColorA(cr, kBg, 0.85);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  roundedRect(cr, x, y, width, height, 4.0);
  setColor(cr, kMeterFace);
  cairo_fill(cr);
  roundedRect(cr, x, y, width, height, 4.0);
  setColorA(cr, kMeterInk, 0.7);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  const double start = kPi * 1.14;
  const double end = kPi * 1.86;
  const double sweep = end - start;
  cairo_set_line_width(cr, 1.0);
  setColor(cr, kMeterInk);
  cairo_arc(cr, cx, pivotY, radius, start, end);
  cairo_stroke(cr);

  const float marks[] = {-20.0f, -10.0f, -5.0f, 0.0f, 3.0f};
  const char* labels[] = {"-20", "-10", "-5", "0", "+3"};
  for (int i = 0; i < 5; ++i) {
    const float norm = (marks[i] + 20.0f) / 23.0f;
    const double a = start + sweep * norm;
    const double x0 = cx + std::cos(a) * (radius - 2.0);
    const double y0 = pivotY + std::sin(a) * (radius - 2.0);
    const double x1 = cx + std::cos(a) * (radius + 4.0);
    const double y1 = pivotY + std::sin(a) * (radius + 4.0);
    setColor(cr, kMeterInk);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, x0, y0);
    cairo_line_to(cr, x1, y1);
    cairo_stroke(cr);

    const double lx = cx + std::cos(a) * (radius - 12.0);
    const double ly = pivotY + std::sin(a) * (radius - 12.0) + 2.5;
    textCentered(cr, lx, ly, labels[i], 7.0, false, kMeterInk);
  }

  const float norm = std::min(std::max((levelDb + 20.0f) / 23.0f, 0.0f), 1.0f);
  const double a = start + sweep * norm;
  setColor(cr, kRed);
  cairo_set_line_width(cr, 1.5);
  cairo_move_to(cr, cx, pivotY);
  cairo_line_to(cr, cx + std::cos(a) * (radius - 6.0),
                pivotY + std::sin(a) * (radius - 6.0));
  cairo_stroke(cr);
  cairo_arc(cr, cx, pivotY, 3.0, 0.0, kPi * 2.0);
  setColor(cr, kMeterInk);
  cairo_fill(cr);

  // glass reflection over the meter face
  cairo_save(cr);
  roundedRect(cr, x, y, width, height, 4.0);
  cairo_clip(cr);
  cairo_pattern_t* glass = cairo_pattern_create_linear(
      x, y, x, y + height * 0.72);
  cairo_pattern_add_color_stop_rgba(glass, 0.0, 1.0, 1.0, 1.0, 0.20);
  cairo_pattern_add_color_stop_rgba(glass, 1.0, 1.0, 1.0, 1.0, 0.0);
  cairo_rectangle(cr, x, y, width, height * 0.72);
  cairo_set_source(cr, glass);
  cairo_fill(cr);
  cairo_pattern_destroy(glass);

  cairo_move_to(cr, x - 10.0, y + height * 0.18);
  cairo_line_to(cr, x + width * 0.48, y - 20.0);
  cairo_line_to(cr, x + width * 0.70, y - 20.0);
  cairo_line_to(cr, x + 2.0, y + height * 0.40);
  cairo_close_path(cr);
  setColorA(cr, kHighlight, 0.06);
  cairo_fill(cr);
  cairo_restore(cr);

  setColorA(cr, kHighlight, 0.16);
  roundedRect(cr, x, y, width, height, 4.0);
  cairo_set_line_width(cr, 0.8);
  cairo_stroke(cr);

  textAt(cr, x + 8.0, y + 14.0, "VU", 8.0, true, kMeterInk);
  textAt(cr, x + width - 28.0, y + height - 5.0, "dBFS", 5.8, false,
         kMeterInk);

  const bool peak = levelDb > -1.0f;
  if (peak) {
    cairo_pattern_t* glow = cairo_pattern_create_radial(
        x + width - 10.0, y + 10.0, 1.0, x + width - 10.0, y + 10.0, 11.0);
    cairo_pattern_add_color_stop_rgba(glow, 0.0, 1.0, 0.20, 0.12, 0.5);
    cairo_pattern_add_color_stop_rgba(glow, 1.0, 1.0, 0.10, 0.05, 0.0);
    cairo_arc(cr, x + width - 10.0, y + 10.0, 11.0, 0.0, kPi * 2.0);
    cairo_set_source(cr, glow);
    cairo_fill(cr);
    cairo_pattern_destroy(glow);
  }
  cairo_arc(cr, x + width - 10.0, y + 10.0, 3.2, 0.0, kPi * 2.0);
  setColor(cr, peak ? kRed : kRedDark);
  cairo_fill(cr);
}

static void drawGr(cairo_t* cr, const Widget& w, float grDb) {
  roundedRect(cr, w.x - 1.5, w.y - 1.5, w.w + 3.0, w.h + 3.0, 3.5);
  setColorA(cr, kBg, 0.55);
  cairo_fill(cr);

  roundedRect(cr, w.x, w.y, w.w, w.h, 2.5);
  cairo_pattern_t* bg = cairo_pattern_create_linear(
      w.x, w.y, w.x + w.w, w.y);
  cairo_pattern_add_color_stop_rgb(bg, 0.0, 0.055, 0.065, 0.075);
  cairo_pattern_add_color_stop_rgb(bg, 0.5, 0.015, 0.020, 0.025);
  cairo_pattern_add_color_stop_rgb(bg, 1.0, 0.045, 0.055, 0.065);
  cairo_set_source(cr, bg);
  cairo_fill(cr);
  cairo_pattern_destroy(bg);
  setColorA(cr, kPanelEdge, 0.8);
  cairo_set_line_width(cr, 0.8);
  roundedRect(cr, w.x, w.y, w.w, w.h, 2.5);
  cairo_stroke(cr);

  const float n = std::min(std::max(-grDb / 30.0f, 0.0f), 1.0f);
  if (n > 0.0f) {
    cairo_pattern_t* red = cairo_pattern_create_linear(
        w.x, w.y, w.x + w.w, w.y);
    cairo_pattern_add_color_stop_rgb(red, 0.0, 0.38, 0.045, 0.035);
    cairo_pattern_add_color_stop_rgb(red, 0.5, 0.82, 0.16, 0.10);
    cairo_pattern_add_color_stop_rgb(red, 1.0, 0.45, 0.055, 0.040);
    cairo_rectangle(cr, w.x + 1.0, w.y + 1.0, w.w - 2.0,
                    (w.h - 2.0) * n);
    cairo_set_source(cr, red);
    cairo_fill(cr);
    cairo_pattern_destroy(red);
  }

  setColorA(cr, kHighlight, 0.16);
  cairo_rectangle(cr, w.x + 1.0, w.y + 1.0, 2.0, w.h - 2.0);
  cairo_fill(cr);

  for (int i = 1; i < 4; ++i) {
    const double y = w.y + w.h * (i / 4.0);
    setColorA(cr, kBg, 0.55);
    cairo_set_line_width(cr, 0.8);
    cairo_move_to(cr, w.x, y);
    cairo_line_to(cr, w.x + w.w, y);
    cairo_stroke(cr);
  }

  textCentered(cr, w.x + w.w * 0.5, w.y + w.h + 18.0, "GR", 7.5, true, kMuted);
}

static void drawPresetArrow(cairo_t* cr, const Rect& r, bool left,
                            bool hovered) {
  roundedRect(cr, r.x, r.y, r.w, r.h, 3.0);
  cairo_pattern_t* pat = cairo_pattern_create_linear(r.x, r.y, r.x,
                                                     r.y + r.h);
  cairo_pattern_add_color_stop_rgb(pat, 0.0, kChassisLight.r,
                                   kChassisLight.g, kChassisLight.b);
  cairo_pattern_add_color_stop_rgb(pat, 1.0, kChassisMid.r, kChassisMid.g,
                                   kChassisMid.b);
  cairo_set_source(cr, pat);
  cairo_fill_preserve(cr);
  cairo_pattern_destroy(pat);
  setColor(cr, hovered ? kAmber : kPanelEdge);
  cairo_set_line_width(cr, hovered ? 1.4 : 0.9);
  cairo_stroke(cr);

  const double cx = r.x + r.w * 0.5;
  const double cy = r.y + r.h * 0.5;
  const double d = 5.0;
  cairo_move_to(cr, cx + (left ? d : -d), cy - d);
  cairo_line_to(cr, cx + (left ? -d : d), cy);
  cairo_line_to(cr, cx + (left ? d : -d), cy + d);
  cairo_close_path(cr);
  setColor(cr, hovered ? kCream : kText);
  cairo_fill(cr);
}

static void drawPresetBar(cairo_t* cr, const State& s) {
  textAt(cr, kPresetLcd.x, 18.0, "PRESET", 5.8, true, kMuted);

  // recessed amber LCD
  roundedRect(cr, kPresetLcd.x - 2.0, kPresetLcd.y - 2.0,
              kPresetLcd.w + 4.0, kPresetLcd.h + 4.0, 4.0);
  setColor(cr, kPanelEdge);
  cairo_fill(cr);
  roundedRect(cr, kPresetLcd.x, kPresetLcd.y, kPresetLcd.w, kPresetLcd.h,
              2.5);
  cairo_pattern_t* lcd = cairo_pattern_create_linear(
      kPresetLcd.x, kPresetLcd.y, kPresetLcd.x, kPresetLcd.y + kPresetLcd.h);
  cairo_pattern_add_color_stop_rgb(lcd, 0.0, 0.075, 0.055, 0.025);
  cairo_pattern_add_color_stop_rgb(lcd, 1.0, 0.025, 0.020, 0.012);
  cairo_set_source(cr, lcd);
  cairo_fill_preserve(cr);
  cairo_pattern_destroy(lcd);
  setColor(cr, s.presetHover == 3 ? kAmber : kBg);
  cairo_set_line_width(cr, s.presetHover == 3 ? 1.2 : 1.0);
  cairo_stroke(cr);

  cairo_save(cr);
  roundedRect(cr, kPresetLcd.x, kPresetLcd.y, kPresetLcd.w, kPresetLcd.h,
              2.5);
  cairo_clip(cr);

  for (double y = kPresetLcd.y + 2.0; y < kPresetLcd.y + kPresetLcd.h;
       y += 2.0) {
    setColorA(cr, kBg, 0.18);
    cairo_rectangle(cr, kPresetLcd.x, y, kPresetLcd.w, 1.0);
    cairo_fill(cr);
  }

  cairo_pattern_t* glass = cairo_pattern_create_linear(
      kPresetLcd.x, kPresetLcd.y, kPresetLcd.x,
      kPresetLcd.y + kPresetLcd.h * 0.65);
  cairo_pattern_add_color_stop_rgba(glass, 0.0, 1.0, 1.0, 1.0, 0.10);
  cairo_pattern_add_color_stop_rgba(glass, 1.0, 1.0, 1.0, 1.0, 0.0);
  cairo_set_source(cr, glass);
  cairo_rectangle(cr, kPresetLcd.x, kPresetLcd.y, kPresetLcd.w,
                  kPresetLcd.h * 0.65);
  cairo_fill(cr);
  cairo_pattern_destroy(glass);
  cairo_restore(cr);

  const char* name =
      (s.presetIndex >= 0) ? kPresets[s.presetIndex].shortName : "INIT";
  char display[32];
  std::snprintf(display, sizeof(display), "%s%s", name,
                s.presetDirty ? "*" : "");
  textAt(cr, kPresetLcd.x + 14.0, kPresetLcd.y + 23.0, display, 10.5, true,
         kAmber, "DejaVu Sans Mono");

  char index[16];
  if (s.presetIndex >= 0) {
    std::snprintf(index, sizeof(index), "%d/%d", s.presetIndex + 1,
                  kPresetCount);
  } else {
    std::snprintf(index, sizeof(index), "--/%d", kPresetCount);
  }
  textAt(cr, kPresetLcd.x + kPresetLcd.w - 38.0,
         kPresetLcd.y + kPresetLcd.h - 5.0, index, 5.5, false, kMuted,
         "DejaVu Sans Mono");

  cairo_arc(cr, kPresetLcd.x + 7.0, kPresetLcd.y + 18.0, 2.2, 0.0,
            kPi * 2.0);
  setColor(cr, s.presetDirty ? kRed : kGreen);
  cairo_fill(cr);

  drawPresetArrow(cr, kPresetPrev, true, s.presetHover == 1);
  drawPresetArrow(cr, kPresetNext, false, s.presetHover == 2);
}

static void drawSkinChip(cairo_t* cr, const State& s) {
  const Rect& r = kSkinChip;
  const auto& skin = hearth::theme::global().theme();
  roundedRect(cr, r.x, r.y, r.w, r.h, 5.0);
  setColorA(cr, kBg, 0.78);
  cairo_fill_preserve(cr);
  setColor(cr, s.skinHover ? kAmber : kPanelEdge);
  cairo_set_line_width(cr, s.skinHover ? 1.3 : 0.9);
  cairo_stroke(cr);

  cairo_save(cr);
  roundedRect(cr, r.x + 1.0, r.y + 1.0, r.w - 2.0, r.h - 2.0, 4.5);
  cairo_clip(cr);

  const double sx = r.x + 8.0;
  const double sy = r.y + 7.0;
  const double sw = 26.0;
  const double sh = 26.0;
  cairo_save(cr);
  roundedRect(cr, sx, sy, sw, sh, 4.0);
  cairo_clip(cr);
  setColor(cr, kRed);
  cairo_rectangle(cr, sx, sy, sw / 3.0, sh);
  cairo_fill(cr);
  setColor(cr, kAmber);
  cairo_rectangle(cr, sx + sw / 3.0, sy, sw / 3.0, sh);
  cairo_fill(cr);
  setColor(cr, kCream);
  cairo_rectangle(cr, sx + 2.0 * sw / 3.0, sy, sw / 3.0, sh);
  cairo_fill(cr);
  cairo_restore(cr);

  textAt(cr, r.x + 44.0, r.y + 16.0, "SKIN", 5.8, true, kMuted);
  textAt(cr, r.x + 44.0, r.y + 31.0, skin.name.c_str(), 8.2, true, kCream);
  cairo_restore(cr);

  setColorA(cr, kHighlight, 0.20);
  cairo_set_line_width(cr, 0.7);
  cairo_move_to(cr, r.x + 8.0, r.y + r.h - 4.0);
  cairo_line_to(cr, r.x + r.w - 8.0, r.y + r.h - 4.0);
  cairo_stroke(cr);
}

static void drawSkinToast(cairo_t* cr, const State& s) {
  if (nowSeconds() >= s.skinToastUntil || s.skinToast[0] == '\0') {
    return;
  }
  const double w = 190.0;
  const double h = 16.0;
  const double x = kSkinChip.x - 6.0;
  const double y = kSkinChip.y + kSkinChip.h + 4.0;
  roundedRect(cr, x, y, w, h, 4.0);
  setColorA(cr, kBg, 0.94);
  cairo_fill_preserve(cr);
  setColorA(cr, kAmber, 0.9);
  cairo_set_line_width(cr, 0.8);
  cairo_stroke(cr);
  textCentered(cr, x + w * 0.5, y + 11.0, s.skinToast, 6.5, true, kCream);
}

static void drawHeader(cairo_t* cr, const State& s) {
  cairo_pattern_t* pat = cairo_pattern_create_linear(0, 0, 0, 80);
  cairo_pattern_add_color_stop_rgb(pat, 0.0, kHeaderTop.r, kHeaderTop.g,
                                   kHeaderTop.b);
  cairo_pattern_add_color_stop_rgb(pat, 1.0, kHeaderBottom.r, kHeaderBottom.g,
                                   kHeaderBottom.b);
  cairo_rectangle(cr, 0, 0, UI_W, 80);
  cairo_set_source(cr, pat);
  cairo_fill(cr);
  cairo_pattern_destroy(pat);

  paintTexture(cr, 0.0, 0.0, UI_W, 80.0, 0.06);

  cairo_pattern_t* glass = cairo_pattern_create_linear(0, 0, 0, 26);
  cairo_pattern_add_color_stop_rgba(glass, 0.0, 1.0, 1.0, 1.0, 0.09);
  cairo_pattern_add_color_stop_rgba(glass, 1.0, 1.0, 1.0, 1.0, 0.0);
  cairo_rectangle(cr, 0, 0, UI_W, 26);
  cairo_set_source(cr, glass);
  cairo_fill(cr);
  cairo_pattern_destroy(glass);

  drawWordmark(cr, 22.0, 52.0, 38.0);
  textAt(cr, 24.0, 70.0, "EQ SUITE", 8.5, true, kMuted);

  drawPresetBar(cr, s);
  drawSkinChip(cr, s);

  // Variant badge.
  roundedRect(cr, 648.0, 24.0, 200.0, 38.0, 4.0);
  setColorA(cr, kBg, 0.65);
  cairo_fill_preserve(cr);
  setColorA(cr, kPanelEdge, 0.8);
  cairo_set_line_width(cr, 0.9);
  cairo_stroke(cr);
#ifdef TROWEL_MASTER
  textAt(cr, 662.0, 40.0, "MASTERING", 10.0, true, kAmber);
  textAt(cr, 662.0, 53.0, "4096 FFT · M/S · DELTA", 6.4, false, kMuted);
#else
  textAt(cr, 662.0, 40.0, "ZERO LATENCY", 10.0, true, kGreen);
  textAt(cr, 662.0, 53.0, "ADAPTIVE TAME · 0 SMP", 6.4, false, kMuted);
#endif

  textAt(cr, 874.0, 38.0, "COLOUR · CONSOLE · TAME", 7.5, true, kMuted);
  textAt(cr, 874.0, 52.0, "6 SIGNAL ROUTINGS", 6.5, false, kMuted);
  textAt(cr, 874.0, 66.0, "v0.2.0", 6.5, false, kMuted);

  // brushed-metal strip along the bottom of the header
  cairo_pattern_t* strip = cairo_pattern_create_linear(0, 76, 0, 80);
  cairo_pattern_add_color_stop_rgb(strip, 0.0, kChassisLight.r,
                                   kChassisLight.g, kChassisLight.b);
  cairo_pattern_add_color_stop_rgb(strip, 1.0, kChassisDark.r, kChassisDark.g,
                                   kChassisDark.b);
  cairo_rectangle(cr, 0, 76, UI_W, 4);
  cairo_set_source(cr, strip);
  cairo_fill(cr);
  cairo_pattern_destroy(strip);

  setColorA(cr, kHighlight, 0.16);
  cairo_set_line_width(cr, 0.8);
  cairo_move_to(cr, 14.0, 76.0);
  cairo_line_to(cr, UI_W - 14.0, 76.0);
  cairo_stroke(cr);
  setColorA(cr, kBg, 0.65);
  cairo_move_to(cr, 14.0, 79.5);
  cairo_line_to(cr, UI_W - 14.0, 79.5);
  cairo_stroke(cr);

  setColor(cr, kRed);
  cairo_rectangle(cr, 14.0, 80.0, 24.0, 2.0);
  cairo_fill(cr);
}

// ---------------------------------------------------------------------------
// Response scope
// ---------------------------------------------------------------------------
static float tameGainAt(const State& s, float freq) {
  const float t = std::log(std::max(freq, 20.0f) / 20.0f) / std::log(1000.0f);
  const float pos = clamp(t, 0.0f, 1.0f) *
                    static_cast<float>(kTelemetryBins - 1);
  const int i0 = static_cast<int>(pos);
  const int i1 = std::min(i0 + 1, static_cast<int>(kTelemetryBins) - 1);
  const float frac = pos - static_cast<float>(i0);
  return s.tameGainSmooth[i0] * (1.0f - frac) +
         s.tameGainSmooth[i1] * frac;
}

static void drawScope(cairo_t* cr, const State& s) {
  drawPanel(cr, 14.0, 86.0, 1212.0, 220.0, "RESPONSE");
  textAt(cr, 124.0, 106.0, "LIVE SPECTRUM · EQ CURVE · TAME REDUCTION", 6.8,
         false, kMuted);

  const double x0 = 30.0;
  const double y0 = 122.0;
  const double w = 1180.0;
  const double h = 148.0;

  roundedRect(cr, x0 - 2.0, y0 - 2.0, w + 4.0, h + 4.0, 5.0);
  setColor(cr, kPanelEdge);
  cairo_fill(cr);
  roundedRect(cr, x0, y0, w, h, 3.5);
  cairo_pattern_t* crt = cairo_pattern_create_linear(x0, y0, x0, y0 + h);
  cairo_pattern_add_color_stop_rgb(crt, 0.0, kScopeBgTop.r, kScopeBgTop.g,
                                   kScopeBgTop.b);
  cairo_pattern_add_color_stop_rgb(crt, 1.0, kScopeBgBottom.r,
                                   kScopeBgBottom.g, kScopeBgBottom.b);
  cairo_set_source(cr, crt);
  cairo_fill(cr);
  cairo_pattern_destroy(crt);

  cairo_save(cr);
  roundedRect(cr, x0, y0, w, h, 3.5);
  cairo_clip(cr);

  const auto fx = [&](double f) {
    return x0 + w * std::log(std::max(f, 20.0) / 20.0) / std::log(1000.0);
  };
  const auto fy = [&](double db) {
    return y0 + h * (1.0 - (db + 24.0) / 48.0);
  };
  // The spectrum has its own -60..0 dB scale, overlaid behind the EQ curves.
  const auto sy = [&](double db) {
    return y0 + h * (1.0 - std::min(std::max((db + 60.0) / 60.0, 0.0), 1.0));
  };

  const double freqs[] = {20, 30, 50, 100, 200, 300, 500,
                          1000, 2000, 3000, 5000, 10000, 20000};
  for (double f : freqs) {
    setColorA(cr, kScopeGrid, f == 1000.0 ? 0.55 : 0.30);
    cairo_set_line_width(cr, f == 1000.0 ? 1.0 : 0.7);
    cairo_move_to(cr, fx(f), y0);
    cairo_line_to(cr, fx(f), y0 + h);
    cairo_stroke(cr);
  }
  const double levels[] = {-18, -12, -6, 0, 6, 12, 18};
  for (double db : levels) {
    setColorA(cr, kScopeGrid, db == 0.0 ? 0.65 : 0.28);
    cairo_set_line_width(cr, db == 0.0 ? 1.1 : 0.7);
    cairo_move_to(cr, x0, fy(db));
    cairo_line_to(cr, x0 + w, fy(db));
    cairo_stroke(cr);
  }
  const double dbLabels[] = {-18, -6, 0, 6, 18};
  for (double db : dbLabels) {
    char label[8];
    std::snprintf(label, sizeof(label), "%+.0f", db);
    textAt(cr, x0 + 5.0, fy(db) + 3.0, label, 5.5, false, kMuted);
  }

  // Live spectrum, filled under the trace.
  cairo_new_path(cr);
  cairo_move_to(cr, x0, y0 + h);
  for (uint32_t i = 0; i < kTelemetryBins; ++i) {
    const double t = static_cast<double>(i) /
                     static_cast<double>(kTelemetryBins - 1);
    const double f = 20.0 * std::pow(1000.0, t);
    cairo_line_to(cr, fx(f), sy(s.spectrumSmooth[i]));
  }
  cairo_line_to(cr, x0 + w, y0 + h);
  cairo_close_path(cr);
  cairo_set_source_rgba(cr, kScopeGreen.r, kScopeGreen.g, kScopeGreen.b, 0.14);
  cairo_fill(cr);

  cairo_new_path(cr);
  for (uint32_t i = 0; i < kTelemetryBins; ++i) {
    const double t = static_cast<double>(i) /
                     static_cast<double>(kTelemetryBins - 1);
    const double f = 20.0 * std::pow(1000.0, t);
    if (i == 0) {
      cairo_move_to(cr, fx(f), sy(s.spectrumSmooth[i]));
    } else {
      cairo_line_to(cr, fx(f), sy(s.spectrumSmooth[i]));
    }
  }
  cairo_set_source_rgba(cr, kScopeGreen.r, kScopeGreen.g, kScopeGreen.b, 0.75);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  // Static EQ curves from the current control values.
  PultecParams pp;
  pp.lowFreq = s.values[PORT_PUL_LOW_FREQ];
  pp.lowBoostDb = s.values[PORT_PUL_LOW_BOOST];
  pp.lowCutDb = s.values[PORT_PUL_LOW_CUT];
  pp.highFreq = s.values[PORT_PUL_HIGH_FREQ];
  pp.highBoostDb = s.values[PORT_PUL_HIGH_BOOST];
  pp.bandwidth = s.values[PORT_PUL_BANDWIDTH];
  pp.highCutFreq = s.values[PORT_PUL_HIGH_CUT_FREQ];
  pp.highCutDb = s.values[PORT_PUL_HIGH_CUT];
  pp.iron = false;
  PultecEq peq;
  peq.prepare(48000.0f);
  peq.setTargets(pp);
  peq.reset();

  SslParams sp;
  sp.lfFreq = s.values[PORT_SSL_LF_FREQ];
  sp.lfGainDb = s.values[PORT_SSL_LF_GAIN];
  sp.lfBell = s.values[PORT_SSL_LF_BELL] >= 0.5f;
  sp.lmfFreq = s.values[PORT_SSL_LMF_FREQ];
  sp.lmfGainDb = s.values[PORT_SSL_LMF_GAIN];
  sp.hmfFreq = s.values[PORT_SSL_HMF_FREQ];
  sp.hmfGainDb = s.values[PORT_SSL_HMF_GAIN];
  sp.hfFreq = s.values[PORT_SSL_HF_FREQ];
  sp.hfGainDb = s.values[PORT_SSL_HF_GAIN];
  sp.hfBell = s.values[PORT_SSL_HF_BELL] >= 0.5f;
  sp.hpfFreq = s.values[PORT_SSL_HPF];
  sp.lpfFreq = s.values[PORT_SSL_LPF];
  SslEq seq;
  seq.prepare(48000.0f);
  seq.setTargets(sp);
  seq.reset();

  const bool pulOn = s.values[PORT_PULTEC_ON] >= 0.5f;
  const bool sslOn = s.values[PORT_SSL_ON] >= 0.5f;
  const bool tameOn = s.values[PORT_TAME_ON] >= 0.5f;

  struct Curve {
    Color color;
    float width;
    float alpha;
    bool enabled;
  };
  const Curve curves[3] = {
      {kCream, 1.2f, 0.65f, pulOn},
      {kAmber, 1.2f, 0.70f, sslOn},
      {kVerdigris, 1.4f, 0.85f, tameOn},
  };

  for (int c = 0; c < 3; ++c) {
    cairo_new_path(cr);
    bool started = false;
    for (int i = 0; i <= 220; ++i) {
      const double t = i / 220.0;
      const double f = 20.0 * std::pow(1000.0, t);
      double db = 0.0;
      if (c == 0) {
        db = peq.magnitudeDb(static_cast<float>(f), 48000.0f);
      } else if (c == 1) {
        db = seq.magnitudeDb(static_cast<float>(f), 48000.0f);
      } else {
        db = tameGainAt(s, static_cast<float>(f));
      }
      db = clamp(db, -24.0, 24.0);
      if (!started) {
        cairo_move_to(cr, fx(f), fy(db));
        started = true;
      } else {
        cairo_line_to(cr, fx(f), fy(db));
      }
    }
    setColorA(cr, curves[c].color, curves[c].enabled ? curves[c].alpha : 0.22);
    cairo_set_line_width(cr, curves[c].width);
    cairo_stroke(cr);
  }

  // Combined response.
  cairo_new_path(cr);
  for (int i = 0; i <= 240; ++i) {
    const double t = i / 240.0;
    const double f = 20.0 * std::pow(1000.0, t);
    double db = 0.0;
    if (pulOn) db += peq.magnitudeDb(static_cast<float>(f), 48000.0f);
    if (sslOn) db += seq.magnitudeDb(static_cast<float>(f), 48000.0f);
    if (tameOn) db += tameGainAt(s, static_cast<float>(f));
    db = clamp(db, -24.0, 24.0);
    if (i == 0) {
      cairo_move_to(cr, fx(f), fy(db));
    } else {
      cairo_line_to(cr, fx(f), fy(db));
    }
  }
  setColorA(cr, kRed, 0.95);
  cairo_set_line_width(cr, 2.0);
  cairo_stroke(cr);

  cairo_restore(cr);

  // Frequency labels.
  const struct {
    double f;
    const char* label;
  } labels[] = {{20, "20"},   {50, "50"},   {100, "100"}, {200, "200"},
                {500, "500"}, {1000, "1k"}, {2000, "2k"}, {5000, "5k"},
                {10000, "10k"}, {20000, "20k"}};
  for (const auto& l : labels) {
    textCentered(cr, fx(l.f), y0 + h + 13.0, l.label, 6.5, false, kMuted);
  }

  // Legend.
  const char* legend[] = {"COLOUR", "CONSOLE", "TAME", "SUM"};
  const Color legendColors[] = {kCream, kAmber, kVerdigris, kRed};
  double lx = x0 + 12.0;
  for (int i = 0; i < 4; ++i) {
    cairo_arc(cr, lx, y0 + 12.0, 2.5, 0.0, kPi * 2.0);
    setColor(cr, legendColors[i]);
    cairo_fill(cr);
    textAt(cr, lx + 7.0, y0 + 15.0, legend[i], 6.8, true, kMuted);
    lx += 76.0;
  }
}

// ---------------------------------------------------------------------------
// Signal-chain bar
// ---------------------------------------------------------------------------
static void drawChainBar(cairo_t* cr, const State& s) {
  roundedRect(cr, 14.0, 312.0, 1212.0, 40.0, 6.0);
  cairo_pattern_t* bar = cairo_pattern_create_linear(14.0, 312.0, 14.0, 352.0);
  cairo_pattern_add_color_stop_rgb(bar, 0.0, 0.115, 0.145, 0.175);
  cairo_pattern_add_color_stop_rgb(bar, 1.0, 0.045, 0.058, 0.070);
  cairo_set_source(cr, bar);
  cairo_fill_preserve(cr);
  cairo_pattern_destroy(bar);
  setColorA(cr, kPanelEdge, 0.7);
  cairo_set_line_width(cr, 0.9);
  cairo_stroke(cr);

  textAt(cr, 24.0, 337.0, "SIGNAL CHAIN", 7.0, true, kMuted);
  drawPresetArrow(cr, kChainPrev, true, false);
  drawPresetArrow(cr, kChainNext, false, false);

  static constexpr int kOrders[6][3] = {
      {0, 1, 2}, {0, 2, 1}, {1, 0, 2},
      {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
  const int routing = std::min(
      std::max(static_cast<int>(std::lround(s.values[PORT_ROUTING])), 0), 5);
  const int* order = kOrders[routing];

  const char* names[] = {"COLOUR", "CONSOLE", "TAME"};
  const uint32_t ports[] = {PORT_PULTEC_ON, PORT_SSL_ON, PORT_TAME_ON};
  const Color colors[] = {kCream, kAmber, kVerdigris};
  const double chipX[] = {160.0, 490.0, 820.0};
  const double chipW = 290.0;

  for (int slot = 0; slot < 3; ++slot) {
    const int module = order[slot];
    const bool on = s.values[ports[module]] >= 0.5f;
    const double x = chipX[slot];
    roundedRect(cr, x, 318.0, chipW, 28.0, 4.0);
    setColorA(cr, on ? kPanelTop : kPanelBottom, 1.0);
    cairo_fill_preserve(cr);
    setColorA(cr, on ? colors[module] : kPanelEdge, on ? 0.75 : 0.35);
    cairo_set_line_width(cr, 0.9);
    cairo_stroke(cr);

    cairo_arc(cr, x + 16.0, 332.0, 3.6, 0.0, kPi * 2.0);
    setColorA(cr, colors[module], on ? 0.95 : 0.22);
    cairo_fill(cr);
    setColorA(cr, colors[module], on ? 1.0 : 0.35);
    textAt(cr, x + 28.0, 337.0, names[module], 10.0, true,
           on ? colors[module] : kMuted);
    textAt(cr, x + chipW - 56.0, 337.0, on ? "ON" : "BYP", 7.0, true,
           on ? kText : kMuted);

    if (slot < 2) {
      textAt(cr, x + chipW + 9.0, 337.0, ">", 14.0, true, kMuted);
    }
  }
}

// ---------------------------------------------------------------------------
// Full render
// ---------------------------------------------------------------------------
static void drawChassis(cairo_t* cr) {
  cairo_save(cr);
  cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
  roundedRect(cr, 4.0, 4.0, UI_W - 8.0, UI_H - 8.0, 14.0);
  roundedRect(cr, 12.0, 12.0, UI_W - 24.0, UI_H - 24.0, 10.0);
  cairo_pattern_t* metal = cairo_pattern_create_linear(0, 0, UI_W, UI_H);
  cairo_pattern_add_color_stop_rgb(metal, 0.0, kChassisLight.r,
                                   kChassisLight.g, kChassisLight.b);
  cairo_pattern_add_color_stop_rgb(metal, 0.45, kChassisDark.r, kChassisDark.g,
                                   kChassisDark.b);
  cairo_pattern_add_color_stop_rgb(metal, 0.55, kChassisMid.r, kChassisMid.g,
                                   kChassisMid.b);
  cairo_pattern_add_color_stop_rgb(metal, 1.0, kChassisLight.r,
                                   kChassisLight.g, kChassisLight.b);
  cairo_set_source(cr, metal);
  cairo_fill(cr);
  cairo_pattern_destroy(metal);
  cairo_restore(cr);

  roundedRect(cr, 4.0, 4.0, UI_W - 8.0, UI_H - 8.0, 14.0);
  setColorA(cr, kBg, 0.9);
  cairo_set_line_width(cr, 1.2);
  cairo_stroke(cr);
  roundedRect(cr, 12.0, 12.0, UI_W - 24.0, UI_H - 24.0, 10.0);
  setColorA(cr, kBg, 0.75);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  setColorA(cr, kHighlight, 0.18);
  cairo_set_line_width(cr, 0.8);
  cairo_move_to(cr, 18.0, 5.5);
  cairo_line_to(cr, UI_W - 18.0, 5.5);
  cairo_stroke(cr);
  setColorA(cr, kBg, 0.75);
  cairo_move_to(cr, 18.0, UI_H - 5.5);
  cairo_line_to(cr, UI_W - 18.0, UI_H - 5.5);
  cairo_stroke(cr);

  drawScrew(cr, 16.0, 16.0);
  drawScrew(cr, UI_W - 16.0, 16.0);
  drawScrew(cr, 16.0, UI_H - 16.0);
  drawScrew(cr, UI_W - 16.0, UI_H - 16.0);
}

static void render(const State& s, cairo_t* cr) {
  cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);

  cairo_pattern_t* bg = cairo_pattern_create_linear(0, 0, 0, UI_H);
  cairo_pattern_add_color_stop_rgb(bg, 0.0, kBgTop.r, kBgTop.g, kBgTop.b);
  cairo_pattern_add_color_stop_rgb(bg, 1.0, kBgBottom.r, kBgBottom.g,
                                   kBgBottom.b);
  cairo_rectangle(cr, 0, 0, UI_W, UI_H);
  cairo_set_source(cr, bg);
  cairo_fill(cr);
  cairo_pattern_destroy(bg);

  cairo_pattern_t* vignette = cairo_pattern_create_radial(
      UI_W * 0.5, UI_H * 0.45, UI_H * 0.10, UI_W * 0.5, UI_H * 0.5,
      UI_H * 0.82);
  cairo_pattern_add_color_stop_rgba(vignette, 0.0, 0.0, 0.0, 0.0, 0.0);
  cairo_pattern_add_color_stop_rgba(vignette, 1.0, 0.0, 0.0, 0.0, 0.42);
  cairo_rectangle(cr, 0, 0, UI_W, UI_H);
  cairo_set_source(cr, vignette);
  cairo_fill(cr);
  cairo_pattern_destroy(vignette);

  paintTexture(cr, 0.0, 0.0, UI_W, UI_H, 0.035);

  drawHeader(cr, s);
  drawScope(cr, s);
  drawChainBar(cr, s);

  drawPanel(cr, 14.0, 360.0, 370.0, 520.0, "COLOUR");
  drawPanel(cr, 396.0, 360.0, 370.0, 520.0, "CONSOLE");
  drawPanel(cr, 778.0, 360.0, 350.0, 520.0, "TAME");
  drawPanel(cr, 1140.0, 360.0, 86.0, 520.0, "I/O");

  textAt(cr, 30.0, 822.0, "BOOST + CUT = LOW-END TRICK", 6.3, false, kMuted);
  textAt(cr, 412.0, 854.0, "18 dB/OCT HPF · 12 dB/OCT LPF", 6.3, false, kMuted);
  textAt(cr, 794.0, 840.0, "LISTEN = HEAR WHAT IS REMOVED", 6.3, false, kMuted);

  const bool enabled = s.values[PORT_ENABLE] >= 0.5f;

  for (int i = 0; i < kNumWidgets; ++i) {
    const Widget& w = kWidgets[i];
    const bool hovered = (s.hover == i);

    switch (w.kind) {
      case Kind::Knob:
        drawKnob(cr, w, s.values[w.port], hovered, enabled);
        break;
      case Kind::Switch:
        drawSwitch(cr, w, s.values[w.port], hovered, enabled);
        break;
      case Kind::Toggle: {
        const bool on = s.values[w.port] >= 0.5f;
        if (w.port == PORT_ENABLE) {
          drawPowerButton(cr, w, on, "IN", "BYP");
        } else if (w.port == PORT_PULTEC_ON || w.port == PORT_SSL_ON ||
                   w.port == PORT_TAME_ON) {
          drawPillToggle(cr, w, on, on ? "ON" : "OFF");
        } else if (w.port == PORT_PUL_IRON) {
          drawPillToggle(cr, w, on, "IRON");
        } else if (w.port == PORT_SSL_LF_BELL || w.port == PORT_SSL_HF_BELL) {
          drawPillToggle(cr, w, on, on ? "BELL" : "SHELF");
        } else if (w.port == PORT_TAME_LISTEN) {
          drawPillToggle(cr, w, on, "LISTEN");
        } else if (w.port == PORT_STEREO_MODE) {
          drawPillToggle(cr, w, on, on ? "M/S" : "LINKED");
        } else if (w.port == PORT_AUTO_GAIN) {
          drawPillToggle(cr, w, on, "AUTO GAIN");
        } else if (w.port == PORT_DELTA) {
          drawPillToggle(cr, w, on, "DELTA");
        } else {
          drawPillToggle(cr, w, on, on ? "ON" : "OFF");
        }
        break;
      }
      case Kind::Fader:
        drawFader(cr, w, s.values[w.port], hovered, enabled);
        break;
      case Kind::Vu:
        drawVu(cr, w, s.vuSmooth);
        break;
      case Kind::Meter:
        drawGr(cr, w, s.meterGrSmooth);
        break;
    }
  }

  // I/O column readouts.
  char latencyText[32];
  std::snprintf(latencyText, sizeof(latencyText), "%.0f SMP",
                static_cast<double>(s.latency));
  textCentered(cr, 1183.0, 852.0, "LATENCY", 6.3, true, kMuted);
  textCentered(cr, 1183.0, 864.0, latencyText, 6.8, false, kText);
  if (s.latency > 0.5f) {
    textCentered(cr, 1183.0, 876.0, "HOST PDC", 5.8, false, kMuted);
  } else {
    textCentered(cr, 1183.0, 876.0, "ZERO LAT", 5.8, false, kGreen);
  }

  char outText[32];
  std::snprintf(outText, sizeof(outText), "%.1f dB",
                static_cast<double>(s.vuSmooth));
  textCentered(cr, 1183.0, 676.0, "PEAK", 6.0, true, kMuted);
  textCentered(cr, 1183.0, 688.0, outText, 6.2, false, kText);

  // Footer.
  setColorA(cr, kPanelEdge, 0.35);
  cairo_set_line_width(cr, 0.7);
  cairo_move_to(cr, 20.0, 890.0);
  cairo_line_to(cr, UI_W - 20.0, 890.0);
  cairo_stroke(cr);
#ifdef TROWEL_MASTER
  textAt(cr, 20.0, 906.0, "TROWEL MASTER — MASTERING EQ SUITE", 6.5, true,
         kMuted);
#else
  textAt(cr, 20.0, 906.0, "TROWEL — ZERO-LATENCY EQ SUITE", 6.5, true, kMuted);
#endif
  textAt(cr, UI_W - 132.0, 906.0, "GPL-3.0-OR-LATER", 6.0, false, kMuted);

  drawChassis(cr);
  drawSkinToast(cr, s);
}

// ---------------------------------------------------------------------------
// Hit testing and interaction
// ---------------------------------------------------------------------------
static int hitTest(double x, double y) {
  for (int i = kNumWidgets - 1; i >= 0; --i) {
    const Widget& w = kWidgets[i];
    if (w.kind == Kind::Knob || w.kind == Kind::Switch) {
      const double dx = x - w.cx;
      const double dy = y - w.cy;
      const double rr = (w.kind == Kind::Switch ? w.r + 9.0 : w.r + 6.0);
      if (dx * dx + dy * dy <= rr * rr) {
        return i;
      }
    } else {
      if (x >= w.x && x <= w.x + w.w && y >= w.y && y <= w.y + w.h) {
        return i;
      }
    }
  }
  return -1;
}

struct Host {
  LV2UI_Write_Function write = nullptr;
  LV2UI_Controller controller = nullptr;
};

static void sendValue(const Host& host, uint32_t port, float value) {
  if (host.write) {
    host.write(host.controller, port, sizeof(float), 0, &value);
  }
}

static void setValue(State& s, const Host& host, uint32_t port, float value) {
  const ParamInfo& p = infoFor(port);
  value = std::min(std::max(value, std::min(p.min, p.max)),
                   std::max(p.min, p.max));
  s.values[port] = value;
  if (s.presetIndex >= 0) {
    s.presetDirty = true;
  }
  s.dirty = true;
  sendValue(host, port, value);
}

// ---------------------------------------------------------------------------
// X11 UI instance
// ---------------------------------------------------------------------------
struct X11UI {
  Display* display = nullptr;
  Window parent = 0;
  Window window = 0;
  bool topLevel = false;
  Visual* visual = nullptr;
  int depth = 24;
  cairo_surface_t* windowSurface = nullptr;
  cairo_surface_t* imageSurface = nullptr;
  cairo_t* imageCr = nullptr;
  State state;
  Host host;
  int windowW = 0;
  int windowH = 0;
  double scale = 1.0;
  double offsetX = 0.0;
  double offsetY = 0.0;
  const LV2UI_Resize* hostResize = nullptr;
  bool resizeRequested = false;
  float scaleFactor = 1.0f;
  int configPollCounter = 0;
};

static X11UI* get(LV2UI_Handle handle) {
  return static_cast<X11UI*>(handle);
}

static void refreshSkin(X11UI* ui) {
  if (!ui) return;
  auto& skin = hearth::theme::global();
  applyTheme(skin.theme());
  loadThemeAssets(skin.bundlePath().c_str());
  showSkinToast(ui->state, skin.theme().name.c_str());
  ui->state.dirty = true;
}

static void applyPreset(X11UI* ui, int index) {
  if (!ui || index < 0 || index >= kPresetCount) {
    return;
  }
  // Start from the defaults so every preset is a complete, repeatable state.
  for (int i = 0; i < kParamInfoCount; ++i) {
    setValue(ui->state, ui->host, kParams[i].index, kParams[i].def);
  }
  const Preset& preset = kPresets[index];
  for (int i = 0; i < preset.count; ++i) {
    setValue(ui->state, ui->host, preset.values[i].port,
             preset.values[i].value);
  }
  ui->state.presetIndex = index;
  ui->state.presetDirty = false;
  ui->state.dirty = true;
}

static void cyclePreset(X11UI* ui, int direction) {
  if (!ui) {
    return;
  }
  int index = ui->state.presetIndex;
  if (index < 0) {
    index = (direction >= 0) ? 0 : kPresetCount - 1;
  } else {
    index = (index + direction + kPresetCount) % kPresetCount;
  }
  applyPreset(ui, index);
}

static void debugLog(const char* fmt, ...) {
  if (!std::getenv("TROWEL_UI_DEBUG")) {
    return;
  }
  std::FILE* f = std::fopen("/tmp/trowel-ui.log", "a");
  if (!f) {
    return;
  }
  va_list args;
  va_start(args, fmt);
  std::vfprintf(f, fmt, args);
  va_end(args);
  std::fputc('\n', f);
  std::fclose(f);
}

static void updateScale(X11UI* ui) {
  if (ui->windowW <= 0 || ui->windowH <= 0) {
    return;
  }
  ui->scale = std::min(static_cast<double>(ui->windowW) / UI_W,
                       static_cast<double>(ui->windowH) / UI_H);
  ui->scale = std::max(ui->scale, 0.1);
  ui->offsetX = (ui->windowW - UI_W * ui->scale) * 0.5;
  ui->offsetY = (ui->windowH - UI_H * ui->scale) * 0.5;
}

static void ensureSurfaces(X11UI* ui) {
  if (!ui->imageSurface) {
    ui->imageSurface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, UI_W, UI_H);
    ui->imageCr = cairo_create(ui->imageSurface);
  }
}

static void blit(X11UI* ui) {
  ensureSurfaces(ui);
  render(ui->state, ui->imageCr);
  cairo_surface_flush(ui->imageSurface);

  if (!ui->windowSurface) {
    ui->windowSurface = cairo_xlib_surface_create(
        ui->display, ui->window, ui->visual, ui->windowW, ui->windowH);
  }
  cairo_t* cr = cairo_create(ui->windowSurface);
  cairo_set_source_rgba(cr, kBg.r, kBg.g, kBg.b, 1.0);
  cairo_paint(cr);

  cairo_save(cr);
  cairo_translate(cr, ui->offsetX, ui->offsetY);
  cairo_scale(cr, ui->scale, ui->scale);
  cairo_set_source_surface(cr, ui->imageSurface, 0.0, 0.0);
  cairo_paint(cr);
  cairo_restore(cr);

  cairo_destroy(cr);
  cairo_surface_flush(ui->windowSurface);
  XFlush(ui->display);
}

static void toLogical(const X11UI* ui, int x, int y, double& lx, double& ly) {
  lx = (x - ui->offsetX) / ui->scale;
  ly = (y - ui->offsetY) / ui->scale;
}

static void handleButtonPress(X11UI* ui, XButtonEvent& ev) {
  double x = 0.0;
  double y = 0.0;
  toLogical(ui, ev.x, ev.y, x, y);

  if (ev.button == Button1) {
    if (inRect(x, y, kSkinChip)) {
      const int direction = (ev.state & ShiftMask) ? -1 : 1;
      hearth::theme::global().cycle(direction);
      refreshSkin(ui);
      return;
    }
    if (inRect(x, y, kPresetPrev)) {
      cyclePreset(ui, -1);
      return;
    }
    if (inRect(x, y, kPresetNext)) {
      cyclePreset(ui, 1);
      return;
    }
    if (inRect(x, y, kPresetLcd)) {
      cyclePreset(ui, 1);
      return;
    }
    if (inRect(x, y, kChainPrev) || inRect(x, y, kChainNext)) {
      int routing = static_cast<int>(
          std::lround(ui->state.values[PORT_ROUTING]));
      routing += inRect(x, y, kChainNext) ? 1 : -1;
      routing = (routing + 6) % 6;
      setValue(ui->state, ui->host, PORT_ROUTING,
               static_cast<float>(routing));
      return;
    }
    // Chain chips toggle the module in that slot.
    if (y >= 318.0 && y <= 346.0) {
      static constexpr int kOrders[6][3] = {
          {0, 1, 2}, {0, 2, 1}, {1, 0, 2},
          {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
      const int routing = std::min(
          std::max(static_cast<int>(
                       std::lround(ui->state.values[PORT_ROUTING])),
                   0),
          5);
      const double chipX[] = {160.0, 490.0, 820.0};
      for (int slot = 0; slot < 3; ++slot) {
        if (x >= chipX[slot] && x <= chipX[slot] + 290.0) {
          const int module = kOrders[routing][slot];
          const uint32_t port = module == 0
                                    ? PORT_PULTEC_ON
                                    : (module == 1 ? PORT_SSL_ON
                                                   : PORT_TAME_ON);
          const float next = ui->state.values[port] >= 0.5f ? 0.0f : 1.0f;
          setValue(ui->state, ui->host, port, next);
          return;
        }
      }
    }
  }

  if (ev.button == Button4 || ev.button == Button5) {
    const int idx = hitTest(x, y);
    if (idx >= 0 && (kWidgets[idx].kind == Kind::Knob ||
                     kWidgets[idx].kind == Kind::Switch)) {
      const Widget& w = kWidgets[idx];
      const ParamInfo& p = infoFor(w.port);
      if (w.kind == Kind::Switch) {
        const int count = p.pointCount;
        int si = switchIndex(p, ui->state.values[w.port]);
        si += (ev.button == Button4) ? 1 : -1;
        si = std::min(std::max(si, 0), count - 1);
        setValue(ui->state, ui->host, w.port, p.points[si].value);
      } else {
        float n = normOf(p, ui->state.values[w.port]);
        n += (ev.button == Button4) ? 0.02f : -0.02f;
        setValue(ui->state, ui->host, w.port, valueOf(p, n));
      }
    }
    return;
  }
  if (ev.button != Button1) {
    return;
  }

  const int idx = hitTest(x, y);
  if (idx < 0) {
    return;
  }
  const Widget& w = kWidgets[idx];
  ui->state.active = idx;

  if (w.kind == Kind::Toggle) {
    const float v = ui->state.values[w.port] >= 0.5f ? 0.0f : 1.0f;
    setValue(ui->state, ui->host, w.port, v);
    ui->state.active = -1;
    return;
  }

  if (w.kind == Kind::Vu || w.kind == Kind::Meter) {
    ui->state.active = -1;
    return;
  }

  if (ui->state.lastClickWidget == idx &&
      ev.time - ui->state.lastClickTime < 400) {
    setValue(ui->state, ui->host, w.port, infoFor(w.port).def);
    ui->state.lastClickWidget = -1;
    ui->state.active = -1;
    return;
  }
  ui->state.lastClickWidget = idx;
  ui->state.lastClickTime = ev.time;

  if (w.kind == Kind::Switch) {
    const ParamInfo& p = infoFor(w.port);
    const int count = p.pointCount;
    if (count > 0) {
      const double start = kPi * 0.75;
      const double sweep = kPi * 1.5;
      const double angle = std::atan2(y - w.cy, x - w.cx);
      double rel = std::fmod(angle - start, kPi * 2.0);
      if (rel < 0.0) rel += kPi * 2.0;
      double t;
      if (rel <= sweep) {
        t = rel / sweep;
      } else {
        const double dEnd = rel - sweep;
        const double dStart = kPi * 2.0 - rel;
        t = (dStart < dEnd) ? 0.0 : 1.0;
      }
      int si = static_cast<int>(std::lround(t * (count - 1)));
      si = std::min(std::max(si, 0), count - 1);
      setValue(ui->state, ui->host, w.port, p.points[si].value);
    }
    return;
  }

  ui->state.dragY = y;
  ui->state.dragValue = ui->state.values[w.port];

  if (w.kind == Kind::Fader) {
    const float n = 1.0f - static_cast<float>((y - w.y) / w.h);
    setValue(ui->state, ui->host, w.port, valueOf(infoFor(w.port), n));
    ui->state.dragValue = ui->state.values[w.port];
  }
}

static void handleMotion(X11UI* ui, XMotionEvent& ev) {
  double x = 0.0;
  double y = 0.0;
  toLogical(ui, ev.x, ev.y, x, y);

  const bool skinHover = inRect(x, y, kSkinChip);
  if (skinHover != ui->state.skinHover) {
    ui->state.skinHover = skinHover;
    ui->state.dirty = true;
  }

  int presetHover = 0;
  if (inRect(x, y, kPresetPrev)) {
    presetHover = 1;
  } else if (inRect(x, y, kPresetNext)) {
    presetHover = 2;
  } else if (inRect(x, y, kPresetLcd)) {
    presetHover = 3;
  }
  if (skinHover) {
    presetHover = 0;
  }
  if (presetHover != ui->state.presetHover) {
    ui->state.presetHover = presetHover;
    ui->state.dirty = true;
  }

  const int hover = (presetHover || skinHover) ? -1 : hitTest(x, y);
  if (hover != ui->state.hover) {
    ui->state.hover = hover;
    ui->state.dirty = true;
  }
  if (ui->state.active < 0) {
    return;
  }
  const Widget& w = kWidgets[ui->state.active];
  if (w.kind == Kind::Fader) {
    const float n = 1.0f - static_cast<float>((y - w.y) / w.h);
    setValue(ui->state, ui->host, w.port, valueOf(infoFor(w.port), n));
    return;
  }
  if (w.kind == Kind::Switch) {
    const ParamInfo& p = infoFor(w.port);
    const int count = p.pointCount;
    if (count > 0) {
      const double start = kPi * 0.75;
      const double sweep = kPi * 1.5;
      const double angle = std::atan2(y - w.cy, x - w.cx);
      double rel = std::fmod(angle - start, kPi * 2.0);
      if (rel < 0.0) rel += kPi * 2.0;
      double t;
      if (rel <= sweep) {
        t = rel / sweep;
      } else {
        const double dEnd = rel - sweep;
        const double dStart = kPi * 2.0 - rel;
        t = (dStart < dEnd) ? 0.0 : 1.0;
      }
      int si = static_cast<int>(std::lround(t * (count - 1)));
      si = std::min(std::max(si, 0), count - 1);
      setValue(ui->state, ui->host, w.port, p.points[si].value);
    }
    return;
  }
  if (w.kind != Kind::Knob) {
    return;
  }

  const ParamInfo& p = infoFor(w.port);
  const double sensitivity = (ev.state & ShiftMask) ? 1200.0 : 180.0;
  const double dy = (ui->state.dragY - y) / sensitivity;
  const float startNorm = normOf(p, ui->state.dragValue);
  setValue(ui->state, ui->host, w.port, valueOf(p, startNorm + dy));
}

static void handleButtonRelease(X11UI* ui) {
  ui->state.active = -1;
}

static void processEvents(X11UI* ui) {
  while (XPending(ui->display)) {
    XEvent ev;
    XNextEvent(ui->display, &ev);
    switch (ev.type) {
      case Expose:
        if (ev.xexpose.count == 0) {
          ui->state.dirty = true;
        }
        break;
      case ConfigureNotify:
        if (ev.xconfigure.window == ui->window &&
            (ev.xconfigure.width != ui->windowW ||
             ev.xconfigure.height != ui->windowH)) {
          ui->windowW = std::max(ev.xconfigure.width, 160);
          ui->windowH = std::max(ev.xconfigure.height, 240);
          if (ui->windowSurface) {
            cairo_xlib_surface_set_size(ui->windowSurface, ui->windowW,
                                        ui->windowH);
          }
          updateScale(ui);
          ui->state.dirty = true;
        }
        break;
      case ButtonPress:
        handleButtonPress(ui, ev.xbutton);
        break;
      case MotionNotify:
        handleMotion(ui, ev.xmotion);
        break;
      case ButtonRelease:
        handleButtonRelease(ui);
        break;
      case LeaveNotify:
        if (ui->state.hover != -1 || ui->state.presetHover != 0 ||
            ui->state.skinHover) {
          ui->state.hover = -1;
          ui->state.presetHover = 0;
          ui->state.skinHover = false;
          ui->state.dirty = true;
        }
        break;
      default:
        break;
    }
  }
}

static int idle(LV2UI_Handle handle) {
  X11UI* ui = get(handle);
  if (!ui) {
    return 1;
  }
  processEvents(ui);

  if (++ui->configPollCounter >= 30) {
    ui->configPollCounter = 0;
    if (hearth::theme::global().reloadIfConfigChanged()) {
      refreshSkin(ui);
    }
  }

  if (ui->state.skinToast[0] != '\0' &&
      nowSeconds() >= ui->state.skinToastUntil) {
    ui->state.skinToast[0] = '\0';
    ui->state.dirty = true;
  }

  const float prevGr = ui->state.meterGrSmooth;
  const float prevOut = ui->state.vuSmooth;
  ui->state.meterGrSmooth +=
      0.28f * (ui->state.meterGr - ui->state.meterGrSmooth);
  // A slower fall for the VU movement.
  if (ui->state.meterOut > ui->state.vuSmooth) {
    ui->state.vuSmooth += 0.55f * (ui->state.meterOut - ui->state.vuSmooth);
  } else {
    ui->state.vuSmooth += 0.10f * (ui->state.meterOut - ui->state.vuSmooth);
  }
  if (std::fabs(prevGr - ui->state.meterGrSmooth) > 0.05f ||
      std::fabs(prevOut - ui->state.vuSmooth) > 0.05f) {
    ui->state.dirty = true;
  }

  // Smooth the scope telemetry so the spectrum and Tame trace breathe instead
  // of stepping on every host block.
  for (uint32_t i = 0; i < kTelemetryBins; ++i) {
    const float prevSpec = ui->state.spectrumSmooth[i];
    const float prevGain = ui->state.tameGainSmooth[i];
    ui->state.spectrumSmooth[i] +=
        0.38f * (ui->state.spectrum[i] - ui->state.spectrumSmooth[i]);
    ui->state.tameGainSmooth[i] +=
        0.38f * (ui->state.tameGain[i] - ui->state.tameGainSmooth[i]);
    if (std::fabs(prevSpec - ui->state.spectrumSmooth[i]) > 0.03f ||
        std::fabs(prevGain - ui->state.tameGainSmooth[i]) > 0.02f) {
      ui->state.dirty = true;
    }
  }

  if (!ui->resizeRequested && ui->hostResize && ui->hostResize->ui_resize) {
    ui->resizeRequested = true;
    ui->hostResize->ui_resize(ui->hostResize->handle, ui->windowW, ui->windowH);
  }

  if (ui->state.dirty) {
    blit(ui);
    ui->state.dirty = false;
  }
  return 0;
}

// ---------------------------------------------------------------------------
// LV2 UI entry points
// ---------------------------------------------------------------------------
static int ignoreXError(Display*, XErrorEvent*) { return 0; }

static bool isValidWindow(Display* display, Window window) {
  if (!window) {
    return false;
  }
  XErrorHandler old = XSetErrorHandler(ignoreXError);
  XWindowAttributes attributes;
  const Status status = XGetWindowAttributes(display, window, &attributes);
  XSync(display, False);
  XSetErrorHandler(old);
  return status != 0;
}

static float readScaleFactor(const LV2_Feature* const* features) {
  const LV2_URID_Map* map = nullptr;
  const LV2_Options_Option* options = nullptr;
  for (const LV2_Feature* const* f = features; f && *f; ++f) {
    if (!std::strcmp((*f)->URI, LV2_URID__map) && (*f)->data) {
      map = static_cast<const LV2_URID_Map*>(
          static_cast<const void*>((*f)->data));
    } else if (!std::strcmp((*f)->URI, LV2_OPTIONS__options) && (*f)->data) {
      options = static_cast<const LV2_Options_Option*>(
          static_cast<const void*>((*f)->data));
    }
  }
  if (!map || !options) {
    return 1.0f;
  }
  const LV2_URID key = map->map(map->handle, LV2_UI__scaleFactor);
  const LV2_URID tFloat = map->map(map->handle, LV2_ATOM__Float);
  const LV2_URID tDouble = map->map(map->handle, LV2_ATOM__Double);
  for (const LV2_Options_Option* o = options; o->key; ++o) {
    if (o->key != key || !o->value) {
      continue;
    }
    if (o->type == tFloat && o->size >= sizeof(float)) {
      return *static_cast<const float*>(o->value);
    }
    if (o->type == tDouble && o->size >= sizeof(double)) {
      return static_cast<float>(*static_cast<const double*>(o->value));
    }
  }
  return 1.0f;
}

static void chooseWindowSize(Display* display, float scaleFactor, int& width,
                             int& height) {
  const int screen = DefaultScreen(display);
  const double screenW = DisplayWidth(display, screen);
  const double screenH = DisplayHeight(display, screen);
  double sf = std::min(std::max(static_cast<double>(scaleFactor), 1.0), 2.5);
  double w = UI_W * sf;
  double h = UI_H * sf;
  const double maxW = screenW * 0.92;
  const double maxH = screenH * 0.86;
  const double fit = std::min(1.0, std::min(maxW / w, maxH / h));
  w *= fit;
  h *= fit;
  width = std::max(static_cast<int>(std::lround(w)), 320);
  height = std::max(static_cast<int>(std::lround(h)), 464);
}

static LV2UI_Handle uiInstantiate(const LV2UI_Descriptor*, const char*,
                                  const char* bundlePath,
                                  LV2UI_Write_Function write,
                                  LV2UI_Controller controller,
                                  LV2UI_Widget* widget,
                                  const LV2_Feature* const* features) {
  auto& skin = hearth::theme::global();
  skin.init(bundlePath ? bundlePath : "", kSkinPluginId);
  applyTheme(skin.theme());
  loadThemeAssets(bundlePath);
  loadLogoFont(bundlePath);
  X11UI* ui = new X11UI();
  initState(ui->state);
  ui->host.write = write;
  ui->host.controller = controller;

  const void* parentData = nullptr;
  for (const LV2_Feature* const* f = features; f && *f; ++f) {
    if (!std::strcmp((*f)->URI, LV2_UI__parent) && (*f)->data) {
      parentData = (*f)->data;
    } else if (!std::strcmp((*f)->URI, LV2_UI__resize) && (*f)->data) {
      ui->hostResize = static_cast<const LV2UI_Resize*>(
          static_cast<const void*>((*f)->data));
    }
  }

  ui->display = XOpenDisplay(nullptr);
  if (!ui->display) {
    debugLog("instantiate: XOpenDisplay failed");
    delete ui;
    return nullptr;
  }

  ui->scaleFactor = readScaleFactor(features);
  debugLog("instantiate: features parentData=%p scale=%f", parentData,
           static_cast<double>(ui->scaleFactor));

  Window parent = 0;
  if (parentData) {
    const uintptr_t raw = reinterpret_cast<uintptr_t>(parentData);
    if (raw != 0 && raw <= 0xffffffffu &&
        isValidWindow(ui->display, static_cast<Window>(raw))) {
      parent = static_cast<Window>(raw);
    } else {
      parent = *static_cast<const Window*>(parentData);
    }
  }

  if (!isValidWindow(ui->display, parent)) {
    // Non-embedded fallback: show a top-level window rather than failing.
    parent = DefaultRootWindow(ui->display);
    ui->topLevel = true;
    debugLog("instantiate: no valid parent, using top-level window");
  }

  XWindowAttributes parentAttributes;
  if (!XGetWindowAttributes(ui->display, parent, &parentAttributes)) {
    debugLog("instantiate: XGetWindowAttributes(parent) failed");
    XCloseDisplay(ui->display);
    delete ui;
    return nullptr;
  }
  ui->visual = parentAttributes.visual;
  ui->depth = parentAttributes.depth;

  int width = 0;
  int height = 0;
  chooseWindowSize(ui->display, ui->scaleFactor, width, height);
  ui->windowW = width;
  ui->windowH = height;
  updateScale(ui);

  XSetWindowAttributes attributes;
  std::memset(&attributes, 0, sizeof(attributes));
  attributes.background_pixel = 0x0b0d10;
  attributes.border_pixel = 0;
  attributes.event_mask = ExposureMask | StructureNotifyMask |
                          ButtonPressMask | ButtonReleaseMask |
                          PointerMotionMask | LeaveWindowMask;
  attributes.colormap = parentAttributes.colormap
                            ? parentAttributes.colormap
                            : DefaultColormap(ui->display,
                                              DefaultScreen(ui->display));
  ui->parent = parent;
  ui->window = XCreateWindow(
      ui->display, parent, 0, 0, width, height, 0, parentAttributes.depth,
      InputOutput, parentAttributes.visual,
      CWBackPixel | CWBorderPixel | CWEventMask | CWColormap, &attributes);
  debugLog("instantiate: parent=0x%lx window=0x%lx size=%dx%d depth=%d",
           static_cast<unsigned long>(parent),
           static_cast<unsigned long>(ui->window), width, height,
           parentAttributes.depth);
  if (!ui->window) {
    XCloseDisplay(ui->display);
    delete ui;
    return nullptr;
  }

  if (ui->topLevel) {
    #ifdef TROWEL_MASTER
    XStoreName(ui->display, ui->window, "Trowel Master");
#else
    XStoreName(ui->display, ui->window, "Trowel");
#endif
  }
  XMapRaised(ui->display, ui->window);
  XFlush(ui->display);

  ensureSurfaces(ui);
  blit(ui);
  *widget = reinterpret_cast<LV2UI_Widget>(static_cast<uintptr_t>(ui->window));

  if (ui->hostResize && ui->hostResize->ui_resize) {
    ui->resizeRequested = true;
    ui->hostResize->ui_resize(ui->hostResize->handle, width, height);
  }
  debugLog("instantiate: done");
  return ui;
}

static void uiCleanup(LV2UI_Handle handle) {
  X11UI* ui = get(handle);
  if (!ui) {
    return;
  }
  if (ui->imageCr) cairo_destroy(ui->imageCr);
  if (ui->imageSurface) cairo_surface_destroy(ui->imageSurface);
  if (ui->windowSurface) cairo_surface_destroy(ui->windowSurface);
  if (ui->display) {
    if (ui->window) XDestroyWindow(ui->display, ui->window);
    XCloseDisplay(ui->display);
  }
  delete ui;
}

static void uiPortEvent(LV2UI_Handle handle, uint32_t portIndex,
                        uint32_t bufferSize, uint32_t format,
                        const void* buffer) {
  X11UI* ui = get(handle);
  if (!ui || !buffer || bufferSize != sizeof(float) || format != 0) {
    return;
  }
  if (portIndex >= PORT_COUNT) {
    return;
  }
  const float value = *static_cast<const float*>(buffer);
  if (portIndex == PORT_METER_GR) {
    ui->state.meterGr = value;
  } else if (portIndex == PORT_METER_OUT) {
    ui->state.meterOut = value;
  } else if (portIndex == PORT_LATENCY) {
    ui->state.latency = value;
  } else if (portIndex >= PORT_SPECTRUM_0 &&
             portIndex < PORT_SPECTRUM_0 + kTelemetryBins) {
    ui->state.spectrum[portIndex - PORT_SPECTRUM_0] = value;
    ui->state.dirty = true;
  } else if (portIndex >= PORT_GAIN_0 &&
             portIndex < PORT_GAIN_0 + kTelemetryBins) {
    ui->state.tameGain[portIndex - PORT_GAIN_0] = value;
    ui->state.dirty = true;
  } else {
    if (ui->state.presetIndex >= 0 &&
        std::fabs(value - ui->state.values[portIndex]) > 1.0e-6f) {
      ui->state.presetDirty = true;
    }
    ui->state.values[portIndex] = value;
    ui->state.dirty = true;
  }
}

static int uiResize(LV2UI_Handle handle, int width, int height) {
  X11UI* ui = get(handle);
  if (!ui) {
    return 1;
  }
  width = std::max(width, 160);
  height = std::max(height, 240);
  ui->windowW = width;
  ui->windowH = height;
  updateScale(ui);
  if (ui->window) {
    XResizeWindow(ui->display, ui->window, width, height);
  }
  if (ui->windowSurface) {
    cairo_xlib_surface_set_size(ui->windowSurface, width, height);
  }
  debugLog("resize: %dx%d", width, height);
  ui->state.dirty = true;
  return 0;
}

static const LV2UI_Idle_Interface kIdleInterface = {idle};
static const LV2UI_Resize kResizeInterface = {nullptr, uiResize};

static const void* uiExtensionData(const char* uri) {
  if (!std::strcmp(uri, LV2_UI__idleInterface)) {
    return &kIdleInterface;
  }
  if (!std::strcmp(uri, LV2_UI__resize)) {
    return &kResizeInterface;
  }
  return nullptr;
}

} // namespace ui
} // namespace trowel

static const LV2UI_Descriptor kDescriptor = {
#ifdef TROWEL_MASTER
    "https://github.com/tylorismusic-commits/trowel-master#ui",
#else
    "https://github.com/tylorismusic-commits/trowel#ui",
#endif
    &trowel::ui::uiInstantiate,
    &trowel::ui::uiCleanup,
    &trowel::ui::uiPortEvent,
    &trowel::ui::uiExtensionData,
};

extern "C" LV2_SYMBOL_EXPORT const LV2UI_Descriptor* lv2ui_descriptor(uint32_t index) {
  return index == 0 ? &kDescriptor : nullptr;
}

// ---------------------------------------------------------------------------
// Standalone preview (build with -DTROWEL_UI_PREVIEW)
// ---------------------------------------------------------------------------
#ifdef TROWEL_UI_PREVIEW
int main(int argc, char** argv) {
  using namespace trowel;
  using namespace trowel::ui;
  auto& skin = hearth::theme::global();
  skin.init("assets/", kSkinPluginId);
  applyTheme(skin.theme());
  loadThemeAssets("assets/");
  loadLogoFont("assets/fonts/");
  State state;
  initState(state);
  // Show the Vocal preset in the documentation screenshot.
  state.values[PORT_PUL_LOW_FREQ] = 100.0f;
  state.values[PORT_PUL_LOW_BOOST] = 1.5f;
  state.values[PORT_PUL_HIGH_FREQ] = 12000.0f;
  state.values[PORT_PUL_HIGH_BOOST] = 2.0f;
  state.values[PORT_PUL_BANDWIDTH] = 6.0f;
  state.values[PORT_SSL_LF_FREQ] = 90.0f;
  state.values[PORT_SSL_LF_GAIN] = -1.0f;
  state.values[PORT_SSL_LMF_FREQ] = 300.0f;
  state.values[PORT_SSL_LMF_GAIN] = -2.0f;
  state.values[PORT_SSL_HMF_FREQ] = 3000.0f;
  state.values[PORT_SSL_HMF_GAIN] = 1.5f;
  state.values[PORT_SSL_HF_FREQ] = 10000.0f;
  state.values[PORT_SSL_HF_GAIN] = 1.0f;
  state.values[PORT_SSL_HPF] = 80.0f;
  state.values[PORT_TAME_AMOUNT] = 35.0f;
  state.values[PORT_TAME_FOCUS] = 60.0f;
  state.values[PORT_TAME_SELECTIVITY] = 55.0f;
  state.values[PORT_TAME_SENSITIVITY] = 55.0f;
  state.presetIndex = 0;
  state.presetDirty = false;
  state.meterGrSmooth = 2.4f;
  state.vuSmooth = -9.0f;
#ifdef TROWEL_MASTER
  state.latency = 4095.0f;
#else
  state.latency = 0.0f;
#endif
  for (int i = 0; i < static_cast<int>(kTelemetryBins); ++i) {
    const float t = i / static_cast<float>(kTelemetryBins - 1);
    state.spectrumSmooth[i] =
        -18.0f - 30.0f * t + 7.0f * std::sin(t * 11.0f) +
        4.0f * std::sin(t * 27.0f);
    state.tameGainSmooth[i] =
        -4.5f * std::exp(-0.5f * std::pow((t - 0.68f) / 0.07f, 2.0f));
  }

  cairo_surface_t* surface =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, UI_W, UI_H);
  cairo_t* cr = cairo_create(surface);
  render(state, cr);
  cairo_destroy(cr);
  const char* path = (argc > 1) ? argv[1] : "trowel-ui.png";
  cairo_surface_write_to_png(surface, path);
  cairo_surface_destroy(surface);
  std::printf("wrote %s\n", path);
  return 0;
}
#endif
