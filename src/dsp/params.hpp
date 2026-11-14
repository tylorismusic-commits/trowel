// Trowel — params.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pultec.hpp"
#include "ssl_eq.hpp"

namespace trowel {

// Controls shared by the zero-latency and spectral Tame engines.
struct TameParams {
  float amount = 0.0f;       // 0..1
  float focus = 0.5f;        // 0..1, low-mid <-> high-mid
  float selectivity = 0.5f;  // 0..1, broad <-> surgical
  float sensitivity = 0.5f;  // 0..1, higher = more reduction
  float attackMs = 10.0f;
  float releaseMs = 250.0f;
  float mix = 1.0f;  // 0..1
  bool listen = false;
  bool msMode = false;  // Trowel Master only; ignored by the dynamic engine
};

struct Params {
  float inputGainDb = 0.0f;
  float outputGainDb = 0.0f;

  PultecParams pultec;
  SslParams ssl;
  TameParams tame;

  bool pultecOn = true;
  bool sslOn = true;
  bool tameOn = true;
  bool enabled = true;

  // 0: P,S,T   1: P,T,S   2: S,P,T
  // 3: S,T,P   4: T,P,S   5: T,S,P
  int routing = 0;

  // Trowel Master only.
  bool msMode = false;
  bool autoGain = false;
  bool delta = false;
};

} // namespace trowel
