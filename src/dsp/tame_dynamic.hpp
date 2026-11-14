// Trowel — tame_dynamic.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cmath>
#include <cstdint>

#include "eq_band.hpp"
#include "filters.hpp"
#include "params.hpp"
#include "util.hpp"

namespace trowel {

// Zero-latency resonance suppressor.
//
// Sixteen log-spaced analysis bands each compare their own short-term level
// with the levels of their neighbours.  A narrow peak that stands above the
// local programme floor is a resonance; a broad note lifts its neighbours too
// and is left alone.  Each band drives a transparent dynamic bell, so at
// Amount 0 the chain is exactly y = x.
class TameDynamic {
public:
  static constexpr uint32_t kNumBands = 16;
  static constexpr uint32_t kLatency = 0;

  void prepare(float sampleRate) noexcept {
    sampleRate_ = sampleRate;
    for (uint32_t i = 0; i < kNumBands; ++i) {
      const float t = static_cast<float>(i) /
                      static_cast<float>(kNumBands - 1);
      freq_[i] = 160.0f * std::pow(100.0f, t);
      band_[i].prepare(sampleRate, EqShape::Bell, freq_[i], 0.0f, 4.0f);
      analysis_[i].set(freq_[i], 4.0f, sampleRate);
      for (auto& s : analysisState_[i]) {
        s.reset();
      }
      level_[i] = 0.0f;
      reductionDb_[i] = 0.0f;
    }
    setTargets(TameParams{});
    reset();
  }

  void setTargets(const TameParams& p) noexcept {
    params_ = p;
    const float sel = clamp(p.selectivity, 0.0f, 1.0f);
    const float q = 2.0f * std::pow(5.0f, sel);
    if (std::fabs(q - lastQ_) > 0.01f) {
      lastQ_ = q;
      for (uint32_t i = 0; i < kNumBands; ++i) {
        analysis_[i].set(freq_[i], q, sampleRate_);
        band_[i].setTuning(freq_[i], q);
      }
    }

    const float focus = clamp(p.focus, 0.0f, 1.0f);
    const float fc = 200.0f * std::pow(80.0f, focus);
    for (uint32_t i = 0; i < kNumBands; ++i) {
      const float oct = std::log2(freq_[i] / fc);
      focusWeight_[i] = std::exp(-0.5f * oct * oct / (1.6f * 1.6f));
    }

    thresholdDb_ = 18.0f - 16.0f * clamp(p.sensitivity, 0.0f, 1.0f);
    maxDepthDb_ = 18.0f * clamp(p.amount, 0.0f, 1.0f);
    attackCoef_ = std::exp(-1000.0f /
                           (std::max(p.attackMs, 0.1f) * sampleRate_));
    releaseCoef_ = std::exp(-1000.0f /
                            (std::max(p.releaseMs, 1.0f) * sampleRate_));
  }

  void process(float* left, float* right, uint32_t frames) noexcept {
    float blockMax = 0.0f;
    for (uint32_t n = 0; n < frames; ++n) {
      const float dryL = left[n];
      const float dryR = right[n];

      for (uint32_t i = 0; i < kNumBands; ++i) {
        float lpL, bpL, hpL;
        float lpR, bpR, hpR;
        analysisState_[i][0].process(dryL, analysis_[i], lpL, bpL, hpL);
        analysisState_[i][1].process(dryR, analysis_[i], lpR, bpR, hpR);
        const float power = std::max(bpL * bpL, bpR * bpR);
        const float coef = power > level_[i] ? attackCoef_ : releaseCoef_;
        level_[i] = power + coef * (level_[i] - power);
      }

      for (uint32_t i = 0; i < kNumBands; ++i) {
        // Compare against the bands two and three steps away.  Skipping the
        // immediate neighbours stops a tone that falls between two bands from
        // hiding itself in the baseline.
        float neighbour = 0.0f;
        int count = 0;
        for (int d = -3; d <= 3; ++d) {
          if (d == 0 || d == -1 || d == 1) {
            continue;
          }
          const int j = static_cast<int>(i) + d;
          if (j < 0 || j >= static_cast<int>(kNumBands)) {
            continue;
          }
          neighbour += level_[static_cast<uint32_t>(j)];
          ++count;
        }
        neighbour /= static_cast<float>(std::max(count, 1));

        float reduction = 0.0f;
        if (level_[i] > 1.0e-8f && neighbour > 1.0e-10f) {
          const float contrastDb =
              10.0f * std::log10(level_[i] / neighbour);
          const float gate =
              smoothStep(thresholdDb_, thresholdDb_ + 12.0f, contrastDb);
          reduction = maxDepthDb_ * gate * focusWeight_[i];
          if (reduction < 0.01f) {
            reduction = 0.0f;
          }
        }
        reductionDb_[i] = reduction;
        band_[i].setGainTargetDb(-reduction);
      }

      float wetL = dryL;
      float wetR = dryR;
      for (uint32_t i = 0; i < kNumBands; ++i) {
        band_[i].tick();
        if ((n & 15u) == 0u) {
          band_[i].update();
        }
        wetL = band_[i].process(0, wetL);
        wetR = band_[i].process(1, wetR);
      }

      if (params_.listen) {
        left[n] = dryL - wetL;
        right[n] = dryR - wetR;
      } else {
        const float mix = clamp(params_.mix, 0.0f, 1.0f);
        left[n] = dryL + mix * (wetL - dryL);
        right[n] = dryR + mix * (wetR - dryR);
      }

      for (uint32_t i = 0; i < kNumBands; ++i) {
        if (reductionDb_[i] > blockMax) {
          blockMax = reductionDb_[i];
        }
      }
    }
    lastMaxReductionDb_ = std::max(blockMax, lastMaxReductionDb_ * 0.85f);
  }

  void reset() noexcept {
    for (uint32_t i = 0; i < kNumBands; ++i) {
      band_[i].reset();
      band_[i].setGainTargetDb(0.0f);
      band_[i].snapGain();
      for (auto& s : analysisState_[i]) {
        s.reset();
      }
      level_[i] = 0.0f;
      reductionDb_[i] = 0.0f;
    }
    lastMaxReductionDb_ = 0.0f;
  }

  float lastMaxReductionDb() const noexcept { return lastMaxReductionDb_; }

  void fillGainBins(float* out, uint32_t bins) const noexcept {
    for (uint32_t j = 0; j < bins; ++j) {
      const float t = bins <= 1 ? 0.0f
                                : static_cast<float>(j) /
                                      static_cast<float>(bins - 1);
      const float f = 20.0f * std::pow(1000.0f, t);
      float pos = std::log(f / 160.0f) / std::log(100.0f) *
                  static_cast<float>(kNumBands - 1);
      pos = clamp(pos, 0.0f, static_cast<float>(kNumBands - 1));
      const int i0 = static_cast<int>(pos);
      const int i1 = std::min(i0 + 1, static_cast<int>(kNumBands - 1));
      const float frac = pos - static_cast<float>(i0);
      out[j] = -reductionDb_[static_cast<uint32_t>(i0)] * (1.0f - frac) +
               -reductionDb_[static_cast<uint32_t>(i1)] * frac;
    }
  }

  void fillSpectrumBins(float* out, uint32_t bins) const noexcept {
    for (uint32_t j = 0; j < bins; ++j) {
      const float t = bins <= 1 ? 0.0f
                                : static_cast<float>(j) /
                                      static_cast<float>(bins - 1);
      const float f = 20.0f * std::pow(1000.0f, t);
      float pos = std::log(f / 160.0f) / std::log(100.0f) *
                  static_cast<float>(kNumBands - 1);
      pos = clamp(pos, 0.0f, static_cast<float>(kNumBands - 1));
      const int i0 = static_cast<int>(pos);
      const int i1 = std::min(i0 + 1, static_cast<int>(kNumBands - 1));
      const float frac = pos - static_cast<float>(i0);
      const float v0 = 10.0f * std::log10(std::max(level_[i0], 1.0e-10f));
      const float v1 = 10.0f * std::log10(std::max(level_[i1], 1.0e-10f));
      out[j] = clamp(v0 * (1.0f - frac) + v1 * frac, -100.0f, 6.0f);
    }
  }

private:
  float sampleRate_ = 48000.0f;
  TameParams params_;
  float freq_[kNumBands] = {};
  SVFCoeffs analysis_[kNumBands];
  SVFState analysisState_[kNumBands][2];
  EqBand band_[kNumBands];
  float level_[kNumBands] = {};
  float reductionDb_[kNumBands] = {};
  float focusWeight_[kNumBands] = {};
  float lastQ_ = 0.0f;
  float thresholdDb_ = 10.0f;
  float maxDepthDb_ = 0.0f;
  float attackCoef_ = 0.0f;
  float releaseCoef_ = 0.0f;
  float lastMaxReductionDb_ = 0.0f;
};

} // namespace trowel
