// Trowel — param_info.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace trowel {

enum class Unit {
  Unitless = 0,
  Db = 1,
  Hz = 2,
  Ms = 3,
  Percent = 4,
};

// Port layout.  The common prefix and every meter/telemetry index are
// identical in both variants; Trowel Master appends three controls at the end.
enum PortIndex : uint32_t {
  PORT_AUDIO_IN_L = 0,
  PORT_AUDIO_IN_R = 1,
  PORT_AUDIO_OUT_L = 2,
  PORT_AUDIO_OUT_R = 3,

  // Pultec / Colour
  PORT_PULTEC_ON = 4,
  PORT_PUL_LOW_FREQ = 5,
  PORT_PUL_LOW_BOOST = 6,
  PORT_PUL_LOW_CUT = 7,
  PORT_PUL_HIGH_FREQ = 8,
  PORT_PUL_HIGH_BOOST = 9,
  PORT_PUL_BANDWIDTH = 10,
  PORT_PUL_HIGH_CUT_FREQ = 11,
  PORT_PUL_HIGH_CUT = 12,
  PORT_PUL_IRON = 13,

  // SSL / Console
  PORT_SSL_ON = 14,
  PORT_SSL_LF_FREQ = 15,
  PORT_SSL_LF_GAIN = 16,
  PORT_SSL_LF_BELL = 17,
  PORT_SSL_LMF_FREQ = 18,
  PORT_SSL_LMF_GAIN = 19,
  PORT_SSL_HMF_FREQ = 20,
  PORT_SSL_HMF_GAIN = 21,
  PORT_SSL_HF_FREQ = 22,
  PORT_SSL_HF_GAIN = 23,
  PORT_SSL_HF_BELL = 24,
  PORT_SSL_HPF = 25,
  PORT_SSL_LPF = 26,

  // Tame
  PORT_TAME_ON = 27,
  PORT_TAME_AMOUNT = 28,
  PORT_TAME_FOCUS = 29,
  PORT_TAME_SELECTIVITY = 30,
  PORT_TAME_SENSITIVITY = 31,
  PORT_TAME_ATTACK = 32,
  PORT_TAME_RELEASE = 33,
  PORT_TAME_MIX = 34,
  PORT_TAME_LISTEN = 35,

  // Global
  PORT_INPUT_GAIN = 36,
  PORT_OUTPUT_GAIN = 37,
  PORT_ROUTING = 38,
  PORT_ENABLE = 39,

  // Meters and latency
  PORT_METER_GR = 40,
  PORT_METER_OUT = 41,
  PORT_LATENCY = 42,

  // Telemetry: 32 log-spaced spectrum bins, then 32 Tame gain bins.
  PORT_SPECTRUM_0 = 43,
  PORT_GAIN_0 = 75,

  // Trowel Master only.
  PORT_STEREO_MODE = 107,
  PORT_AUTO_GAIN = 108,
  PORT_DELTA = 109,

  PORT_COUNT = 110,
};

inline constexpr uint32_t kTelemetryBins = 32;
inline constexpr uint32_t kSpectrumPort = PORT_SPECTRUM_0;
inline constexpr uint32_t kGainPort = PORT_GAIN_0;

enum ParamGroup : uint32_t {
  GROUP_PULTEC = 0,
  GROUP_SSL = 1,
  GROUP_TAME = 2,
  GROUP_GLOBAL = 3,
  GROUP_MASTER = 4,
};

struct ScalePoint {
  float value = 0.0f;
  const char* label = "";
};

struct ParamInfo {
  uint32_t index = 0;
  const char* symbol = "";
  const char* name = "";
  float min = 0.0f;
  float max = 1.0f;
  float def = 0.0f;
  Unit unit = Unit::Unitless;
  bool logarithmic = false;
  bool toggle = false;
  bool enumeration = false;
  int group = GROUP_PULTEC;
  const ScalePoint* points = nullptr;
  int pointCount = 0;
};

inline constexpr ScalePoint kScalePulLow[] = {
    {20.0f, "20"}, {30.0f, "30"}, {60.0f, "60"}, {100.0f, "100"}};

inline constexpr ScalePoint kScalePulHigh[] = {
    {3000.0f, "3k"},   {4000.0f, "4k"},   {5000.0f, "5k"},
    {8000.0f, "8k"},   {10000.0f, "10k"}, {12000.0f, "12k"},
    {16000.0f, "16k"}};

inline constexpr ScalePoint kScalePulHighCut[] = {
    {5000.0f, "5k"}, {10000.0f, "10k"}, {20000.0f, "20k"}};

inline constexpr ScalePoint kScaleSslLf[] = {
    {30.0f, "30"},  {40.0f, "40"},  {60.0f, "60"},  {75.0f, "75"},
    {90.0f, "90"},  {110.0f, "110"}, {130.0f, "130"}, {150.0f, "150"}};

inline constexpr ScalePoint kScaleSslLmf[] = {
    {100.0f, "100"}, {150.0f, "150"}, {200.0f, "200"}, {300.0f, "300"},
    {400.0f, "400"}, {600.0f, "600"}, {800.0f, "800"}, {1000.0f, "1k"},
    {1500.0f, "1.5k"}, {2000.0f, "2k"}};

inline constexpr ScalePoint kScaleSslHmf[] = {
    {1500.0f, "1.5k"}, {2000.0f, "2k"}, {3000.0f, "3k"},
    {4000.0f, "4k"},   {5000.0f, "5k"}, {6000.0f, "6k"},
    {8000.0f, "8k"},   {10000.0f, "10k"}};

inline constexpr ScalePoint kScaleSslHf[] = {
    {3000.0f, "3k"},   {4000.0f, "4k"},   {5000.0f, "5k"},
    {6000.0f, "6k"},   {8000.0f, "8k"},   {10000.0f, "10k"},
    {12000.0f, "12k"}, {16000.0f, "16k"}};

inline constexpr ScalePoint kScaleSslHpf[] = {
    {0.0f, "OFF"}, {30.0f, "30"}, {50.0f, "50"},
    {80.0f, "80"}, {120.0f, "120"}, {160.0f, "160"}};

inline constexpr ScalePoint kScaleSslLpf[] = {
    {0.0f, "OFF"}, {8000.0f, "8k"}, {10000.0f, "10k"},
    {12000.0f, "12k"}, {16000.0f, "16k"}, {20000.0f, "20k"}};

inline constexpr ScalePoint kScaleRouting[] = {
    {0.0f, "COLOUR > CONSOLE > TAME"},
    {1.0f, "COLOUR > TAME > CONSOLE"},
    {2.0f, "CONSOLE > COLOUR > TAME"},
    {3.0f, "CONSOLE > TAME > COLOUR"},
    {4.0f, "TAME > COLOUR > CONSOLE"},
    {5.0f, "TAME > CONSOLE > COLOUR"}};

inline constexpr ScalePoint kScaleStereoSsl[] = {
    {0.0f, "LINKED"}, {1.0f, "MID/SIDE"}};

inline constexpr ParamInfo kParams[] = {
    // Pultec / Colour
    {4, "pultec_on", "Colour", 0.0f, 1.0f, 1.0f, Unit::Unitless, false, true,
     false, GROUP_PULTEC, nullptr, 0},
    {5, "pul_low_freq", "Low Freq", 20.0f, 100.0f, 60.0f, Unit::Hz, false,
     false, true, GROUP_PULTEC, kScalePulLow, 4},
    {6, "pul_low_boost", "Low Boost", 0.0f, 13.5f, 0.0f, Unit::Db, false, false,
     false, GROUP_PULTEC, nullptr, 0},
    {7, "pul_low_cut", "Low Cut", -17.5f, 0.0f, 0.0f, Unit::Db, false, false,
     false, GROUP_PULTEC, nullptr, 0},
    {8, "pul_high_freq", "High Freq", 3000.0f, 16000.0f, 10000.0f, Unit::Hz,
     false, false, true, GROUP_PULTEC, kScalePulHigh, 7},
    {9, "pul_high_boost", "High Boost", 0.0f, 18.0f, 0.0f, Unit::Db, false,
     false, false, GROUP_PULTEC, nullptr, 0},
    {10, "pul_bandwidth", "Bandwidth", 0.0f, 10.0f, 5.0f, Unit::Unitless,
     false, false, false, GROUP_PULTEC, nullptr, 0},
    {11, "pul_high_cut_freq", "High Cut Freq", 5000.0f, 20000.0f, 10000.0f,
     Unit::Hz, false, false, true, GROUP_PULTEC, kScalePulHighCut, 3},
    {12, "pul_high_cut", "High Cut", -16.0f, 0.0f, 0.0f, Unit::Db, false, false,
     false, GROUP_PULTEC, nullptr, 0},
    {13, "pul_iron", "Iron", 0.0f, 1.0f, 0.0f, Unit::Unitless, false, true,
     false, GROUP_PULTEC, nullptr, 0},

    // SSL / Console
    {14, "ssl_on", "Console", 0.0f, 1.0f, 1.0f, Unit::Unitless, false, true,
     false, GROUP_SSL, nullptr, 0},
    {15, "ssl_lf_freq", "LF Freq", 30.0f, 150.0f, 60.0f, Unit::Hz, false, false,
     true, GROUP_SSL, kScaleSslLf, 8},
    {16, "ssl_lf_gain", "LF Gain", -15.0f, 15.0f, 0.0f, Unit::Db, false, false,
     false, GROUP_SSL, nullptr, 0},
    {17, "ssl_lf_bell", "LF Shape", 0.0f, 1.0f, 0.0f, Unit::Unitless, false,
     true, false, GROUP_SSL, nullptr, 0},
    {18, "ssl_lmf_freq", "LMF Freq", 100.0f, 2000.0f, 300.0f, Unit::Hz, false,
     false, true, GROUP_SSL, kScaleSslLmf, 10},
    {19, "ssl_lmf_gain", "LMF Gain", -15.0f, 15.0f, 0.0f, Unit::Db, false,
     false, false, GROUP_SSL, nullptr, 0},
    {20, "ssl_hmf_freq", "HMF Freq", 1500.0f, 10000.0f, 3000.0f, Unit::Hz,
     false, false, true, GROUP_SSL, kScaleSslHmf, 8},
    {21, "ssl_hmf_gain", "HMF Gain", -15.0f, 15.0f, 0.0f, Unit::Db, false,
     false, false, GROUP_SSL, nullptr, 0},
    {22, "ssl_hf_freq", "HF Freq", 3000.0f, 16000.0f, 10000.0f, Unit::Hz,
     false, false, true, GROUP_SSL, kScaleSslHf, 8},
    {23, "ssl_hf_gain", "HF Gain", -15.0f, 15.0f, 0.0f, Unit::Db, false, false,
     false, GROUP_SSL, nullptr, 0},
    {24, "ssl_hf_bell", "HF Shape", 0.0f, 1.0f, 0.0f, Unit::Unitless, false,
     true, false, GROUP_SSL, nullptr, 0},
    {25, "ssl_hpf", "HPF", 0.0f, 160.0f, 0.0f, Unit::Hz, false, false, true,
     GROUP_SSL, kScaleSslHpf, 6},
    {26, "ssl_lpf", "LPF", 0.0f, 20000.0f, 0.0f, Unit::Hz, false, false, true,
     GROUP_SSL, kScaleSslLpf, 6},

    // Tame
    {27, "tame_on", "Tame", 0.0f, 1.0f, 1.0f, Unit::Unitless, false, true, false,
     GROUP_TAME, nullptr, 0},
    {28, "tame_amount", "Amount", 0.0f, 100.0f, 0.0f, Unit::Percent, false,
     false, false, GROUP_TAME, nullptr, 0},
    {29, "tame_focus", "Focus", 0.0f, 100.0f, 50.0f, Unit::Percent, false,
     false, false, GROUP_TAME, nullptr, 0},
    {30, "tame_selectivity", "Selectivity", 0.0f, 100.0f, 50.0f, Unit::Percent,
     false, false, false, GROUP_TAME, nullptr, 0},
    {31, "tame_sensitivity", "Sensitivity", 0.0f, 100.0f, 50.0f, Unit::Percent,
     false, false, false, GROUP_TAME, nullptr, 0},
    {32, "tame_attack", "Attack", 1.0f, 100.0f, 10.0f, Unit::Ms, true, false,
     false, GROUP_TAME, nullptr, 0},
    {33, "tame_release", "Release", 50.0f, 2000.0f, 250.0f, Unit::Ms, true,
     false, false, GROUP_TAME, nullptr, 0},
    {34, "tame_mix", "Mix", 0.0f, 100.0f, 100.0f, Unit::Percent, false, false,
     false, GROUP_TAME, nullptr, 0},
    {35, "tame_listen", "Listen", 0.0f, 1.0f, 0.0f, Unit::Unitless, false, true,
     false, GROUP_TAME, nullptr, 0},

    // Global
    {36, "input_gain", "Input", -24.0f, 24.0f, 0.0f, Unit::Db, false, false,
     false, GROUP_GLOBAL, nullptr, 0},
    {37, "output_gain", "Output", -24.0f, 24.0f, 0.0f, Unit::Db, false, false,
     false, GROUP_GLOBAL, nullptr, 0},
    {38, "routing", "Routing", 0.0f, 5.0f, 0.0f, Unit::Unitless, false, false,
     true, GROUP_GLOBAL, kScaleRouting, 6},
    {39, "enable", "Enabled", 0.0f, 1.0f, 1.0f, Unit::Unitless, false, true,
     false, GROUP_GLOBAL, nullptr, 0},

#ifdef TROWEL_MASTER
    {PORT_STEREO_MODE, "stereo_mode", "Stereo", 0.0f, 1.0f, 0.0f,
     Unit::Unitless, false, false, true, GROUP_TAME, kScaleStereoSsl, 2},
    {PORT_AUTO_GAIN, "auto_gain", "Auto Gain", 0.0f, 1.0f, 0.0f, Unit::Unitless,
     false, true, false, GROUP_MASTER, nullptr, 0},
    {PORT_DELTA, "delta", "Delta", 0.0f, 1.0f, 0.0f, Unit::Unitless, false, true,
     false, GROUP_MASTER, nullptr, 0},
#endif
};

inline constexpr int kParamInfoCount =
    static_cast<int>(sizeof(kParams) / sizeof(kParams[0]));

} // namespace trowel
