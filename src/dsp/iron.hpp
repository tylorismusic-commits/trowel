// Trowel — iron.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "filters.hpp"
#include "saturator.hpp"
#include "smoother.hpp"
#include "util.hpp"

namespace trowel {

// Pultec-style iron: a gentle transformer high-pass and a touch of ADAA
// saturation.  Deliberately subtle — Trowel is an EQ, not a saturator.
class IronStage {
public:
  void prepare(float sampleRate) noexcept {
    sampleRate_ = sampleRate;
    hp_.set(18.0f, sampleRate);
    for (auto& s : sat_) {
      s.reset();
    }
    amount_.setTime(20.0f, sampleRate);
    amount_.reset(0.0f);
  }

  void setTargets(bool enabled) noexcept {
    amount_.setTarget(enabled ? 1.0f : 0.0f);
  }

  inline float process(int channel, float x) noexcept {
    const float a = amount_.next();
    if (a <= 1.0e-5f) {
      return x;
    }
    const float driven = sat_[channel].process(x * 1.15f, 0.035f);
    const float wet = driven * (1.0f / 1.15f);
    return hp_.hp(x + a * 0.45f * (wet - x));
  }

  void reset() noexcept {
    for (auto& s : sat_) {
      s.reset();
    }
    hp_.reset();
    amount_.snap();
  }

private:
  float sampleRate_ = 48000.0f;
  OnePole hp_;
  AdaaTanh sat_[2];
  Smoothed amount_;
};

} // namespace trowel
