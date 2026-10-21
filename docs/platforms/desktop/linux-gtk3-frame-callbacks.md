# GTK3 Wayland child frame callback experiment

Related to [Flutter #191245](https://github.com/flutter/flutter/issues/191245).
This experiment investigates GDK frame-clock progress when Flutter renders into
an independent Wayland child surface. It does not implement Flutter's
`FlutterProjectArgs.vsync_callback`, high-refresh scheduling, presentation
feedback, or VRR.

## Optional runtime capability

`fl_gtk3_runtime_api.h/.cc` resolves the following pair from the already loaded
process using `dlsym(RTLD_DEFAULT, ...)`:

- `gdk_wayland_window_add_frame_callback_surface`
- `gdk_wayland_window_remove_frame_callback_surface`

Both must exist. There is no runtime version gate, so compatible distributor
backports work. The typed pointers compile with GTK 3.24.24 headers and add no
required imports for the GTK 3.24.25 APIs. The internal GN header visibility is
separate from the installed `flutter_linux` public headers. GTK4 is unaffected.

Set `FLUTTER_GTK3_WAYLAND_FRAME_CALLBACKS=1` **before starting the process** to
opt in. Any other value leaves the experiment disabled. `G_MESSAGES_DEBUG=all`
prints capability and registration diagnostics. The GTK3 OpenGL renderer selects
the subsurface path on Wayland; X11 does not register callback surfaces.

## Ownership and ordering

GTK owns the parent `GdkWindow` and its native `wl_surface`. Flutter's
`FlSubsurface` owns the child `wl_surface` and synchronized `wl_subsurface`.
`FlSubsurfaceEGL` retains the child while its EGL surface exists.

The GTK thread creates/replaces these resources and registers only a mapped
renderer with a ready EGL child and a matching, live Wayland parent. The renderer
retains the exact `GdkWindow` and records the exact child used for registration.
Repeated allocation or mapping of that pair does not add it twice.

Unmap unregisters a hidden child. Parent replacement and unrealize unregister
before releasing EGL and the child. Dispose also unregisters defensively;
unrealize disconnects the parent-map observer. The frame mutex serializes these
operations with raster-thread presentation. Its lifetime extends through
finalization so late presentation on a destroyed, retained widget can safely
check that the compositor is gone.

A retained `GdkWindow` does not keep its native surface alive. GTK withdraws the
parent before unmapping its children. The reviewed GTK remove implementation
only removes the child from the window implementation's hash table, which the
retained window keeps alive; it does not dereference the old parent surface.

GTK maps children before recreating the toplevel's native surface. With the
experiment enabled and supported, a parent-map observer rechecks after that
handler finishes. Without this observer, the native hide/show test removed the
registration but never restored it. Child-only hide/show can reuse the existing
parent and child pair. Each renderer manages its own registration.

## What registration does and does not establish

The current raster path swaps the EGL child buffer, then queues an idle callback
on the GTK thread. That callback notifies the first frame, performs existing
content-size handling, and queues a GTK draw. These steps are unchanged.
GTK runs its `before-paint`/`after-paint` cycle on the main thread; there is no
explicit ordering barrier between that cycle and the raster-thread swap.

GDK requests additional child frame callbacks during its `after-paint` handler.
The child must subsequently commit for that request to take effect. A
synchronized child's pending state becomes effective with a parent commit.
Registration alone therefore does not establish a correct commit sequence or
Flutter VSync. No additional `wl_surface_commit`, frame-clock hook, or change to
subsurface synchronization is made here.

The child currently receives an empty input region, but Flutter does not
explicitly set its opaque region. Parent callback suppression under an opaque
covering child remains a hypothesis for this renderer.

[Firefox's WaylandSurface implementation](https://github.com/mozilla-firefox/firefox/blob/main/widget/gtk/WaylandSurface.cpp)
is a design precedent: it resolves the pair atomically, cleans up on the main
thread, and can install an `after-paint` hook that forces a commit under its own
surface lock. Its commit policy cannot be assumed safe for Flutter's EGL/raster
ownership. The GTK behavior was checked against
[GTK 3.24.49](https://github.com/GNOME/gtk/blob/3.24.49/gdk/wayland/gdkwindow-wayland.c);
the declarations are absent in 3.24.24 and present in 3.24.25.

## Build and tests

From a checkout with its engine dependencies synchronized, use the normal engine
configuration. For a new worktree, first follow the
[engine setup instructions](../../engine/contributing/Setting-up-the-Engine-development-environment.md)
(`engine/scripts/standard.gclient` and `gclient sync`). Dependency checkouts and
build outputs are not automatically shared by Git worktrees.

```sh
cd engine/src
./flutter/tools/gn --unoptimized
ninja -C out/host_debug_unopt flutter_linux_gtk flutter_linux_unittests \
  flutter_linux_gtk3_frame_callback_tests
xvfb-run -a out/host_debug_unopt/flutter_linux_unittests

# Under a Wayland session, or an isolated headless compositor:
GDK_BACKEND=wayland FLUTTER_GTK3_WAYLAND_FRAME_CALLBACKS=1 \
  out/host_debug_unopt/flutter_linux_gtk3_frame_callback_tests
GDK_BACKEND=wayland FLUTTER_GTK3_WAYLAND_FRAME_CALLBACKS=0 \
  out/host_debug_unopt/flutter_linux_gtk3_frame_callback_tests \
  --gtest_filter=FlSubsurfaceFrameCallbacksTest.Disabled

xvfb-run -a env GDK_BACKEND=x11 FLUTTER_GTK3_WAYLAND_FRAME_CALLBACKS=1 \
  out/host_debug_unopt/flutter_linux_gtk3_frame_callback_tests \
  --gtest_filter=FlSubsurfaceFrameCallbacksTest.OtherBackend
```

The native executable uses real GTK, Wayland and EGL, with a mock Flutter engine.
It observes the optional calls and forwards them to GDK. It checks the exact
window/child pair, GTK-thread execution, balanced registration, and a live child
at removal. These tests cover lifecycle behavior, not Flutter animation cadence.
The mock-based unit target separately checks absent/partial symbol sets.

## Initial evidence and remaining measurements

On GTK 3.24.52 with headless Sway 1.11, pixman compositor and software GL, the six
opt-in lifecycle cases pass: parent hide/show, child hide/show, unrealize/realize,
resize, late presentation after destruction, and independent views. Disabled
Wayland and X11 checks make no registrations. A Bullseye GTK 3.24.24 container
loads the rebuilt engine with `RTLD_NOW` while both symbols are absent. ELF
inspection finds no required imports for them.

No real Flutter animation benchmark or physical presentation timing was measured.
No GNOME/KDE, physical GPU, high-refresh, scaling, idle-load, or VRR conclusions
follow from these tests. Keep the experiment opt-in.

For benchmarking, build once and launch the same GTK3 application binary with the
variable set to `0` and `1`. Record the engine revision, GTK version, compositor,
GPU/renderer, display scale and refresh rate. Capture `WAYLAND_DEBUG=client`
separately from performance measurements because logging perturbs timing. Match
child callback requests to later child commits, parent commits and callback
completion; also count GTK paint opportunities and Flutter-produced frames.
Measure idle load, resize and responsiveness separately. Actual presentation
remains unmeasured unless presentation feedback or an external measurement is
used. Investigate commit coordination only if these traces demonstrate a stall;
Flutter's engine scheduling remains a separate issue.
