// Trowel — pultec.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "eq_band.hpp"
#include "iron.hpp"

namespace trowel {

struct PultecParams {
  float lowFreq = 60.0f;
  float lowBoostDb = 0.0f;
  float lowCutDb = 0.0f;
  float highFreq = 10000.0f;
  float highBoostDb = 0.0f;
  float bandwidth = 5.0f; // 0 = sharp, 10 = broad
  float highCutFreq = 10000.0f;
  float highCutDb = 0.0f;
  bool iron = false;
};

// EQP-1A-inspired broad EQ.  Low boost and low cut are separate filters with
// different shelf Qs, so boosting and cutting at the same frequency produces
// the famous resonant low bump and sub-bass roll-off instead of cancelling.
class PultecEq {
public:
  void prepare(float sampleRate) noexcept {
    sampleRate_ = sampleRate;
    // The low band is a broad resonant lift; combined with the octave-lower
    // cut shelf it produces the classic tight low bump and sub roll-off.
    lowBoost_.prepare(sampleRate, EqShape::Bell, 60.0f, 0.0f, 0.70f);
    lowCut_.prepare(sampleRate, EqShape::LowShelf, 30.0f, 0.0f, 0.707f, 0.70f);
    highBoost_.prepare(sampleRate, EqShape::Bell, 10000.0f, 0.0f, 1.0f);
    highCut_.prepare(sampleRate, EqShape::HighShelf, 10000.0f, 0.0f, 0.707f,
                     0.70f);
    iron_.prepare(sampleRate);
  }

  void setTargets(const PultecParams& p) noexcept {
    // The low cut sits an octave below the boost.  That is what turns the
    // boost + cut pair into the classic resonant low bump instead of a tilt.
    lowBoost_.setTargets(p.lowFreq, p.lowBoostDb, 0.70f);
    lowCut_.setTargets(p.lowFreq * 0.5f, p.lowCutDb, 0.707f);
    const float bw = clamp(p.bandwidth, 0.0f, 10.0f);
    const float q = 4.0f * std::pow(0.1f, bw * 0.1f);
    highBoost_.setTargets(p.highFreq, p.highBoostDb, q);
    highCut_.setTargets(p.highCutFreq, p.highCutDb, 0.707f);
    iron_.setTargets(p.iron);
  }

  void tick() noexcept {
    lowBoost_.tick();
    lowCut_.tick();
    highBoost_.tick();
    highCut_.tick();
  }

  void update() noexcept {
    lowBoost_.update();
    lowCut_.update();
    highBoost_.update();
    highCut_.update();
  }

  inline float process(int channel, float x) noexcept {
    x = lowBoost_.process(channel, x);
    x = lowCut_.process(channel, x);
    x = highBoost_.process(channel, x);
    x = highCut_.process(channel, x);
    return iron_.process(channel, x);
  }

  void reset() noexcept {
    lowBoost_.reset();
    lowCut_.reset();
    highBoost_.reset();
    highCut_.reset();
    iron_.reset();
  }

  float magnitudeDb(float freq, float sampleRate) const noexcept {
    return lowBoost_.magnitudeDb(freq, sampleRate) +
           lowCut_.magnitudeDb(freq, sampleRate) +
           highBoost_.magnitudeDb(freq, sampleRate) +
           highCut_.magnitudeDb(freq, sampleRate);
  }

private:
  float sampleRate_ = 48000.0f;
  EqBand lowBoost_;
  EqBand lowCut_;
  EqBand highBoost_;
  EqBand highCut_;
  IronStage iron_;
};

} // namespace trowel
