// Trowel — trowel_strip.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "params.hpp"
#include "pultec.hpp"
#include "saturator.hpp"
#include "smoother.hpp"
#include "ssl_eq.hpp"
#include "util.hpp"

namespace trowel {

// The shared strip.  Tame is a template parameter so the zero-latency and
// spectral builds share every other line of DSP.  `Mastering` enables the
// Master-only auto-gain, delta monitor and M/S control.
template <class Tame, bool Mastering>
class TrowelStrip {
public:
  static constexpr uint32_t kLatency = Tame::kLatency;
  static constexpr uint32_t kMaxBlock = 8192;
  static constexpr uint32_t kTelemetryBins = 32;

  void prepare(float sampleRate) noexcept {
    sampleRate_ = sampleRate;
    pultec_.prepare(sampleRate);
    ssl_.prepare(sampleRate);
    tame_.prepare(sampleRate);

    inputGain_.setTime(20.0f, sampleRate);
    outputGain_.setTime(20.0f, sampleRate);
    pultecAmt_.setTime(10.0f, sampleRate);
    sslAmt_.setTime(10.0f, sampleRate);
    tameAmt_.setTime(10.0f, sampleRate);
    masterAmt_.setTime(10.0f, sampleRate);
    deltaAmt_.setTime(10.0f, sampleRate);
    autoGainCoef_ = std::exp(-1.0f / (0.50f * sampleRate));
    rmsCoef_ = std::exp(-1.0f / (1.00f * sampleRate));
    routingFadeStep_ = 1.0f / std::max(1.0f, 0.020f * sampleRate);

    if constexpr (kLatency > 0) {
      tameDryL_.prepare(kLatency);
      tameDryR_.prepare(kLatency);
      globalDryL_.prepare(kLatency);
      globalDryR_.prepare(kLatency);
    }

    workL_.assign(kMaxBlock, 0.0f);
    workR_.assign(kMaxBlock, 0.0f);
    tameInL_.assign(kMaxBlock, 0.0f);
    tameInR_.assign(kMaxBlock, 0.0f);
    dryL_.assign(kMaxBlock, 0.0f);
    dryR_.assign(kMaxBlock, 0.0f);
    bypassL_.assign(kMaxBlock, 0.0f);
    bypassR_.assign(kMaxBlock, 0.0f);

    prepared_ = true;
    setTargets(Params{});
    reset();
  }

  void setTargets(const Params& p) noexcept {
    params_ = p;
    if (!prepared_) {
      return;
    }
    pultec_.setTargets(p.pultec);
    ssl_.setTargets(p.ssl);

    TameParams tp = p.tame;
    tp.msMode = p.msMode;
    tame_.setTargets(tp);

    inputGain_.setTarget(dbToGain(p.inputGainDb));
    outputGain_.setTarget(dbToGain(p.outputGainDb));
    pultecAmt_.setTarget(p.pultecOn ? 1.0f : 0.0f);
    sslAmt_.setTarget(p.sslOn ? 1.0f : 0.0f);
    tameAmt_.setTarget(p.tameOn ? 1.0f : 0.0f);
    masterAmt_.setTarget(p.enabled ? 1.0f : 0.0f);
    deltaAmt_.setTarget(p.delta ? 1.0f : 0.0f);

    if (p.routing != pendingRouting_) {
      pendingRouting_ = p.routing;
      routingFadeTarget_ = 0.0f;
    }
  }

  void reset() noexcept {
    if (!prepared_) {
      return;
    }
    pultec_.reset();
    ssl_.reset();
    tame_.reset();
    inputGain_.snap();
    outputGain_.snap();
    pultecAmt_.snap();
    sslAmt_.snap();
    tameAmt_.snap();
    masterAmt_.snap();
    deltaAmt_.snap();
    if constexpr (kLatency > 0) {
      tameDryL_.reset();
      tameDryR_.reset();
      globalDryL_.reset();
      globalDryR_.reset();
    }
    for (auto& c : clip_) {
      c.reset();
    }
    activeRouting_ = pendingRouting_;
    routingFade_ = 1.0f;
    routingFadeTarget_ = 1.0f;
    dryRms_ = 0.0f;
    wetRms_ = 0.0f;
    autoGainDb_ = 0.0f;
    std::fill(spectrum_.begin(), spectrum_.end(), -100.0f);
    std::fill(gain_.begin(), gain_.end(), 0.0f);
  }

  void process(const float* inLeft, const float* inRight, float* outLeft,
               float* outRight, uint32_t frames, float& grDb,
               float& outDb) noexcept {
    float gr = 0.0f;
    float peak = 0.0f;
    uint32_t offset = 0;
    while (offset < frames) {
      const uint32_t n = std::min(frames - offset, kMaxBlock);
      processChunk(inLeft + offset, inRight + offset, outLeft + offset,
                   outRight + offset, n, gr, peak);
      offset += n;
    }
    // GR is reported negative, matching the host meters and inline display.
    grDb = -gr;
    outDb = clamp(gainToDb(peak), -60.0f, 6.0f);
  }

  void fillTelemetry(float* spectrumBins, float* gainBins) const noexcept {
    tame_.fillSpectrumBins(spectrumBins, kTelemetryBins);
    tame_.fillGainBins(gainBins, kTelemetryBins);
  }

private:
  static void orderFor(int routing, int out[3]) noexcept {
    static constexpr int kOrders[6][3] = {
        {0, 1, 2}, {0, 2, 1}, {1, 0, 2},
        {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
    const int r = std::min(std::max(routing, 0), 5);
    for (int i = 0; i < 3; ++i) {
      out[i] = kOrders[r][i];
    }
  }

  void processPultec(uint32_t n) noexcept {
    for (uint32_t i = 0; i < n; ++i) {
      pultec_.tick();
      if ((i & 15u) == 0u) {
        pultec_.update();
      }
      const float amt = pultecAmt_.next();
      const float l = workL_[i];
      const float r = workR_[i];
      workL_[i] = l + amt * (pultec_.process(0, l) - l);
      workR_[i] = r + amt * (pultec_.process(1, r) - r);
    }
  }

  void processSsl(uint32_t n) noexcept {
    for (uint32_t i = 0; i < n; ++i) {
      ssl_.tick();
      if ((i & 15u) == 0u) {
        ssl_.update();
      }
      const float amt = sslAmt_.next();
      const float l = workL_[i];
      const float r = workR_[i];
      workL_[i] = l + amt * (ssl_.process(0, l) - l);
      workR_[i] = r + amt * (ssl_.process(1, r) - r);
    }
  }

  void processTame(uint32_t n) noexcept {
    std::copy(workL_.begin(), workL_.begin() + n, tameInL_.begin());
    std::copy(workR_.begin(), workR_.begin() + n, tameInR_.begin());

    if constexpr (kLatency > 0) {
      for (uint32_t i = 0; i < n; ++i) {
        dryL_[i] = tameDryL_.process(workL_[i]);
        dryR_[i] = tameDryR_.process(workR_[i]);
      }
    } else {
      std::copy(workL_.begin(), workL_.begin() + n, dryL_.begin());
      std::copy(workR_.begin(), workR_.begin() + n, dryR_.begin());
    }

    tame_.process(tameInL_.data(), tameInR_.data(), n);

    for (uint32_t i = 0; i < n; ++i) {
      const float amt = tameAmt_.next();
      workL_[i] = dryL_[i] + amt * (tameInL_[i] - dryL_[i]);
      workR_[i] = dryR_[i] + amt * (tameInR_[i] - dryR_[i]);
    }
  }

  void processChunk(const float* inLeft, const float* inRight, float* outLeft,
                    float* outRight, uint32_t n, float& gr,
                    float& peak) noexcept {
    for (uint32_t i = 0; i < n; ++i) {
      const float g = inputGain_.next();
      workL_[i] = inLeft[i] * g;
      workR_[i] = inRight[i] * g;
    }

    if constexpr (kLatency > 0) {
      for (uint32_t i = 0; i < n; ++i) {
        bypassL_[i] = globalDryL_.process(inLeft[i]);
        bypassR_[i] = globalDryR_.process(inRight[i]);
      }
    } else {
      for (uint32_t i = 0; i < n; ++i) {
        bypassL_[i] = inLeft[i];
        bypassR_[i] = inRight[i];
      }
    }

    int order[3];
    orderFor(activeRouting_, order);
    for (int m = 0; m < 3; ++m) {
      if (order[m] == 0) {
        processPultec(n);
      } else if (order[m] == 1) {
        processSsl(n);
      } else {
        processTame(n);
      }
    }

    if constexpr (Mastering) {
      if (params_.autoGain) {
        for (uint32_t i = 0; i < n; ++i) {
          dryRms_ += rmsCoef_ * (bypassL_[i] * bypassL_[i] +
                                 bypassR_[i] * bypassR_[i] - dryRms_);
          wetRms_ += rmsCoef_ * (workL_[i] * workL_[i] +
                                 workR_[i] * workR_[i] - wetRms_);
        }
        if (dryRms_ > 1.0e-8f && wetRms_ > 1.0e-8f) {
          const float target =
              clamp(10.0f * std::log10(dryRms_ / wetRms_), -12.0f, 12.0f);
          autoGainDb_ += (1.0f - autoGainCoef_) * (target - autoGainDb_);
        }
        const float ag = dbToGain(autoGainDb_);
        for (uint32_t i = 0; i < n; ++i) {
          workL_[i] *= ag;
          workR_[i] *= ag;
        }
      } else {
        autoGainDb_ *= autoGainCoef_;
      }
    }

    for (uint32_t i = 0; i < n; ++i) {
      const float g = outputGain_.next();
      workL_[i] = clip_[0].process(workL_[i] * g);
      workR_[i] = clip_[1].process(workR_[i] * g);
    }

    if constexpr (Mastering) {
      if (params_.delta) {
        for (uint32_t i = 0; i < n; ++i) {
          const float d = deltaAmt_.next();
          const float deltaL = bypassL_[i] - workL_[i];
          const float deltaR = bypassR_[i] - workR_[i];
          workL_[i] = workL_[i] + d * (deltaL - workL_[i]);
          workR_[i] = workR_[i] + d * (deltaR - workR_[i]);
        }
      } else {
        for (uint32_t i = 0; i < n; ++i) {
          deltaAmt_.next();
        }
      }
    }

    for (uint32_t i = 0; i < n; ++i) {
      if (routingFade_ > routingFadeTarget_) {
        routingFade_ = std::max(routingFadeTarget_,
                                routingFade_ - routingFadeStep_);
      } else if (routingFade_ < routingFadeTarget_) {
        routingFade_ = std::min(routingFadeTarget_,
                                routingFade_ + routingFadeStep_);
      }
      if (routingFade_ <= 0.0f && pendingRouting_ != activeRouting_) {
        activeRouting_ = pendingRouting_;
        routingFadeTarget_ = 1.0f;
      }

      const float m = masterAmt_.next();
      const float l = bypassL_[i] + m * (workL_[i] - bypassL_[i]);
      const float r = bypassR_[i] + m * (workR_[i] - bypassR_[i]);
      const float f = routingFade_;
      const float fl = l * f;
      const float fr = r * f;
      outLeft[i] = fl;
      outRight[i] = fr;
      peak = std::max(peak, std::max(std::fabs(fl), std::fabs(fr)));
    }

    gr = std::max(gr, tame_.lastMaxReductionDb());
    tame_.fillSpectrumBins(spectrum_.data(), kTelemetryBins);
    tame_.fillGainBins(gain_.data(), kTelemetryBins);
  }

  float sampleRate_ = 48000.0f;
  Params params_;
  bool prepared_ = false;

  PultecEq pultec_;
  SslEq ssl_;
  Tame tame_;
  SoftClip clip_[2];

  Smoothed inputGain_;
  Smoothed outputGain_;
  Smoothed pultecAmt_;
  Smoothed sslAmt_;
  Smoothed tameAmt_;
  Smoothed masterAmt_;
  Smoothed deltaAmt_;

  DelayLine tameDryL_;
  DelayLine tameDryR_;
  DelayLine globalDryL_;
  DelayLine globalDryR_;

  std::vector<float> workL_;
  std::vector<float> workR_;
  std::vector<float> tameInL_;
  std::vector<float> tameInR_;
  std::vector<float> dryL_;
  std::vector<float> dryR_;
  std::vector<float> bypassL_;
  std::vector<float> bypassR_;
  std::vector<float> spectrum_ = std::vector<float>(kTelemetryBins, -100.0f);
  std::vector<float> gain_ = std::vector<float>(kTelemetryBins, 0.0f);

  int activeRouting_ = 0;
  int pendingRouting_ = 0;
  float routingFade_ = 1.0f;
  float routingFadeTarget_ = 1.0f;
  float routingFadeStep_ = 1.0f;
  float dryRms_ = 0.0f;
  float wetRms_ = 0.0f;
  float autoGainDb_ = 0.0f;
  float autoGainCoef_ = 0.0f;
  float rmsCoef_ = 0.0f;
};

} // namespace trowel
