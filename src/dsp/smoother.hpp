// Trowel — smoother.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "util.hpp"

namespace trowel {

// One-pole parameter smoother.  `next()` is called once per sample;
// `setTarget()` is called from the port values, once per block.
class Smoothed {
public:
  void setTime(float ms, float sampleRate) noexcept {
    const float samples = std::max(1.0f, ms * 0.001f * sampleRate);
    coeff_ = std::exp(-1.0f / samples);
  }

  void reset(float value) noexcept { current_ = target_ = value; }

  void setTarget(float value) noexcept { target_ = value; }

  float target() const noexcept { return target_; }
  float current() const noexcept { return current_; }

  float next() noexcept {
    current_ = target_ + coeff_ * (current_ - target_);
    return current_;
  }

  void snap() noexcept { current_ = target_; }

private:
  float current_ = 0.0f;
  float target_ = 0.0f;
  float coeff_ = 0.0f;
};

} // namespace trowel
