// Copyright 2014 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Native data contracts shared by the experimental Linux windowing bindings.
// Keep this file independent of dart:ui and native symbol resolution so that
// registration lifetimes can be tested in isolation.

import 'dart:ffi' as ffi;

import 'package:meta/meta.dart';

typedef _LinuxWindowNativeHandles = ({
  ffi.Pointer<ffi.Void> host,
  ffi.Pointer<ffi.Void> view,
  bool isWindow,
});

/// Borrowed native handles indexed by Flutter view ID.
///
/// A popover is a widget, not a GtkWindow. Replacing a registration must replace
/// all its handles so that a stale window cannot be used for a non-window host.
@internal
class LinuxWindowNativeRegistry {
  final Map<int, _LinuxWindowNativeHandles> _registrations = <int, _LinuxWindowNativeHandles>{};

  /// Registers a GtkWindow and its FlView, replacing any previous registration.
  void registerWindow({
    required int viewId,
    required ffi.Pointer<ffi.Void> windowHandle,
    required ffi.Pointer<ffi.Void> viewHandle,
  }) {
    assert(windowHandle != ffi.nullptr);
    assert(viewHandle != ffi.nullptr);
    _registrations[viewId] = (host: windowHandle, view: viewHandle, isWindow: true);
  }

  /// Registers a GtkPopover and its FlView without treating it as a GtkWindow.
  void registerPopover({
    required int viewId,
    required ffi.Pointer<ffi.Void> popoverHandle,
    required ffi.Pointer<ffi.Void> viewHandle,
  }) {
    assert(popoverHandle != ffi.nullptr);
    assert(viewHandle != ffi.nullptr);
    _registrations[viewId] = (host: popoverHandle, view: viewHandle, isWindow: false);
  }

  /// Removes borrowed handles without destroying the native objects.
  void unregister(int viewId) => _registrations.remove(viewId);

  /// Returns only hosts that were registered as GtkWindows.
  ffi.Pointer<ffi.Void>? windowHandleForViewId(int viewId) {
    final _LinuxWindowNativeHandles? registration = _registrations[viewId];
    return registration != null && registration.isWindow ? registration.host : null;
  }

  /// Returns the native host, which is not necessarily a GtkWindow.
  ffi.Pointer<ffi.Void>? hostHandleForViewId(int viewId) => _registrations[viewId]?.host;

  /// Returns the FlView for either kind of host.
  ffi.Pointer<ffi.Void>? viewHandleForViewId(int viewId) => _registrations[viewId]?.view;
}
