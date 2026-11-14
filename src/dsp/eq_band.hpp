// Trowel — eq_band.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <complex>

#include "filters.hpp"
#include "smoother.hpp"
#include "util.hpp"

namespace trowel {

enum class EqShape {
  LowShelf,
  Bell,
  HighShelf,
};

// A single transparent EQ band built from a TPT state-variable filter.
//
// At 0 dB the band is mathematically transparent (a == 1), so a flat band
// adds no phase shift.  Shelves use the SVF low/high outputs with a
// configurable shelf Q, which lets Pultec's low boost be slightly resonant
// while the console shelves stay maximally flat.
class EqBand {
public:
  void prepare(float sampleRate, EqShape shape, float freq, float gainDb,
               float q, float shelfQ = 0.70710678f) noexcept {
    sampleRate_ = sampleRate;
    shape_ = shape;
    shelfQ_ = shelfQ;
    freq_.setTime(20.0f, sampleRate);
    gain_.setTime(20.0f, sampleRate);
    q_.setTime(20.0f, sampleRate);
    freq_.reset(freq);
    gain_.reset(dbToGain(gainDb));
    q_.reset(q);
    for (auto& s : bellState_) {
      s.reset();
    }
    for (auto& s : shelfState_) {
      s.reset();
    }
    update();
  }

  void setTargets(float freq, float gainDb, float q) noexcept {
    freq_.setTarget(freq);
    gain_.setTarget(dbToGain(gainDb));
    q_.setTarget(q);
  }

  void setTuning(float freq, float q) noexcept {
    freq_.setTarget(freq);
    q_.setTarget(q);
  }

  void setGainTargetDb(float db) noexcept { gain_.setTarget(dbToGain(db)); }

  void updateGainOnly() noexcept { a_ = gain_.current(); }

  void snapGain() noexcept {
    gain_.snap();
    a_ = gain_.current();
  }

  void setShelfQ(float q) noexcept { shelfQ_ = q; }

  void tick() noexcept {
    freq_.next();
    gain_.next();
    q_.next();
  }

  void update() noexcept {
    const float f = freq_.current();
    if (shape_ == EqShape::Bell) {
      bellCoeffs_.set(f, q_.current(), sampleRate_);
      a_ = gain_.current();
    } else if (shape_ == EqShape::LowShelf) {
      shelfCoeffs_.setLowShelf(f, gainToDb(gain_.current()), shelfQ_,
                               sampleRate_);
    } else {
      shelfCoeffs_.setHighShelf(f, gainToDb(gain_.current()), shelfQ_,
                                sampleRate_);
    }
  }

  inline float process(int channel, float x) noexcept {
    if (shape_ == EqShape::LowShelf || shape_ == EqShape::HighShelf) {
      return shelfState_[channel].process(x, shelfCoeffs_);
    }
    float lp, bp, hp;
    bellState_[channel].process(x, bellCoeffs_, lp, bp, hp);
    return x + (a_ - 1.0f) * bellCoeffs_.k * bp;
  }

  void reset() noexcept {
    for (auto& s : bellState_) {
      s.reset();
    }
    for (auto& s : shelfState_) {
      s.reset();
    }
    freq_.snap();
    gain_.snap();
    q_.snap();
    update();
  }

  // Approximate magnitude of the band at `freq` for the UI curve display.
  float magnitudeDb(float freq, float sampleRate) const noexcept {
    if (shape_ == EqShape::LowShelf || shape_ == EqShape::HighShelf) {
      return shelfCoeffs_.magnitudeDb(freq, sampleRate);
    }
    const float f0 = freq_.current();
    if (f0 < 1.0f || freq < 1.0f) {
      return 0.0f;
    }
    const float r = freq / f0;
    const float a = gain_.current();
    const float k = 1.0f / std::max(q_.current(), 0.05f);
    const std::complex<float> den(1.0f - r * r, k * r);
    const std::complex<float> bp(0.0f, r);
    const std::complex<float> h = 1.0f + (a - 1.0f) * k * (bp / den);
    return gainToDb(std::abs(h));
  }

  float frequency() const noexcept { return freq_.current(); }
  float gainDb() const noexcept { return gainToDb(gain_.current()); }
  float q() const noexcept { return q_.current(); }

private:
  float sampleRate_ = 48000.0f;
  EqShape shape_ = EqShape::Bell;
  SVFCoeffs bellCoeffs_;
  BiquadCoeffs shelfCoeffs_;
  SVFState bellState_[2];
  BiquadState shelfState_[2];
  Smoothed freq_;
  Smoothed gain_;
  Smoothed q_;
  float shelfQ_ = 0.70710678f;
  float a_ = 1.0f;
};

} // namespace trowel
