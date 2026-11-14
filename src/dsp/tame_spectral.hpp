// Trowel — tame_spectral.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <complex>
#include <cstdint>
#include <cstring>
#include <vector>

#include "fft.hpp"
#include "filters.hpp"
#include "params.hpp"
#include "util.hpp"

namespace trowel {

// Trowel Master's spectral resonance suppressor.
//
// A 4096-point STFT with a 1024-sample hop (75% overlap).  Each frame is
// windowed with a sqrt-Hann window; the analyser compares a narrow
// (1/24-octave) spectral envelope against a broad (1/2-octave) baseline and
// attenuates only bins that stand above the local programme floor.  Gains are
// smoothed across frequency and time before being applied to the complex
// spectrum, then the frame is overlap-added back.
//
// The engine is causal and has a fixed latency of kFftSize - 1 samples.
class TameSpectral {
public:
  static constexpr uint32_t kFftSize = 4096;
  static constexpr uint32_t kHop = 1024;
  static constexpr uint32_t kBins = kFftSize / 2 + 1;
  static constexpr uint32_t kLatency = kFftSize - 1;
  static constexpr uint32_t kQueue = kHop * 2;

  TameSpectral() : fft_(kFftSize) {}

  void prepare(float sampleRate) noexcept {
    sampleRate_ = sampleRate;
    window_.resize(kFftSize);
    for (uint32_t i = 0; i < kFftSize; ++i) {
      window_[i] = std::sin(kPi * static_cast<float>(i) /
                            static_cast<float>(kFftSize));
    }
    xA_.assign(kFftSize, std::complex<float>(0.0f, 0.0f));
    xB_.assign(kFftSize, std::complex<float>(0.0f, 0.0f));
    powerA_.assign(kBins, 0.0f);
    powerB_.assign(kBins, 0.0f);
    powerMax_.assign(kBins, 0.0f);
    prefix_.assign(kBins + 1, 0.0f);
    gainA_.assign(kBins, 0.0f);
    gainB_.assign(kBins, 0.0f);
    scratchA_.assign(kBins, 0.0f);
    scratchB_.assign(kBins, 0.0f);
    focus_.assign(kBins, 1.0f);

    dryL_.prepare(kLatency);
    dryR_.prepare(kLatency);
    reset();
    setTargets(TameParams{});
  }

  void setTargets(const TameParams& p) noexcept {
    params_ = p;
    if (focus_.empty()) {
      return; // not prepared yet; prepare() will apply the targets
    }

    const float amount = clamp(p.amount, 0.0f, 1.0f);
    maxDepthDb_ = 18.0f * amount;
    thresholdDb_ = 18.0f - 16.0f * clamp(p.sensitivity, 0.0f, 1.0f);

    const float hopMs = 1000.0f * static_cast<float>(kHop) / sampleRate_;
    attackCoef_ = std::exp(-hopMs / std::max(p.attackMs, 0.1f));
    releaseCoef_ = std::exp(-hopMs / std::max(p.releaseMs, 1.0f));
    attackCoef_ = clamp(attackCoef_, 0.0f, 0.9999f);
    releaseCoef_ = clamp(releaseCoef_, 0.0f, 0.9999f);

    const float sel = clamp(p.selectivity, 0.0f, 1.0f);
    smoothRadius_ = 1 + static_cast<int>(std::lround((1.0f - sel) * 7.0f));

    const float focus = clamp(p.focus, 0.0f, 1.0f);
    if (std::fabs(focus - lastFocus_) > 0.002f) {
      lastFocus_ = focus;
      const float fc = 200.0f * std::pow(80.0f, focus);
      for (uint32_t k = 0; k < kBins; ++k) {
        const float f = static_cast<float>(k) * sampleRate_ /
                        static_cast<float>(kFftSize);
        const float oct = std::log2(std::max(f, 20.0f) / fc);
        focus_[k] = std::exp(-0.5f * oct * oct / (1.6f * 1.6f));
      }
    }
  }

  void process(float* left, float* right, uint32_t frames) noexcept {
    for (uint32_t n = 0; n < frames; ++n) {
      const float inL = left[n];
      const float inR = right[n];

      const float dryL = dryL_.process(inL);
      const float dryR = dryR_.process(inR);

      float a = inL;
      float b = inR;
      if (params_.msMode) {
        a = 0.5f * (inL + inR);
        b = 0.5f * (inL - inR);
      }

      pendingA_[pendingCount_] = a;
      pendingB_[pendingCount_] = b;
      ++pendingCount_;
      if (pendingCount_ == kHop) {
        pendingCount_ = 0;
        processHop();
      }

      float wetA = 0.0f;
      float wetB = 0.0f;
      if (outCount_ > 0) {
        wetA = outQA_[outRead_];
        wetB = outQB_[outRead_];
        outRead_ = (outRead_ + 1) % kQueue;
        --outCount_;
      }

      float wetL = wetA;
      float wetR = wetB;
      if (params_.msMode) {
        wetL = wetA + wetB;
        wetR = wetA - wetB;
      }

      if (params_.listen) {
        left[n] = dryL - wetL;
        right[n] = dryR - wetR;
      } else {
        const float mix = clamp(params_.mix, 0.0f, 1.0f);
        left[n] = dryL + mix * (wetL - dryL);
        right[n] = dryR + mix * (wetR - dryR);
      }
    }

    lastMaxReductionDb_ =
        std::max(lastBlockReduction_, lastMaxReductionDb_ * 0.9f);
  }

  void reset() noexcept {
    std::fill(histA_, histA_ + kFftSize, 0.0f);
    std::fill(histB_, histB_ + kFftSize, 0.0f);
    std::fill(olaA_, olaA_ + kFftSize, 0.0f);
    std::fill(olaB_, olaB_ + kFftSize, 0.0f);
    std::fill(pendingA_, pendingA_ + kHop, 0.0f);
    std::fill(pendingB_, pendingB_ + kHop, 0.0f);
    std::fill(outQA_, outQA_ + kQueue, 0.0f);
    std::fill(outQB_, outQB_ + kQueue, 0.0f);
    pendingCount_ = 0;
    outRead_ = 0;
    outWrite_ = 0;
    outCount_ = 0;
    std::fill(gainA_.begin(), gainA_.end(), 0.0f);
    std::fill(gainB_.begin(), gainB_.end(), 0.0f);
    std::fill(spectrumBins_, spectrumBins_ + 32, -100.0f);
    std::fill(gainBins_, gainBins_ + 32, 0.0f);
    lastMaxReductionDb_ = 0.0f;
    lastBlockReduction_ = 0.0f;
    dryL_.reset();
    dryR_.reset();
  }

  float lastMaxReductionDb() const noexcept { return lastMaxReductionDb_; }

  void fillGainBins(float* out, uint32_t bins) const noexcept {
    copyBins(gainBins_, out, bins);
  }

  void fillSpectrumBins(float* out, uint32_t bins) const noexcept {
    copyBins(spectrumBins_, out, bins);
  }

private:
  static void copyBins(const float* source, float* out,
                       uint32_t bins) noexcept {
    const uint32_t n = std::min<uint32_t>(bins, 32);
    for (uint32_t i = 0; i < n; ++i) {
      out[i] = source[i];
    }
    for (uint32_t i = n; i < bins; ++i) {
      out[i] = source[n > 0 ? n - 1 : 0];
    }
  }

  void processHop() noexcept {
    // Slide the analysis history by one hop and append the new samples.
    std::memmove(histA_, histA_ + kHop, (kFftSize - kHop) * sizeof(float));
    std::memmove(histB_, histB_ + kHop, (kFftSize - kHop) * sizeof(float));
    std::memcpy(histA_ + kFftSize - kHop, pendingA_, kHop * sizeof(float));
    std::memcpy(histB_ + kFftSize - kHop, pendingB_, kHop * sizeof(float));

    for (uint32_t i = 0; i < kFftSize; ++i) {
      xA_[i] = std::complex<float>(histA_[i] * window_[i], 0.0f);
      xB_[i] = std::complex<float>(histB_[i] * window_[i], 0.0f);
    }
    fft_.forward(xA_.data());
    fft_.forward(xB_.data());

    for (uint32_t k = 0; k < kBins; ++k) {
      powerA_[k] = std::norm(xA_[k]);
      powerB_[k] = std::norm(xB_[k]);
    }

    if (params_.msMode) {
      computeGain(powerA_, gainA_, scratchA_);
      computeGain(powerB_, gainB_, scratchB_);
    } else {
      for (uint32_t k = 0; k < kBins; ++k) {
        powerMax_[k] = std::max(powerA_[k], powerB_[k]);
      }
      computeGain(powerMax_, gainA_, scratchA_);
      std::copy(gainA_.begin(), gainA_.end(), gainB_.begin());
    }

    applyGain(xA_, gainA_);
    applyGain(xB_, gainB_);

    fft_.inverse(xA_.data());
    fft_.inverse(xB_.data());

    for (uint32_t i = 0; i < kFftSize; ++i) {
      olaA_[i] += xA_[i].real() * window_[i] * 0.5f;
      olaB_[i] += xB_[i].real() * window_[i] * 0.5f;
    }

    for (uint32_t i = 0; i < kHop; ++i) {
      const uint32_t w = (outWrite_ + i) % kQueue;
      outQA_[w] = olaA_[i];
      outQB_[w] = olaB_[i];
    }
    outWrite_ = (outWrite_ + kHop) % kQueue;
    outCount_ = std::min<uint32_t>(outCount_ + kHop, kQueue);

    std::memmove(olaA_, olaA_ + kHop, (kFftSize - kHop) * sizeof(float));
    std::memmove(olaB_, olaB_ + kHop, (kFftSize - kHop) * sizeof(float));
    std::fill(olaA_ + kFftSize - kHop, olaA_ + kFftSize, 0.0f);
    std::fill(olaB_ + kFftSize - kHop, olaB_ + kFftSize, 0.0f);

    updateTelemetry();

    float maxReduction = 0.0f;
    for (uint32_t k = 0; k < kBins; ++k) {
      maxReduction = std::max(maxReduction, -gainA_[k]);
      maxReduction = std::max(maxReduction, -gainB_[k]);
    }
    lastBlockReduction_ = maxReduction;
  }

  void computeGain(const std::vector<float>& power, std::vector<float>& gain,
                   std::vector<float>& scratch) noexcept {
    prefix_[0] = 0.0f;
    for (uint32_t k = 0; k < kBins; ++k) {
      prefix_[k + 1] = prefix_[k] + power[k];
    }

    const float binHz = sampleRate_ / static_cast<float>(kFftSize);
    const float narrowOct = 1.0f / 48.0f;
    const float broadOct = 1.0f / 4.0f;

    for (uint32_t k = 1; k + 1 < kBins; ++k) {
      const float f = static_cast<float>(k) * binHz;
      if (f < 20.0f) {
        scratch[k] = 0.0f;
        continue;
      }

      const float narrowHz = f * (std::pow(2.0f, narrowOct) - 1.0f);
      const float broadHz = f * (std::pow(2.0f, broadOct) - 1.0f);
      int nlo = static_cast<int>(k) - static_cast<int>(narrowHz / binHz + 0.5f);
      int nhi = static_cast<int>(k) + static_cast<int>(narrowHz / binHz + 0.5f);
      int blo = static_cast<int>(k) - static_cast<int>(broadHz / binHz + 0.5f);
      int bhi = static_cast<int>(k) + static_cast<int>(broadHz / binHz + 0.5f);
      nlo = std::max(nlo, 0);
      blo = std::max(blo, 0);
      nhi = std::min(nhi, static_cast<int>(kBins) - 1);
      bhi = std::min(bhi, static_cast<int>(kBins) - 1);

      const float narrow =
          (prefix_[nhi + 1] - prefix_[nlo]) / static_cast<float>(nhi - nlo + 1);
      const float broad =
          (prefix_[bhi + 1] - prefix_[blo]) / static_cast<float>(bhi - blo + 1);

      float target = 0.0f;
      if (power[k] > 1.0e-9f && narrow > 1.0e-12f && broad > 1.0e-12f) {
        const float excessDb = 10.0f * std::log10(narrow / broad);
        const float gate =
            smoothStep(thresholdDb_, thresholdDb_ + 12.0f, excessDb);
        target = -maxDepthDb_ * gate * focus_[k];
      }

      const float previous = gain[k];
      const float coef = target < previous ? attackCoef_ : releaseCoef_;
      scratch[k] = target + coef * (previous - target);
    }

    // Triangular smoothing across frequency kills musical noise without
    // blurring broad tonal shapes.
    const int r = std::max(1, smoothRadius_);
    for (uint32_t k = 1; k + 1 < kBins; ++k) {
      float sum = 0.0f;
      float weight = 0.0f;
      for (int d = -r; d <= r; ++d) {
        const int j = static_cast<int>(k) + d;
        if (j <= 0 || j >= static_cast<int>(kBins) - 1) {
          continue;
        }
        const float w = static_cast<float>(r + 1 - std::abs(d));
        sum += scratch[static_cast<uint32_t>(j)] * w;
        weight += w;
      }
      gain[k] = weight > 0.0f ? sum / weight : 0.0f;
    }
    gain[0] = 0.0f;
    gain[kBins - 1] = 0.0f;
  }

  void applyGain(std::vector<std::complex<float>>& x,
                 const std::vector<float>& gain) noexcept {
    for (uint32_t k = 1; k + 1 < kBins; ++k) {
      const float g = dbToGain(gain[k]);
      x[k] *= g;
      x[kFftSize - k] *= g;
    }
  }

  void updateTelemetry() noexcept {
    for (uint32_t j = 0; j < 32; ++j) {
      const float t = static_cast<float>(j) / 31.0f;
      const float f = 20.0f * std::pow(1000.0f, t);
      int k = static_cast<int>(f * static_cast<float>(kFftSize) /
                               sampleRate_ + 0.5f);
      k = std::max(1, std::min(k, static_cast<int>(kBins) - 2));
      const float p = std::max(powerA_[k], powerB_[k]);
      spectrumBins_[j] =
          clamp(10.0f * std::log10(std::max(p, 1.0e-12f)), -100.0f, 6.0f);
      const float g = params_.msMode ? std::min(gainA_[k], gainB_[k])
                                     : gainA_[k];
      gainBins_[j] = clamp(g, -24.0f, 0.0f);
    }
  }

  float sampleRate_ = 48000.0f;
  TameParams params_;
  Fft fft_;

  std::vector<float> window_;
  float histA_[kFftSize] = {};
  float histB_[kFftSize] = {};
  float olaA_[kFftSize] = {};
  float olaB_[kFftSize] = {};
  float pendingA_[kHop] = {};
  float pendingB_[kHop] = {};
  float outQA_[kQueue] = {};
  float outQB_[kQueue] = {};
  uint32_t pendingCount_ = 0;
  uint32_t outRead_ = 0;
  uint32_t outWrite_ = 0;
  uint32_t outCount_ = 0;

  std::vector<std::complex<float>> xA_;
  std::vector<std::complex<float>> xB_;
  std::vector<float> powerA_;
  std::vector<float> powerB_;
  std::vector<float> powerMax_;
  std::vector<float> prefix_;
  std::vector<float> gainA_;
  std::vector<float> gainB_;
  std::vector<float> scratchA_;
  std::vector<float> scratchB_;
  std::vector<float> focus_;

  DelayLine dryL_;
  DelayLine dryR_;

  float maxDepthDb_ = 0.0f;
  float thresholdDb_ = 10.0f;
  float attackCoef_ = 0.0f;
  float releaseCoef_ = 0.0f;
  float lastFocus_ = -1.0f;
  int smoothRadius_ = 4;
  float lastMaxReductionDb_ = 0.0f;
  float lastBlockReduction_ = 0.0f;
  float spectrumBins_[32] = {};
  float gainBins_[32] = {};
};

} // namespace trowel
