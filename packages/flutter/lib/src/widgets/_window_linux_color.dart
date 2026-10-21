// Copyright 2014 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Native data contracts shared by the experimental Linux windowing bindings.
// Keep this file independent of dart:ui and native symbol resolution so that
// both toolkit layouts can be tested in isolation.

import 'dart:ffi' as ffi;

import 'package:meta/meta.dart';

/// GTK3's GdkRGBA uses four doubles.
@internal
final class LinuxGdkRGBA3 extends ffi.Struct {
  @ffi.Double()
  external double red;
  @ffi.Double()
  external double green;
  @ffi.Double()
  external double blue;
  @ffi.Double()
  external double alpha;
}

/// GTK4's GdkRGBA uses four floats, despite having the same native type name.
@internal
final class LinuxGdkRGBA4 extends ffi.Struct {
  @ffi.Float()
  external double red;
  @ffi.Float()
  external double green;
  @ffi.Float()
  external double blue;
  @ffi.Float()
  external double alpha;
}

/// The allocation size required by the toolkit's native background-color API.
@internal
int linuxWindowBackgroundColorSize(int gtkMajorVersion) => switch (gtkMajorVersion) {
  3 => ffi.sizeOf<LinuxGdkRGBA3>(),
  4 => ffi.sizeOf<LinuxGdkRGBA4>(),
  _ => throw UnsupportedError('Unsupported GTK major version: $gtkMajorVersion'),
};

/// Writes components using the layout of the toolkit that owns the FlView.
///
/// [memory] must point to at least [linuxWindowBackgroundColorSize] bytes and
/// remain valid for the duration of the native setter call.
@internal
void writeLinuxWindowBackgroundColor(
  ffi.Pointer<ffi.Void> memory, {
  required int gtkMajorVersion,
  required double red,
  required double green,
  required double blue,
  required double alpha,
}) {
  switch (gtkMajorVersion) {
    case 3:
      memory.cast<LinuxGdkRGBA3>().ref
        ..red = red
        ..green = green
        ..blue = blue
        ..alpha = alpha;
    case 4:
      memory.cast<LinuxGdkRGBA4>().ref
        ..red = red
        ..green = green
        ..blue = blue
        ..alpha = alpha;
    default:
      throw UnsupportedError('Unsupported GTK major version: $gtkMajorVersion');
  }
}
