// Trowel — CLAP host smoke test
// SPDX-License-Identifier: GPL-3.0-or-later
#include <clap/all.h>
#include <X11/Xlib.h>
#include <dlfcn.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

static int gFailures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    ++gFailures;
    std::printf("FAIL: %s\n", what);
  }
}

static const void* hostGetExtension(const clap_host_t*, const char*) {
  return nullptr;
}
static void hostNoop(const clap_host_t*) {}

struct EventList {
  clap_input_events_t in;
  clap_output_events_t out;
};

static uint32_t inSize(const clap_input_events_t*) { return 0; }
static const clap_event_header_t* inGet(const clap_input_events_t*, uint32_t) {
  return nullptr;
}
static bool outPush(const clap_output_events_t*, const clap_event_header_t*) {
  return true;
}

int main(int argc, char** argv) {
  const char* pluginPath = (argc > 1) ? argv[1] : nullptr;
  const char* pluginId = (argc > 2) ? argv[2] : nullptr;
  if (!pluginPath || !pluginId) {
    std::printf("usage: %s <plugin.clap> <plugin-id>\n", argv[0]);
    return 2;
  }
  const bool mastering = std::strstr(pluginId, "master") != nullptr;

  void* dl = dlopen(pluginPath, RTLD_NOW | RTLD_LOCAL);
  check(dl != nullptr, "CLAP loaded");
  if (!dl) {
    return 1;
  }
  auto entry = static_cast<const clap_plugin_entry_t*>(dlsym(dl, "clap_entry"));
  check(entry != nullptr, "clap_entry exported");
  if (!entry) {
    return 1;
  }
  check(entry->init(pluginPath), "CLAP init");

  auto factory = static_cast<const clap_plugin_factory_t*>(
      entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
  check(factory != nullptr, "plugin factory available");
  if (!factory) {
    return 1;
  }

  clap_host_t host{};
  host.clap_version = CLAP_VERSION;
  host.name = "smoke-test";
  host.vendor = "test";
  host.url = "";
  host.version = "1";
  host.get_extension = &hostGetExtension;
  host.request_restart = &hostNoop;
  host.request_process = &hostNoop;
  host.request_callback = &hostNoop;

  const clap_plugin_t* plugin = factory->create_plugin(factory, &host, pluginId);
  check(plugin != nullptr, "plugin created");
  if (!plugin) {
    return 1;
  }
  check(plugin->init(plugin), "plugin initialized");
  check(plugin->activate(plugin, 48000.0, 16, 2048), "plugin activated");

  const auto* gain = static_cast<const clap_plugin_gain_adjustment_metering_t*>(
      plugin->get_extension(plugin, CLAP_EXT_GAIN_ADJUSTMENT_METERING));
  check(gain && gain->get, "gain metering extension available");

  const auto* latency = static_cast<const clap_plugin_latency_t*>(
      plugin->get_extension(plugin, CLAP_EXT_LATENCY));
  const uint32_t expectedLatency = mastering ? 4095u : 0u;
  check(latency && latency->get &&
            latency->get(plugin) == expectedLatency,
        "reported latency matches the variant");

  const uint32_t frames = 2048;
  std::vector<float> inL(frames), inR(frames), outL(frames), outR(frames);
  for (uint32_t i = 0; i < frames; ++i) {
    inL[i] = inR[i] =
        0.5f * std::sin(2.0f * 3.14159265f * 1000.0f * i / 48000.0f);
  }
  float* inChannels[2] = {inL.data(), inR.data()};
  float* outChannels[2] = {outL.data(), outR.data()};
  clap_audio_buffer_t audioIn{};
  audioIn.data32 = inChannels;
  audioIn.channel_count = 2;
  clap_audio_buffer_t audioOut{};
  audioOut.data32 = outChannels;
  audioOut.channel_count = 2;

  EventList events{};
  events.in.ctx = nullptr;
  events.in.size = &inSize;
  events.in.get = &inGet;
  events.out.ctx = nullptr;
  events.out.try_push = &outPush;

  clap_process_t process{};
  process.frames_count = frames;
  process.audio_inputs = &audioIn;
  process.audio_inputs_count = 1;
  process.audio_outputs = &audioOut;
  process.audio_outputs_count = 1;
  process.in_events = &events.in;
  process.out_events = &events.out;

  for (int block = 0; block < 8; ++block) {
    plugin->process(plugin, &process);
  }
  check(std::isfinite(gain ? gain->get(plugin) : 0.0),
        "gain metering value is finite");
  bool finite = true;
  for (uint32_t i = 0; i < frames; ++i) {
    finite = finite && std::isfinite(outL[i]) && std::isfinite(outR[i]);
  }
  check(finite, "processed audio is finite");

  const auto* gui =
      static_cast<const clap_plugin_gui_t*>(plugin->get_extension(plugin,
                                                                  CLAP_EXT_GUI));
  check(gui != nullptr, "GUI extension available");
  if (gui && gui->create(plugin, CLAP_WINDOW_API_X11, false)) {
    Display* display = XOpenDisplay(nullptr);
    if (display) {
      uint32_t width = 0, height = 0;
      gui->get_size(plugin, &width, &height);
      Window parent = XCreateSimpleWindow(display, DefaultRootWindow(display),
                                          0, 0, width, height, 0, 0, 0);
      XMapWindow(display, parent);
      XSync(display, False);
      clap_window_t win{};
      win.api = CLAP_WINDOW_API_X11;
      win.x11 = parent;
      check(gui->set_parent(plugin, &win), "GUI parent accepted");
      check(gui->show(plugin), "GUI shown");
      usleep(300000);
      Window root, parentWindow, *children = nullptr;
      unsigned int count = 0;
      XQueryTree(display, parent, &root, &parentWindow, &children, &count);
      check(count > 0, "GUI window embedded");
      if (children) {
        XFree(children);
      }
      gui->hide(plugin);
      gui->destroy(plugin);
      XDestroyWindow(display, parent);
      XCloseDisplay(display);
    }
  }

  plugin->deactivate(plugin);
  plugin->destroy(plugin);
  entry->deinit();
  dlclose(dl);

  if (gFailures == 0) {
    std::printf("ALL CLAP TESTS PASSED\n");
  }
  return gFailures == 0 ? 0 : 1;
}
