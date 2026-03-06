# Wayland Overlay Plan

## Goal

Implement a reliable multi-monitor capture/edit overlay on Wayland that:

- allows a single selection across multiple displays
- preserves drawing/editing across displays
- removes the monitor selector popup for the graphical capture/edit flow

Primary target:

- seamless cross-screen selection and annotation on Wayland
- correct behavior on KDE Plasma Wayland
- shared global coordinates across outputs

Non-goal for the next implementation:

- a true clickable/drawable Wayland surface in the empty virtual gaps between displays

This is specifically for the user layout:

- `eDP-1`: `1920x1080+0+561`
- `HDMI-A-1`: `1200x1920+1920+0`
- virtual desktop bounding box: `3120x1920+0+0`
- top-left corner `(0,0)` is not on `eDP-1`; it is in empty virtual space

## Confirmed Findings

### 1. Screenshot composition is correct

The screenshot itself is not the problem.

Observed runtime debug:

```text
[capture-debug] desktopGeometry=3120x1920+0+0
[capture-debug] grabFullDesktop-exit ok=1 pixmap=null=0 size=3120x1920 dpr=1 deviceIndependent=3120x1920
```

So Flameshot is correctly computing the desktop bounding box and receiving a full-desktop pixmap of the expected size.

### 2. Constructor-time widget geometry is correct

Before the window is mapped, Flameshot is creating the capture widget with the expected size:

```text
[capture-debug] after-geometry-setup widget=3120x1920+0+0
[capture-debug] gui-after-construct widget=3120x1920+0+0
```

### 3. Plasma Wayland is overriding the top-level window after show

After the widget is shown, KWin/Plasma Wayland reconfigures it to the laptop work area:

```text
[capture-debug] resize-event widget=1920x1036+0+0
```

That matches:

- width of `eDP-1`
- height reduced by the taskbar/panel
- origin pinned to `0,0`

This means the current one-big-`QWidget` approach is being treated as a normal managed Wayland toplevel and forcibly constrained by the compositor.

### 4. This is not just a coordinate math bug anymore

Evidence:

- widget requested `3120x1920+0+0`
- screenshot pixmap is `3120x1920`
- compositor resizes the actual surface after show
- user can move/resize the overlay with KDE Plasma window-management shortcuts

That confirms the surface is being managed like a normal window, not as a true fullscreen capture overlay.

### 5. Cursor/screen fallback noise was partially addressed

The repeated:

```text
Unable to get current screen, starting to use primary screen.
```

was reduced by changing `QGuiAppCurrentScreen::screenAt()` to choose the nearest screen when Qt returns `nullptr`.

That reduced log noise but did not solve the fundamental issue.

## What Was Tried

### A. Remove monitor selector and restore full-desktop capture/edit flow

Changed behavior:

- graphical capture path uses full desktop screenshot
- launcher monitor selection hidden
- capture overlay initialized across desktop bounds

Result:

- screenshot data correct
- Wayland window still constrained by compositor

### B. Replace Linux `showFullScreen()` with `show()`

Reason:

- `showFullScreen()` was suspected to snap the window to a single monitor

Result:

- still constrained
- taskbar remained visible
- user could move/resize overlay as a normal window

### C. Reapply geometry during/after `showEvent`

Reason:

- to catch compositor remapping after initial show

Result:

- no effective change
- compositor still resized surface to work area

### D. Force window onto top-left screen

Reason:

- reduce dependence on cursor/current-screen selection

Result:

- no effective change on Plasma Wayland

### E. Improve `screenAt()` fallback

Reason:

- Qt was returning `nullptr` in gap/outside-display regions

Result:

- log noise reduced
- did not affect compositor-managed window sizing

## Current Technical Conclusion

The current architecture for graphical capture on Linux Wayland:

- one large `QWidget`
- top-level managed by Qt Wayland as a normal toplevel
- moved/resized to full desktop bounding box

is not sufficient on KDE Plasma Wayland for a true gap-spanning overlay.

The compositor is free to clamp managed toplevels to work areas and/or a single output.

Therefore the next implementation cannot be finished as another round of `move()`/`resize()`/`showFullScreen()` tweaks.

It requires a different surface model.

## Spectacle Findings

Two independent inspections of KDE Spectacle's Wayland path reached the same conclusion.

### 1. Spectacle does not use one giant virtual-desktop window

Relevant files:

- `/tmp/spectacle-inspect/src/SpectacleCore.cpp`
- `/tmp/spectacle-inspect/src/Gui/CaptureWindow.cpp`
- `/tmp/spectacle-inspect/src/Gui/CaptureWindow.h`
- `/tmp/spectacle-inspect/src/Gui/SpectacleWindow.cpp`
- `/tmp/spectacle-inspect/src/Gui/SpectacleWindow.h`

Observed design:

- one fullscreen capture window per `QScreen`
- `QQuickView`/QML-based windows, not one oversized `QWidget`
- each overlay window is bound to exactly one output geometry

This is the key reason Spectacle avoids the Plasma clamping behavior seen in Flameshot.

### 2. Spectacle keeps one shared global selection model

Relevant files:

- `/tmp/spectacle-inspect/src/Gui/SelectionEditor.cpp`
- `/tmp/spectacle-inspect/src/Gui/Selection.cpp`
- `/tmp/spectacle-inspect/src/Gui/CaptureOverlay.qml`

Observed design:

- all capture windows contribute to one union `screensRect`
- input from a per-screen window is remapped into shared logical desktop coordinates
- each screen-local overlay renders as a viewport into the same global selection/edit state

This is the portable design idea Flameshot should reuse.

### 3. Spectacle is not proof that gap-space surfaces are possible

Spectacle can behave as if one selection spans multiple outputs, but it still only creates windows on real screens.

Implications:

- shared selection/export coordinates can include gap space
- there is no actual Wayland input/render surface in empty virtual space
- Spectacle is a template for seamless multi-screen UX, not for a true gap-surface backend

### 4. Spectacle uses KDE/KWin-specific integration

Relevant files:

- `/tmp/spectacle-inspect/src/Platforms/ImagePlatformKWin.cpp`
- `/tmp/spectacle-inspect/src/Platforms/screencasting.cpp`

Observed integration points:

- `org.kde.KWin.ScreenShot2`
- `zkde_screencast_unstable_v1`
- `KWaylandExtras::setXdgToplevelTag("region-editor")`

Notes:

- `LayerShellQt` exists in Spectacle, but not for the normal region-selection overlay path
- the portable takeaway is the per-output overlay architecture, not the KDE-specific backend APIs

## Recommended Architecture

### Pivot to a Spectacle-style per-output Wayland overlay backend

On Wayland, replace the current one-big-`QWidget` model with:

- one overlay window per output
- one shared global capture/selection/annotation model
- per-window coordinate mapping into that shared model
- one combined exported image in virtual-desktop coordinates

The likely direction is:

1. Introduce a Wayland-specific overlay abstraction.
2. Create one fullscreen overlay surface per `QScreen`.
3. Keep the capture model in virtual-desktop coordinates, not per-widget local coordinates tied to one top-level.
4. Render each overlay window as a viewport into the same global capture state.
5. Export the final selection from one desktop-wide backing image.

### Why this pivot is justified

This is the first design observed to work on Plasma Wayland without fighting compositor window management.

Benefits:

- aligns with Wayland's output-oriented window model
- avoids compositor clamping of one oversized managed window
- matches the architecture already proven by Spectacle
- preserves seamless cross-screen selection and drawing from the user's perspective

Tradeoff:

- there will still be no true interactive surface in empty gap-only space
- gap regions can still exist in exported coordinates and in the backing image model, but pointer interaction can only happen on real outputs unless a future compositor/protocol-specific solution is added

## Implementation Plan

### Phase 1: Isolate the graphical capture surface from generic QWidget assumptions

Files likely involved:

- `src/core/flameshot.cpp`
- `src/widgets/capture/capturewidget.cpp`
- `src/widgets/capture/capturewidget.h`
- `src/tools/capturecontext.h`

Work:

- define a clear platform abstraction for the graphical capture overlay
- separate screenshot/selection/edit state from the current top-level widget assumptions
- stop relying on `widgetOffset` as the authoritative source of global capture coordinates for Wayland
- introduce a virtual desktop origin and explicit desktop bounding rect into capture state

Deliverable:

- capture logic can operate in virtual desktop coordinates independently of the actual top-level widget geometry

### Phase 2: Introduce per-output Wayland overlay backend

New code area likely needed:

- `src/widgets/capture/waylandoverlay*` or `src/platform/wayland/*`

Work:

- create a Wayland-specific overlay manager
- create one fullscreen overlay surface per screen
- bind each overlay window to its `QScreen` geometry
- ensure each overlay is treated as a normal fullscreen per-output surface, not one oversized resizable desktop window
- keep the implementation inside Qt unless a compositor-specific issue proves otherwise

Deliverable:

- all outputs show a coordinated overlay without Plasma collapsing it to one work area

### Phase 3: Support virtual desktop coordinate mapping

Work:

- represent selections in full virtual-desktop coordinates
- map mouse/touch input from each screen-local surface into virtual desktop coordinates
- preserve correct behavior when `(0,0)` is in empty space
- preserve correct behavior when monitors have offsets or mixed rotations

Deliverable:

- selection rectangle and annotation tools operate across displays as one logical canvas

### Phase 4: Project shared state into each overlay window

Work:

- render the same logical selection/edit state in each per-screen overlay
- make handles, guides, and dimming appear continuous across outputs
- keep annotation strokes and selection bounds in one shared coordinate system

Deliverable:

- user perceives one continuous multi-screen capture/edit canvas

### Phase 5: Handle gap regions in the backing model and export path

Work:

- preserve gap space in the combined screenshot/backing image
- allow exported selections to include gap coordinates
- fill gap regions consistently in output images
- do not rely on interactive pointer events existing in gap-only space

Deliverable:

- exported images remain correct for mixed-offset monitor layouts, even though the UI itself is per-output

### Phase 6: Preserve existing non-Wayland behavior

Work:

- keep X11 path as current QWidget-based flow unless regression requires cleanup
- keep macOS behavior unchanged
- keep Windows behavior unchanged

Deliverable:

- Wayland-specific architecture does not destabilize other platforms

### Phase 7: Remove temporary diagnostics and dead-end workaround code

Work:

- remove `FLAMESHOT_CAPTURE_DEBUG` tracing once architecture is stable
- remove geometry-reapply hacks that only existed to test compositor behavior
- remove now-obsolete current-screen workarounds if no longer needed

Deliverable:

- clean final implementation

## Immediate Engineering Tasks

1. Add a proper virtual desktop rect to capture state.
2. Audit every use of:
   - `widgetOffset`
   - `mapFromGlobal`
   - `mapToGlobal`
   - `QGuiAppCurrentScreen::currentScreen()`
3. Prototype one fullscreen overlay window per output on Wayland.
4. Prove shared global coordinate mapping across those windows before touching annotation behavior.
5. Only after the prototype proves surface behavior, wire it back into `CaptureWidget` or replace the Wayland capture presentation path entirely.

## Relevant Files

### Core behavior

- `src/core/flameshot.cpp`
- `src/widgets/capture/capturewidget.cpp`
- `src/widgets/capture/capturewidget.h`
- `src/utils/screengrabber.cpp`
- `src/core/qguiappcurrentscreen.cpp`
- `src/tools/capturecontext.h`
- `src/tools/capturecontext.cpp`

### UI affected by selector removal

- `src/widgets/capturelauncher.cpp`

### Existing docs that mention compositor constraints

- `docs/Sway and wlroots support.md`

## Runtime Evidence To Preserve

### Screen layout

```text
eDP-1:    Geometry 0,561 1920x1080
HDMI-A-1: Geometry 1920,0 1200x1920
desktop:  3120x1920+0+0
```

### Key debug sequence

```text
[capture-debug] after-geometry-setup widget=3120x1920+0+0
[capture-debug] gui-after-show widget=3120x1920+0+0 window=3120x1920+0+0 screen=eDP-1
[capture-debug] resize-event widget=1920x1036+0+0
```

Interpretation:

- Flameshot requested the right size.
- Plasma Wayland resized the mapped window afterward.

## Known Secondary Issues

These appeared during testing but are not yet the primary blocker:

```text
QLayout: Attempting to add QLayout "" to SidePanelWidget "", which already has a layout
QPainter::begin: Paint device returned engine == 0, type: 2
QPainter warnings (Painter not active / engine == 0)
```

Notes:

- The `QPainter` warnings are likely downstream of the invalid/constrained surface state.
- Do not treat them as the main bug until the overlay architecture is corrected.
- The `QLayout` warning seems unrelated to the Wayland geometry failure and should be handled separately.

## Suggested Branch Strategy

Use a fresh branch for the Wayland overlay rework.

Suggested branch name:

```text
wayland-per-output-overlay
```

## Bottom Line

The problem is now understood:

- screenshot composition is correct
- requested single-window geometry is correct
- Plasma Wayland is overriding that normal top-level window
- Spectacle avoids this by never using that model on Wayland
- the best next implementation is per-output fullscreen overlays with one shared global capture model

So the remaining work is architectural, and the recommended architecture is now the Spectacle-style per-output design rather than a speculative true gap-surface backend.
