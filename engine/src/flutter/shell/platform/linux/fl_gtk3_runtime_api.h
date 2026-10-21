// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_FL_GTK3_RUNTIME_API_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_FL_GTK3_RUNTIME_API_H_

#include <gdk/gdk.h>

struct wl_surface;

// Optional GTK3 APIs, resolved from the already loaded GDK. These signatures
// also compile with GTK 3.24.24 headers, before the declarations were added.
struct FlGtk3RuntimeApi {
  using FrameCallbackSurface = void (*)(GdkWindow*, wl_surface*);
  using Resolver = void* (*)(const char*);

  FrameCallbackSurface add_frame_callback_surface = nullptr;
  FrameCallbackSurface remove_frame_callback_surface = nullptr;

  // Both functions are required, including on distributions that backport
  // them without changing the reported GTK version. Resolver is injectable
  // so tests need not depend on the installed GTK version.
  static FlGtk3RuntimeApi Resolve(Resolver resolver);
  bool SupportsFrameCallbackSurfaces() const;

  // Call on the GTK thread with a Wayland window and a live child surface.
  // A successful Add must be balanced by Remove before destroying the child.
  // The caller owns the window and surface and prevents duplicate additions.
  bool AddFrameCallbackSurface(GdkWindow* window, wl_surface* surface) const;
  void RemoveFrameCallbackSurface(GdkWindow* window, wl_surface* surface) const;
};

// Initialized once using C++ thread-safe static initialization. Symbol presence
// determines capability; this does not load another GTK/GDK library.
const FlGtk3RuntimeApi* fl_gtk3_runtime_api_get();

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_FL_GTK3_RUNTIME_API_H_
