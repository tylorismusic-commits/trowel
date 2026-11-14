// Trowel — DSP tests
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "pultec.hpp"
#include "ssl_eq.hpp"
#include "tame_dynamic.hpp"
#include "tame_spectral.hpp"
#include "trowel_strip.hpp"

using namespace trowel;

static int gFailures = 0;

static void check(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what);
    ++gFailures;
  } else {
    std::printf("ok: %s\n", what);
  }
}

static Params bypassParams() {
  Params p;
  p.pultecOn = false;
  p.sslOn = false;
  p.tameOn = false;
  p.enabled = true;
  p.inputGainDb = 0.0f;
  p.outputGainDb = 0.0f;
  return p;
}

template <class Strip>
static void testTransparentImpulse(const char* name, Strip& strip,
                                   uint32_t expectedLatency) {
  const float sr = 48000.0f;
  strip.prepare(sr);
  Params p = bypassParams();
  strip.setTargets(p);
  strip.reset();

  const uint32_t total = 48000;
  std::vector<float> inL(total, 0.0f);
  std::vector<float> inR(total, 0.0f);
  std::vector<float> outL(total, 0.0f);
  std::vector<float> outR(total, 0.0f);
  const uint32_t impulseAt = 20000;
  inL[impulseAt] = 0.1f;
  inR[impulseAt] = 0.1f;

  const uint32_t block = 512;
  float gr = 0.0f;
  float outDb = 0.0f;
  for (uint32_t i = 0; i < total; i += block) {
    const uint32_t n = std::min(block, total - i);
    strip.process(inL.data() + i, inR.data() + i, outL.data() + i,
                  outR.data() + i, n, gr, outDb);
  }

  uint32_t peakAt = 0;
  float peak = 0.0f;
  for (uint32_t i = 0; i < total; ++i) {
    const float v = std::fabs(outL[i]);
    if (v > peak) {
      peak = v;
      peakAt = i;
    }
  }
  const uint32_t measured = peakAt - impulseAt;
  std::printf("%s: impulse latency %u (expected %u), peak %.4f\n", name,
              measured, expectedLatency, static_cast<double>(peak));
  check(measured == expectedLatency, "impulse latency matches");
  check(std::fabs(peak - 0.1f) < 0.005f, "impulse amplitude preserved");
}

template <class Strip>
static void testSineTransparency(const char* name, Strip& strip,
                                 uint32_t latency) {
  const float sr = 48000.0f;
  strip.prepare(sr);
  Params p = bypassParams();
  strip.setTargets(p);
  strip.reset();

  const uint32_t total = 96000;
  std::vector<float> inL(total), inR(total), outL(total), outR(total);
  for (uint32_t i = 0; i < total; ++i) {
    const float s = 0.1f * std::sin(2.0f * kPi * 1000.0f * i / sr);
    inL[i] = s;
    inR[i] = s;
  }
  const uint32_t block = 512;
  float gr = 0.0f;
  float outDb = 0.0f;
  for (uint32_t i = 0; i < total; i += block) {
    const uint32_t n = std::min(block, total - i);
    strip.process(inL.data() + i, inR.data() + i, outL.data() + i,
                  outR.data() + i, n, gr, outDb);
  }

  double sum = 0.0;
  uint32_t count = 0;
  for (uint32_t i = 48000; i < total; ++i) {
    const float ref = inL[i - latency];
    const float d = outL[i] - ref;
    sum += d * d;
    ++count;
  }
  const double rms = std::sqrt(sum / std::max(count, 1u));
  std::printf("%s: transparent-sine error %.8f\n", name, rms);
  check(rms < 2.0e-3, "bypass sine is transparent");
}

static void testPultec() {
  PultecEq eq;
  eq.prepare(48000.0f);
  PultecParams p;
  p.lowFreq = 60.0f;
  p.lowBoostDb = 10.0f;
  p.lowCutDb = -10.0f;
  p.highFreq = 10000.0f;
  p.highBoostDb = 0.0f;
  p.highCutDb = 0.0f;
  eq.setTargets(p);
  eq.reset();

  const float at30 = eq.magnitudeDb(30.0f, 48000.0f);
  const float at60 = eq.magnitudeDb(60.0f, 48000.0f);
  const float at120 = eq.magnitudeDb(120.0f, 48000.0f);
  const float at1k = eq.magnitudeDb(1000.0f, 48000.0f);
  std::printf("pultec low boost+cut: 30Hz %.2f dB, 60Hz %.2f dB, 120Hz %.2f dB, 1k %.2f dB\n",
              at30, at60, at120, at1k);
  check(at60 > at120 + 0.5f, "pultec low bump sits above 120 Hz");
  check(at60 > at30 + 0.5f, "pultec low bump sits above 30 Hz");
  check(std::fabs(at1k) < 0.5f, "pultec low band is flat at 1 kHz");

  PultecParams flat;
  eq.setTargets(flat);
  eq.reset();
  check(std::fabs(eq.magnitudeDb(60.0f, 48000.0f)) < 0.01f,
        "pultec flat is transparent");
}

static void testSsl() {
  SslEq eq;
  eq.prepare(48000.0f);
  SslParams p;
  p.lfFreq = 100.0f;
  p.lfGainDb = 10.0f;
  p.hfFreq = 10000.0f;
  p.hfGainDb = -10.0f;
  eq.setTargets(p);
  eq.reset();

  const float lf = eq.magnitudeDb(100.0f, 48000.0f);
  const float mid = eq.magnitudeDb(1000.0f, 48000.0f);
  const float hf = eq.magnitudeDb(10000.0f, 48000.0f);
  std::printf("ssl: 100Hz %.2f dB, 1k %.2f dB, 10k %.2f dB\n", lf, mid, hf);
  check(lf > 4.5f, "ssl LF shelf boosts");
  check(hf < -4.5f, "ssl HF shelf cuts");
  check(std::fabs(mid) < 1.0f, "ssl mid is flat");
}

static void testDynamicIdle() {
  TameDynamic tame;
  tame.prepare(48000.0f);
  TameParams p;
  p.amount = 0.0f;
  tame.setTargets(p);
  tame.reset();

  std::vector<float> l(48000), r(48000);
  for (uint32_t i = 0; i < l.size(); ++i) {
    l[i] = 0.1f * std::sin(2.0f * kPi * 1000.0f * i / 48000.0f);
    r[i] = l[i];
  }
  std::vector<float> orig = l;
  tame.process(l.data(), r.data(), static_cast<uint32_t>(l.size()));
  float maxError = 0.0f;
  for (uint32_t i = 0; i < l.size(); ++i) {
    maxError = std::max(maxError, std::fabs(l[i] - orig[i]));
  }
  std::printf("dynamic tame idle error %.8f\n", static_cast<double>(maxError));
  check(maxError < 1.0e-6f, "dynamic tame is transparent at amount 0");
}

static void testDynamicReduction() {
  TameDynamic tame;
  tame.prepare(48000.0f);
  TameParams p;
  p.amount = 1.0f;
  p.focus = 0.5f;
  p.selectivity = 0.7f;
  p.sensitivity = 0.7f;
  p.attackMs = 10.0f;
  p.releaseMs = 200.0f;
  p.mix = 1.0f;
  tame.setTargets(p);
  tame.reset();

  std::vector<float> l(96000), r(96000);
  for (uint32_t i = 0; i < l.size(); ++i) {
    l[i] = 0.1f * std::sin(2.0f * kPi * 3000.0f * i / 48000.0f);
    r[i] = l[i];
  }
  tame.process(l.data(), r.data(), static_cast<uint32_t>(l.size()));
  float peak = 0.0f;
  bool finite = true;
  for (float v : l) {
    if (!std::isfinite(v)) finite = false;
    peak = std::max(peak, std::fabs(v));
  }
  std::printf("dynamic tame reduction %.2f dB, peak %.4f\n",
              tame.lastMaxReductionDb(), static_cast<double>(peak));
  check(finite, "dynamic tame output is finite");
  check(tame.lastMaxReductionDb() > 0.5f, "dynamic tame reduces a narrow tone");
}

static void testSpectralLatency() {
  TameSpectral tame;
  tame.prepare(48000.0f);
  TameParams p;
  p.amount = 0.0f;
  tame.setTargets(p);
  tame.reset();

  const uint32_t total = 48000;
  std::vector<float> l(total, 0.0f), r(total, 0.0f);
  const uint32_t impulseAt = 20000;
  l[impulseAt] = 0.1f;
  r[impulseAt] = 0.1f;
  tame.process(l.data(), r.data(), total);

  uint32_t peakAt = 0;
  float peak = 0.0f;
  for (uint32_t i = 0; i < total; ++i) {
    if (std::fabs(l[i]) > peak) {
      peak = std::fabs(l[i]);
      peakAt = i;
    }
  }
  std::printf("spectral tame latency %u (expected %u), peak %.4f\n",
              peakAt - impulseAt, TameSpectral::kLatency,
              static_cast<double>(peak));
  check(peakAt - impulseAt == TameSpectral::kLatency,
        "spectral tame latency matches");
  check(std::fabs(peak - 0.1f) < 0.01f, "spectral tame impulse amplitude");
}

int main() {
  {
    TrowelStrip<TameDynamic, false> strip;
    testTransparentImpulse("zero-latency strip", strip, 0);
    testSineTransparency("zero-latency strip", strip, 0);
  }
  {
    TrowelStrip<TameSpectral, true> strip;
    testTransparentImpulse("master strip", strip, TameSpectral::kLatency);
    testSineTransparency("master strip", strip, TameSpectral::kLatency);
  }
  testPultec();
  testSsl();
  testDynamicIdle();
  testDynamicReduction();
  testSpectralLatency();

  std::printf("\n%s (%d failure%s)\n", gFailures == 0 ? "PASS" : "FAIL",
              gFailures, gFailures == 1 ? "" : "s");
  return gFailures == 0 ? 0 : 1;
}
