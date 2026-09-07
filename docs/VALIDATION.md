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

Pending implementation integration. No runtime acceptance gate is marked passed by this document's existence.
