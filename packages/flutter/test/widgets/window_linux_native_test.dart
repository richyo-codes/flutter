// Copyright 2014 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ffi' as ffi;

import 'package:flutter/src/widgets/_window_linux_native.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  final window = ffi.Pointer<ffi.Void>.fromAddress(1);
  final popover = ffi.Pointer<ffi.Void>.fromAddress(2);
  final view = ffi.Pointer<ffi.Void>.fromAddress(3);
  final replacementView = ffi.Pointer<ffi.Void>.fromAddress(4);

  test('legacy window registration exposes the window and view', () {
    final registry = LinuxWindowNativeRegistry();
    registry.registerWindow(viewId: 10, windowHandle: window, viewHandle: view);
    expect(registry.windowHandleForViewId(10), window);
    expect(registry.hostHandleForViewId(10), window);
    expect(registry.viewHandleForViewId(10), view);
    expect(registry.windowHandleForViewId(11), isNull);
    expect(registry.viewHandleForViewId(11), isNull);
  });

  test('popover registration never exposes its host as a GtkWindow', () {
    final registry = LinuxWindowNativeRegistry();
    registry.registerPopover(viewId: 10, popoverHandle: popover, viewHandle: view);
    expect(registry.windowHandleForViewId(10), isNull);
    expect(registry.hostHandleForViewId(10), popover);
    expect(registry.viewHandleForViewId(10), view);
  });

  test('replacing a window with a popover removes the stale window', () {
    final registry = LinuxWindowNativeRegistry();
    registry.registerWindow(viewId: 10, windowHandle: window, viewHandle: view);
    registry.registerWindow(viewId: 11, windowHandle: window, viewHandle: view);
    registry.registerPopover(viewId: 10, popoverHandle: popover, viewHandle: replacementView);
    expect(registry.windowHandleForViewId(10), isNull);
    expect(registry.hostHandleForViewId(10), popover);
    expect(registry.viewHandleForViewId(10), replacementView);
    expect(registry.windowHandleForViewId(11), window);
  });

  test('a window can replace a popover registration', () {
    final registry = LinuxWindowNativeRegistry();
    registry.registerPopover(viewId: 10, popoverHandle: popover, viewHandle: view);
    registry.registerWindow(viewId: 10, windowHandle: window, viewHandle: replacementView);
    expect(registry.windowHandleForViewId(10), window);
    expect(registry.hostHandleForViewId(10), window);
    expect(registry.viewHandleForViewId(10), replacementView);
  });

  test('unregister removes all handles and tolerates unknown view IDs', () {
    final registry = LinuxWindowNativeRegistry();
    registry.registerWindow(viewId: 10, windowHandle: window, viewHandle: view);
    registry.registerPopover(viewId: 11, popoverHandle: popover, viewHandle: view);
    for (final viewId in <int>[10, 11, 12]) {
      registry.unregister(viewId);
      registry.unregister(viewId);
      expect(registry.windowHandleForViewId(viewId), isNull);
      expect(registry.hostHandleForViewId(viewId), isNull);
      expect(registry.viewHandleForViewId(viewId), isNull);
    }
  });
}
