# Operator-panel validation

This record separates source/build checks from native Rack observations. The implementation base is `3e2e100c7bd595241f68265039567eede18230e5`. Exact reviewed and native heads are recorded when those checks complete.

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

## Native Rack matrix

Native evidence must come from a disposable Rack app/profile and must not modify the user's running Rack process, normal profile, or edited patch.

| Check | State | Evidence or limit |
|---|---|---|
| All six panels at 100% and zoomed out | Pending | Requires isolated host screenshot |
| Retina rendering and packaged font load | Pending | Requires isolated host log and screenshot |
| Module-browser null previews | Pending | Requires opening RVX in the native module browser |
| Cables over controls and labels | Pending | Review fixture includes all six modules and five video cables |
| Rack brightness below 100% | Pending | Requires isolated host interaction |
| Long publisher/source/status text | Pending | Fixture supplies a long publisher; a live source name and synthetic status still require native observation |
| Frames knob/menu entry at 1 and 60, Clear, reset and bypass | Pending | Existing SDK behavior tests pass; visual interaction requires isolated host |
| Save/reload and included example patches | Pending | Requires isolated host interaction |
| Preview color neutrality | Source checked | `Preview::updateImage()` conversion is unchanged; native visual comparison remains pending |
| UI/resource regression | Source checked | Theme drawing is static UI-thread NanoVG/SVG work with no audio callback or renderer entry; native observation remains pending |

Until the pending rows are run, issue #20 and its PR remain draft. These open visual/UI checks do not change the separately open native, physical-audio, latency, full combined, or fidelity gates in issues #13 and #18.
