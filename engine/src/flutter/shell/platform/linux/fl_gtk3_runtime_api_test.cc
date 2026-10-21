// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/fl_gtk3_runtime_api.h"

#include <cstring>

#include "gtest/gtest.h"

namespace {

void callback(GdkWindow*, wl_surface*) {}

TEST(FlGtk3RuntimeApiTest, MissingSymbols) {
  const auto api =
      FlGtk3RuntimeApi::Resolve([](const char*) -> void* { return nullptr; });
  EXPECT_FALSE(api.SupportsFrameCallbackSurfaces());
  EXPECT_FALSE(api.AddFrameCallbackSurface(nullptr, nullptr));
  api.RemoveFrameCallbackSurface(nullptr, nullptr);
}

TEST(FlGtk3RuntimeApiTest, PartialCapability) {
  for (const char* missing :
       {"gdk_wayland_window_add_frame_callback_surface",
        "gdk_wayland_window_remove_frame_callback_surface"}) {
    // A resolver without captures models each partial installation in turn.
    static const char* missing_symbol;
    missing_symbol = missing;
    const auto api = FlGtk3RuntimeApi::Resolve([](const char* name) -> void* {
      return strcmp(name, missing_symbol) == 0
                 ? nullptr
                 : reinterpret_cast<void*>(callback);
    });
    EXPECT_FALSE(api.SupportsFrameCallbackSurfaces());
    EXPECT_EQ(api.add_frame_callback_surface, nullptr);
    EXPECT_EQ(api.remove_frame_callback_surface, nullptr);
  }
}

TEST(FlGtk3RuntimeApiTest, CompleteCapability) {
  const auto api = FlGtk3RuntimeApi::Resolve(
      [](const char*) -> void* { return reinterpret_cast<void*>(callback); });
  EXPECT_TRUE(api.SupportsFrameCallbackSurfaces());
  EXPECT_EQ(api.add_frame_callback_surface, callback);
  EXPECT_EQ(api.remove_frame_callback_surface, callback);
}

TEST(FlGtk3RuntimeApiTest, DispatchAndNullArguments) {
  static int adds;
  static int removes;
  adds = removes = 0;
  // Opaque sentinels: callbacks check identity without accessing GTK or
  // Wayland.
  auto* window = reinterpret_cast<GdkWindow*>(0x1000);
  auto* surface = reinterpret_cast<wl_surface*>(0x2000);
  FlGtk3RuntimeApi api;
  api.add_frame_callback_surface = [](GdkWindow* w, wl_surface* s) {
    EXPECT_EQ(w, reinterpret_cast<GdkWindow*>(0x1000));
    EXPECT_EQ(s, reinterpret_cast<wl_surface*>(0x2000));
    adds++;
  };
  api.remove_frame_callback_surface = [](GdkWindow* w, wl_surface* s) {
    EXPECT_EQ(w, reinterpret_cast<GdkWindow*>(0x1000));
    EXPECT_EQ(s, reinterpret_cast<wl_surface*>(0x2000));
    removes++;
  };
  EXPECT_FALSE(api.AddFrameCallbackSurface(nullptr, surface));
  EXPECT_FALSE(api.AddFrameCallbackSurface(window, nullptr));
  api.RemoveFrameCallbackSurface(nullptr, surface);
  api.RemoveFrameCallbackSurface(window, nullptr);
  EXPECT_EQ(adds, 0);
  EXPECT_EQ(removes, 0);
  EXPECT_TRUE(api.AddFrameCallbackSurface(window, surface));
  api.RemoveFrameCallbackSurface(window, surface);
  EXPECT_EQ(adds, 1);
  EXPECT_EQ(removes, 1);
  FlGtk3RuntimeApi unavailable;
  EXPECT_FALSE(unavailable.AddFrameCallbackSurface(window, surface));
  unavailable.RemoveFrameCallbackSurface(window, surface);
  EXPECT_EQ(adds, 1);
  EXPECT_EQ(removes, 1);
}

TEST(FlGtk3RuntimeApiTest, StableProcessTable) {
  EXPECT_EQ(fl_gtk3_runtime_api_get(), fl_gtk3_runtime_api_get());
}

}  // namespace
