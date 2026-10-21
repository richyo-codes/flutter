// Copyright 2014 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ffi' as ffi;
import 'dart:io';
import 'dart:typed_data';

import 'package:flutter/src/widgets/_window_linux_color.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('GTK color layouts have their native sizes', () {
    expect(linuxWindowBackgroundColorSize(3), 32);
    expect(linuxWindowBackgroundColorSize(4), 16);
    expect(() => linuxWindowBackgroundColorSize(5), throwsUnsupportedError);
  });

  for (final gtkMajorVersion in <int>[3, 4]) {
    test('GTK$gtkMajorVersion color components use the native order and width', () {
      // malloc/free are used only on Linux; no GTK installation or display is
      // needed to verify the exact bytes passed to fl_view_set_background_color.
      final process = ffi.DynamicLibrary.process();
      final ffi.Pointer<ffi.Void> Function(int) malloc = process
          .lookupFunction<
            ffi.Pointer<ffi.Void> Function(ffi.IntPtr),
            ffi.Pointer<ffi.Void> Function(int)
          >('malloc');
      final void Function(ffi.Pointer<ffi.Void>) free = process
          .lookupFunction<
            ffi.Void Function(ffi.Pointer<ffi.Void>),
            void Function(ffi.Pointer<ffi.Void>)
          >('free');
      final int size = linuxWindowBackgroundColorSize(gtkMajorVersion);
      final ffi.Pointer<ffi.Void> memory = malloc(size + 8);
      expect(memory, isNot(ffi.nullptr));
      try {
        final Uint8List bytes = memory.cast<ffi.Uint8>().asTypedList(size + 8);
        bytes.fillRange(0, size + 8, 0xA5);
        writeLinuxWindowBackgroundColor(
          memory,
          gtkMajorVersion: gtkMajorVersion,
          red: 0.125,
          green: 0.25,
          blue: 0.5,
          alpha: 0.75,
        );
        final List<double> components = gtkMajorVersion == 3
            ? memory.cast<ffi.Double>().asTypedList(4)
            : memory.cast<ffi.Float>().asTypedList(4);
        expect(components, <double>[0.125, 0.25, 0.5, 0.75]);
        expect(bytes.sublist(size), List<int>.filled(8, 0xA5));
        expect(
          () => writeLinuxWindowBackgroundColor(
            memory,
            gtkMajorVersion: 5,
            red: 0,
            green: 0,
            blue: 0,
            alpha: 0,
          ),
          throwsUnsupportedError,
        );
      } finally {
        free(memory);
      }
    }, skip: !Platform.isLinux);
  }
}
