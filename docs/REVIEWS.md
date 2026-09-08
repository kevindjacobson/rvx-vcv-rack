# Prototype independent review record

All reviews below were newly spawned with `fork_turns: "none"`, with requirements and exact repository revisions supplied but no inherited implementation conversation. Writers worked in separate engine, Rack and Syphon worktrees; integration is on `issue-13-prototype`. Every review uses main `e46ae395909d0cf023b57dc00fda0ab9c38baaa1` as the overall change base. A review of one component does not establish acceptance of unrelated components or untested host behavior.

| Reviewer | Reviewed head | Scope and result | Resolution |
|---|---|---|---|
| `/root/fresh_core_review` | `6e12c88b74ef42546891e90161139e2177323be3` | Core: false same-I/O cycles, resize out-of-bounds reads, backend destruction off its worker, underestimated retained-frame storage, per-pixel phase-speed sampling | Repaired in `0e907b8`; original P1 reproductions independently passed the next review under ASan/UBSan |
| `/root/fresh_io_review` | `fc32b26ae72f9bd668043b4fa1fe083217fae601` | Syphon: full-source CPU readback allocation, stale source cache after selection change, forgotten adopted restart UUID, stale requested canvas shape | Repaired in `c75aa8f`; independently re-reviewed below |
| `/root/fresh_core_rack_final` | `23646e7a372477293383018b8e642a0c929bf31f` | Core/Rack: ignored bypass modes, default publisher collisions after reload, invisible cable diagnostics, stale CV triggers on reset, silent audio queue overflow | Core changes in `6fcdb8c`; Rack repairs and new review tracked below |
| `/root/fresh_io_final` | `c75aa8fc4e69b568206aa443549a2e6463166e55` | No actionable Syphon findings. Independently verified real SDK loopback, selection/restart/canvas repairs, raw SDK orientation/straight alpha, and bounded conversion of an 8192² source | I/O review complete for this source revision; no core/Rack acceptance implied |
| `/root/review_phase_core` | `6fcdb8c` | Existing sanitizer suite passed. Independent probes found retroactive phase-speed integration, atomic triggers crossing audio epochs, omitted retained monitor previews in resize memory preflight, and uncounted audio-history capacity eviction | Repairs and another fresh review pending |

Reviewer probes were kept outside the repository while investigating. Durable regression coverage belongs in the committed tests. Superseded reviews and reproduced defects are preserved here so later claims can identify which source revision was actually checked.

The native audio example additionally exposed drift between Rack's fallback audio clock and the independent video clock. This integration finding is separate from the review findings; it requires a bounded recovery policy and native-host verification before the buffered-audio path is called usable.
