# Linux GTK Rendering Paths

## Scope

This document describes how the Linux embedder presents Flutter OpenGL output
through GTK3 and GTK4. It deliberately distinguishes CPU-copy avoidance,
GPU-copy avoidance, and synchronization: describing every accelerated path as
"direct" hides important performance and correctness tradeoffs.

The selected path depends on the GTK variant, display backend, GTK/GDK runtime
capabilities, EGL driver extensions, and the selected renderer.

## Terminology

* **CPU-copy-free**: pixels do not pass through `glReadPixels` or CPU memory.
* **Strict zero-copy**: the consumer samples the producer's original backing
  allocation; no CPU or GPU copy is made.
* **GPU-only copy**: a GPU blit or resolve produces a second allocation, but
  pixels never pass through system memory.
* **Direct rendering** is avoided below unless the exact ownership and copy
  behavior are stated.

## Paths

| Path | GTK | Pixel transfer | Synchronization | Notes |
| --- | --- | --- | --- | --- |
| Memory texture/readback | GTK4 | `glReadPixels`, RGBA-to-BGRA swizzle, `GdkMemoryTexture` | Readback synchronizes | Most compatible GTK4 fallback; highest CPU cost. |
| Shared GL texture | GTK4 | EGLImage import into a sibling framebuffer, then `GdkGLTexture` | EGL fence; GPU wait when GDK has a compatible EGL context | CPU-copy-free and usually no GPU pixel copy. Falls back to a client wait for GLX/non-EGL consumers, or `glFinish()` when fences are unavailable. |
| Exported DMA-BUF texture | GTK4 | Render framebuffer is GPU-blitted to an EGLImage-backed export snapshot; GDK imports its DMA-BUF | Native DMA-BUF fence | CPU-copy-free and cross-renderer-oriented. Current implementation is **not** strict zero-copy because of the snapshot blit. |
| Wayland EGL subsurface | GTK3/GTK4 | Flutter texture is rendered into a separate subsurface EGL surface | EGL/context handoff | CPU-copy-free presentation path for whole-view Wayland content. It is not a replacement for a `GdkTexture` used in GTK composition. |
| GTK3 normal compositor | GTK3 | CPU readback/upload where a Wayland subsurface is not used | Readback | Compatibility fallback; upstream intentionally removed GTK3 shared-EGL-image frame sharing. |

## GTK4 OpenGL Selection

The GTK4 compositor chooses a route in this order:

1. **DMA-BUF**, when `FLUTTER_GTK4_ENABLE_DMABUF=1` and all runtime checks
   pass:
   GTK exposes the DMA-BUF texture APIs, the EGL driver exposes
   `EGL_MESA_image_dma_buf_export`, GDK accepts the exported format/modifier,
   and an export snapshot is available.
2. **Shared GL texture**, when the compositor framebuffer is shareable. An
   EGLImage lets a framebuffer in the GDK GL context sample the same backing
   texture and produce a `GdkGLTexture`.
3. **Memory texture readback**, when native GL sharing is unavailable. Set
   `FLUTTER_GTK4_DISABLE_READBACK=1` to turn this fallback into a visible
   failure while diagnosing an accelerated path.

DMA-BUF is deliberately opt-in while the compatibility matrix is still being
validated. A failure in the DMA-BUF route falls back to the shared-GL route,
then to readback when sharing itself is unavailable.

## What DMA-BUF Improves

DMA-BUF avoids the expensive CPU readback and re-upload boundary. Its imported
texture can be consumed by GTK renderers that are not using the same OpenGL
context, including Vulkan-oriented GTK configurations, subject to driver and
format support.

The current route still has a GPU-only copy:

```text
Flutter render framebuffer
  -> GPU framebuffer blit
DMA-BUF export snapshot (EGLImage-backed)
  -> EGL DMA-BUF export and native fence
GDK DMA-BUF texture
```

The snapshot is necessary today because GTK may retain a texture after Flutter
has begun rendering the next frame. The snapshot pool gives GTK stable
ownership without racing the producer. Eliminating that blit would require a
safe producer-buffer lifetime protocol and should be measured before adding
complexity.

## Synchronization Boundaries

The paths have different synchronization quality:

* **DMA-BUF** exports an Android native EGL fence and imports it into the
  exported DMA-BUF plane. This allows GPU-side readiness tracking without a
  CPU completion wait.
* **Shared GL texture** creates an `EGL_KHR_fence_sync` fence after Flutter
  submits the frame. When GDK has a compatible EGL context, it inserts an
  `eglWaitSyncKHR` GPU wait before sampling. GLX or non-EGL consumers use a
  client wait; drivers without fences retain the `glFinish()` fallback.
* **Readback** necessarily synchronizes as part of `glReadPixels`.
* **Subsurface** has its own EGL presentation boundary. It needs explicit
  fence handling if producer and consumer contexts can run independently.

Consequently, DMA-BUF is the best accelerated GTK4 texture route where it is
available, but it is not proof that every GTK4 frame is zero-copy or optimal.

## GTK Renderer Considerations

GTK4 renderers make the distinction more important:

* `GSK_RENDERER=ngl` or `gl`: shared GL textures are a natural fit, though the
  current `glFinish()` can still limit frame pacing.
* `GSK_RENDERER=vulkan`: DMA-BUF is the promising interop route. A GL texture
  alone does not create Vulkan interop.
* `GSK_RENDERER=cairo`: useful as a diagnostic software baseline, not an
  accelerated presentation target.

Renderer names alone do not guarantee a path. The runtime API, EGL extensions,
format/modifier acceptance, and frame size must all pass the compositor's
checks.

## Debugging Matrix

For an app with native textures or video, compare at least:

```bash
GSK_RENDERER=ngl flutter run -d linux --linux-gtk=gtk4
GSK_RENDERER=gl flutter run -d linux --linux-gtk=gtk4
GSK_RENDERER=vulkan flutter run -d linux --linux-gtk=gtk4
GSK_RENDERER=cairo flutter run -d linux --linux-gtk=gtk4

FLUTTER_GTK4_ENABLE_DMABUF=1 GSK_RENDERER=vulkan \
  flutter run -d linux --linux-gtk=gtk4
FLUTTER_GTK4_DISABLE_READBACK=1 GSK_RENDERER=ngl \
  flutter run -d linux --linux-gtk=gtk4
```

Exercise startup, texture/video playback, resize in both directions, hiding
and restoring windows, and multi-window/popover surfaces. The accelerated
route must be compared with GTK3 and the GTK4 readback fallback for correctness
as well as throughput and latency.

## Current Direction

1. Keep readback as the compatibility fallback.
2. Keep DMA-BUF opportunistic until the runtime/driver matrix is proven.
3. Measure the explicit shared-GL fence path on GL, NGL, and Vulkan GTK
   renderers, including its GLX client-wait fallback.
4. Investigate direct export of a producer framebuffer only if measurements
   show the snapshot blit is material and a safe lifetime model is available.
5. Treat Wayland subsurfaces as a separate whole-surface presentation option,
   not a substitute for GTK texture interop.
