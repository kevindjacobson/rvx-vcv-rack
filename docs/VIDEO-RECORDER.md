# Video Recorder / Scrubber

Issue #33 is the separately approved recorder extension to the original six-module prototype. The user requested it on September 9, 2026 after discussing recording an incoming video, retaining the result, knob/CV scrubbing, forward/reverse playback and looping. This is an RVX utility, not a Memory Palace emulation or an NTSC recording format. Validation status belongs in the evidence section below and the issue PR; specifications are not evidence that a native interaction passed.

## Clip and controls

The first version stores 1–60 completed RVX input presentations in memory, with capacity 60 by default. At 30000/1001 presentations per second, 60 images span 2.002 seconds of nominal clip duration. A paused or slow source can appear repeatedly in a recording. This preserves float RGB/alpha values, including signed and above-range values, within the shared 512 MiB graph budget. It is intended for short recorded gestures and loops. Longer clips and disk recording/export need a separate storage design.

| Parameter ID | Control | Range and default |
|---|---|---|
| 0 | Record | Off/on, default off; rising edge starts a replacement clip |
| 1 | Play | Off/on, default off |
| 2 | Position | 0–100%, default 0%; first through last retained image |
| 3 | Speed | −4 to +4 times nominal cadence, default +1; zero holds |
| 4 | Loop | Off/on, default on |
| 5 | Clear | Momentary; erase clip and stop recording |
| 6 | Capacity | Snapped integer 1–60 images, default 60 |

| Port | Domain | Meaning |
|---|---|---|
| Input 0: Image | RVX image | Incoming stream to record |
| Input 1: Position CV | Ordinary Rack voltage | Connected 0–10 V selects 0–100% of the retained clip |
| Output 0: Image | RVX image | Selected recorded image; live input when bypassed |

Position CV overrides the knob and free-running playback while connected; values outside 0–10 V clamp to the clip endpoints. This is video-rate position control, not buffered audio-to-raster conversion. The audio callback publishes only scalar values/connection state and Clear events; image storage and transport run on the video worker. Parameters are latched at video execution boundaries, so Record and Play are sustained controls rather than timestamped trigger inputs.

Record takes priority over playback and scrubbing. Turning it on starts a new clip after memory admission succeeds. The old clip remains available if admission fails. Each completed render can retain one valid direct input presentation. Turning Record off stops before the next capture. Reaching Capacity stops automatically and keeps the beginning of the clip; the Record switch must return off before another recording can start. Capacity is read on every video tick. Raising it can extend a recording that has not yet stopped; lowering it to the current count or below stops capture without truncating retained images. A clip that has already stopped does not restart when Capacity increases; rearm Record to begin a replacement.

While recording, the output shows the most recently retained image from an earlier render; the first recording tick is transparent black. Once stopped, Position selects across the retained clip. With `N > 1` images, manual normalized position `p` selects the nearest integer to `p × (N − 1)`; exact 0 and 1 select the first and last. A one-image clip always selects its sole image. An empty clip outputs transparent black and reports its empty state. Displayed image numbers are human-readable indices into this clip, separate from source sequence metadata.

## Playback and time

Play advances a fractional playhead at `speed × nominal images/second`, using elapsed scheduled video time. Positive speeds move forward; negative speeds move backward. Zero holds the current playhead. Knob changes seek during playback; an unchanged knob does not reset an advancing playhead every tick. Turning Play on or seeking displays the requested image before applying subsequent elapsed time. Connected Position CV holds absolute control over the playhead; disconnecting seeks the knob position, then resumes transport on later ticks if Play is on. Turning Play off holds the current playhead until Position changes.

Looping wraps over all `N` retained image positions, including reverse wrap. With Loop off, playback holds at the first or last endpoint while the Play switch remains armed, so reversing Speed can resume movement. Turning Loop off clamps its final fractional interval to the last image even when paused or at zero speed. Repeated identical source images remain distinct recorded presentations. Source `Frame.sequence` and `Frame.seconds` are preserved; the renderer clock and clip index remain separate from those source fields. Per-image capture gaps are not stored as a playback timeline.

The clip is an ordered sequence of completed input presentations. Playback assigns them a uniform nominal cadence. Skipped render slots and intervals with a disconnected direct input create no synthetic captures, so playback can compress recording gaps. This is not timestamp-preserving file playback, external genlock, interlaced capture or a guarantee of fresh external camera frames. In particular, Video I/O can supply a valid black/held image when its external source is absent; the recorder cannot infer that source's freshness from current frame metadata. A disconnected or structurally invalid direct recorder input reports missing input and does not count as successful capture.

## Causality, lifecycle and memory

An active recorder reads only retained images from previous completed render calls. Every recorder/delay output is evaluated before current recorder inputs are committed. This allows explicit causal feedback without depending on module placement. A bypassed recorder instead passes its input immediately and cannot break a zero-delay graph cycle.

| Event | Clip behavior |
|---|---|
| Record off | Retain the clip independently of further incoming images |
| Record on after rearm | Replace only after admission succeeds; begin at image zero |
| Capacity reached | Stop capture without wrapping or overwriting; require Record off before restart |
| Clear or module reset | Erase clip, reset position and suppress capture until Record is rearmed |
| Bypass | Retain clip, pause capture/playhead, pass input immediately; require Record rearm |
| Format/rate change or backward/repeated video tick | Retire incompatible clip; begin a new recording epoch |
| Missing direct input | Skip capture and report it; retained pictures remain available |
| Save/reload or duplicate | Restore configuration with Record and Play off and an empty clip |
| Removal | Release worker-owned clip references |

Patch files do not contain recorded pixels in this version. Save the patch to retain its wiring and settings; keep the module/session alive to retain the recording. Empty-clip recall is explicit, rather than silently recording over a newly loaded patch. Sample-rate/reset events from the host also follow the adapter's reset contract.

Storage shares immutable frame references without per-capture image copies. Retained clips remain charged while stopped or bypassed and when graph admission fails. A capacity decrease cannot hide already retained images from accounting. Clear/reset and removal must permit recovery from rejected memory budgets. As with Frame Delay, admission reserves conservative capacity and may refuse multiple instances even when some current image references are shared. No capacity is silently reduced. Actual frame-byte diagnostics deduplicate shared frame pointers; process RSS and external clients retaining arbitrary image generations remain separate scopes.

At 720×480 RGBA float32, each unique image uses 5,529,600 bytes and 60 use 331,776,000 bytes (316.40625 MiB), before current outputs, other modules and backend surfaces. This is why the initial clip is short. The recorder does not repurpose Frame Delay's scheduled capture-age semantics or Memory Palace's completed-history/control policies.

## Validation requirements

| Area | Required evidence |
|---|---|
| Recording | Numbered input with unrelated source sequence metadata; stop, replacement, capacity latch, missing direct input and valid upstream black |
| Scrubbing | Exact first/last/one-image selection, intermediate nearest image, CV clamp/override and disconnect |
| Playback | Forward/reverse, fractional speed, zero hold, loop wrap, non-loop endpoint and seek while running |
| Causality | Output precedes current capture; feedback is independent of graph ordering; bypassed cycle rejected |
| Lifecycle | Clear/reset, bypass/rearm, format/tick discontinuity, restored settings with empty clip, removal |
| Resources | Unique and hidden retained frames, capacity shrink, multi-instance refusal, failed replacement preserving clip, Clear/remove recovery |
| Native adapter | Stable IDs, snapped Capacity, named controls, CV scalar publication, reset/restore; no image work in audio callback |
| Product evidence | Native panel plus recorded/scrubbed example, clearly distinguished from SDK tests or fixture loading |

## Recorded validation — September 9, 2026

Runtime `350524305f331594ba9240958d13c1bd7ca1c54c` passed the core suite, lifecycle checks, AddressSanitizer/UndefinedBehaviorSanitizer, actual Rack SDK/adapter tests, arm64 build and private packaging. Package verification reports 18 files and seven modules, including the new example. The separate package-verifier regression suite passed six tests. These are scoped software checks, not a full native workload or physical-audio/AV measurement.

Fresh independent review found a Loop-off transition defect in the preceding runtime: pausing within the final fractional loop interval could retain a wrapped first-image selection. The repair clamps non-loop state even while paused, with regressions for Play off and Speed zero. A new independent reviewer found no actionable findings and verified 396,000 selections with a separate integer quarter-frame oracle across capacities 1–60, signed speeds −4…+4, and both loop modes. Delayed source metadata and valid upstream black have explicit retained-pointer tests.

The first Linux run of the new fallback-black test failed to compile because its standard-library algorithm header was only included indirectly on Mac. Test-only commit `a3a01999620f80c7b8fcca052508906760fc7cdc` adds the explicit include. [Hosted run 34411551940](https://github.com/kevindjacobson/rvx-vcv-rack/actions/runs/34411551940) then passed portable/sanitizer and Mac SDK/package jobs. Production behavior remains the runtime named above.

The [native evidence record](evidence/recorder-native/README.md) contains final-runtime panel captures at 100%/150%, native example save/reopen and cold settings recall. It verifies Record/Play normalize off and the empty clip remains visible; it does not prove clearing a previously recorded clip. The initial computer-use attempts at `2c98733` targeted the wrong widget despite successful tool returns, and native-saved Record/Play remained off. No native recorded/stopped/scrubbed clip was obtained. **Issue #33 and its PR remain open/draft for direct recorded-clip interaction evidence**, including scrubbing/playback and Clear/reset/bypass with a populated clip. SDK tests and empty fixtures do not replace that gate. Earlier #13/#18/#30 gates remain separate.
