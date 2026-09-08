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

The prototype builds and runs in an isolated Rack 2.6.6 test profile. Independent review covers runtime `2a077dd`, which adds bounded native timing statistics to the previously reviewed `ced3bab` signal implementation. The combined native audio/video/Syphon run below is complete. The PR remains draft because parts of the native lifecycle matrix, physical device reconnect and end-to-end latency remain unverified; Rack-internal audio underrun counters are unavailable. Completed evidence is limited to the scope and revisions below.

| Evidence | Revision / scope | Result |
|---|---|---|
| Core numeric, temporal, phase and bypass suite | Runtime `ced3bab`; sanitizer coverage at `b92c7ce`, followed by SDK/core integration review at `ced3bab` | Passed. Independent checks cover all four phase modes, signed values, same-tick graphs, delay, reset, retained-frame memory and capture recovery. Earlier failures and repairs remain in the [review record](REVIEWS.md). |
| Worker lifecycle | 50-cycle harness introduced at `177ecef`; rerun through `ced3bab` | All 51 mock backends destroyed on their owner thread; retired node/frame weak references expired. Initial optimized run: 0.772 s, current RSS 1,327,104 → 9,764,864 bytes. RSS includes allocator caching; this proves owned-resource retirement, not native widget lifecycle. |
| Rack SDK-linked adapter tests and independent probes | `ced3bab`, Rack SDK 2.6.6 | Publisher defaults/custom recall, synchronous native state restoration, stacked invalid-cable diagnostics and actual Engine cable callbacks passed. Audio disconnect and overflow start new capture epochs; reconnect remains zero until a full fresh interval is available. These tests have no native window or physical audio device. |
| Syphon SDK loopback and independent probes | `c75aa8f`; Syphon source unchanged through `ced3bab` | Simultaneous receive/relay/publication, resize, source selection, disconnection/hold, restart UUID, ambiguity, canvas conversion, orientation and straight alpha passed. An 8192² external texture converts to a 720×480 CPU frame containing 1,382,400 floats without a CPU allocation above 128 MiB. See [pinned dependency and boundary](SYPHON.md). |
| Native Rack chain smoke | Installed core `0e907b8`, Rack `23646e7`, Syphon `fc32b26` | Chain/fan-out loaded and previewed; controls changed and patch Save As / replacement worked. Across minimize/restore, video tick advanced from 34047 to 35223 with six nodes/six edges and no adapter errors. This predates final repairs. |
| Native module delete/undo | Installed `0a6e9a6` runtime, core `76e43ae` | Eight whole-patch delete/undo cycles with eight RVX modules: 64 module create/delete cycles. Four cables and previews restored each time; advancing ticks, zero adapter errors. Host RSS 248,192 → 252,592 KiB; this single before/after sample does not prove an RSS plateau. |
| Native keyboard lifecycle | Installed `2a077dd`, isolated validation host after combined stress | Disconnect-all / undo / redo changed video edges 9→0→9→0, then restored. Bypass-all produced expected errors when the only delay became instantaneous; undo stopped further errors. Initialize reset parameters and closed Rack's audio device; undo restored values/device/preview. Duplicate and duplicate-with-cables produced 16 RVX nodes with 9 and 18 edges respectively. Undo restored eight nodes/nine edges; adapter errors stayed zero. The doubled wired graph exceeded the frame budget, which diagnostics recorded. [Exact observations and snapshots](evidence/native-keyboard-lifecycle-2a077dd.json). |
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

### Combined native audio/video/Syphon run

Runtime `2a077dd50791247bffac4c7a863a89ace496aa66` ran in a separate Rack Pro 2.6.6 process/profile with eight RVX nodes and nine video edges: Test Image and external Syphon input through two processors, two buffered CV Bridges, explicit feedback delay, Monitor and simultaneous Syphon publication. A Fundamental VCO feeds one Bridge; Scarlett 4i4 4th Gen input feeds the other through Core Audio Audio-2 at 48 kHz / 512 frames. No cable feeds a hardware audio output. The generated fixture is identified by hash in the [sanitized measurement record](evidence/native-av-stress-2a077dd.json).

| Measurement | Observed result |
|---|---|
| Native render p50 / p95 / p99 / maximum | 22.200 / 23.700 / 24.900 / 80.853 ms |
| Completed renders / missed render deadlines | 18,812 / 26 (0.13821%) |
| Skipped scheduled ticks | 24, separate from completed-render deadline misses |
| Renderer / adapter errors | 0 / 0 |
| Current RVX frame storage | 34,655,640 bytes |
| Sampled whole-Rack RSS | 243,088 → 248,672 KiB; sampled maximum 248,672 KiB |
| Independent Syphon receiver | 19,113 completed frame identities, 19,112 pixel-hash changes; 29.9325 FPS over a 638.502-second active receive window |
| Syphon working format / identity | 720×480 RGBA; no format mismatch, output ambiguity or identity change |
| CoreAudio baseline / stress | Separate 600-second observations; 0 / 0 device-wide overload notifications |

The native timing snapshot spans more than 600 seconds and ends while the external source is still active. Quantiles include startup and all completed renders since worker start; fixed 0.05 ms buckets report upper-bound estimates through 100 ms, with zero saturated samples in this run. Render duration includes synchronous Syphon receive/publish work. Worker run IDs distinguish replacement engines and restarts. This workload met the declared p99, missed-deadline and current-storage budgets. It does not establish the remaining acceptance gates.

The external source ran for 660 seconds, published 19,779 frames and missed two publisher cadence slots. Initial discovery is excluded from its active receive-window FPS. The receiver recorded 22 long interarrival gaps, an estimated 26 missing cadence slots and a maximum gap of 91.9865 ms. Received frame identity and complete pixel hashes establish changing completed pixels; locally assigned frame sequence numbers alone do not. These measurements do not establish operator fidelity, exact source-native texture dimensions or end-to-end latency.

The [baseline record](evidence/coreaudio-baseline-native.json) used Core Audio Audio-2 and Fundamental VCO with no RVX nodes in the validation process. Listener registration/removal succeeded in both baseline and stress. These are device-wide HAL notifications, not Rack-specific underrun counts: the pinned Rack source ignores `RtAudioStreamStatus` and does not export its internal ring-buffer zero-fill/drop counts. See the [observer's limits](NATIVE-VALIDATION.md). Rack briefly opens its default block size before restoring the fixture; its log and saved patch confirm 48 kHz / 512 frames for measurement. The input is live device data; no audio was recorded and hardware outputs remained uncabled.

Two other Rack processes and development activity remained present during both runs. RSS comprises 476 one-second samples during the later 479.081 seconds, so the sampled maximum is not a kernel high-water mark or a leak proof. Current frame accounting and owned-resource retirement tests remain distinct from host RSS. The original user's edited patch was left running in its own process. Committed evidence omits hardware UIDs, serials and transient process/device IDs.

Reproduction commands from the checkout:

```sh
make test
make test-lifecycle
make test-rack RACK_DIR=/path/to/Rack-SDK
make test-syphon RACK_DIR=/path/to/Rack-SDK
make benchmark
```

For native Rack testing, build/package, install into a separate user directory with `python3 scripts/install-prototype.py --user-dir /path/to/test-profile`, then launch Rack with `--user /path/to/test-profile` and an included patch. Set `RVX_DIAGNOSTICS=1` for one status line per second. This test profile must have its own valid Rack license if using Rack Pro; no license or test host executable is distributed with RVX. The opt-in [native audio and external Syphon workflow](NATIVE-VALIDATION.md) defines repeatable probes and their measurement limits.

Unverified gates remain explicit: single-cable drag/fan-out and the complete native lifecycle matrix, offscreen-monitor and window-recreation cases; physical input-device removal/reconnection and measured end-to-end audiovisual latency. Keyboard disconnect/undo/redo/duplicate/bypass/reset now have the scoped native checks above; a logical device close/reopen during initialization does not prove physical unplug handling. Device-wide CoreAudio baseline/stress and combined native audio/video/Syphon evidence are now recorded above; zero Rack-internal underruns cannot be inferred from the available counters. Native module cycles and monitorless/minimized publication above are completed evidence, with their tested revisions stated. Independent-process Syphon SDK tests and mock lifecycle tests do not substitute for unrun native scenarios. DAW, Intel, full GPU processing, Memory Palace, NTSC/composite timing and hardware fidelity are outside this prototype's implemented claims.
