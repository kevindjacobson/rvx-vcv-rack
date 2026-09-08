# Operator-panel validation

This record separates source/build checks from native Rack observations. The implementation base is `3e2e100c7bd595241f68265039567eede18230e5`. Native observations and after images use implementation head `b3b1371c089048b32b76467a3c5008ded0ef189a`; later commits only add this evidence.

## Source and package checks

The following commands passed on Apple M4 / macOS 26.2 using Rack SDK 2.6.6 at `/Users/kj/Documents/Codex/2026-09-07/rvx-vcv-rack-deps/Rack-SDK` and pinned Syphon `71351d4` through its read-only issue-18 checkout:

- `make test`
- `make test-lifecycle`
- forced rebuild plus `make test-rack`
- `make test-syphon`
- `make -j4`
- `make dist`

The resulting arm64 package contains both Barlow Condensed fonts, all three RVX SVG control assets, the SIL license and provenance record, and no private reference capture. The existing core, lifecycle, adapter, Rack-host, and Syphon tests pass. The diff makes no change under `src/core/`, to the Syphon backend, to `plugin.json`, or to example patch behavior. `Modules.cpp` changes panel widget construction and state presentation while retaining every module definition, parameter configuration, parameter/port enumeration, and persistence callback. `RackAdapter.cpp` changes only the UI drawing of the existing `VideoPort`; its binding and cable identity contract are unchanged.

Static mockups were rendered through macOS Quick Look before implementation and visually inspected for clipping and hierarchy. Both SVG source files parse, as do the three packaged component SVGs. Font file type and hashes match `licenses/BarlowCondensed-PROVENANCE.md`.

## Native before and after images

Rack Pro 2.6.6's `--screenshot 1` mode constructed null-instance widgets and wrote 100% PNGs from the disposable `local.rvx.issue20.panel-review` app/profile. The before set was generated from base `3e2e100c7bd595241f68265039567eede18230e5`; the after set was generated from `b3b1371c089048b32b76467a3c5008ded0ef189a`. The command log records Apple M4 / OpenGL 2.1 Metal and successful loading of both packaged Barlow Condensed faces.

| Module | Before | After |
|---|---|---|
| Test Image | [PNG](native/before/TestImage.png) | [PNG](native/after/TestImage.png) |
| Signal Processor | [PNG](native/before/SignalProcessor.png) | [PNG](native/after/SignalProcessor.png) |
| CV Bridge | [PNG](native/before/CvBridge.png) | [PNG](native/after/CvBridge.png) |
| Frame Delay | [PNG](native/before/FrameDelay.png) | [PNG](native/after/FrameDelay.png) |
| Video Monitor | [PNG](native/before/VideoMonitor.png) | [PNG](native/after/VideoMonitor.png) |
| Video I/O | [PNG](native/before/VideoIo.png) | [PNG](native/after/VideoIo.png) |

## Native Rack matrix

Native evidence must come from a disposable Rack app/profile and must not modify the user's running Rack process, normal profile, or edited patch.

| Check | State | Evidence or limit |
|---|---|---|
| All six panels at 100% and zoomed out | Passed | Interactive fixture observed at 100% and 75%; all module titles, scales, cells, ports and controls remained inside their panels. Durable null-instance 100% images are linked above. |
| Retina rendering and packaged font load | Passed | Interactive capture on the host's Retina display remained crisp; isolated-host log records both packaged Barlow faces loaded. |
| Module-browser null previews | Passed | Rack browser opened with Enter and was filtered to `RVX`; all six null-instance widgets rendered without a crash or engine dereference. Rack's screenshot mode independently constructed the same six widgets. |
| Cables over controls and labels | Passed | The six-module fixture loaded with five video cables at 75% and 100%; themed video port shapes and labels remained distinguishable under cables. |
| Rack brightness below 100% | Passed | The fixture was relaunched with isolated `rackBrightness` 0.55; hierarchy and green/amber/red semantics remained legible. |
| Long publisher/source/status text | Partial | The fixture's long publisher was clipped inside its field at 75% and 100%. Source and status widgets use explicit NanoVG scissors, and classifier cases are host-tested; a live long Syphon source was not available. |
| Frames knob/menu entry at 1 and 60, Clear, reset and bypass | Partial | Native fixtures rendered Frames 1 and 60 correctly and showed Rack bypass dimming. Rack-host tests cover snapping, typed clamp, Clear edge behavior and reset. Mouse/menu interaction was not recorded. |
| Save/reload and included example patches | Partial | The review fixture repeatedly loaded after clean host exits. Rack-host tests cover parameter JSON persistence and legacy omission. Every included example was not opened during this visual pass. |
| Preview color neutrality | Passed | The live Test Image grayscale ramp appeared in Video Monitor without tint or overlay at Frames 60; `Preview::updateImage()` conversion remains unchanged. |
| UI/resource regression | Passed | All six modules and five cables ran in the isolated host without a crash. Theme drawing remains static UI-thread NanoVG/SVG work with no audio callback or renderer entry. |

The PR remains draft while live long-source/status behavior, direct mouse/menu interaction, and the full example set remain partial. These open visual/UI checks do not change the separately open native, physical-audio, latency, full combined, or fidelity gates in issues #13 and #18.
