// Trowel — LV2 wrapper
// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

#include <lv2/core/lv2.h>

#include "inline_display.hpp"
#include "param_info.hpp"
#include "params.hpp"
#include "tame_dynamic.hpp"
#include "tame_spectral.hpp"
#include "trowel_strip.hpp"

namespace trowel {

#ifdef TROWEL_MASTER
using TameEngine = TameSpectral;
constexpr bool kMastering = true;
constexpr const char* kPluginURI =
    "https://github.com/tylorismusic-commits/trowel-master";
constexpr const char* kPluginName = "Trowel Master";
#else
using TameEngine = TameDynamic;
constexpr bool kMastering = false;
constexpr const char* kPluginURI =
    "https://github.com/tylorismusic-commits/trowel";
constexpr const char* kPluginName = "Trowel";
#endif

using Strip = TrowelStrip<TameEngine, kMastering>;

class TrowelInstance {
public:
  float* ports[PORT_COUNT] = {};
  float sampleRate = 48000.0f;
  Strip strip;
  Params params;
  InlineDisplay display;
  const InlineDisplayQueueDraw* queueDraw = nullptr;
  uint32_t drawCounter = 0;
  float spectrum[kTelemetryBins] = {};
  float gains[kTelemetryBins] = {};
};

static const ParamInfo& infoFor(uint32_t index) noexcept {
  for (int i = 0; i < kParamInfoCount; ++i) {
    if (kParams[i].index == index) {
      return kParams[i];
    }
  }
  static ParamInfo fallback;
  fallback.index = index;
  return fallback;
}

static float ctrlValue(float* const* ports, uint32_t index,
                       float fallback) noexcept {
  const ParamInfo& info = infoFor(index);
  float v = fallback;
  if (index < PORT_COUNT && ports[index]) {
    v = ports[index][0];
  }
  if (!std::isfinite(v)) {
    v = fallback;
  }
  const float lo = std::min(info.min, info.max);
  const float hi = std::max(info.min, info.max);
  if (hi > lo) {
    v = std::min(std::max(v, lo), hi);
  }
  return v;
}

static void readParams(TrowelInstance* self) noexcept {
  self->params.inputGainDb = ctrlValue(self->ports, PORT_INPUT_GAIN, 0.0f);
  self->params.outputGainDb = ctrlValue(self->ports, PORT_OUTPUT_GAIN, 0.0f);

  self->params.pultec.lowFreq =
      ctrlValue(self->ports, PORT_PUL_LOW_FREQ, 60.0f);
  self->params.pultec.lowBoostDb =
      ctrlValue(self->ports, PORT_PUL_LOW_BOOST, 0.0f);
  self->params.pultec.lowCutDb =
      ctrlValue(self->ports, PORT_PUL_LOW_CUT, 0.0f);
  self->params.pultec.highFreq =
      ctrlValue(self->ports, PORT_PUL_HIGH_FREQ, 10000.0f);
  self->params.pultec.highBoostDb =
      ctrlValue(self->ports, PORT_PUL_HIGH_BOOST, 0.0f);
  self->params.pultec.bandwidth =
      ctrlValue(self->ports, PORT_PUL_BANDWIDTH, 5.0f);
  self->params.pultec.highCutFreq =
      ctrlValue(self->ports, PORT_PUL_HIGH_CUT_FREQ, 10000.0f);
  self->params.pultec.highCutDb =
      ctrlValue(self->ports, PORT_PUL_HIGH_CUT, 0.0f);
  self->params.pultec.iron =
      ctrlValue(self->ports, PORT_PUL_IRON, 0.0f) > 0.5f;

  self->params.ssl.lfFreq = ctrlValue(self->ports, PORT_SSL_LF_FREQ, 60.0f);
  self->params.ssl.lfGainDb = ctrlValue(self->ports, PORT_SSL_LF_GAIN, 0.0f);
  self->params.ssl.lfBell = ctrlValue(self->ports, PORT_SSL_LF_BELL, 0.0f) > 0.5f;
  self->params.ssl.lmfFreq = ctrlValue(self->ports, PORT_SSL_LMF_FREQ, 300.0f);
  self->params.ssl.lmfGainDb = ctrlValue(self->ports, PORT_SSL_LMF_GAIN, 0.0f);
  self->params.ssl.hmfFreq = ctrlValue(self->ports, PORT_SSL_HMF_FREQ, 3000.0f);
  self->params.ssl.hmfGainDb = ctrlValue(self->ports, PORT_SSL_HMF_GAIN, 0.0f);
  self->params.ssl.hfFreq = ctrlValue(self->ports, PORT_SSL_HF_FREQ, 10000.0f);
  self->params.ssl.hfGainDb = ctrlValue(self->ports, PORT_SSL_HF_GAIN, 0.0f);
  self->params.ssl.hfBell = ctrlValue(self->ports, PORT_SSL_HF_BELL, 0.0f) > 0.5f;
  self->params.ssl.hpfFreq = ctrlValue(self->ports, PORT_SSL_HPF, 0.0f);
  self->params.ssl.lpfFreq = ctrlValue(self->ports, PORT_SSL_LPF, 0.0f);

  self->params.tame.amount =
      ctrlValue(self->ports, PORT_TAME_AMOUNT, 0.0f) / 100.0f;
  self->params.tame.focus =
      ctrlValue(self->ports, PORT_TAME_FOCUS, 50.0f) / 100.0f;
  self->params.tame.selectivity =
      ctrlValue(self->ports, PORT_TAME_SELECTIVITY, 50.0f) / 100.0f;
  self->params.tame.sensitivity =
      ctrlValue(self->ports, PORT_TAME_SENSITIVITY, 50.0f) / 100.0f;
  self->params.tame.attackMs =
      ctrlValue(self->ports, PORT_TAME_ATTACK, 10.0f);
  self->params.tame.releaseMs =
      ctrlValue(self->ports, PORT_TAME_RELEASE, 250.0f);
  self->params.tame.mix = ctrlValue(self->ports, PORT_TAME_MIX, 100.0f) / 100.0f;
  self->params.tame.listen =
      ctrlValue(self->ports, PORT_TAME_LISTEN, 0.0f) > 0.5f;

  self->params.pultecOn =
      ctrlValue(self->ports, PORT_PULTEC_ON, 1.0f) > 0.5f;
  self->params.sslOn = ctrlValue(self->ports, PORT_SSL_ON, 1.0f) > 0.5f;
  self->params.tameOn = ctrlValue(self->ports, PORT_TAME_ON, 1.0f) > 0.5f;
  self->params.enabled = ctrlValue(self->ports, PORT_ENABLE, 1.0f) > 0.5f;
  self->params.routing =
      static_cast<int>(std::lround(ctrlValue(self->ports, PORT_ROUTING, 0.0f)));

  if constexpr (kMastering) {
    self->params.msMode =
        ctrlValue(self->ports, PORT_STEREO_MODE, 0.0f) > 0.5f;
    self->params.autoGain =
        ctrlValue(self->ports, PORT_AUTO_GAIN, 0.0f) > 0.5f;
    self->params.delta = ctrlValue(self->ports, PORT_DELTA, 0.0f) > 0.5f;
  } else {
    self->params.msMode = false;
    self->params.autoGain = false;
    self->params.delta = false;
  }
}

static void publishTelemetry(TrowelInstance* self) noexcept {
  self->strip.fillTelemetry(self->spectrum, self->gains);
  for (uint32_t i = 0; i < kTelemetryBins; ++i) {
    float* spec = self->ports[PORT_SPECTRUM_0 + i];
    if (spec) {
      *spec = self->spectrum[i];
    }
    float* gain = self->ports[PORT_GAIN_0 + i];
    if (gain) {
      *gain = self->gains[i];
    }
  }
  float* latency = self->ports[PORT_LATENCY];
  if (latency) {
    *latency = static_cast<float>(Strip::kLatency);
  }
}

} // namespace trowel

using namespace trowel;

extern "C" LV2_Handle lv2_instantiate(const LV2_Descriptor* descriptor,
                                      double rate, const char* bundle_path,
                                      const LV2_Feature* const* features) {
  (void)descriptor;
  (void)bundle_path;

  trowel::TrowelInstance* self = new (std::nothrow) trowel::TrowelInstance();
  if (!self) {
    return nullptr;
  }
  for (const LV2_Feature* const* f = features; f && *f; ++f) {
    if (!std::strcmp((*f)->URI, trowel::kInlineDisplayQueueDrawURI) &&
        (*f)->data) {
      self->queueDraw = static_cast<const trowel::InlineDisplayQueueDraw*>(
          static_cast<const void*>((*f)->data));
    }
  }
  self->sampleRate = std::max(8000.0f, static_cast<float>(rate));
  self->strip.prepare(self->sampleRate);
  trowel::readParams(self);
  self->strip.setTargets(self->params);
  self->strip.reset();
  self->display.setGainReductionDb(0.0f);
  return self;
}

extern "C" void lv2_connect_port(LV2_Handle handle, uint32_t port,
                                 void* data_location) {
  trowel::TrowelInstance* self =
      static_cast<trowel::TrowelInstance*>(handle);
  if (!self) {
    return;
  }
  if (port < trowel::PORT_COUNT) {
    self->ports[port] = static_cast<float*>(data_location);
  }
}

extern "C" void lv2_activate(LV2_Handle handle) {
  trowel::TrowelInstance* self =
      static_cast<trowel::TrowelInstance*>(handle);
  if (!self) {
    return;
  }
  trowel::readParams(self);
  self->strip.setTargets(self->params);
  self->strip.reset();
  self->display.setGainReductionDb(0.0f);
}

extern "C" void lv2_run(LV2_Handle handle, uint32_t sampleCount) {
  trowel::TrowelInstance* self =
      static_cast<trowel::TrowelInstance*>(handle);
  if (!self) {
    return;
  }

  const bool audioValid = self->ports[PORT_AUDIO_IN_L] &&
                          self->ports[PORT_AUDIO_IN_R] &&
                          self->ports[PORT_AUDIO_OUT_L] &&
                          self->ports[PORT_AUDIO_OUT_R];

  float grDb = 0.0f;
  float outDb = -60.0f;
  if (audioValid) {
    trowel::readParams(self);
    self->strip.setTargets(self->params);
    self->strip.process(self->ports[PORT_AUDIO_IN_L],
                        self->ports[PORT_AUDIO_IN_R],
                        self->ports[PORT_AUDIO_OUT_L],
                        self->ports[PORT_AUDIO_OUT_R], sampleCount, grDb, outDb);
  }

  float* meterGr = self->ports[PORT_METER_GR];
  float* meterOut = self->ports[PORT_METER_OUT];
  if (meterGr) {
    *meterGr = grDb;
  }
  if (meterOut) {
    *meterOut = outDb;
  }

  trowel::publishTelemetry(self);

  self->display.setGainReductionDb(std::max(0.0f, -grDb));
  if (self->queueDraw && self->queueDraw->queue_draw &&
      ((self->drawCounter++ & 31u) == 0u)) {
    self->queueDraw->queue_draw(self->queueDraw->handle);
  }
}

extern "C" void lv2_deactivate(LV2_Handle handle) {
  trowel::TrowelInstance* self =
      static_cast<trowel::TrowelInstance*>(handle);
  if (!self) {
    return;
  }
  self->strip.reset();
  self->display.setGainReductionDb(0.0f);
}

extern "C" void lv2_cleanup(LV2_Handle handle) {
  delete static_cast<trowel::TrowelInstance*>(handle);
}

static trowel::InlineDisplaySurface* renderInlineDisplay(void* instance,
                                                        uint32_t width,
                                                        uint32_t height) {
  trowel::TrowelInstance* self =
      static_cast<trowel::TrowelInstance*>(instance);
  return self ? self->display.render(width, height) : nullptr;
}

static const trowel::InlineDisplayInterface trowelDisplayInterface = {
    &renderInlineDisplay};

extern "C" const void* lv2_extension_data(const char* uri) {
  if (!std::strcmp(uri, trowel::kInlineDisplayInterfaceURI)) {
    return &trowelDisplayInterface;
  }
  return nullptr;
}

static const LV2_Descriptor trowel_descriptor = {
    trowel::kPluginURI,
    &lv2_instantiate,
    &lv2_connect_port,
    &lv2_activate,
    &lv2_run,
    &lv2_deactivate,
    &lv2_cleanup,
    &lv2_extension_data,
};

extern "C" LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(
    uint32_t index) {
  if (index == 0) {
    return &trowel_descriptor;
  }
  return nullptr;
}
