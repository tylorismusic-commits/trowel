// Trowel — fft.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <complex>
#include <cstdint>
#include <vector>

#include "util.hpp"

namespace trowel {

// Small in-place radix-2 complex FFT.  Fixed size, no allocation in the audio
// path.  The spectral Tame uses a 4096-point transform, which is plenty for a
// mastering resonance suppressor and keeps the dependency surface tiny.
class Fft {
public:
  explicit Fft(uint32_t size) : size_(size) {
    rev_.resize(size_);
    tw_.resize(size_ / 2);
    uint32_t bits = 0;
    while ((1u << bits) < size_) {
      ++bits;
    }
    for (uint32_t i = 0; i < size_; ++i) {
      uint32_t x = i;
      uint32_t r = 0;
      for (uint32_t b = 0; b < bits; ++b) {
        r = (r << 1) | (x & 1u);
        x >>= 1;
      }
      rev_[i] = r;
    }
    for (uint32_t k = 0; k < size_ / 2; ++k) {
      const float angle = -2.0f * kPi * static_cast<float>(k) /
                          static_cast<float>(size_);
      tw_[k] = std::complex<float>(std::cos(angle), std::sin(angle));
    }
  }

  uint32_t size() const noexcept { return size_; }

  void forward(std::complex<float>* a) const noexcept {
    for (uint32_t i = 0; i < size_; ++i) {
      const uint32_t j = rev_[i];
      if (j > i) {
        const std::complex<float> t = a[i];
        a[i] = a[j];
        a[j] = t;
      }
    }
    for (uint32_t len = 2; len <= size_; len <<= 1) {
      const uint32_t half = len >> 1;
      const uint32_t step = size_ / len;
      for (uint32_t i = 0; i < size_; i += len) {
        for (uint32_t k = 0; k < half; ++k) {
          const std::complex<float> u = a[i + k];
          const std::complex<float> v = a[i + k + half] * tw_[k * step];
          a[i + k] = u + v;
          a[i + k + half] = u - v;
        }
      }
    }
  }

  void inverse(std::complex<float>* a) const noexcept {
    for (uint32_t i = 0; i < size_; ++i) {
      a[i] = std::conj(a[i]);
    }
    forward(a);
    const float scale = 1.0f / static_cast<float>(size_);
    for (uint32_t i = 0; i < size_; ++i) {
      a[i] = std::conj(a[i]) * scale;
    }
  }

private:
  uint32_t size_ = 0;
  std::vector<uint32_t> rev_;
  std::vector<std::complex<float>> tw_;
};

} // namespace trowel
