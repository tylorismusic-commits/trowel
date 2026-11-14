// Trowel — ssl_eq.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "eq_band.hpp"
#include "filters.hpp"

namespace trowel {

struct SslParams {
  float lfFreq = 60.0f;
  float lfGainDb = 0.0f;
  bool lfBell = false;
  float lmfFreq = 300.0f;
  float lmfGainDb = 0.0f;
  float hmfFreq = 3000.0f;
  float hmfGainDb = 0.0f;
  float hfFreq = 10000.0f;
  float hfGainDb = 0.0f;
  bool hfBell = false;
  float hpfFreq = 0.0f;
  float lpfFreq = 0.0f;
};

// SSL 4000E-inspired four-band console EQ with fixed Q and a switched
// high-pass / low-pass.  LF and HF run a shelf and a bell in parallel and
// crossfade between them, so the shape switch is click-free.
class SslEq {
public:
  void prepare(float sampleRate) noexcept {
    sampleRate_ = sampleRate;
    lfShelf_.prepare(sampleRate, EqShape::LowShelf, 60.0f, 0.0f, 0.707f,
                     0.70710678f);
    lfBell_.prepare(sampleRate, EqShape::Bell, 60.0f, 0.0f, 1.0f);
    lmf_.prepare(sampleRate, EqShape::Bell, 300.0f, 0.0f, 0.9f);
    hmf_.prepare(sampleRate, EqShape::Bell, 3000.0f, 0.0f, 0.9f);
    hfShelf_.prepare(sampleRate, EqShape::HighShelf, 10000.0f, 0.0f, 0.707f,
                     0.70710678f);
    hfBell_.prepare(sampleRate, EqShape::Bell, 10000.0f, 0.0f, 1.0f);
    hpf_.prepare(sampleRate);
    lpf_.prepare(sampleRate);

    lfShape_.setTime(15.0f, sampleRate);
    hfShape_.setTime(15.0f, sampleRate);
    lfShape_.reset(0.0f);
    hfShape_.reset(0.0f);
  }

  void setTargets(const SslParams& p) noexcept {
    lfShelf_.setTargets(p.lfFreq, p.lfGainDb, 0.707f);
    lfBell_.setTargets(p.lfFreq, p.lfGainDb, 1.0f);
    lmf_.setTargets(p.lmfFreq, p.lmfGainDb, 0.9f);
    hmf_.setTargets(p.hmfFreq, p.hmfGainDb, 0.9f);
    hfShelf_.setTargets(p.hfFreq, p.hfGainDb, 0.707f);
    hfBell_.setTargets(p.hfFreq, p.hfGainDb, 1.0f);
    hpf_.setTargets(p.hpfFreq);
    lpf_.setTargets(p.lpfFreq);
    lfShape_.setTarget(p.lfBell ? 1.0f : 0.0f);
    hfShape_.setTarget(p.hfBell ? 1.0f : 0.0f);
  }

  void tick() noexcept {
    lfShelf_.tick();
    lfBell_.tick();
    lmf_.tick();
    hmf_.tick();
    hfShelf_.tick();
    hfBell_.tick();
    hpf_.tick();
    lpf_.tick();
    lfShape_.next();
    hfShape_.next();
  }

  void update() noexcept {
    lfShelf_.update();
    lfBell_.update();
    lmf_.update();
    hmf_.update();
    hfShelf_.update();
    hfBell_.update();
    hpf_.update();
    lpf_.update();
  }

  inline float process(int channel, float x) noexcept {
    x = hpf_.process(channel, x);

    const float lf = lfShelf_.process(channel, x);
    const float lfB = lfBell_.process(channel, x);
    x = lf + lfShape_.current() * (lfB - lf);

    x = lmf_.process(channel, x);
    x = hmf_.process(channel, x);

    const float hf = hfShelf_.process(channel, x);
    const float hfB = hfBell_.process(channel, x);
    x = hf + hfShape_.current() * (hfB - hf);

    x = lpf_.process(channel, x);
    return x;
  }

  void reset() noexcept {
    lfShelf_.reset();
    lfBell_.reset();
    lmf_.reset();
    hmf_.reset();
    hfShelf_.reset();
    hfBell_.reset();
    hpf_.reset();
    lpf_.reset();
    lfShape_.snap();
    hfShape_.snap();
  }

  float magnitudeDb(float freq, float sampleRate) const noexcept {
    const float lfS = lfShelf_.magnitudeDb(freq, sampleRate);
    const float lfB = lfBell_.magnitudeDb(freq, sampleRate);
    const float hfS = hfShelf_.magnitudeDb(freq, sampleRate);
    const float hfB = hfBell_.magnitudeDb(freq, sampleRate);
    return lfS + lfShape_.target() * (lfB - lfS) +
           lmf_.magnitudeDb(freq, sampleRate) +
           hmf_.magnitudeDb(freq, sampleRate) + hfS +
           hfShape_.target() * (hfB - hfS);
  }

private:
  float sampleRate_ = 48000.0f;
  EqBand lfShelf_;
  EqBand lfBell_;
  EqBand lmf_;
  EqBand hmf_;
  EqBand hfShelf_;
  EqBand hfBell_;
  Hpf18 hpf_;
  Lpf12 lpf_;
  Smoothed lfShape_;
  Smoothed hfShape_;
};

} // namespace trowel
