// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Run separately from flutter_linux_unittests: real GTK and Wayland resources
// are essential for checking hide/unrealize ordering and native lifetimes.
#include "flutter/shell/platform/linux/testing/linux_test.h"

#include <dlfcn.h>
#include <gdk/gdkwayland.h>
#include <wayland-client.h>

#include <map>
#include <utility>

#include "flutter/shell/platform/linux/fl_gtk3_runtime_api.h"
#include "flutter/shell/platform/linux/fl_view_renderer_subsurface.h"

namespace {
using Pair = std::pair<GdkWindow*, wl_surface*>;
std::map<Pair, uint32_t> registrations;
int additions = 0;
int removals = 0;
GThread* gtk_thread = nullptr;
}  // namespace

// Interpose only the optional APIs. Resolve forwards to the real GDK, while
// recording the exact pair and verifying removal before child destruction.
extern "C" G_MODULE_EXPORT void gdk_wayland_window_add_frame_callback_surface(
    GdkWindow* window,
    wl_surface* surface) {
  EXPECT_EQ(g_thread_self(), gtk_thread);
  EXPECT_TRUE(GDK_IS_WAYLAND_WINDOW(window));
  EXPECT_NE(gdk_wayland_window_get_wl_surface(window), nullptr);
  const Pair pair(window, surface);
  EXPECT_EQ(registrations.count(pair), 0u);
  registrations[pair] = wl_proxy_get_id(reinterpret_cast<wl_proxy*>(surface));
  additions++;
  auto fn = reinterpret_cast<FlGtk3RuntimeApi::FrameCallbackSurface>(
      dlsym(RTLD_NEXT, "gdk_wayland_window_add_frame_callback_surface"));
  ASSERT_NE(fn, nullptr);
  fn(window, surface);
}

extern "C" G_MODULE_EXPORT void
gdk_wayland_window_remove_frame_callback_surface(GdkWindow* window,
                                                 wl_surface* surface) {
  EXPECT_EQ(g_thread_self(), gtk_thread);
  const Pair pair(window, surface);
  ASSERT_EQ(registrations.count(pair), 1u);
  EXPECT_EQ(registrations[pair],
            wl_proxy_get_id(reinterpret_cast<wl_proxy*>(surface)));
  registrations.erase(pair);
  removals++;
  auto fn = reinterpret_cast<FlGtk3RuntimeApi::FrameCallbackSurface>(
      dlsym(RTLD_NEXT, "gdk_wayland_window_remove_frame_callback_surface"));
  ASSERT_NE(fn, nullptr);
  fn(window, surface);
}

namespace {
class FlSubsurfaceFrameCallbacksTest : public flutter::testing::LinuxTest {
 protected:
  void SetUp() override {
    gtk_thread = g_thread_self();
    additions = removals = 0;
    ASSERT_TRUE(registrations.empty());
    if (!GDK_IS_WAYLAND_DISPLAY(gdk_display_get_default())) {
      return;
    }
    if (dlsym(RTLD_NEXT, "gdk_wayland_window_add_frame_callback_surface") ==
            nullptr ||
        dlsym(RTLD_NEXT, "gdk_wayland_window_remove_frame_callback_surface") ==
            nullptr) {
      GTEST_SKIP()
          << "Native lifecycle tests require GTK >= 3.24.25 or a backport";
    }
    StartEngine();
  }

  void TearDown() override {
    if (window != nullptr) {
      gtk_widget_destroy(window);
      g_clear_object(&window);
    }
    g_clear_object(&renderer);
    EXPECT_TRUE(registrations.empty());
    EXPECT_EQ(additions, removals);
    LinuxTest::TearDown();
  }

  void Show() {
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    g_object_ref_sink(window);
    gtk_window_set_default_size(GTK_WINDOW(window), 240, 160);
    renderer = fl_view_renderer_subsurface_new(engine, TRUE);
    g_object_ref_sink(renderer);
    gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(renderer));
    gtk_widget_show_all(window);
  }

  bool IsWayland() { return GDK_IS_WAYLAND_DISPLAY(gdk_display_get_default()); }

  GtkWidget* window = nullptr;
  FlViewRendererSubsurface* renderer = nullptr;
};

TEST_F(FlSubsurfaceFrameCallbacksTest, Disabled) {
  if (g_strcmp0(g_getenv("FLUTTER_GTK3_WAYLAND_FRAME_CALLBACKS"), "1") == 0) {
    GTEST_SKIP() << "Run with the experiment disabled";
  }
  Show();
  EXPECT_EQ(additions, 0);
}

TEST_F(FlSubsurfaceFrameCallbacksTest, OtherBackend) {
  if (IsWayland()) {
    GTEST_SKIP() << "Run under X11 to check the fallback";
  }
  Show();
  EXPECT_EQ(additions, 0);
}

TEST_F(FlSubsurfaceFrameCallbacksTest, HideShowReplacesParent) {
  if (!IsWayland() ||
      g_strcmp0(g_getenv("FLUTTER_GTK3_WAYLAND_FRAME_CALLBACKS"), "1") != 0)
    GTEST_SKIP();
  Show();
  ASSERT_EQ(additions, 1);
  for (int i = 0; i < 3; i++) {
    gtk_widget_hide(window);
    EXPECT_TRUE(registrations.empty());
    EXPECT_EQ(additions, removals);
    gtk_widget_show_all(window);
    // GTK can defer recreating the parent's native surface until its next
    // allocation. Exercise that main-loop work, with a bounded wait.
    const gint64 deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND;
    while (registrations.empty() && g_get_monotonic_time() < deadline) {
      g_main_context_iteration(nullptr, FALSE);
      g_usleep(1000);
    }
    EXPECT_EQ(registrations.size(), 1u);
    EXPECT_EQ(additions, i + 2);
  }
}

TEST_F(FlSubsurfaceFrameCallbacksTest, HideChildReusesParent) {
  if (!IsWayland() ||
      g_strcmp0(g_getenv("FLUTTER_GTK3_WAYLAND_FRAME_CALLBACKS"), "1") != 0)
    GTEST_SKIP();
  Show();
  ASSERT_EQ(additions, 1);
  const Pair pair = registrations.begin()->first;
  gtk_widget_hide(GTK_WIDGET(renderer));
  EXPECT_TRUE(registrations.empty());
  gtk_widget_show(GTK_WIDGET(renderer));
  ASSERT_EQ(additions, 2);
  EXPECT_EQ(registrations.begin()->first, pair);
}

TEST_F(FlSubsurfaceFrameCallbacksTest, UnrealizeRealize) {
  if (!IsWayland() ||
      g_strcmp0(g_getenv("FLUTTER_GTK3_WAYLAND_FRAME_CALLBACKS"), "1") != 0)
    GTEST_SKIP();
  Show();
  ASSERT_EQ(additions, 1);
  gtk_widget_unrealize(window);
  EXPECT_TRUE(registrations.empty());
  gtk_widget_realize(window);
  gtk_widget_map(window);
  EXPECT_EQ(additions, 2);
  EXPECT_EQ(registrations.size(), 1u);
}

TEST_F(FlSubsurfaceFrameCallbacksTest, ResizeDoesNotDuplicate) {
  if (!IsWayland() ||
      g_strcmp0(g_getenv("FLUTTER_GTK3_WAYLAND_FRAME_CALLBACKS"), "1") != 0)
    GTEST_SKIP();
  Show();
  ASSERT_EQ(additions, 1);
  GtkAllocation allocation = {0, 0, 320, 240};
  gtk_widget_size_allocate(GTK_WIDGET(renderer), &allocation);
  allocation.width = 480;
  gtk_widget_size_allocate(GTK_WIDGET(renderer), &allocation);
  EXPECT_EQ(additions, 1);
  EXPECT_EQ(removals, 0);
}

TEST_F(FlSubsurfaceFrameCallbacksTest, DestroyThenPresent) {
  if (!IsWayland() ||
      g_strcmp0(g_getenv("FLUTTER_GTK3_WAYLAND_FRAME_CALLBACKS"), "1") != 0)
    GTEST_SKIP();
  Show();
  ASSERT_EQ(additions, 1);
  gtk_widget_destroy(window);
  EXPECT_TRUE(registrations.empty());
  fl_view_renderer_present_layers(FL_VIEW_RENDERER(renderer), nullptr, 0);
  // Repeated disposal must not unregister twice or clear a live mutex.
  g_object_run_dispose(G_OBJECT(renderer));
  EXPECT_EQ(removals, 1);
}

TEST_F(FlSubsurfaceFrameCallbacksTest, IndependentViews) {
  if (!IsWayland() ||
      g_strcmp0(g_getenv("FLUTTER_GTK3_WAYLAND_FRAME_CALLBACKS"), "1") != 0)
    GTEST_SKIP();
  Show();
  GtkWidget* second_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  auto* second_renderer = fl_view_renderer_subsurface_new(engine, TRUE);
  gtk_container_add(GTK_CONTAINER(second_window), GTK_WIDGET(second_renderer));
  gtk_widget_show_all(second_window);
  EXPECT_EQ(registrations.size(), 2u);
  gtk_widget_destroy(second_window);
  EXPECT_EQ(registrations.size(), 1u);
  EXPECT_EQ(removals, 1);
}
}  // namespace
