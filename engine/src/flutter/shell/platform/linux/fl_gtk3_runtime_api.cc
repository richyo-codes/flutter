// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/fl_gtk3_runtime_api.h"

#include <dlfcn.h>

FlGtk3RuntimeApi FlGtk3RuntimeApi::Resolve(Resolver resolver) {
  FlGtk3RuntimeApi api;
  api.add_frame_callback_surface = reinterpret_cast<FrameCallbackSurface>(
      resolver("gdk_wayland_window_add_frame_callback_surface"));
  api.remove_frame_callback_surface = reinterpret_cast<FrameCallbackSurface>(
      resolver("gdk_wayland_window_remove_frame_callback_surface"));
  if (!api.SupportsFrameCallbackSurfaces()) {
    return {};
  }
  return api;
}

bool FlGtk3RuntimeApi::SupportsFrameCallbackSurfaces() const {
  return add_frame_callback_surface != nullptr &&
         remove_frame_callback_surface != nullptr;
}

bool FlGtk3RuntimeApi::AddFrameCallbackSurface(GdkWindow* window,
                                               wl_surface* surface) const {
  if (!SupportsFrameCallbackSurfaces() || window == nullptr ||
      surface == nullptr) {
    return false;
  }
  add_frame_callback_surface(window, surface);
  return true;
}

void FlGtk3RuntimeApi::RemoveFrameCallbackSurface(GdkWindow* window,
                                                  wl_surface* surface) const {
  if (SupportsFrameCallbackSurfaces() && window != nullptr &&
      surface != nullptr) {
    remove_frame_callback_surface(window, surface);
  }
}

const FlGtk3RuntimeApi* fl_gtk3_runtime_api_get() {
  static const FlGtk3RuntimeApi api = FlGtk3RuntimeApi::Resolve(
      [](const char* name) { return dlsym(RTLD_DEFAULT, name); });
  return &api;
}
