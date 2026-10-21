# GTK4 compatibility on the window-widget redesign

This review branch starts at flutter/flutter#192888's head
`54e02df5aeab5256065f57e2e744ffe8520a84f3` (canonical/flutter,
`better-windowing-api`). It is an integration experiment, not a complete GTK4
implementation or a claim that a GTK4 application can run this branch.

## What is implemented

Two native-layer changes are adapted from the separate Linux compatibility
branch, without changing `NestedWindow`, `WindowManager`, or `showWindow`:

* Register a popover host and its Flutter view without exposing that host as a
  `GtkWindow`. Re-registration replaces all handles, and unregister removes them.
  The registrar borrows handles; it does not own or destroy native objects.
* Marshal `GdkRGBA` using four doubles on GTK3 and four floats on GTK4, selected
  through the common `gtk_get_major_version` symbol. Keep the existing
  `fl_view_set_background_color` pointer ABI and free temporary memory in `finally`.

The public registrar gains `registerPopover`; existing `register` callers retain
GtkWindow semantics. No GTK4-only engine export is introduced.

## Why the redesigned widget API fits

`NestedWindow.windowBuilder` creates the controller on show, using the anchor's
geometry. `NestedWindow` owns that controller, tracks popup/tooltip anchor changes,
and removes content when the native controller reports destruction. The native
backend can therefore implement a popup using `GtkPopover` rather than pretending
it is a `GtkWindow`. `NestedWindowLayoutInfo.anchorRect` is expressed in the
containing view's coordinates; it is not a global screen position.

Widget-tree nesting does not choose a native parent. The builder must choose the
matching native parent explicitly. Application windows that should survive removal
of their launching button belong in `WindowManager`/`showWindow`, not in a
`NestedWindow` tied to that button's lifecycle.

The upstream redesign already contains widget tests for native destruction,
show/hide, removed widgets, destroyed-on-create controllers, and anchor tracking.
This branch does not claim to add or run those existing tests.

## Useful review questions before stabilizing the API

1. What is the contract if a keyed, visible `NestedWindow` moves to a different
   Flutter view? The current tracker forwards changed geometry but does not
   recreate its controller or change its native parent. Either require hide/show
   across that move or define and test a reparenting policy.
2. Should a builder choosing a different native parent than the anchor's containing
   view be rejected or documented as unsupported for automatic positioning?
   Otherwise the supplied rectangle belongs to a different coordinate space.
3. Verify native dismissal end to end: GTK4 outside-click/Escape and parent teardown
   must report controller destruction exactly once, set `isShowing` false, release
   the registration, and permit a subsequent show with fresh handles. Widget stub
   tests alone cannot verify the GTK signal and ownership bridge.
4. Verify transformed anchors, scale changes, and compositor-constrained popup
   placement. Preserve requested-versus-actual positioning semantics; do not promise
   absolute positioning on Wayland. Also test failed/immediately-dismissed native
   presentation, since GDK can decline to show an autohiding popup.

These are review/test targets, not claims that all four are confirmed defects.

## What still needs the GTK4 backend

The existing controller implementation still calls GTK3-only window/widget APIs.
This branch does not port popup construction, GtkPopover signal/lifecycle handling,
GTK4 event-controller-driven move/resize, rendering, accessibility, or engine build
selection. Registering a popover does not make it usable as a GtkWindow parent for
the remaining GTK3 dialog/popup code. Do not load GTK3 and GTK4 into one process.

A complete experiment needs the matching GTK4 engine and controller backend, then
real GTK3 and GTK4 runs on Wayland and X11. These native pieces should remain
separate from the widget-tree API redesign and be stacked only where they depend
on its new API.

## Validation performed

* Eight focused registry/color tests passed with Dart in an isolated package.
  The helpers were copied unchanged; test imports alone were adapted from
  flutter_test to package:test and to local helper paths.
* Standalone analysis using Flutter's shared analyzer rules: no issues.
* All five changed Dart files pass the Dart formatter unchanged; git diff --check
  passes.
* A real installed-GTK3 FFI probe resolved gtk_get_major_version and passed the
  marshalled color to gdk_rgba_to_string, which returned rgb(255,0,0).
* Not run: full Flutter analysis/tests, engine compilation, the redesign's widget
  tests, GTK4 runtime, or graphical Wayland/X11 integration. GTK4 layout unit tests
  validate memory representation, not a running GTK4 backend.

## Sources

* Redesign: https://github.com/flutter/flutter/pull/192888
* Latest ownership rationale: https://github.com/flutter/flutter/pull/192888#issuecomment-6001514354
* Existing lifecycle discussion: https://github.com/flutter/flutter/pull/192888#issuecomment-5820127568
* GTK4 Popover: https://docs.gtk.org/gtk4/class.Popover.html
* GDK popup presentation: https://docs.gtk.org/gdk4/method.Popup.present.html
