# Prototype integration issue audit

September 7, 2026. Reviewed main `e46ae395909d0cf023b57dc00fda0ab9c38baaa1`, prototype runtime `ced3bab14831c83d04f24a9a92f2671c75f8a101` and subsequent documentation/evidence changes. All 11 open issues (#1–#9, #11, #13) and all three open PRs (#10, #12, #14) were read. Repository visibility remains private; main is unchanged and no PR has merged.

| Issues | Finding and action |
|---|---|
| #1, #11 | Worktree, fresh-review and consistency requirements remain active user instructions. PR #10 at `ca2b73eb5c1a836b28ff56255de3aa61c7b71a61` and PR #12 at `55ad792fb2132ac4724ff104a08bfa714d4c85fe` are unmerged proposals. Their older planning-only wording must be reconciled with explicit #13 approval before readiness/merge. Both PRs now carry that correction and remain draft; policy file reconciliation and fresh review are still required. |
| #2 | Actual M4/Rack target and prototype budgets are recorded. Added qualified CPU stress and separate lifecycle evidence; full-release target, physical-audio workload and combined stress acceptance remain open. |
| #3 | Removed the stale claim that no bridge implementation exists. Recorded implemented image/field, clock/trigger and audio-gap contracts, including discontinuous reanchoring and empty intervals. Full-release raster/composite fidelity and external alignment remain open. |
| #4 | Replaced evaluation-only adapter wording with supported endpoint-snapshot implementation and scoped SDK/native evidence. Retained the full lifecycle/GPU audit and unrun host cases. |
| #5 | Recorded the pinned Syphon source, simultaneous I/O, bounded transfer, 8-bit external boundary, SDK tests and native monitorless/minimized continuity. First-release behavior sheets and remaining coexistence/latency gates remain open. |
| #6, #7, #8 | No changed assumptions: Memory Palace routing/controls, NTSC/dirty receiver behavior and LZX behavior sheets remain pending. The prototype does not satisfy or replace them. |
| #9 | Explicit six-module approval continues to authorize #13. The complete release plan and its approval remain separate; no circular planning prerequisite was reintroduced. |
| #13 | Updated completed numeric/build/SDK/phase evidence and reviewed repairs; retained unchecked native/physical-audio/combined-stress gates. PR #14 stays draft. |

The implementation uses shared frame/graph, signal arithmetic, timestamped capture, delay and backend interfaces across six module kinds. Specific Rack lifecycle and Syphon ownership code remains at their boundaries. [Contracts](PROTOTYPE.md), [independent reviews](REVIEWS.md), [Rack adapter](RACK-ADAPTER.md), [Syphon](SYPHON.md) and [validation](VALIDATION.md) identify behavior, reuse decisions, exact evidence and approximation limits. README remains installation, quick start and the module table only.

Before any readiness/merge action, inspect the actual latest base and open issue/PR bodies again. A later merge requires a separate post-merge audit; this record does not claim one occurred.

## Autonomous continuation audit

The user requested continued autonomous building on September 7, 2026. Re-read all 11 issue bodies and three open PRs against unchanged main before further work. Scoped prototype validation continues in `issue-13-prototype`; a separate `issue-13-host-validation` worktree supplied bounded timing statistics, with fresh review and repaired run identities before integration as `2a077dd`. A separate validation app/profile leaves the user's edited test patch running.

Policy #12 at `095c4bb62f44fcec887f39973b058f4ab0a195e1` and workflow #10 at `4730b124858b55617db56efbcb3e1b58ca36f1d4` now repair their scoped-approval wording. Fresh reviewer `/root/review_workflow_approval` found no actionable findings in those heads and prospective tree `175c42f8e61e95aaf041f21ff61bc511e8925f1d`. The policy PR is ready for user review; workflow #10 remains draft pending its declared policy dependency and actual-base reconciliation. No merge occurred. This supersedes the earlier pending-wording action above.

Issue #6 has a separate worktree for its Memory Palace behavior specification. This advances the existing planning deliverable without treating prototype delay as the complete Memory Palace instrument. Broader first-release requirements and their fidelity gates remain open.

The continuation at runtime `2a077dd` records the configured native audio/video/Syphon workload and separate 600-second CoreAudio baseline/stress observations. Its p99 is 24.900 ms, with 26/18,812 completed-render deadline misses, 24 skipped ticks, no renderer/adapter errors and 34,655,640 current frame bytes. Both CoreAudio intervals report zero device-wide overload notifications; Rack-internal underrun counters remain unavailable. Update #2–#5/#13 and PR #14 with that distinction, the separate keyboard lifecycle checks and the remaining physical reconnect/latency and native interaction gates. No acceptance threshold is weakened, and PR #14 remains draft.

Fresh review then found that the original external observer could falsely pass with missing input, a different same-name publisher or a later outage. Keep the timing/count data as observations, qualify source contribution and output application identity, repair the test and require source-bound proof before claiming combined transport acceptance. This supersedes the earlier unqualified combined-run wording.

Memory Palace specification PR #15 at `bd8620276a20bb8b375708488b693bcaa4d7afca` is now ready for review, unmerged. A fresh Ultra reviewer verified primary artifacts, source boundaries and independent ring arithmetic with no actionable findings. Updated #6/#7/#8/#9 to preserve its proposed-versus-documented distinctions and unresolved V19 fidelity gates. The current open PR set is #10/#12/#14/#15.

The corrected source-bound relay at `8d0f363` separately passed two 30-second native runs (900 verified frames each), including source-process restart. Wrong-source/application controls and controlled cable disconnection failed as expected; a separate three-repeat cadence failure is retained. This does not retroactively validate source contribution in the earlier mixed stress measurement. Fresh review found no functional relay defect; its regression-coverage findings were repaired in `6224613`. Offscreen publication now has a scoped native observation at runtime `2a077dd`.

The user additionally requested video → ordinary Rack audio waveform → video conversion with NTSC-like behavior. New issue #16 tracks the focused specification, coordinated with #3/#5/#7/#9 and the Memory Palace sheet. Real-time audio-rate conversion, time-scaled composite transport and high-rate RVX composite must remain distinct; no claim of full-bandwidth NTSC through ordinary audio wires is made. The open issue set now contains 12 issues (#1–#9, #11, #13, #16).

The source-bound rejection regressions at `6224613` and sanitized evidence at `892c3ec` both passed fresh independent review; exact scope is in [REVIEWS.md](REVIEWS.md). The user then explicitly deferred the video/audio waveform feature. Issue #16 is retained as future implementation work, sequenced after the prototype and shared signal/NTSC prerequisites. At this checkpoint, PR #17 was parked as draft preliminary planning notes; it could not close that implementation issue or block #13. No waveform implementation was underway.


## Adjustable Frame Delay kickoff

Issue #18 is a focused follow-on prompted by the user's missing frame-count question while continuing prototype work. The initial #13 contract deliberately required one frame; its historical results remain tied to that runtime. New work uses `issue-18-frame-count` stacked on prototype checkpoint `413a149`, with independent `issue-18-delay-engine` and `issue-18-delay-panel` worktrees. The prototype branch remains a separate reviewable checkpoint; no merge is authorized by this development task.

Read all 13 open issues (#1–#9, #11, #13, #16, #18) and PRs #10/#12/#14/#15/#17 against unchanged main `e46ae395909d0cf023b57dc00fda0ab9c38baaa1`. #2's shared 512 MiB budget and #3's tick/feedback rules constrain the variable history. #6 may later reuse bounded storage, but its timing, capture, freeze and four routings are not implemented by this utility. #4/#5 routing and Syphon boundaries are unchanged. #7/#8/#9 retain broader specification scope. #1/#11 require a focused PR, actual-base reconciliation and fresh independent review. At this checkpoint, waveform implementation #16 was explicitly deferred and PR #17 was parked as draft notes; it was not a prerequisite of the prototype, delay utility or shared specifications. Repository remained private and no PR had merged.


## Adjustable Frame Delay delivery audit

Draft PR [#19](https://github.com/kevindjacobson/rvx-vcv-rack/pull/19) targets `issue-13-prototype` at `413a14991168cbd6ff69785fef90cfa643ac2030` to keep #18's change focused. Runtime `9a33523` has separate core and Rack reviews; final history-retirement test `193ffcd` passed a fresh review, including paused-observer and cleanup-only mutation cases. Evidence at `2dfc1c7` received a separate fresh review; its wording correction distinguishes current adapter errors from cumulative renderer/deadline counters. Earlier #13 fixed-delay stress is not attributed to the new runtime.

Re-read all 13 open issues (#1–#9, #11, #13, #16, #18), the five existing PR bodies and the new #19 delivery body against unchanged main `e46ae395909d0cf023b57dc00fda0ab9c38baaa1`. The current PR set is #10/#12/#14/#15/#17/#19; #10/#14/#17/#19 are draft, and #12/#15 are ready but unmerged.

| Issues | Final reconciliation |
|---|---|
| #1, #11 | Clarified dependent draft PRs may target a prerequisite branch, then must wait for its merge, retarget/reconcile against actual main, re-audit and receive fresh review before readiness/merge. PR #10 now contains the matching three-file correction at `030f48ab6dab8fbd8ebecf984ae1d19516adb209`, freshly reviewed by `/root/review_dependent_draft_workflow` with no actionable findings; its two ENGINEERING.md links still require policy #12 before readiness |
| #2 | Updated the guard's conservative requested-capacity versus actual retained/current-work distinction and separate actual-byte reporting. Native 1/15/60 observations are scoped to the four-node fixture, not earlier mixed stress |
| #3 | Recorded exact capture-tick ages, one-frame legacy/default, clear/reset/change/bypass/discontinuity semantics and recovery. Progressive image ticks remain distinct from NTSC fields; no audio or Syphon adapter code changed |
| #13 | Linked the separately implemented #18 follow-on and review/native evidence while preserving #14's historical one-frame scope and all outstanding host/audio/lifecycle/combined gates |
| #18 | Updated completed core/storage/review/package criteria and linked #19. Kept its first combined interaction criterion unchecked for the outstanding native mouse knob/context-menu check; actual SDK parameter and persistence behavior passed |
| #4, #5, #6, #7, #8, #9, #16 | Read and unchanged: native routing/I/O scope, Memory Palace fidelity, broader sheets/approval, reuse/review policy and deferred waveform sequencing remain valid |

The core reuses shared immutable frames, graph evaluation, diagnostic accounting and normalization across the renderer and Rack panel. It adds no rendering allocation/wait to the audio callback. README contains installation, quick start and modules only. No acceptance threshold changed. Repository remains private and no merge occurred; #19 cannot bypass its prototype dependency. Native mouse editing and the broader #13 acceptance gaps remain explicit follow-up work.

## Experimental merge reconciliation

The user explicitly requested review and merge of all existing PRs. Current main `d570e8a59b170b4ddb94fd838e64e5e47fbf8316` contains the README notice, engineering policy, workflow, Memory Palace specification and deferred waveform notes. All 11 open issue bodies (#2–#5, #7–#9, #13, #16, #18, #20) and remaining PRs #14/#19 were read after those merges. Historical entries above describe their named revisions.

The prototype integration preserves the observed target, source contracts, exact notice and all unrun gates. I/O transition repairs through `3115d57` satisfy existing #5/#13 behavior without changing the signal contract. Issue #13 stays open for native lifecycle, physical reconnect, latency and complete combined acceptance; merging the experimental source does not complete those criteria. Adjustable delay #18 follows this integrated runtime and needs its own actual-main review. Memory Palace history counts completed images, while the adjustable utility counts scheduled capture ticks with black for missing ages. #3/#7/#8/#9 retain that distinction and unresolved fidelity gates. The notes in merged PR #17 remain preliminary; #16 implementation stays deferred. Design #20 consumes the reconciled six-panel controls separately. Final review and post-merge reconciliation belong to the PR/issue records.

## Adjustable delay actual-main reconciliation

This preparation integrates actual main `ccce071305e9822a80a9be4376e99ee239172bf8`, where prototype PR #14 landed, into adjustable-delay base `3b75a2ad262b613022bd0c462cf1382a384f7a35`. PR #19 no longer waits on an unmerged prototype branch, but still requires this integrated tree to pass validation, the issue audit and a fresh exact-base review before readiness or merge.

The post-prototype-merge audit covered all 11 open issues (#2–#5, #7–#9, #13, #16, #18, #20). The combined tree preserves the adjustable 1–60-frame history, legacy one-frame default, shared 512 MiB guard, visible controls, metadata and historical native evidence, together with the prototype's source-selection cache and publisher teardown repairs. Memory Palace retains completed-image ages; Frame Delay retains scheduled capture-tick ages with transparent black for a missing requested age. Merged PR #17 remains preliminary notes and #16 implementation remains deferred. Native knob dragging/context-menu entry and all broader #13 host, physical-device, latency and combined-acceptance gates remain unverified.
