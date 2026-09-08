# Prototype validation record

Issue #13 and PR #14 implement the explicitly approved prototype. Test results must identify the tested revision and scope; unexecuted gates remain open.

## Target and engineering budgets

Observed target: Apple M4, 16 GiB, macOS 26.2 (25C56), Rack Pro 2.6.6 standalone arm64. SDK 2.6.6. Default canvas 720 × 480 RGBA float at 30000/1001 ticks/s. CPU arithmetic is a prototype reference backend, with owned OpenGL resources for Syphon transfer. These choices do not establish NTSC timing or final GPU performance.

Initial engineering budgets, declared before the prototype stress run: p99 graph processing within the 33.367 ms video period, fewer than 1% missed video deadlines over a 10-minute steady run, no monotonic retained-resource growth after 50 node create/delete cycles, and less than 512 MiB steady RVX frame/storage usage for the supplied demonstration graph. Host total RSS includes Rack and libraries and is reported separately. Record any environment interference and every failure; budget changes require an explicit rationale and issue update.

Audio coexistence requires separate baseline and video-stress evidence at the same sample rate/block size. Core queue tests and CPU benchmarks do not measure physical Core Audio underruns or end-to-end audiovisual alignment. If host counters are unavailable, report that limit rather than claiming zero underruns. Device removal/reconnect is a distinct scenario.

## Required evidence

- Independent numeric and temporal tests: unclipped signed arithmetic; typed empty inputs; explicit component conversions; same-tick acyclic processing; exactly one-tick delay and clear/reset; deterministic cycle errors; finite bounded state and audio queue behavior; phase/sample order across raster boundaries.
- Source-level adapter review plus actual Rack cable edits, fan-out, undo/redo, save/reload, duplicate/delete, bypass/reset, patch replacement, invalid input and hidden/minimized/removed-monitor behavior.
- Syphon actual SDK loopback: simultaneous receive/publish, declared orientation/alpha conversion, resize, stop/restart, identity recall, duplicate source-name handling and two-node isolation.
- Ten-minute timed run with p50/p95/p99/max, missed deadlines, memory and retained resources. At least 50 lifecycle cycles. Treat separate core and Rack runs as different evidence.
- Fresh reviewer, no inherited author conversation, with exact base/head, requirements and test evidence. Re-review substantive fixes with another fresh agent.

## Results

The prototype builds and runs in an isolated Rack 2.6.6 test profile. Independent code review is complete for runtime revision `ced3bab`; the PR remains draft because physical-audio and parts of the native lifecycle matrix remain unverified. Completed evidence is limited to the scope and revisions below.

| Evidence | Revision / scope | Result |
|---|---|---|
| Core numeric, temporal, phase and bypass suite | Runtime `ced3bab`; sanitizer coverage at `b92c7ce`, followed by SDK/core integration review at `ced3bab` | Passed. Independent checks cover all four phase modes, signed values, same-tick graphs, delay, reset, retained-frame memory and capture recovery. Earlier failures and repairs remain in the [review record](REVIEWS.md). |
| Worker lifecycle | 50-cycle harness introduced at `177ecef`; rerun through `ced3bab` | All 51 mock backends destroyed on their owner thread; retired node/frame weak references expired. Initial optimized run: 0.772 s, current RSS 1,327,104 → 9,764,864 bytes. RSS includes allocator caching; this proves owned-resource retirement, not native widget lifecycle. |
| Rack SDK-linked adapter tests and independent probes | `ced3bab`, Rack SDK 2.6.6 | Publisher defaults/custom recall, synchronous native state restoration, stacked invalid-cable diagnostics and actual Engine cable callbacks passed. Audio disconnect and overflow start new capture epochs; reconnect remains zero until a full fresh interval is available. These tests have no native window or physical audio device. |
| Syphon SDK loopback and independent probes | `c75aa8f`; Syphon source unchanged through `ced3bab` | Simultaneous receive/relay/publication, resize, source selection, disconnection/hold, restart UUID, ambiguity, canvas conversion, orientation and straight alpha passed. An 8192² external texture converts to a 720×480 CPU frame containing 1,382,400 floats without a CPU allocation above 128 MiB. See [pinned dependency and boundary](SYPHON.md). |
| Native Rack chain smoke | Installed core `0e907b8`, Rack `23646e7`, Syphon `fc32b26` | Chain/fan-out loaded and previewed; controls changed and patch Save As / replacement worked. Across minimize/restore, video tick advanced from 34047 to 35223 with six nodes/six edges and no adapter errors. This predates final repairs. |
| Native module delete/undo | Installed `0a6e9a6` runtime, core `76e43ae` | Eight whole-patch delete/undo cycles with eight RVX modules: 64 module create/delete cycles. Four cables and previews restored each time; advancing ticks, zero adapter errors. Host RSS 248,192 → 252,592 KiB; this single before/after sample does not prove an RSS plateau. |
| Native phase example | Installed final runtime `ced3bab`, Rack 2.6.6 | All four modes display and animate together in `RVX-Phase-Patterns.vcv`. Independent numeric tests additionally check positive/negative/zero speed and continuity across speed/mode changes; 23,040 raster positions were checked in the phase review. |
| Native buffered audio example | Installed final runtime `ced3bab`; Fundamental VCO 2.6.4, Rack engine 48 kHz, no physical audio device | Moving grayscale waveform displayed. The earlier flat-field failure was repaired with bounded clock reanchoring and fresh capture-interval recovery. Reanchor diagnostics are visible; this is not proof of seamless clock synchronization or the physical-audio gate. |
| Native Syphon with no Monitor and minimized window | Installed `0a6e9a6` runtime; two-node Test Image → Video I/O fixture | Independent receiver saw 94 distinct completed frames and 93 pixel-hash changes over 3.105 s visible, then 94/93 over 3.106 s minimized. Same selected server identity, 720×480. Counts use received frame identity rather than caller-assigned sequence values. An absent-server control failed as expected. See [measurement record](evidence/native-syphon-monitorless.json). |

Ten-minute **CPU renderer** runs, at 720×480 and 30000/1001 ticks/s:

| Core revision | Render p50 / p95 / p99 / maximum (ms) | Deadline misses / ticks | Reported current frame storage | Peak process RSS |
|---|---|---|---|---|
| `0e907b8` | 3.728 / 8.678 / 9.319 / 71.477 | 4 / 17,982 (0.022%) | 27,648,000 bytes | 83,230,720 bytes |
| `6fcdb8c` | 2.520 / 3.113 / 3.724 / 81.427 | 5 / 17,982 (0.028%) | 27,648,000 bytes | 79,118,336 bytes |
| `76e43ae` | 2.510 / 3.134 / 7.173 / 59.303 | 3 / 17,982 (0.0167%) | 27,648,000 bytes | 83,296,256 bytes |

All completed in 600.003–600.004 seconds with zero renderer errors and met the provisional p99/miss/storage budgets for that workload. The latest [measurement and provenance](evidence/core-stress-76e43ae.json) predates the later buffered-capture/Rack repairs. The benchmark uses Test Image, two processors, a **latched-CV** Bridge, Frame Delay and Monitor; it does not exercise buffered audio, real Syphon, native Rack rendering or physical audio. It therefore does not establish final-build integrated stress acceptance. A separate Rack test process and review/build activity were running on the same Mac, so the measurements are not isolated machine capacity limits. Reported frame storage is the renderer's current unique frame/history accounting, not total process memory or every externally retained frame generation.

Reproduction commands from the checkout:

```sh
make test
make test-lifecycle
make test-rack RACK_DIR=/path/to/Rack-SDK
make test-syphon RACK_DIR=/path/to/Rack-SDK
make benchmark
```

For native Rack testing, build/package, install into a separate user directory with `python3 scripts/install-prototype.py --user-dir /path/to/test-profile`, then launch Rack with `--user /path/to/test-profile` and an included patch. Set `RVX_DIAGNOSTICS=1` for one status line per second. This test profile must have its own valid Rack license if using Rack Pro; no license or test host executable is distributed with RVX. The opt-in [native audio and external Syphon workflow](NATIVE-VALIDATION.md) defines repeatable probes and their measurement limits.

Unverified gates remain explicit: the complete native cable-drag/undo/duplicate/bypass/reset matrix, offscreen-monitor and window-recreation cases; physical Core Audio baseline versus video-stress underruns, input-device removal/reconnection and measured end-to-end audiovisual latency; a combined native audio/video/Syphon stress run. Native module cycles and monitorless/minimized publication above are completed evidence, with their tested revisions stated. Independent-process Syphon SDK tests and mock lifecycle tests do not substitute for those scenarios. DAW, Intel, full GPU processing, Memory Palace, NTSC/composite timing and hardware fidelity are outside this prototype's implemented claims.
