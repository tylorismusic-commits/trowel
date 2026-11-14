// Trowel — CLAP wrapper
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This is a thin CLAP host around the same DSP and the same X11 UI as the LV2
// plugin.  It exists so hosts that support CLAP gain-reduction metering (REAPER,
// for example) can display the compressor's gain reduction on their track
// meters, while Ardour uses the LV2 build's inline display.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <mutex>
#include <new>
#include <string>
#include <strings.h>
#include <thread>

#include <clap/all.h>
#include <lv2/ui/ui.h>

#include "param_info.hpp"
#include "params.hpp"
#include "tame_dynamic.hpp"
#include "tame_spectral.hpp"
#include "trowel_strip.hpp"

namespace trowel {
namespace clapwrap {

#ifdef TROWEL_MASTER
using TameEngine = TameSpectral;
constexpr bool kMastering = true;
static constexpr const char* kPluginId = "com.tylorismusic.trowel-master";
static constexpr const char* kPluginName = "Trowel Master";
static constexpr const char* kUiUri =
    "https://github.com/tylorismusic-commits/trowel-master#ui";
static constexpr const char* kVersion = "0.1.0";
static constexpr const char* kDescription =
    "Mastering EQ suite: Pultec-style colour, 4000E console EQ and a "
    "4096-point spectral resonance suppressor.";
#else
using TameEngine = TameDynamic;
constexpr bool kMastering = false;
static constexpr const char* kPluginId = "com.tylorismusic.trowel";
static constexpr const char* kPluginName = "Trowel";
static constexpr const char* kUiUri =
    "https://github.com/tylorismusic-commits/trowel#ui";
static constexpr const char* kVersion = "0.1.0";
static constexpr const char* kDescription =
    "Zero-latency EQ suite: Pultec-style colour, 4000E console EQ and an "
    "adaptive resonance suppressor.";
#endif

using Strip = TrowelStrip<TameEngine, kMastering>;

static constexpr uint32_t kUiWidth = 1240;
static constexpr uint32_t kUiHeight = 920;

static std::string gBundlePath;

static const ParamInfo& infoFor(uint32_t port) {
  for (int i = 0; i < kParamInfoCount; ++i) {
    if (kParams[i].index == port) {
      return kParams[i];
    }
  }
  static ParamInfo fallback;
  return fallback;
}

static const char* groupName(int group) {
  switch (group) {
    case GROUP_PULTEC: return "Colour";
    case GROUP_SSL: return "Console";
    case GROUP_TAME: return "Tame";
    case GROUP_GLOBAL: return "Global";
    case GROUP_MASTER: return "Master";
    default: return "";
  }
}

static float load(const std::atomic<float>* v, uint32_t port) {
  return v[port].load(std::memory_order_relaxed);
}

static void readParams(const std::atomic<float>* v, Params& p) {
  p.inputGainDb = load(v, PORT_INPUT_GAIN);
  p.outputGainDb = load(v, PORT_OUTPUT_GAIN);

  p.pultec.lowFreq = load(v, PORT_PUL_LOW_FREQ);
  p.pultec.lowBoostDb = load(v, PORT_PUL_LOW_BOOST);
  p.pultec.lowCutDb = load(v, PORT_PUL_LOW_CUT);
  p.pultec.highFreq = load(v, PORT_PUL_HIGH_FREQ);
  p.pultec.highBoostDb = load(v, PORT_PUL_HIGH_BOOST);
  p.pultec.bandwidth = load(v, PORT_PUL_BANDWIDTH);
  p.pultec.highCutFreq = load(v, PORT_PUL_HIGH_CUT_FREQ);
  p.pultec.highCutDb = load(v, PORT_PUL_HIGH_CUT);
  p.pultec.iron = load(v, PORT_PUL_IRON) > 0.5f;

  p.ssl.lfFreq = load(v, PORT_SSL_LF_FREQ);
  p.ssl.lfGainDb = load(v, PORT_SSL_LF_GAIN);
  p.ssl.lfBell = load(v, PORT_SSL_LF_BELL) > 0.5f;
  p.ssl.lmfFreq = load(v, PORT_SSL_LMF_FREQ);
  p.ssl.lmfGainDb = load(v, PORT_SSL_LMF_GAIN);
  p.ssl.hmfFreq = load(v, PORT_SSL_HMF_FREQ);
  p.ssl.hmfGainDb = load(v, PORT_SSL_HMF_GAIN);
  p.ssl.hfFreq = load(v, PORT_SSL_HF_FREQ);
  p.ssl.hfGainDb = load(v, PORT_SSL_HF_GAIN);
  p.ssl.hfBell = load(v, PORT_SSL_HF_BELL) > 0.5f;
  p.ssl.hpfFreq = load(v, PORT_SSL_HPF);
  p.ssl.lpfFreq = load(v, PORT_SSL_LPF);

  p.tame.amount = load(v, PORT_TAME_AMOUNT) / 100.0f;
  p.tame.focus = load(v, PORT_TAME_FOCUS) / 100.0f;
  p.tame.selectivity = load(v, PORT_TAME_SELECTIVITY) / 100.0f;
  p.tame.sensitivity = load(v, PORT_TAME_SENSITIVITY) / 100.0f;
  p.tame.attackMs = load(v, PORT_TAME_ATTACK);
  p.tame.releaseMs = load(v, PORT_TAME_RELEASE);
  p.tame.mix = load(v, PORT_TAME_MIX) / 100.0f;
  p.tame.listen = load(v, PORT_TAME_LISTEN) > 0.5f;

  p.pultecOn = load(v, PORT_PULTEC_ON) > 0.5f;
  p.sslOn = load(v, PORT_SSL_ON) > 0.5f;
  p.tameOn = load(v, PORT_TAME_ON) > 0.5f;
  p.enabled = load(v, PORT_ENABLE) > 0.5f;
  p.routing = static_cast<int>(std::lround(load(v, PORT_ROUTING)));

  if constexpr (kMastering) {
    p.msMode = load(v, PORT_STEREO_MODE) > 0.5f;
    p.autoGain = load(v, PORT_AUTO_GAIN) > 0.5f;
    p.delta = load(v, PORT_DELTA) > 0.5f;
  }
}

static int nearestPoint(const ParamInfo& p, float value) {
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

static void formatValue(uint32_t port, float value, char* out,
                        uint32_t capacity) {
  const ParamInfo& p = infoFor(port);
  if (port == PORT_SSL_LF_BELL || port == PORT_SSL_HF_BELL) {
    std::snprintf(out, capacity, "%s", value >= 0.5f ? "Bell" : "Shelf");
    return;
  }
  if (p.toggle) {
    std::snprintf(out, capacity, "%s", value >= 0.5f ? "On" : "Off");
    return;
  }
  if (p.enumeration) {
    if (p.points && p.pointCount > 0) {
      std::snprintf(out, capacity, "%s",
                    p.points[nearestPoint(p, value)].label);
      return;
    }
    std::snprintf(out, capacity, "%s", value >= 0.5f ? "1" : "0");
    return;
  }
  if (p.points && p.pointCount > 0) {
    std::snprintf(out, capacity, "%s", p.points[nearestPoint(p, value)].label);
    return;
  }
  switch (p.unit) {
    case Unit::Db:
      std::snprintf(out, capacity, "%+.1f dB", value);
      break;
    case Unit::Hz:
      if (value >= 1000.0f) {
        std::snprintf(out, capacity, "%.2f kHz", value / 1000.0f);
      } else {
        std::snprintf(out, capacity, "%.0f Hz", value);
      }
      break;
    case Unit::Ms:
      if (value >= 100.0f) {
        std::snprintf(out, capacity, "%.0f ms", value);
      } else {
        std::snprintf(out, capacity, "%.1f ms", value);
      }
      break;
    case Unit::Percent:
      std::snprintf(out, capacity, "%.0f%%", value);
      break;
    case Unit::Unitless:
    default:
      if (port == PORT_PUL_BANDWIDTH) {
        std::snprintf(out, capacity, "%.1f", value);
      } else {
        std::snprintf(out, capacity, "%.1f", value);
      }
      break;
  }
}

static clap_param_info_flags paramFlags(const ParamInfo& p) {
  uint32_t flags = CLAP_PARAM_IS_AUTOMATABLE;
  if (p.toggle) {
    flags |= CLAP_PARAM_IS_STEPPED;
  }
  if (p.enumeration) {
    flags |= CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;
  }
  return flags;
}

struct ClapPlugin {
  clap_plugin_t plugin{};
  const clap_host_t* host = nullptr;
  const clap_host_params_t* hostParams = nullptr;

  Strip strip;
  Params params;
  std::atomic<float> values[PORT_COUNT];
  std::atomic<bool> uiDirty[PORT_COUNT];
  std::atomic<bool> pendingFlag[PORT_COUNT];
  std::atomic<float> pendingValue[PORT_COUNT];
  std::atomic<float> grDb{0.0f};
  std::atomic<float> meterGr{0.0f};
  std::atomic<float> meterOut{-60.0f};
  std::atomic<float> telemetry[kTelemetryBins * 2];
  std::atomic<bool> meterDirty{false};

  double sampleRate = 48000.0;
  bool active = false;

  void* uiLib = nullptr;
  const LV2UI_Descriptor* uiDesc = nullptr;
  LV2UI_Handle ui = nullptr;
  const LV2UI_Idle_Interface* uiIdle = nullptr;
  const LV2UI_Resize* uiResize = nullptr;
  std::thread uiThread;
  std::atomic<bool> uiRunning{false};
  std::mutex uiMutex;
  uint32_t uiWidth = kUiWidth;
  uint32_t uiHeight = kUiHeight;
  bool floating = false;

  explicit ClapPlugin(const clap_host_t* h) : host(h) {
    for (uint32_t i = 0; i < PORT_COUNT; ++i) {
      values[i].store(0.0f);
      uiDirty[i].store(false);
      pendingFlag[i].store(false);
      pendingValue[i].store(0.0f);
    }
    for (int i = 0; i < kParamInfoCount; ++i) {
      values[kParams[i].index].store(kParams[i].def);
    }
    for (auto& t : telemetry) {
      t.store(0.0f);
    }

    plugin.desc = &descriptor();
    plugin.plugin_data = this;
    plugin.init = &init;
    plugin.destroy = &destroy;
    plugin.activate = &activate;
    plugin.deactivate = &deactivate;
    plugin.start_processing = &startProcessing;
    plugin.stop_processing = &stopProcessing;
    plugin.reset = &reset;
    plugin.process = &process;
    plugin.get_extension = &getExtension;
    plugin.on_main_thread = &onMainThread;
  }

  ~ClapPlugin() { destroyUi(); }

  static ClapPlugin* self(const clap_plugin_t* p) {
    return static_cast<ClapPlugin*>(p->plugin_data);
  }

  static const clap_plugin_descriptor_t& descriptor() {
    static const char* features[] = {
        CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
        CLAP_PLUGIN_FEATURE_EQUALIZER,
        nullptr};
    static const clap_plugin_descriptor_t desc = {
        CLAP_VERSION,
        kPluginId,
        kPluginName,
        "Tylor",
        "https://github.com/tylorismusic-commits/trowel",
        "",
        "https://github.com/tylorismusic-commits/trowel/issues",
        kVersion,
        kDescription,
        features};
    return desc;
  }

  static bool CLAP_ABI init(const clap_plugin_t* p) {
    ClapPlugin* s = self(p);
    if (!s || !s->host) {
      return false;
    }
    s->hostParams = static_cast<const clap_host_params_t*>(
        s->host->get_extension(s->host, CLAP_EXT_PARAMS));
    readParams(s->values, s->params);
    s->strip.setTargets(s->params);
    return true;
  }

  static void CLAP_ABI destroy(const clap_plugin_t* p) {
    delete self(p);
  }

  static bool CLAP_ABI activate(const clap_plugin_t* p, double sampleRate,
                                uint32_t, uint32_t) {
    ClapPlugin* s = self(p);
    if (!s) {
      return false;
    }
    s->sampleRate = sampleRate;
    s->strip.prepare(static_cast<float>(sampleRate));
    readParams(s->values, s->params);
    s->strip.setTargets(s->params);
    s->strip.reset();
    s->grDb.store(0.0f);
    s->meterGr.store(0.0f);
    s->meterOut.store(-60.0f);
    s->meterDirty.store(true);
    s->active = true;
    return true;
  }

  static void CLAP_ABI deactivate(const clap_plugin_t* p) {
    ClapPlugin* s = self(p);
    if (s) {
      s->strip.reset();
      s->grDb.store(0.0f);
      s->meterGr.store(0.0f);
      s->meterOut.store(-60.0f);
      s->meterDirty.store(true);
      s->active = false;
    }
  }

  static bool CLAP_ABI startProcessing(const clap_plugin_t*) { return true; }
  static void CLAP_ABI stopProcessing(const clap_plugin_t*) {}

  static void CLAP_ABI reset(const clap_plugin_t* p) {
    ClapPlugin* s = self(p);
    if (s) {
      s->strip.reset();
      s->grDb.store(0.0f);
      s->meterGr.store(0.0f);
      s->meterOut.store(-60.0f);
      s->meterDirty.store(true);
    }
  }

  static void handleInputEvents(ClapPlugin* s,
                                const clap_input_events_t* events) {
    if (!s || !events) {
      return;
    }
    const uint32_t count = events->size(events);
    for (uint32_t i = 0; i < count; ++i) {
      const clap_event_header_t* header = events->get(events, i);
      if (!header || header->type != CLAP_EVENT_PARAM_VALUE) {
        continue;
      }
      const auto* value =
          reinterpret_cast<const clap_event_param_value_t*>(header);
      if (value->param_id >= PORT_COUNT) {
        continue;
      }
      s->values[value->param_id].store(static_cast<float>(value->value));
      s->pendingValue[value->param_id].store(static_cast<float>(value->value));
      s->pendingFlag[value->param_id].store(true);
    }
  }

  static void emitParamValue(const clap_output_events_t* events,
                             uint32_t id, float value) {
    if (!events) {
      return;
    }
    clap_event_param_value_t event{};
    event.header.size = sizeof(event);
    event.header.time = 0;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_PARAM_VALUE;
    event.header.flags = 0;
    event.param_id = id;
    event.cookie = nullptr;
    event.note_id = -1;
    event.port_index = -1;
    event.channel = -1;
    event.key = -1;
    event.value = value;
    events->try_push(events, &event.header);
  }

  static void emitUiChanges(ClapPlugin* s,
                            const clap_output_events_t* events) {
    if (!s) {
      return;
    }
    for (uint32_t i = 0; i < PORT_COUNT; ++i) {
      if (s->uiDirty[i].exchange(false)) {
        emitParamValue(events, i, s->values[i].load());
      }
    }
  }

  static clap_process_status CLAP_ABI process(const clap_plugin_t* p,
                                              const clap_process_t* process) {
    ClapPlugin* s = self(p);
    if (!s || !process || !s->active) {
      return CLAP_PROCESS_CONTINUE;
    }

    handleInputEvents(s, process->in_events);
    emitUiChanges(s, process->out_events);

    readParams(s->values, s->params);
    s->strip.setTargets(s->params);

    const uint32_t frames = process->frames_count;
    if (frames == 0) {
      return CLAP_PROCESS_CONTINUE;
    }

    if (process->audio_inputs_count < 1 ||
        process->audio_outputs_count < 1) {
      return CLAP_PROCESS_CONTINUE;
    }

    const clap_audio_buffer_t& in = process->audio_inputs[0];
    clap_audio_buffer_t& out = process->audio_outputs[0];
    if (in.channel_count < 2 || out.channel_count < 2 || !in.data32 ||
        !out.data32) {
      return CLAP_PROCESS_ERROR;
    }

    const float* inL = in.data32[0];
    const float* inR = in.data32[1];
    float* outL = out.data32[0];
    float* outR = out.data32[1];

    float gr = 0.0f;
    float outDb = -60.0f;
    s->strip.process(inL, inR, outL, outR, frames, gr, outDb);
    s->grDb.store(gr);
    s->meterGr.store(gr);
    s->meterOut.store(outDb);

    float spec[kTelemetryBins] = {};
    float gains[kTelemetryBins] = {};
    s->strip.fillTelemetry(spec, gains);
    for (uint32_t i = 0; i < kTelemetryBins; ++i) {
      s->telemetry[i].store(spec[i]);
      s->telemetry[kTelemetryBins + i].store(gains[i]);
    }
    s->meterDirty.store(true);
    return CLAP_PROCESS_CONTINUE;
  }

  static const void* CLAP_ABI getExtension(const clap_plugin_t* p,
                                           const char* id) {
    ClapPlugin* s = self(p);
    if (!s || !id) {
      return nullptr;
    }
    if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &audioPorts();
    if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &paramsExt();
    if (!std::strcmp(id, CLAP_EXT_LATENCY)) return &latencyExt();
    if (!std::strcmp(id, CLAP_EXT_STATE)) return &stateExt();
    if (!std::strcmp(id, CLAP_EXT_GUI)) return &guiExt();
    if (!std::strcmp(id, CLAP_EXT_GAIN_ADJUSTMENT_METERING)) {
      return &gainExt();
    }
    return nullptr;
  }

  static void CLAP_ABI onMainThread(const clap_plugin_t*) {}

  // -------------------------------------------------------------------------
  // Audio ports
  // -------------------------------------------------------------------------
  static uint32_t CLAP_ABI audioPortCount(const clap_plugin_t*, bool) {
    return 1;
  }
  static bool CLAP_ABI audioPortInfo(const clap_plugin_t*, uint32_t index,
                                     bool isInput,
                                     clap_audio_port_info_t* info) {
    if (index != 0 || !info) {
      return false;
    }
    std::memset(info, 0, sizeof(*info));
    info->id = 0;
    std::snprintf(info->name, sizeof(info->name), "%s",
                  isInput ? "Stereo In" : "Stereo Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
  }
  static const clap_plugin_audio_ports_t& audioPorts() {
    static const clap_plugin_audio_ports_t ext = {&audioPortCount,
                                                   &audioPortInfo};
    return ext;
  }

  // -------------------------------------------------------------------------
  // Parameters
  // -------------------------------------------------------------------------
  static uint32_t CLAP_ABI paramCount(const clap_plugin_t*) {
    return static_cast<uint32_t>(kParamInfoCount);
  }

  static bool CLAP_ABI paramInfo(const clap_plugin_t*, uint32_t index,
                                 clap_param_info_t* info) {
    if (!info || index >= static_cast<uint32_t>(kParamInfoCount)) {
      return false;
    }
    const ParamInfo& p = kParams[index];
    std::memset(info, 0, sizeof(*info));
    info->id = p.index;
    info->flags = paramFlags(p);
    std::snprintf(info->name, sizeof(info->name), "%s", p.name);
    std::snprintf(info->module, sizeof(info->module), "%s",
                  groupName(p.group));
    info->min_value = p.min;
    info->max_value = p.max;
    info->default_value = p.def;
    return true;
  }

  static bool CLAP_ABI paramGetValue(const clap_plugin_t* p, clap_id id,
                                     double* outValue) {
    ClapPlugin* s = self(p);
    if (!s || !outValue || id >= PORT_COUNT) {
      return false;
    }
    *outValue = s->values[id].load();
    return true;
  }

  static bool CLAP_ABI paramValueToText(const clap_plugin_t*, clap_id id,
                                        double value, char* out,
                                        uint32_t capacity) {
    if (!out || capacity == 0 || id >= PORT_COUNT) {
      return false;
    }
    formatValue(id, static_cast<float>(value), out, capacity);
    return true;
  }

  static bool CLAP_ABI paramTextToValue(const clap_plugin_t*, clap_id id,
                                        const char* text, double* outValue) {
    if (!text || !outValue || id >= PORT_COUNT) {
      return false;
    }
    const ParamInfo& p = infoFor(id);
    if (p.toggle) {
      if (!strcasecmp(text, "on") || !strcmp(text, "1")) {
        *outValue = 1.0;
        return true;
      }
      if (!strcasecmp(text, "off") || !strcmp(text, "0")) {
        *outValue = 0.0;
        return true;
      }
    }
    if (p.points && p.pointCount > 0) {
      for (int i = 0; i < p.pointCount; ++i) {
        if (!strcasecmp(text, p.points[i].label)) {
          *outValue = p.points[i].value;
          return true;
        }
      }
    }
    char* end = nullptr;
    const double parsed = std::strtod(text, &end);
    if (end == text || !std::isfinite(parsed)) {
      return false;
    }
    *outValue = std::min(std::max(parsed, static_cast<double>(p.min)),
                         static_cast<double>(p.max));
    return true;
  }

  static void CLAP_ABI paramFlush(const clap_plugin_t* p,
                                  const clap_input_events_t* in,
                                  const clap_output_events_t* out) {
    ClapPlugin* s = self(p);
    if (!s) {
      return;
    }
    handleInputEvents(s, in);
    emitUiChanges(s, out);
  }

  static const clap_plugin_params_t& paramsExt() {
    static const clap_plugin_params_t ext = {
        &paramCount, &paramInfo, &paramGetValue, &paramValueToText,
        &paramTextToValue, &paramFlush};
    return ext;
  }

  // -------------------------------------------------------------------------
  // Latency
  // -------------------------------------------------------------------------
  static uint32_t CLAP_ABI latencyGet(const clap_plugin_t*) {
    return Strip::kLatency;
  }
  static const clap_plugin_latency_t& latencyExt() {
    static const clap_plugin_latency_t ext = {&latencyGet};
    return ext;
  }

  // -------------------------------------------------------------------------
  // Gain reduction metering
  // -------------------------------------------------------------------------
  static double CLAP_ABI gainGet(const clap_plugin_t* p) {
    ClapPlugin* s = self(p);
    const double value = s ? static_cast<double>(s->grDb.load()) : 0.0;
    return value;
  }
  static const clap_plugin_gain_adjustment_metering_t& gainExt() {
    static const clap_plugin_gain_adjustment_metering_t ext = {&gainGet};
    return ext;
  }

  // -------------------------------------------------------------------------
  // State
  // -------------------------------------------------------------------------
  static bool CLAP_ABI stateSave(const clap_plugin_t* p,
                                 const clap_ostream_t* stream) {
    ClapPlugin* s = self(p);
    if (!s || !stream) {
      return false;
    }
    std::string text = "TROWEL1\n";
    for (int i = 0; i < kParamInfoCount; ++i) {
      const uint32_t id = kParams[i].index;
      char line[64];
      std::snprintf(line, sizeof(line), "%u %.9g\n", id,
                    static_cast<double>(s->values[id].load()));
      text += line;
    }
    uint64_t written = 0;
    while (written < text.size()) {
      const int64_t n =
          stream->write(stream, text.data() + written, text.size() - written);
      if (n <= 0) {
        return false;
      }
      written += static_cast<uint64_t>(n);
    }
    return true;
  }

  static bool CLAP_ABI stateLoad(const clap_plugin_t* p,
                                 const clap_istream_t* stream) {
    ClapPlugin* s = self(p);
    if (!s || !stream) {
      return false;
    }
    char buffer[4096];
    uint64_t total = 0;
    while (total < sizeof(buffer) - 1) {
      const int64_t n =
          stream->read(stream, buffer + total, sizeof(buffer) - 1 - total);
      if (n < 0) {
        return false;
      }
      if (n == 0) {
        break;
      }
      total += static_cast<uint64_t>(n);
    }
    if (total == 0) {
      return false;
    }
    buffer[total] = '\0';
    const char* cursor = std::strstr(buffer, "TROWEL1\n");
    if (!cursor) {
      return false;
    }
    cursor += 8;
    while (*cursor) {
      char* end = nullptr;
      const unsigned long id = std::strtoul(cursor, &end, 10);
      if (end == cursor || id >= PORT_COUNT) {
        break;
      }
      cursor = end;
      const double value = std::strtod(cursor, &end);
      if (end == cursor) {
        break;
      }
      s->values[id].store(static_cast<float>(value));
      s->pendingValue[id].store(static_cast<float>(value));
      s->pendingFlag[id].store(true);
      cursor = end;
      while (*cursor == '\n' || *cursor == '\r' || *cursor == ' ') {
        ++cursor;
      }
    }
    readParams(s->values, s->params);
    s->strip.setTargets(s->params);
    return true;
  }

  static const clap_plugin_state_t& stateExt() {
    static const clap_plugin_state_t ext = {&stateSave, &stateLoad};
    return ext;
  }

  // -------------------------------------------------------------------------
  // GUI — embeds the existing LV2 X11 UI module.
  // -------------------------------------------------------------------------
  static void uiWrite(LV2UI_Controller controller, uint32_t port, uint32_t size,
                      uint32_t, const void* buffer) {
    ClapPlugin* s = static_cast<ClapPlugin*>(controller);
    if (!s || !buffer || size != sizeof(float) || port >= PORT_COUNT) {
      return;
    }
    s->values[port].store(*static_cast<const float*>(buffer));
    s->uiDirty[port].store(true);
    if (s->host) {
      s->host->request_process(s->host);
    }
  }

  void uiThreadLoop() {
    while (uiRunning.load()) {
      {
        std::lock_guard<std::mutex> lock(uiMutex);
        if (ui) {
          for (uint32_t i = 0; i < PORT_COUNT; ++i) {
            if (pendingFlag[i].exchange(false)) {
              const float value = pendingValue[i].load();
              uiDesc->port_event(ui, i, sizeof(float), 0, &value);
            }
          }
          if (meterDirty.exchange(false)) {
            float gr = meterGr.load();
            float out = meterOut.load();
            float latency = static_cast<float>(Strip::kLatency);
            uiDesc->port_event(ui, PORT_METER_GR, sizeof(float), 0, &gr);
            uiDesc->port_event(ui, PORT_METER_OUT, sizeof(float), 0, &out);
            uiDesc->port_event(ui, PORT_LATENCY, sizeof(float), 0, &latency);
            for (uint32_t i = 0; i < kTelemetryBins; ++i) {
              float spec = telemetry[i].load();
              float gain = telemetry[kTelemetryBins + i].load();
              uiDesc->port_event(ui, PORT_SPECTRUM_0 + i, sizeof(float), 0,
                                 &spec);
              uiDesc->port_event(ui, PORT_GAIN_0 + i, sizeof(float), 0,
                                 &gain);
            }
          }
          if (uiIdle) {
            uiIdle->idle(ui);
          }
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
  }

  void stopUiThread() {
    if (uiThread.joinable()) {
      uiRunning.store(false);
      uiThread.join();
    }
  }

  bool createUi(uintptr_t parent) {
    std::lock_guard<std::mutex> lock(uiMutex);
    if (ui) {
      return true;
    }
    if (!uiLib) {
      std::string dir = gBundlePath;
      if (!dir.empty() && dir.back() != '/') {
        dir += '/';
      }
      const std::string path =
          dir + (kMastering ? "trowelMasterUI.so" : "trowelUI.so");
      uiLib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
      if (!uiLib) {
        std::fprintf(stderr, "Trowel CLAP: could not load %s: %s\n",
                     path.c_str(), dlerror());
        return false;
      }
      auto descriptorFn = reinterpret_cast<const LV2UI_Descriptor* (*)(
          uint32_t)>(dlsym(uiLib, "lv2ui_descriptor"));
      if (!descriptorFn) {
        dlclose(uiLib);
        uiLib = nullptr;
        return false;
      }
      uiDesc = descriptorFn(0);
      if (!uiDesc) {
        dlclose(uiLib);
        uiLib = nullptr;
        return false;
      }
    }

    uintptr_t parentWindow = parent;
    LV2_Feature parentFeature{LV2_UI__parent, &parentWindow};
    const LV2_Feature* features[] = {&parentFeature, nullptr};
    LV2UI_Widget widget = nullptr;
    std::string dir = gBundlePath;
    if (!dir.empty() && dir.back() != '/') {
      dir += '/';
    }
    ui = uiDesc->instantiate(uiDesc, kUiUri, dir.c_str(), &uiWrite, this,
                             &widget, features);
    if (!ui) {
      return false;
    }
    uiIdle = static_cast<const LV2UI_Idle_Interface*>(
        uiDesc->extension_data(LV2_UI__idleInterface));
    uiResize = static_cast<const LV2UI_Resize*>(
        uiDesc->extension_data(LV2_UI__resize));
    if (uiResize && uiResize->ui_resize) {
      uiResize->ui_resize(ui, static_cast<int>(uiWidth),
                          static_cast<int>(uiHeight));
    }
    return true;
  }

  void destroyUi() {
    stopUiThread();
    std::lock_guard<std::mutex> lock(uiMutex);
    if (ui) {
      uiDesc->cleanup(ui);
      ui = nullptr;
    }
    uiIdle = nullptr;
    uiResize = nullptr;
    uiDesc = nullptr;
    if (uiLib) {
      dlclose(uiLib);
      uiLib = nullptr;
    }
  }

  void startUiThread() {
    if (uiThread.joinable()) {
      return;
    }
    uiRunning.store(true);
    uiThread = std::thread(&ClapPlugin::uiThreadLoop, this);
  }

  static bool CLAP_ABI guiIsApiSupported(const clap_plugin_t*, const char* api,
                                         bool) {
    return api && !std::strcmp(api, CLAP_WINDOW_API_X11);
  }

  static bool CLAP_ABI guiGetPreferredApi(const clap_plugin_t*, const char** api,
                                          bool* isFloating) {
    if (api) *api = CLAP_WINDOW_API_X11;
    if (isFloating) *isFloating = false;
    return true;
  }

  static bool CLAP_ABI guiCreate(const clap_plugin_t* p, const char*, bool floating) {
    ClapPlugin* s = self(p);
    if (!s) {
      return false;
    }
    s->floating = floating;
    if (!floating) {
      return true;
    }
    return s->createUi(0);
  }

  static void CLAP_ABI guiDestroy(const clap_plugin_t* p) {
    ClapPlugin* s = self(p);
    if (s) {
      s->destroyUi();
    }
  }

  static bool CLAP_ABI guiSetScale(const clap_plugin_t*, double) {
    return false;
  }

  static bool CLAP_ABI guiGetSize(const clap_plugin_t* p, uint32_t* width,
                                  uint32_t* height) {
    ClapPlugin* s = self(p);
    if (!s || !width || !height) {
      return false;
    }
    *width = s->uiWidth;
    *height = s->uiHeight;
    return true;
  }

  static bool CLAP_ABI guiCanResize(const clap_plugin_t*) { return true; }

  static bool CLAP_ABI guiGetResizeHints(const clap_plugin_t*,
                                         clap_gui_resize_hints_t* hints) {
    if (!hints) {
      return false;
    }
    hints->can_resize_horizontally = true;
    hints->can_resize_vertically = true;
    hints->preserve_aspect_ratio = false;
    hints->aspect_ratio_width = kUiWidth;
    hints->aspect_ratio_height = kUiHeight;
    return true;
  }

  static bool CLAP_ABI guiAdjustSize(const clap_plugin_t*, uint32_t* width,
                                     uint32_t* height) {
    if (!width || !height) {
      return false;
    }
    *width = std::max(*width, 320u);
    *height = std::max(*height, 464u);
    return true;
  }

  static bool CLAP_ABI guiSetSize(const clap_plugin_t* p, uint32_t width,
                                  uint32_t height) {
    ClapPlugin* s = self(p);
    if (!s || width == 0 || height == 0) {
      return false;
    }
    s->uiWidth = width;
    s->uiHeight = height;
    std::lock_guard<std::mutex> lock(s->uiMutex);
    if (s->ui && s->uiResize && s->uiResize->ui_resize) {
      s->uiResize->ui_resize(s->ui, static_cast<int>(width),
                             static_cast<int>(height));
    }
    return true;
  }

  static bool CLAP_ABI guiSetParent(const clap_plugin_t* p,
                                    const clap_window_t* window) {
    ClapPlugin* s = self(p);
    if (!s || !window || !window->api ||
        std::strcmp(window->api, CLAP_WINDOW_API_X11)) {
      return false;
    }
    return s->createUi(static_cast<uintptr_t>(window->x11));
  }

  static bool CLAP_ABI guiSetTransient(const clap_plugin_t*, const clap_window_t*) {
    return true;
  }

  static void CLAP_ABI guiSuggestTitle(const clap_plugin_t*, const char*) {}

  static bool CLAP_ABI guiShow(const clap_plugin_t* p) {
    ClapPlugin* s = self(p);
    if (!s) {
      return false;
    }
    if (!s->ui && !s->createUi(0)) {
      return false;
    }
    s->startUiThread();
    return true;
  }

  static bool CLAP_ABI guiHide(const clap_plugin_t* p) {
    ClapPlugin* s = self(p);
    if (!s) {
      return false;
    }
    s->stopUiThread();
    return true;
  }

  static const clap_plugin_gui_t& guiExt() {
    static const clap_plugin_gui_t ext = {
        &guiIsApiSupported, &guiGetPreferredApi, &guiCreate, &guiDestroy,
        &guiSetScale, &guiGetSize, &guiCanResize, &guiGetResizeHints,
        &guiAdjustSize, &guiSetSize, &guiSetParent, &guiSetTransient,
        &guiSuggestTitle, &guiShow, &guiHide};
    return ext;
  }
};

// ---------------------------------------------------------------------------
// Factory and entry point
// ---------------------------------------------------------------------------
static uint32_t CLAP_ABI factoryPluginCount(const clap_plugin_factory_t*) {
  return 1;
}

static const clap_plugin_descriptor_t* CLAP_ABI factoryPluginDescriptor(
    const clap_plugin_factory_t*, uint32_t index) {
  return index == 0 ? &ClapPlugin::descriptor() : nullptr;
}

static const clap_plugin_t* CLAP_ABI factoryCreatePlugin(
    const clap_plugin_factory_t*, const clap_host_t* host,
    const char* pluginId) {
  if (!host || !pluginId || std::strcmp(pluginId, kPluginId)) {
    return nullptr;
  }
  ClapPlugin* instance = new (std::nothrow) ClapPlugin(host);
  return instance ? &instance->plugin : nullptr;
}

static const clap_plugin_factory_t kFactory = {
    &factoryPluginCount, &factoryPluginDescriptor, &factoryCreatePlugin};

static bool CLAP_ABI entryInit(const char* pluginPath) {
  if (!pluginPath) {
    return false;
  }
  // On Linux the CLAP is a flat Trowel.clap shared object.  Its UI module and
  // image/font assets live in Trowel.resources next to it.
  std::string path(pluginPath);
  const std::string suffix = ".clap";
  if (path.size() >= suffix.size() &&
      path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0) {
    path = path.substr(0, path.size() - suffix.size()) + ".resources";
  } else {
    const size_t slash = path.find_last_of('/');
    if (slash != std::string::npos) {
      path = path.substr(0, slash);
    }
  }
  gBundlePath = path;
  return true;
}

static void CLAP_ABI entryDeinit() { gBundlePath.clear(); }

static const void* CLAP_ABI entryGetFactory(const char* factoryId) {
  if (factoryId && !std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID)) {
    return &kFactory;
  }
  return nullptr;
}

} // namespace clapwrap
} // namespace trowel

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION,
    &trowel::clapwrap::entryInit,
    &trowel::clapwrap::entryDeinit,
    &trowel::clapwrap::entryGetFactory};
