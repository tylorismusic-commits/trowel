// Trowel — filters.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cmath>
#include <complex>
#include <vector>

#include "smoother.hpp"
#include "util.hpp"

namespace trowel {

// Topology-preserving one-pole (Zavalishin).  Stable under modulation.
struct OnePole {
  float G = 0.0f;
  float ic = 0.0f;

  void set(float cutoff, float sampleRate) noexcept {
    cutoff = clamp(cutoff, 1.0f, sampleRate * 0.49f);
    const float g = std::tan(kPi * cutoff / sampleRate);
    G = g / (1.0f + g);
  }

  void reset() noexcept { ic = 0.0f; }

  inline float lp(float x) noexcept {
    const float v = (x - ic) * G;
    const float y = v + ic;
    ic = y + v;
    if (std::fabs(ic) < 1.0e-20f) {
      ic = 0.0f;
    }
    return y;
  }

  inline float hp(float x) noexcept { return x - lp(x); }
};

// Topology-preserving state-variable filter.  Coefficients are shared by both
// channels; state is per channel.
struct SVFCoeffs {
  float g = 0.0f;
  float k = 1.0f;
  float a1 = 0.0f;
  float a2 = 0.0f;
  float a3 = 0.0f;

  void set(float cutoff, float q, float sampleRate) noexcept {
    cutoff = clamp(cutoff, 5.0f, sampleRate * 0.49f);
    g = std::tan(kPi * cutoff / sampleRate);
    k = 1.0f / std::max(q, 0.05f);
    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
  }
};

struct SVFState {
  float ic1 = 0.0f;
  float ic2 = 0.0f;

  void reset() noexcept { ic1 = ic2 = 0.0f; }

  inline void process(float x, const SVFCoeffs& c,
                      float& lp, float& bp, float& hp) noexcept {
    const float v3 = x - ic2;
    const float v1 = c.a1 * ic1 + c.a2 * v3;
    const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    if (std::fabs(ic1) < 1.0e-20f) {
      ic1 = 0.0f;
    }
    if (std::fabs(ic2) < 1.0e-20f) {
      ic2 = 0.0f;
    }
    lp = v2;
    bp = v1;
    hp = x - c.k * v1 - v2;
  }
};

// RBJ-style shelving biquad.  Coefficients are shared; state is per channel.
// At exactly 0 dB the coefficients collapse to a true identity filter, so a
// flat shelf adds neither gain nor phase.
struct BiquadCoeffs {
  float b0 = 1.0f;
  float b1 = 0.0f;
  float b2 = 0.0f;
  float a1 = 0.0f;
  float a2 = 0.0f;

  void setLowShelf(float f0, float gainDb, float q, float sampleRate) noexcept {
    if (std::fabs(gainDb) < 1.0e-5f) {
      identity();
      return;
    }
    const float a = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * kPi * clamp(f0, 10.0f, sampleRate * 0.49f) /
                     sampleRate;
    const float cosw = std::cos(w0);
    const float sinw = std::sin(w0);
    const float alpha = sinw / (2.0f * std::max(q, 0.05f));
    const float sqrtA = std::sqrt(a);
    const float a0 = (a + 1.0f) + (a - 1.0f) * cosw + 2.0f * sqrtA * alpha;
    b0 = a * ((a + 1.0f) - (a - 1.0f) * cosw + 2.0f * sqrtA * alpha) / a0;
    b1 = 2.0f * a * ((a - 1.0f) - (a + 1.0f) * cosw) / a0;
    b2 = a * ((a + 1.0f) - (a - 1.0f) * cosw - 2.0f * sqrtA * alpha) / a0;
    a1 = -2.0f * ((a - 1.0f) + (a + 1.0f) * cosw) / a0;
    a2 = ((a + 1.0f) + (a - 1.0f) * cosw - 2.0f * sqrtA * alpha) / a0;
  }

  void setHighShelf(float f0, float gainDb, float q, float sampleRate) noexcept {
    if (std::fabs(gainDb) < 1.0e-5f) {
      identity();
      return;
    }
    const float a = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * kPi * clamp(f0, 10.0f, sampleRate * 0.49f) /
                     sampleRate;
    const float cosw = std::cos(w0);
    const float sinw = std::sin(w0);
    const float alpha = sinw / (2.0f * std::max(q, 0.05f));
    const float sqrtA = std::sqrt(a);
    const float a0 = (a + 1.0f) - (a - 1.0f) * cosw + 2.0f * sqrtA * alpha;
    b0 = a * ((a + 1.0f) + (a - 1.0f) * cosw + 2.0f * sqrtA * alpha) / a0;
    b1 = -2.0f * a * ((a - 1.0f) + (a + 1.0f) * cosw) / a0;
    b2 = a * ((a + 1.0f) + (a - 1.0f) * cosw - 2.0f * sqrtA * alpha) / a0;
    a1 = 2.0f * ((a - 1.0f) - (a + 1.0f) * cosw) / a0;
    a2 = ((a + 1.0f) - (a - 1.0f) * cosw - 2.0f * sqrtA * alpha) / a0;
  }

  void identity() noexcept {
    b0 = 1.0f;
    b1 = b2 = a1 = a2 = 0.0f;
  }

  float magnitudeDb(float freq, float sampleRate) const noexcept {
    const float w = 2.0f * kPi * std::max(freq, 1.0f) / sampleRate;
    const std::complex<float> z1(std::cos(w), -std::sin(w));
    const std::complex<float> z2 = z1 * z1;
    const std::complex<float> num = b0 + b1 * z1 + b2 * z2;
    const std::complex<float> den = 1.0f + a1 * z1 + a2 * z2;
    return gainToDb(std::abs(num) / std::max(std::abs(den), 1.0e-12f));
  }
};

struct BiquadState {
  float x1 = 0.0f;
  float x2 = 0.0f;
  float y1 = 0.0f;
  float y2 = 0.0f;

  void reset() noexcept { x1 = x2 = y1 = y2 = 0.0f; }

  inline float process(float x, const BiquadCoeffs& c) noexcept {
    const float y = c.b0 * x + c.b1 * x1 + c.b2 * x2 - c.a1 * y1 - c.a2 * y2;
    x2 = x1;
    x1 = x;
    y2 = y1;
    y1 = y;
    if (std::fabs(y1) < 1.0e-20f) {
      y1 = 0.0f;
    }
    if (std::fabs(y2) < 1.0e-20f) {
      y2 = 0.0f;
    }
    return y;
  }
};

// Third-order Butterworth high-pass (18 dB/oct), the classic console sweep
// filter.  A first-order section is cascaded with a second-order section.
class Hpf18 {
public:
  void prepare(float sampleRate) noexcept {
    sampleRate_ = sampleRate;
    for (auto& p : p1_) {
      p.set(20.0f, sampleRate);
    }
    coeffs_.set(20.0f, 1.0f, sampleRate);
    for (auto& s : state_) {
      s.reset();
    }
    freq_.setTime(20.0f, sampleRate);
    mix_.setTime(20.0f, sampleRate);
    freq_.reset(20.0f);
    mix_.reset(0.0f);
  }

  // 0 Hz means "off"; any positive value engages the filter.
  void setTargets(float hz) noexcept {
    if (hz < 1.0f) {
      freq_.setTarget(20.0f);
      mix_.setTarget(0.0f);
    } else {
      freq_.setTarget(hz);
      mix_.setTarget(1.0f);
    }
  }

  void tick() noexcept {
    freq_.next();
    mix_.next();
  }

  void update() noexcept {
    const float f = freq_.current();
    for (auto& p : p1_) {
      p.set(f, sampleRate_);
    }
    coeffs_.set(f, 1.0f, sampleRate_);
  }

  inline float process(int channel, float x) noexcept {
    float lp, bp, hp;
    state_[channel].process(p1_[channel].hp(x), coeffs_, lp, bp, hp);
    const float m = mix_.current();
    return x + m * (hp - x);
  }

  void reset() noexcept {
    for (auto& p : p1_) {
      p.reset();
    }
    for (auto& s : state_) {
      s.reset();
    }
    freq_.snap();
    mix_.snap();
    update();
  }

private:
  float sampleRate_ = 48000.0f;
  OnePole p1_[2];
  SVFCoeffs coeffs_;
  SVFState state_[2];
  Smoothed freq_;
  Smoothed mix_;
};

// Second-order Butterworth low-pass (12 dB/oct).
class Lpf12 {
public:
  void prepare(float sampleRate) noexcept {
    sampleRate_ = sampleRate;
    coeffs_.set(20000.0f, 0.70710678f, sampleRate);
    for (auto& s : state_) {
      s.reset();
    }
    freq_.setTime(20.0f, sampleRate);
    mix_.setTime(20.0f, sampleRate);
    freq_.reset(20000.0f);
    mix_.reset(0.0f);
  }

  void setTargets(float hz) noexcept {
    if (hz < 1.0f) {
      freq_.setTarget(20000.0f);
      mix_.setTarget(0.0f);
    } else {
      freq_.setTarget(hz);
      mix_.setTarget(1.0f);
    }
  }

  void tick() noexcept {
    freq_.next();
    mix_.next();
  }

  void update() noexcept {
    coeffs_.set(freq_.current(), 0.70710678f, sampleRate_);
  }

  inline float process(int channel, float x) noexcept {
    float lp, bp, hp;
    state_[channel].process(x, coeffs_, lp, bp, hp);
    const float m = mix_.current();
    return x + m * (lp - x);
  }

  void reset() noexcept {
    for (auto& s : state_) {
      s.reset();
    }
    freq_.snap();
    mix_.snap();
    update();
  }

private:
  float sampleRate_ = 48000.0f;
  SVFCoeffs coeffs_;
  SVFState state_[2];
  Smoothed freq_;
  Smoothed mix_;
};

// Fixed-length circular delay.  Used for latency compensation and the
// spectral engine's dry path.
class DelayLine {
public:
  void prepare(uint32_t length) {
    length_ = std::max(length, 1u);
    buffer_.assign(length_, 0.0f);
    write_ = 0;
  }

  void reset() {
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    write_ = 0;
  }

  void setLength(uint32_t length) {
    if (length == length_) {
      return;
    }
    prepare(length);
  }

  uint32_t latency() const noexcept { return length_; }

  inline float process(float x) noexcept {
    const float y = buffer_[write_];
    buffer_[write_] = x;
    if (++write_ >= length_) {
      write_ = 0;
    }
    return y;
  }

private:
  std::vector<float> buffer_;
  uint32_t length_ = 1;
  uint32_t write_ = 0;
};

} // namespace trowel
