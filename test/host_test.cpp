// Trowel — lilv host integration test
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <cstdio>

#include <lilv/lilv.h>

#include "inline_display.hpp"
#include "param_info.hpp"

using namespace trowel;

static int gFailures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    ++gFailures;
    std::printf("FAIL: %s\n", what);
  }
}

static void testPlugin(LilvWorld* world, const char* uriString,
                       uint32_t expectedPorts, float expectedLatency) {
  LilvNode* uri = lilv_new_uri(world, uriString);
  const LilvPlugins* plugins = lilv_world_get_all_plugins(world);
  const LilvPlugin* plugin = lilv_plugins_get_by_uri(plugins, uri);
  lilv_node_free(uri);
  check(plugin != nullptr, "plugin found");
  if (!plugin) {
    return;
  }

  const uint32_t numPorts = lilv_plugin_get_num_ports(plugin);
  std::printf("%s: %u ports (expected %u)\n", uriString, numPorts,
              expectedPorts);
  check(numPorts == expectedPorts, "port count matches RDF");

  LilvUIs* uis = lilv_plugin_get_uis(plugin);
  check(lilv_uis_size(uis) >= 1, "UI declared");
  lilv_uis_free(uis);

  LilvNode* presetClass =
      lilv_new_uri(world, "http://lv2plug.in/ns/ext/presets#Preset");
  LilvNodes* presets = lilv_plugin_get_related(plugin, presetClass);
  check(lilv_nodes_size(presets) >= 4, "factory presets found");
  lilv_nodes_free(presets);
  lilv_node_free(presetClass);

  LilvInstance* instance = lilv_plugin_instantiate(plugin, 48000.0, nullptr);
  check(instance != nullptr, "plugin instantiated");
  if (!instance) {
    return;
  }

  const uint32_t block = 256;
  float inL[block] = {};
  float inR[block] = {};
  float outL[block] = {};
  float outR[block] = {};
  float ctrl[PORT_COUNT] = {};

  for (uint32_t i = 0; i < numPorts && i < PORT_COUNT; ++i) {
    void* ptr = nullptr;
    if (i == PORT_AUDIO_IN_L) ptr = inL;
    else if (i == PORT_AUDIO_IN_R) ptr = inR;
    else if (i == PORT_AUDIO_OUT_L) ptr = outL;
    else if (i == PORT_AUDIO_OUT_R) ptr = outR;
    else ptr = &ctrl[i];
    lilv_instance_connect_port(instance, i, ptr);
  }

  // Flat, latency-only chain.
  ctrl[PORT_ENABLE] = 1.0f;
  ctrl[PORT_PULTEC_ON] = 0.0f;
  ctrl[PORT_SSL_ON] = 0.0f;
  ctrl[PORT_TAME_ON] = 0.0f;
  ctrl[PORT_INPUT_GAIN] = 0.0f;
  ctrl[PORT_OUTPUT_GAIN] = 0.0f;
  ctrl[PORT_ROUTING] = 0.0f;
  ctrl[PORT_TAME_AMOUNT] = 0.0f;
  ctrl[PORT_TAME_MIX] = 100.0f;

  lilv_instance_activate(instance);

  for (int b = 0; b < 48; ++b) {
    for (uint32_t i = 0; i < block; ++i) {
      inL[i] = 0.05f;
      inR[i] = 0.05f;
    }
    lilv_instance_run(instance, block);
  }

  std::printf("%s: latency %.0f, out %.4f, meter %.2f\n", uriString,
              static_cast<double>(ctrl[PORT_LATENCY]),
              static_cast<double>(outL[block - 1]),
              static_cast<double>(ctrl[PORT_METER_OUT]));
  check(std::fabs(ctrl[PORT_LATENCY] - expectedLatency) < 0.5f,
        "reported latency matches");
  check(std::fabs(outL[block - 1] - 0.05f) < 0.002f,
        "flat chain is near unity after latency");
  check(std::isfinite(ctrl[PORT_METER_OUT]), "output meter finite");
  check(std::isfinite(ctrl[PORT_SPECTRUM_0 + 8]), "spectrum telemetry finite");
  check(std::isfinite(ctrl[PORT_GAIN_0 + 8]), "gain telemetry finite");

  {
    const auto* iface = static_cast<const InlineDisplayInterface*>(
        lilv_instance_get_extension_data(instance,
                                         kInlineDisplayInterfaceURI));
    check(iface && iface->render, "inline display available");
    if (iface && iface->render) {
      auto* surface = iface->render(lilv_instance_get_handle(instance), 320, 40);
      check(surface && surface->width == 320 && surface->height == 40,
            "inline display renders");
    }
  }

  ctrl[PORT_ENABLE] = 0.0f;
  for (int b = 0; b < 48; ++b) {
    for (uint32_t i = 0; i < block; ++i) {
      inL[i] = 0.05f;
      inR[i] = 0.05f;
    }
    lilv_instance_run(instance, block);
  }
  check(std::fabs(outL[block - 1] - 0.05f) < 0.002f,
        "bypass passes the dry signal at the reported latency");

  lilv_instance_deactivate(instance);
  lilv_instance_free(instance);
}

int main(int argc, char** argv) {
  const char* bundleDir = (argc > 1) ? argv[1] : nullptr;
  if (!bundleDir) {
    std::printf("usage: %s <lv2-dir>\n", argv[0]);
    return 2;
  }

  LilvWorld* world = lilv_world_new();
  LilvNode* pathNode = lilv_new_string(world, bundleDir);
  lilv_world_set_option(world, LILV_OPTION_LV2_PATH, pathNode);
  lilv_node_free(pathNode);
  lilv_world_load_all(world);

  testPlugin(world, "https://github.com/tylorismusic-commits/trowel", 107, 0.0f);
  testPlugin(world, "https://github.com/tylorismusic-commits/trowel-master",
             110, 4095.0f);

  lilv_world_free(world);

  if (gFailures == 0) {
    std::printf("ALL HOST TESTS PASSED\n");
  }
  return gFailures == 0 ? 0 : 1;
}
