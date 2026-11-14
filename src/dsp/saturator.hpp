// Trowel — saturator.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cmath>

#include "util.hpp"

namespace trowel {

// First-order antiderivative anti-aliased asymmetric tanh.  The asymmetry
// (bias) generates even-order harmonics; ADAA keeps aliasing under control
// without adding latency.
class AdaaTanh {
public:
  static float antiderivative(float x, float bias) noexcept {
    return logCosh(x + bias) - std::tanh(bias) * x;
  }

  float process(float x, float bias) noexcept {
    if (!primed_) {
      x1_ = x;
      f1_ = antiderivative(x, bias);
      bias_ = bias;
      primed_ = true;
      return std::tanh(x + bias) - std::tanh(bias);
    }

    if (std::fabs(bias - bias_) > 1.0e-7f) {
      f1_ = antiderivative(x1_, bias);
      bias_ = bias;
    }

    const float fx = antiderivative(x, bias);
    const float dx = x - x1_;
    float y;
    if (std::fabs(dx) < 1.0e-4f) {
      y = std::tanh(0.5f * (x + x1_) + bias) - std::tanh(bias);
    } else {
      y = (fx - f1_) / dx;
    }

    x1_ = x;
    f1_ = fx;
    return y;
  }

  void reset() noexcept {
    x1_ = 0.0f;
    f1_ = 0.0f;
    bias_ = 0.0f;
    primed_ = false;
  }

private:
  float x1_ = 0.0f;
  float f1_ = 0.0f;
  float bias_ = 0.0f;
  bool primed_ = false;
};

// Linear below the knee, softly saturating up to the +0.2 dBFS ceiling.
// Unlike a bare tanh this is exactly unity for normal programme material, so
// the output trim means what it says.
class SoftClip {
public:
  static constexpr float kKnee = 0.90f;
  static constexpr float kCeiling = 1.02329299f;

  float process(float x) noexcept {
    const float ax = std::fabs(x);
    if (ax <= kKnee) {
      return x;
    }
    const float over = ax - kKnee;
    const float y =
        kKnee + (kCeiling - kKnee) * std::tanh(over / (kCeiling - kKnee));
    return x < 0.0f ? -y : y;
  }

  void reset() noexcept {}
};

} // namespace trowel
