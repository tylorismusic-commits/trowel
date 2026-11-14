// Trowel — X11 UI embedding smoke test
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <cstring>
#include <string>
#include <dlfcn.h>
#include <unistd.h>

#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>

#include <lv2/core/lv2.h>
#include <lv2/ui/ui.h>

#include "param_info.hpp"

using namespace trowel;
using LV2UIDescriptorFn = const LV2UI_Descriptor* (*)(uint32_t);

static int gWrites = 0;

static void writeFn(LV2UI_Controller, uint32_t port, uint32_t size,
                    uint32_t protocol, const void* buffer) {
  (void)size;
  (void)protocol;
  ++gWrites;
  if (port < 4) {
    return;
  }
  std::printf("UI write: port=%u value=%.3f\n", port,
              buffer ? *static_cast<const float*>(buffer) : 0.0f);
}

int main(int argc, char** argv) {
  const char* uiPathArg = (argc > 1) ? argv[1] : nullptr;
  const char* outPath = (argc > 2) ? argv[2] : "/tmp/opencode/trowel-ui-live.png";
  if (!uiPathArg) {
    std::printf("usage: %s <trowelUI.so> [out.png]\n", argv[0]);
    return 2;
  }

  Display* display = XOpenDisplay(nullptr);
  if (!display) {
    std::printf("FAIL: cannot open X display\n");
    return 1;
  }

  const int width = 1400;
  const int height = 1000;
  Window parent = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0,
                                      width, height, 0, 0, 0x101216);
  XMapWindow(display, parent);
  XSync(display, False);

  void* dl = dlopen(uiPathArg, RTLD_NOW);
  if (!dl) {
    std::printf("FAIL: dlopen: %s\n", dlerror());
    return 1;
  }
  auto descriptorFn =
      reinterpret_cast<LV2UIDescriptorFn>(dlsym(dl, "lv2ui_descriptor"));
  if (!descriptorFn) {
    std::printf("FAIL: lv2ui_descriptor missing\n");
    return 1;
  }
  const LV2UI_Descriptor* descriptor = descriptorFn(0);
  if (!descriptor) {
    std::printf("FAIL: no UI descriptor\n");
    return 1;
  }
  std::printf("UI URI: %s\n", descriptor->URI);

  LV2_Feature parentFeature{LV2_UI__parent, &parent};
  const LV2_Feature* features[] = {&parentFeature, nullptr};

  std::string uiPath = uiPathArg;
  std::string bundleDir = uiPath.substr(0, uiPath.find_last_of('/') + 1);
  const char* pluginUri =
      uiPath.find("trowel-master") != std::string::npos
          ? "https://github.com/tylorismusic-commits/trowel-master"
          : "https://github.com/tylorismusic-commits/trowel";

  LV2UI_Widget widget = nullptr;
  LV2UI_Handle handle = descriptor->instantiate(
      descriptor, pluginUri, bundleDir.c_str(), &writeFn, nullptr, &widget,
      features);
  if (!handle) {
    std::printf("FAIL: instantiate\n");
    return 1;
  }
  std::printf("widget=%p\n", widget);

  float gr = -2.5f;
  float out = -12.0f;
  float latency = 0.0f;
  float inputGain = 4.0f;
  descriptor->port_event(handle, PORT_METER_GR, sizeof(float), 0, &gr);
  descriptor->port_event(handle, PORT_METER_OUT, sizeof(float), 0, &out);
  descriptor->port_event(handle, PORT_LATENCY, sizeof(float), 0, &latency);
  descriptor->port_event(handle, PORT_INPUT_GAIN, sizeof(float), 0, &inputGain);
  for (uint32_t i = 0; i < kTelemetryBins; ++i) {
    float spec = -30.0f - static_cast<float>(i);
    float gain = -1.0f;
    descriptor->port_event(handle, PORT_SPECTRUM_0 + i, sizeof(float), 0, &spec);
    descriptor->port_event(handle, PORT_GAIN_0 + i, sizeof(float), 0, &gain);
  }

  const LV2UI_Idle_Interface* idle = static_cast<const LV2UI_Idle_Interface*>(
      descriptor->extension_data(LV2_UI__idleInterface));
  if (!idle) {
    std::printf("FAIL: no idle interface\n");
    return 1;
  }
  for (int i = 0; i < 10; ++i) {
    idle->idle(handle);
    usleep(10000);
  }

  Window uiWindow = static_cast<Window>(reinterpret_cast<uintptr_t>(widget));

  // Use the logical size so the test coordinates are exact.
  const LV2UI_Resize* resizeInterface = static_cast<const LV2UI_Resize*>(
      descriptor->extension_data(LV2_UI__resize));
  if (resizeInterface && resizeInterface->ui_resize) {
    resizeInterface->ui_resize(handle, 1240, 920);
    for (int i = 0; i < 5; ++i) {
      idle->idle(handle);
      usleep(10000);
    }
  }
  XSync(display, False);

  const int writesBefore = gWrites;
  // Preset LCD cycles a preset and writes every control.
  XTestFakeMotionEvent(display, -1, 400, 42, CurrentTime);
  XTestFakeButtonEvent(display, Button1, True, CurrentTime);
  XTestFakeButtonEvent(display, Button1, False, CurrentTime);
  // Input trim drag.
  XTestFakeMotionEvent(display, -1, 1183, 438, CurrentTime);
  XTestFakeButtonEvent(display, Button1, True, CurrentTime);
  XTestFakeMotionEvent(display, -1, 1183, 400, CurrentTime);
  XTestFakeButtonEvent(display, Button1, False, CurrentTime);
  // Master bypass.
  XTestFakeMotionEvent(display, -1, 1183, 635, CurrentTime);
  XTestFakeButtonEvent(display, Button1, True, CurrentTime);
  XTestFakeButtonEvent(display, Button1, False, CurrentTime);
  // Tame amount drag.
  XTestFakeMotionEvent(display, -1, 838, 455, CurrentTime);
  XTestFakeButtonEvent(display, Button1, True, CurrentTime);
  XTestFakeMotionEvent(display, -1, 838, 410, CurrentTime);
  XTestFakeButtonEvent(display, Button1, False, CurrentTime);
  XFlush(display);
  for (int i = 0; i < 10; ++i) {
    idle->idle(handle);
    usleep(10000);
  }
  XSync(display, False);
  const int interactionWrites = gWrites - writesBefore;
  std::printf("interaction writes=%d\n", interactionWrites);
  if (interactionWrites < 10) {
    std::printf("FAIL: UI did not write parameters\n");
    return 1;
  }

  XWindowAttributes uiAttributes;
  XGetWindowAttributes(display, uiWindow, &uiAttributes);
  const int uiWidth = uiAttributes.width;
  const int uiHeight = uiAttributes.height;
  cairo_surface_t* xlib =
      cairo_xlib_surface_create(display, uiWindow, DefaultVisual(display, 0),
                                uiWidth, uiHeight);
  cairo_surface_t* image =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, uiWidth, uiHeight);
  cairo_t* cr = cairo_create(image);
  cairo_set_source_surface(cr, xlib, 0.0, 0.0);
  cairo_paint(cr);
  cairo_destroy(cr);
  cairo_surface_flush(image);
  const cairo_status_t status = cairo_surface_write_to_png(image, outPath);
  std::printf("capture status=%d path=%s writes=%d\n", static_cast<int>(status),
              outPath, gWrites);

  cairo_surface_destroy(image);
  cairo_surface_destroy(xlib);
  descriptor->cleanup(handle);
  dlclose(dl);
  XDestroyWindow(display, parent);
  XCloseDisplay(display);

  return (status == CAIRO_STATUS_SUCCESS && widget != nullptr) ? 0 : 1;
}
