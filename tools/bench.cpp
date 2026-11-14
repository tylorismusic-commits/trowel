#include "tame_dynamic.hpp"
#include "tame_spectral.hpp"
#include <chrono>
#include <cstdio>
#include <vector>
#include "trowel_strip.hpp"
using namespace trowel;
template <class Strip>
static void bench(const char* name, float sr) {
  Strip strip; strip.prepare(sr);
  Params p; p.tame.amount = 60; p.tame.mix = 1;
  strip.setTargets(p); strip.reset();
  const uint32_t total = static_cast<uint32_t>(sr * 20.0f);
  std::vector<float> l(512), r(512);
  for (auto& v : l) v = 0.1f;
  for (auto& v : r) v = 0.1f;
  float gr = 0, out = 0;
  auto t0 = std::chrono::steady_clock::now();
  for (uint32_t i = 0; i < total; i += 512) strip.process(l.data(), r.data(), l.data(), r.data(), 512, gr, out);
  auto t1 = std::chrono::steady_clock::now();
  double sec = std::chrono::duration<double>(t1 - t0).count();
  std::printf("%s @ %.0f Hz: %.2f s for 20 s audio (%.1f%% of one core)\n", name, sr, sec, 100.0 * sec / 20.0);
}
int main() {
  bench<TrowelStrip<TameDynamic, false>>("Trowel", 48000);
  bench<TrowelStrip<TameSpectral, true>>("Trowel Master", 48000);
  bench<TrowelStrip<TameDynamic, false>>("Trowel", 192000);
  bench<TrowelStrip<TameSpectral, true>>("Trowel Master", 192000);
}
